// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The samples of a v5 station netCDF file (docs/station-netcdf.md 7, 8,
// 12.4): the times and the data of the selected stations, read a group of
// stations at a time (the chunk-aware grouping of WP9) and only as far as the
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
#include "station_netcdf_reader.hpp"

namespace mov::io::detail::station_nc {

namespace {

namespace sn = ::mov::io::detail::station_nc;

/// What a v5 read needs besides the file: its structure and the generic row
/// specification.
struct Rows {
  const Structure& s;
  RowSpec spec;
};

/// How far into the sample dimension a group reads: its longest station's
/// samples and the first padding element (boundary), or all of it (whole).
std::size_t read_length(const RowSpec& rows, const StationGroup& group) {
  const std::size_t obs = rows.sample_length;
  if (rows.padding == PaddingCheck::whole) {
    return obs;
  }
  std::size_t longest = 0;
  for (const SelectedStation& m : group.members) {
    longest = std::max(longest, rows.counts[m.station]);
  }
  // Without a padding check nothing past the samples is read.
  return std::min(obs, rows.padding ? longest + 1 : longest);
}

/// Calls sink(member, row) for each selected station with its row of `var`
/// (station, sample) as raw values of type T, read_length(group) long.
template <nc::Numeric T, class Sink>
std::expected<void, Error> raw_rows(const nc::File& file, nc::NcNameRef var,
                                    std::span<const StationGroup> groups,
                                    const RowSpec& rows, const Sink& sink) {
  for (const StationGroup& group : groups) {
    const std::size_t length = read_length(rows, group);
    const nc::Slab slab{{.start = group.first, .count = group.width()},
                        {.start = 0, .count = length}};
    auto done = file.read_blocks<T>(
        var, slab,
        [&](std::span<const T> block,
            nc::DimRange outer) -> std::expected<void, Error> {
          for (const SelectedStation& m : group.members) {
            if (m.station < outer.start or
                m.station >= outer.start + outer.count) {
              continue;
            }
            const std::size_t row = m.station - outer.start;
            if (auto r = sink(m, block.subspan(row * length, length)); not r) {
              return r;
            }
          }
          return {};
        },
        rows.stop);
    if (not done) {
      return done;
    }
  }
  return {};
}

/// padding_not_missing at the first element of `tail` (the padding after the
/// station's `count` samples) for which `present` holds.
template <class T, class Present>
std::expected<void, Error> check_padding(std::span<const T> tail,
                                         std::size_t count,
                                         std::string_view var,
                                         std::size_t station,
                                         const Present& present) {
  const auto it = std::ranges::find_if(tail, present);
  if (it != tail.end()) {
    return fail(
        format_error(FormatErrc::padding_not_missing, std::string{var}, station,
                     count + static_cast<std::size_t>(it - tail.begin())));
  }
  return {};
}

/// The kept samples of each selected station of `var` as a T, masked in T;
/// the padding read is checked for fill when `rows.padding` says so.
template <nc::Numeric T>
std::expected<void, Error> rows_as(const nc::File& file, const nc::VarInfo& var,
                                   std::span<const StationGroup> groups,
                                   const RowSpec& rows, const RowSink& sink) {
  auto mask = file.masking<T>(var.name);
  if (not mask) {
    return fail(std::move(mask).error());
  }
  std::vector<core::Sample> kept;
  return raw_rows<T>(
      file, var.name, groups, rows,
      [&](const SelectedStation& m,
          std::span<const T> raw) -> std::expected<void, Error> {
        const std::size_t n = rows.counts[m.station];
        if (rows.padding) {
          auto padding = check_padding(
              raw.subspan(n), n, var.name.view(), m.station,
              [&](T x) { return not mask->apply(x).is_missing(); });
          if (not padding) {
            return padding;
          }
        }
        kept.resize(n);
        std::ranges::transform(raw.first(n), kept.begin(),
                               [&](T x) { return mask->apply(x); });
        return sink(m, std::span<const core::Sample>{kept});
      });
}

}  // namespace

std::expected<void, Error> sample_rows(const nc::File& file,
                                       const nc::VarInfo& var,
                                       const RowSpec& rows, RowRole role,
                                       const RowSink& sink) {
  auto groups =
      plan_groups(file, var, rows.station_dim, rows.selected, std::nullopt);
  if (not groups) {
    return std::unexpected{std::move(groups).error()};
  }
  if (role == RowRole::times and var.type == nc::Type::int64) {
    // A 64-bit time is read as integers (masked in its own type); checked_time
    // bounds it, so the widening to a Sample cannot hide an out-of-range time.
    return rows_as<std::int64_t>(file, var, *groups, rows, sink);
  }
  return dispatch_model_numeric(
      file, var, [&]<class T>() -> std::expected<void, Error> {
        return rows_as<T>(file, var, *groups, rows, sink);
      });
}

std::expected<std::vector<core::Sample>, Error> read_masked(
    const nc::File& file, const nc::VarInfo& var, const nc::Slab& slab,
    RowRole role, const StopToken& stop) {
  if (role != RowRole::times or var.type != nc::Type::int64) {
    return file.read_samples(var.name, slab, stop);
  }
  auto mask = file.masking<std::int64_t>(var.name);
  if (not mask) {
    return fail(std::move(mask).error());
  }
  auto raw = file.read<std::int64_t>(var.name, slab, stop);
  if (not raw) {
    return std::unexpected{std::move(raw).error()};
  }
  std::vector<core::Sample> out(raw->size());
  std::ranges::transform(*raw, out.begin(),
                         [&mask](std::int64_t x) { return mask->apply(x); });
  return out;
}

std::expected<core::TimeAxis, Error> times_of(
    std::span<const core::Sample> row, const CfClock& clock,
    std::string_view var, std::optional<std::size_t> station) {
  core::TimeAxis times;
  times.reserve(row.size());
  for (std::size_t j = 0; j < row.size(); ++j) {
    const std::optional<double> x = row[j].value();
    if (not x) {
      return fail(
          format_error(FormatErrc::time_missing, std::string{var}, station, j));
    }
    const auto instant = clock.at(*x);
    if (not instant) {
      return fail(format_error(FormatErrc::time_out_of_range, std::string{var},
                               station, j));
    }
    times.push_back(*instant);
  }
  return times;
}

std::optional<std::size_t> sum_selected(std::span<const std::size_t> counts,
                                        std::span<const std::size_t> selected) {
  std::size_t total = 0;
  for (const std::size_t i : selected) {
    if (counts[i] > std::numeric_limits<std::size_t>::max() - total) {
      return std::nullopt;
    }
    total += counts[i];
  }
  return total;
}

std::expected<void, Error> check_rows_size(const nc::File& file,
                                           std::string_view variable,
                                           std::optional<std::size_t> samples,
                                           std::size_t columns) {
  if (not samples) {
    return fail(
        nc_fault(file, WrapperFault::too_large, NcOp::get_var, variable));
  }
  return check_result_size(file, variable, 1, *samples, columns);
}

std::expected<core::TimeAxis, Error> axis_of(std::span<const core::Sample> row,
                                             const CfClock& clock,
                                             std::string_view var,
                                             std::size_t station) {
  auto times = times_of(row, clock, var, station);
  if (not times) {
    return times;
  }
  const auto descent = std::ranges::adjacent_find(
      *times, [](core::Time a, core::Time b) { return not(a < b); });
  if (descent != times->end()) {
    return fail(
        format_error(FormatErrc::time_not_increasing, std::string{var}, station,
                     static_cast<std::size_t>(descent - times->begin()) + 1));
  }
  return times;
}

std::expected<Read<CfClock>, Error> clock_of(const nc::File& file,
                                             const nc::VarInfo& time_var) {
  auto units = optional_text(file, time_var.name, "units");
  if (not units) {
    return std::unexpected{std::move(units).error()};
  }
  if (not *units) {
    return fail(format_error(FormatErrc::missing_attribute,
                             std::string{time_var.name.view()} + ":units"));
  }
  auto parsed = parse_cf_time_units(**units);
  if (not parsed) {
    return fail(std::move(parsed).error());
  }
  return read_calendar(file, time_var)
      .and_then([&](CfCalendar cal) {
        return make_clock(parsed->value, cal, time_var.name.view());
      })
      .transform([&](CfClock clock) {
        return Read<CfClock>{.value = clock,
                             .warnings = std::move(parsed->warnings)};
      });
}

namespace {

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
        return axis_of(kept, clock, time.name.view(), m.station)
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

/// The wet/dry flags at each selected station, raw (a flag outside
/// valid_range must be seen, not masked); the padding read must be fill.
std::expected<Flags, Error> read_flags(const nc::File& file,
                                       const nc::VarInfo& var,
                                       const Rows& rows) {
  auto setup = collect(
      [&] {
        return file.masking<std::int8_t>(var.name).transform_error(lift<Error>);
      },
      [&] {
        return plan_groups(file, var, rows.spec.station_dim, rows.spec.selected,
                           std::nullopt);
      });
  if (not setup) {
    return std::unexpected{std::move(setup).error()};
  }
  const auto& [mask, groups] = *setup;
  Flags flags{
      .rows = std::vector<std::vector<std::int8_t>>(rows.spec.selected.size()),
      .fill = mask.fill};
  auto done = raw_rows<std::int8_t>(
      file, var.name, groups, rows.spec,
      [&](const SelectedStation& m,
          std::span<const std::int8_t> raw) -> std::expected<void, Error> {
        const std::size_t n = rows.spec.counts[m.station];
        auto padding =
            check_padding(raw.subspan(n), n, var.name.view(), m.station,
                          [&](std::int8_t f) { return f != flags.fill; });
        if (not padding) {
          return padding;
        }
        flags.rows[m.position].assign(
            raw.begin(), raw.begin() + static_cast<std::ptrdiff_t>(n));
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
