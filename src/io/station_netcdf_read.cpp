// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The entry points of the station netCDF reader: open the file once, find out
// which kind of station file it is (netcdf_kind.hpp: v5, foreign CF or legacy
// v4), run that kind's reader (station_netcdf_open.cpp and
// station_netcdf_samples.cpp for v5, station_netcdf_foreign_*.cpp,
// station_netcdf_legacy.cpp) and close the file on every path.

#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "model_netcdf.hpp"
#include "mov/core/station_table.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/station_netcdf.hpp"
#include "netcdf_kind.hpp"
#include "station_netcdf_dialects.hpp"
#include "station_netcdf_reader.hpp"

namespace mov::io {

namespace {

namespace sn = detail::station_nc;
using detail::classify_netcdf;
using detail::NetcdfKind;

/// Opens `path`, runs `read` on it and closes it, on every path.
template <class F>
auto with_file(const std::filesystem::path& path, const ReadContext& ctx,
               const F& read)
    -> std::invoke_result_t<const F&, const nc::File&> {
  auto file = nc::File::open(path, ctx.limits);
  if (not file) {
    return detail::fail(std::move(file).error());
  }
  auto result = read(std::as_const(*file));
  if (auto closed = std::move(*file).close(); not closed and result) {
    return detail::fail(std::move(closed).error());
  }
  return result;
}

/// The file indices `which` names, after checking that a selection was made
/// for this file.
std::expected<std::vector<std::size_t>, Error> selected_indices(
    const StationNcSelection& which, std::size_t count) {
  return std::visit(
      [count](const auto& w) -> std::expected<std::vector<std::size_t>, Error> {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(w)>,
                                     AllStations>) {
          std::vector<std::size_t> all(count);
          std::ranges::copy(std::views::iota(std::size_t{0}, count),
                            all.begin());
          return all;
        } else {
          return detail::require_selection(w, count).transform([&w] {
            return std::vector<std::size_t>(w.indices().begin(),
                                            w.indices().end());
          });
        }
      },
      which);
}

/// The station file `opened` describes, with the table of the selected
/// stations that `read_table` makes.
template <class Opened, class ReadTable>
std::expected<Read<StationFile>, Error> with_table(
    Read<Opened> opened, const StationNcSelection& which,
    const ReadTable& read_table) {
  auto selected = selected_indices(which, opened.value.catalog.stations.size());
  if (not selected) {
    return std::unexpected{std::move(selected).error()};
  }
  auto table = read_table(opened.value, *selected);
  if (not table) {
    return std::unexpected{std::move(table).error()};
  }
  return Read<StationFile>{
      .value = {.table = std::move(table->value),
                .origin = opened.value.catalog.origin},
      .warnings = detail::concatenated(std::move(opened.warnings),
                                       std::move(table->warnings))};
}

std::expected<Read<StationFile>, Error> read_v5(
    const nc::File& file, const StationNcSelection& which,
    const StationNcReadOptions& options, const StopToken& stop) {
  auto opened = sn::open_v5(file, stop);
  if (not opened) {
    return std::unexpected{std::move(opened).error()};
  }
  return with_table(*std::move(opened), which,
                    [&](const sn::Opened& o, std::span<const std::size_t> sel) {
                      return sn::read_table(file, o, sel, options.padding,
                                            stop);
                    });
}

std::expected<Read<StationFile>, Error> read_foreign_file(
    const nc::File& file, const StationNcSelection& which,
    const StopToken& stop) {
  auto opened = sn::open_foreign(file, stop);
  if (not opened) {
    return std::unexpected{std::move(opened).error()};
  }
  return with_table(
      *std::move(opened), which,
      [&](const sn::ForeignOpened& o, std::span<const std::size_t> sel) {
        return sn::read_foreign(file, o, sel, stop);
      });
}

std::expected<Read<StationFile>, Error> read_legacy_file(
    const nc::File& file, const StationNcSelection& which,
    const StopToken& stop) {
  auto opened = sn::open_legacy(file, stop);
  if (not opened) {
    return std::unexpected{std::move(opened).error()};
  }
  return with_table(
      *std::move(opened), which,
      [&](const sn::LegacyOpened& o, std::span<const std::size_t> sel) {
        return sn::read_legacy(file, o, sel, stop);
      });
}

/// The kind of file is not one of the station kinds: `not_this_format`.
std::unexpected<Error> not_a_station_file(NetcdfKind kind) {
  switch (kind) {
    case NetcdfKind::adcirc:
      return sn::invalid(FormatErrc::not_this_format, "model");
    case NetcdfKind::dflow:
      return sn::invalid(FormatErrc::not_this_format, "station_x_coordinate");
    case NetcdfKind::station_v5:
    case NetcdfKind::foreign_cf:
    case NetcdfKind::legacy_station:
    case NetcdfKind::other:
      break;
  }
  return sn::invalid(FormatErrc::not_this_format, ":metoceanviewer_format");
}

std::expected<Read<StationFile>, Error> read_any(
    const nc::File& file, const StationNcSelection& which,
    const StationNcReadOptions& options, const StopToken& stop) {
  const auto kind = classify_netcdf(file);
  if (not kind) {
    return std::unexpected{kind.error()};
  }
  switch (*kind) {
    case NetcdfKind::station_v5:
      return read_v5(file, which, options, stop);
    case NetcdfKind::foreign_cf:
      return read_foreign_file(file, which, stop);
    case NetcdfKind::legacy_station:
      return read_legacy_file(file, which, stop);
    case NetcdfKind::adcirc:
    case NetcdfKind::dflow:
    case NetcdfKind::other:
      break;
  }
  return not_a_station_file(*kind);
}

template <class Opened>
Read<StationNcCatalog> catalog_of(Read<Opened> opened) {
  return {.value = std::move(opened.value.catalog),
          .warnings = std::move(opened.warnings)};
}

std::expected<Read<StationNcCatalog>, Error> inspect_any(
    const nc::File& file, const StopToken& stop) {
  const auto kind = classify_netcdf(file);
  if (not kind) {
    return std::unexpected{kind.error()};
  }
  switch (*kind) {
    case NetcdfKind::station_v5:
      return sn::open_v5(file, stop).transform([](Read<sn::Opened> o) {
        return catalog_of(std::move(o));
      });
    case NetcdfKind::foreign_cf:
      return sn::open_foreign(file, stop)
          .transform([](Read<sn::ForeignOpened> o) {
            return catalog_of(std::move(o));
          });
    case NetcdfKind::legacy_station:
      return sn::open_legacy(file, stop)
          .transform([](Read<sn::LegacyOpened> o) {
            return catalog_of(std::move(o));
          });
    case NetcdfKind::adcirc:
    case NetcdfKind::dflow:
    case NetcdfKind::other:
      break;
  }
  return not_a_station_file(*kind);
}

}  // namespace

std::expected<Read<StationNcCatalog>, Error> inspect_station_netcdf(
    const std::filesystem::path& path, const ReadContext& ctx) {
  return with_file(path, ctx, [&](const nc::File& file) {
    return inspect_any(file, ctx.stop);
  });
}

std::expected<Read<StationFile>, Error> read_station_netcdf(
    const std::filesystem::path& path, const StationNcSelection& which,
    const ReadContext& ctx, const StationNcReadOptions& options) {
  return with_file(path, ctx, [&](const nc::File& file) {
    return read_any(file, which, options, ctx.stop);
  });
}

std::expected<Read<StationFile>, Error> read_station_netcdf(
    const std::filesystem::path& path, const StationNcSelection& which,
    const ReadContext& ctx) {
  return read_station_netcdf(path, which, ctx, StationNcReadOptions{});
}

}  // namespace mov::io
