// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The samples of a v5 station netCDF file (docs/station-netcdf.md 7, 8,
// 12.4): the times and the data of the selected stations, read a group of
// stations at a time (as the file's chunks make cheap) and only as far as the
// stations' samples reach. In the incomplete layout a group reads its longest
// station's samples plus one element, the first of the padding, which must
// be fill (SN 12.4's boundary check); PaddingCheck::whole reads and checks
// all of it. Only the samples kept are turned into core::Samples.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "model_netcdf.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/io/cf_time.hpp"
#include "mov/io/detail/checked_product.hpp"
#include "mov/io/detail/station_groups.hpp"
#include "mov/io/detail/table_error.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "station_netcdf_format.hpp"
#include "station_netcdf_v5.hpp"

namespace mov::io::detail::station_nc {

namespace {

namespace sn = ::mov::io::detail::station_nc;

/// What a v5 read needs besides the file: its structure and the generic row
/// specification.
struct Rows {
  const Structure& s;
  RowSpec spec;
};

/// The axes: one shared (orthogonal) or one per selected station.
std::expected<std::vector<core::TimeAxis>, Error> read_axes(
    const nc::File& file, const Rows& rows, const CfClock& clock) {
  const nc::VarInfo& time = rows.s.timing.time;
  if (rows.s.timing.layout == StationNcLayout::orthogonal) {
    return read_time_axis(file, time, clock, rows.spec.stop)
        .transform([](core::TimeAxis axis) {
          return std::vector<core::TimeAxis>{std::move(axis)};
        });
  }
  std::vector<core::TimeAxis> axes(rows.spec.selected.size());
  auto done = sample_rows(
      file, time, rows.spec, RowRole::times,
      [&](const SelectedStation& m,
          std::span<const core::Sample> kept) -> std::expected<void, Error> {
        return times_of(kept, clock, time.name.view(), m.station)
            .and_then([&](core::TimeAxis times) {
              return strictly_increasing(std::move(times), time.name.view(),
                                         m.station);
            })
            .transform([&](core::TimeAxis axis) {
              axes[m.position] = std::move(axis);
            });
      });
  if (not done) {
    return std::unexpected{std::move(done).error()};
  }
  return axes;
}

/// The kept samples of a data variable at each selected station.
std::expected<std::vector<core::Column>, Error> read_values(
    const nc::File& file, const nc::VarInfo& var, const Rows& rows) {
  std::vector<core::Column> columns(rows.spec.selected.size());
  auto done = sample_rows(
      file, var, rows.spec, RowRole::values,
      [&](const SelectedStation& m,
          std::span<const core::Sample> kept) -> std::expected<void, Error> {
        columns[m.position].assign(kept.begin(), kept.end());
        return {};
      });
  if (not done) {
    return std::unexpected{std::move(done).error()};
  }
  return columns;
}

struct Flags {
  std::vector<std::vector<std::int8_t>> rows;
  std::optional<std::int8_t> fill;
};

/// The wet/dry flags at each selected station, raw (flag_rows); the padding
/// read must be fill.
std::expected<Flags, Error> read_flags(const nc::File& file,
                                       const nc::VarInfo& var,
                                       const Rows& rows) {
  auto mask = file.masking<std::int8_t>(var.name);
  if (not mask) {
    return fail(std::move(mask).error());
  }
  Flags flags{
      .rows = std::vector<std::vector<std::int8_t>>(rows.spec.selected.size()),
      .fill = mask->fill};
  auto done = flag_rows(
      file, var, rows.spec, flags.fill,
      [&](const SelectedStation& m,
          std::span<const std::int8_t> kept) -> std::expected<void, Error> {
        flags.rows[m.position].assign(kept.begin(), kept.end());
        return {};
      });
  if (not done) {
    return std::unexpected{std::move(done).error()};
  }
  return flags;
}

/// The column of one station with its flags applied (SN 8.2).
std::expected<void, Error> apply_flags(core::Column& column,
                                       std::span<const std::int8_t> flags,
                                       std::optional<std::int8_t> fill,
                                       const nc::VarInfo& data,
                                       const nc::VarInfo& status,
                                       std::size_t station) {
  for (std::size_t j = 0; j < flags.size(); ++j) {
    const std::int8_t f = flags[j];
    if (f == fill) {
      continue;  // unclassified
    }
    if (f != sn::status_dry and f != sn::status_wet) {
      return fail(format_error(FormatErrc::bad_flag,
                               std::string{status.name.view()}, station, j));
    }
    if ((f == sn::status_dry) == column[j].is_value()) {
      return fail(format_error(FormatErrc::wet_dry_inconsistent,
                               std::string{data.name.view()}, station, j));
    }
    if (f == sn::status_dry) {
      column[j] = core::Sample{core::Dry{}};
    }
  }
  return {};
}

std::expected<std::vector<core::Column>, Error> read_data(const nc::File& file,
                                                          const DataVar& d,
                                                          const Rows& rows) {
  auto columns = read_values(file, d.var, rows);
  if (not columns or not d.status) {
    return columns;
  }
  const nc::VarInfo& status = *d.status;
  auto flags = read_flags(file, status, rows);
  if (not flags) {
    return std::unexpected{std::move(flags).error()};
  }
  for (std::size_t p = 0; p < rows.spec.selected.size(); ++p) {
    if (auto done = apply_flags((*columns)[p], flags->rows[p], flags->fill,
                                d.var, status, rows.spec.selected[p]);
        not done) {
      return std::unexpected{std::move(done).error()};
    }
  }
  return columns;
}

/// `too_large` unless the samples a read holds fit ReadLimits: the kept
/// samples (boundary), or every selected station's whole row (whole).
std::expected<void, Error> check_size(const nc::File& file, const Rows& rows) {
  const std::array<std::size_t, 2> whole_rows{rows.spec.selected.size(),
                                              rows.spec.sample_length};
  const std::optional<std::size_t> samples =
      rows.spec.padding == PaddingCheck::whole
          ? checked_product(whole_rows)
          : sum_selected(rows.spec.counts, rows.spec.selected);
  return check_rows_size(file, rows.s.data.front().var.name.view(), samples,
                         rows.s.data.size());
}

}  // namespace

std::expected<Read<core::StationTable>, Error> read_table(
    const nc::File& file, const Opened& opened,
    std::span<const std::size_t> selected, PaddingCheck padding,
    const StopToken& stop) {
  const Structure& s = opened.structure;
  std::vector<std::size_t> counts;
  counts.reserve(opened.catalog.stations.size());
  for (const CatalogStation& c : opened.catalog.stations) {
    counts.push_back(c.samples);
  }
  const Rows rows{.s = s,
                  .spec = {.station_dim = s.station.id,
                           .sample_length = s.timing.sample.length,
                           .counts = counts,
                           .selected = selected,
                           .padding = padding,
                           .stop = stop}};
  auto timing = check_size(file, rows)
                    .and_then([&] { return clock_of(file, s.timing.time); })
                    .and_then([&](Read<CfClock> clock) {
                      return read_axes(file, rows, clock.value)
                          .transform([&](std::vector<core::TimeAxis> axes) {
                            return Read<std::vector<core::TimeAxis>>{
                                .value = std::move(axes),
                                .warnings = std::move(clock.warnings)};
                          });
                    });
  if (not timing) {
    return std::unexpected{std::move(timing).error()};
  }
  std::vector<core::Variable> variables;
  variables.reserve(s.data.size());
  for (std::size_t k = 0; k < s.data.size(); ++k) {
    auto columns = read_data(file, s.data[k], rows);
    if (not columns) {
      return std::unexpected{std::move(columns).error()};
    }
    variables.push_back(
        {.meta = opened.catalog.schema[k], .per_station = *std::move(columns)});
  }
  std::vector<core::StationRow> station_rows;
  station_rows.reserve(selected.size());
  for (std::size_t p = 0; p < selected.size(); ++p) {
    station_rows.push_back(
        {.station = opened.catalog.stations[selected[p]].station,
         .axis = s.timing.layout == StationNcLayout::orthogonal ? 0 : p});
  }
  auto table = core::StationTable::make(
      std::move(variables), std::move(timing->value), std::move(station_rows));
  if (not table) {
    return fail(to_format_error(table.error()));
  }
  return Read<core::StationTable>{.value = *std::move(table),
                                  .warnings = std::move(timing->warnings)};
}

}  // namespace mov::io::detail::station_nc
