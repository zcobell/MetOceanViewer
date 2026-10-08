// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The entry points of the v5 station netCDF reader: open the file once, run
// station_netcdf_open.cpp (and station_netcdf_samples.cpp for a read), close
// it on every path.

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
#include "station_netcdf_reader.hpp"

namespace mov::io {

namespace {

namespace sn = detail::station_nc;

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

std::expected<Read<StationFile>, Error> read_v5(
    const nc::File& file, const StationNcSelection& which,
    const StationNcReadOptions& options, const StopToken& stop) {
  auto opened = sn::open_v5(file, stop);
  if (not opened) {
    return std::unexpected{std::move(opened).error()};
  }
  auto selected =
      selected_indices(which, opened->value.catalog.stations.size());
  if (not selected) {
    return std::unexpected{std::move(selected).error()};
  }
  auto table =
      sn::read_table(file, opened->value, *selected, options.padding, stop);
  if (not table) {
    return std::unexpected{std::move(table).error()};
  }
  return Read<StationFile>{
      .value = {.table = std::move(table->value),
                .origin = opened->value.origin},
      .warnings = detail::concatenated(std::move(opened->warnings),
                                       std::move(table->warnings))};
}

}  // namespace

std::expected<Read<StationNcCatalog>, Error> inspect_station_netcdf(
    const std::filesystem::path& path, const ReadContext& ctx) {
  return with_file(
      path, ctx,
      [&](const nc::File& file)
          -> std::expected<Read<StationNcCatalog>, Error> {
        return sn::open_v5(file, ctx.stop).transform([](Read<sn::Opened> o) {
          return Read<StationNcCatalog>{.value = std::move(o.value.catalog),
                                        .warnings = std::move(o.warnings)};
        });
      });
}

std::expected<Read<StationFile>, Error> read_station_netcdf(
    const std::filesystem::path& path, const StationNcSelection& which,
    const ReadContext& ctx, const StationNcReadOptions& options) {
  return with_file(path, ctx, [&](const nc::File& file) {
    return read_v5(file, which, options, ctx.stop);
  });
}

std::expected<Read<StationFile>, Error> read_station_netcdf(
    const std::filesystem::path& path, const StationNcSelection& which,
    const ReadContext& ctx) {
  return read_station_netcdf(path, which, ctx, StationNcReadOptions{});
}

}  // namespace mov::io
