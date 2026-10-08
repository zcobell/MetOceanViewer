// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The samples of selected stations of a foreign CF discrete-sampling-geometry
// file (CF 9.3), by representation: a matrix over (station, time) or (time,
// station) for the orthogonal and incomplete layouts, runs of an observation
// dimension for the contiguous ragged layout and for one station, and a pass
// over the observation dimension for the indexed ragged layout. Every value is
// masked in the variable's own type (nc::Masking: _FillValue, missing_value,
// valid_*, the library's default fill, NaN; packing applied after).

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "model_netcdf.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/io/cf_time.hpp"
#include "mov/io/detail/station_groups.hpp"
#include "mov/io/detail/table_error.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read.hpp"
#include "mov/io/station_netcdf.hpp"
#include "station_netcdf_dialects.hpp"
#include "station_netcdf_reader.hpp"

namespace mov::io::detail::station_nc {

namespace {

/// What reading the selected stations of an opened file needs.
struct Wanted {
  const nc::File& file;
  const ForeignOpened& opened;
  std::span<const std::size_t> selected;
  const StopToken& stop;
};

/// The id of the station dimension (a matrix layout has one).
int station_dim_id(const ForeignStructure& s) {
  return s.station.transform([](const nc::DimInfo& d) { return d.id; })
      .value_or(-1);
}

bool station_major(const Wanted& w, const nc::VarInfo& var) {
  return var.dims.front().id == station_dim_id(w.opened.s);
}

/// A matrix over (station, sample): the first `counts` samples of each row.
std::expected<std::vector<core::Column>, Error> station_major_columns(
    const Wanted& w, const nc::VarInfo& var, RowRole role) {
  const ForeignStructure& s = w.opened.s;
  const RowSpec spec{.station_dim = station_dim_id(s),
                     .sample_length = s.sample.length,
                     .counts = w.opened.counts,
                     .selected = w.selected,
                     .padding = PaddingCheck::boundary,
                     .check_padding = false,
                     .stop = w.stop};
  std::vector<core::Column> columns(w.selected.size());
  auto done = sample_rows(
      w.file, var, spec, role,
      [&columns](const SelectedStation& m, std::span<const core::Sample> kept)
          -> std::expected<void, Error> {
        columns[m.position].assign(kept.begin(), kept.end());
        return {};
      });
  if (not done) {
    return std::unexpected{std::move(done).error()};
  }
  return columns;
}

/// A matrix over (sample, station): every time step of the stations' groups
/// (the whole rows are read; only each station's first `counts` are kept).
std::expected<std::vector<core::Column>, Error> time_major_columns(
    const Wanted& w, const nc::VarInfo& var, RowRole role) {
  const ForeignStructure& s = w.opened.s;
  std::vector<core::Column> columns(w.selected.size());
  for (std::size_t p = 0; p < w.selected.size(); ++p) {
    columns[p].assign(w.opened.counts[w.selected[p]], core::Sample{});
  }
  auto groups =
      plan_groups(w.file, var, station_dim_id(s), w.selected, std::nullopt);
  if (not groups) {
    return std::unexpected{std::move(groups).error()};
  }
  const GatherPlan plan{.variable = var.name,
                        .times = s.sample.length,
                        .layer = std::nullopt,
                        .groups = *groups};
  auto done = dispatch_role_numeric(
      role, w.file, var, [&]<class T>() -> std::expected<void, Error> {
        auto mask = w.file.masking<T>(var.name);
        if (not mask) {
          return fail(std::move(mask).error());
        }
        return gather<T>(w.file, plan, w.stop,
                         [&](std::size_t position, std::size_t t, T raw) {
                           core::Column& column = columns[position];
                           if (t < column.size()) {
                             column[t] = mask->apply(raw);
                           }
                         });
      });
  if (not done) {
    return std::unexpected{std::move(done).error()};
  }
  return columns;
}

/// Where the samples of station `i` start in the observation dimension
/// (contiguous ragged; a single station starts at 0).
std::size_t start_of(const ForeignOpened& o, std::size_t i) {
  return o.offsets.empty() ? 0 : o.offsets[i];
}

/// Runs of the observation dimension: the stations are in file order there,
/// so consecutive stations of the selection are one read.
std::expected<std::vector<core::Column>, Error> run_columns(
    const Wanted& w, const nc::VarInfo& var, RowRole role) {
  const ForeignOpened& o = w.opened;
  std::vector<std::size_t> order(w.selected.size());
  std::ranges::copy(std::views::iota(std::size_t{0}, order.size()),
                    order.begin());
  std::ranges::sort(order, {}, [&w](std::size_t p) { return w.selected[p]; });
  std::vector<core::Column> columns(w.selected.size());
  for (std::size_t k = 0; k < order.size();) {
    std::size_t last = k;
    while (last + 1 < order.size() and
           w.selected[order[last + 1]] == w.selected[order[last]] + 1) {
      ++last;
    }
    const std::size_t first_station = w.selected[order[k]];
    const std::size_t last_station = w.selected[order[last]];
    const std::size_t begin = start_of(o, first_station);
    const std::size_t end = start_of(o, last_station) + o.counts[last_station];
    if (end > begin) {
      auto samples = read_masked(
          w.file, var, {{.start = begin, .count = end - begin}}, role, w.stop);
      if (not samples) {
        return std::unexpected{std::move(samples).error()};
      }
      for (std::size_t j = k; j <= last; ++j) {
        const std::size_t station = w.selected[order[j]];
        const auto from = samples->begin() + static_cast<std::ptrdiff_t>(
                                                 start_of(o, station) - begin);
        columns[order[j]].assign(
            from, from + static_cast<std::ptrdiff_t>(o.counts[station]));
      }
    }
    k = last + 1;
  }
  return columns;
}

/// Indexed ragged: one pass over the observation dimension, each sample to the
/// station its index names (in file order within a station).
std::expected<std::vector<core::Column>, Error> indexed_columns(
    const Wanted& w, const nc::VarInfo& var, RowRole role) {
  const ForeignOpened& o = w.opened;
  const std::size_t stations = o.counts.size();
  std::vector<std::optional<std::size_t>> position(stations);
  std::vector<core::Column> columns(w.selected.size());
  bool any = false;
  for (std::size_t p = 0; p < w.selected.size(); ++p) {
    position[w.selected[p]] = p;
    columns[p].assign(o.counts[w.selected[p]], core::Sample{});
    any = any or o.counts[w.selected[p]] > 0;
  }
  if (not any) {
    return columns;
  }
  std::vector<std::size_t> next(stations, 0);
  auto done = dispatch_role_numeric(
      role, w.file, var, [&]<class T>() -> std::expected<void, Error> {
        auto mask = w.file.masking<T>(var.name);
        if (not mask) {
          return fail(std::move(mask).error());
        }
        return w.file.read_blocks<T>(
            var.name, nc::whole(var),
            [&](std::span<const T> block,
                nc::DimRange outer) -> std::expected<void, Error> {
              for (std::size_t k = 0; k < block.size(); ++k) {
                const std::size_t station = o.index[outer.start + k];
                if (position[station]) {
                  columns[*position[station]][next[station]++] =
                      mask->apply(block[k]);
                }
              }
              return {};
            },
            w.stop);
      });
  if (not done) {
    return std::unexpected{std::move(done).error()};
  }
  return columns;
}

/// The selected stations' samples of `var`, in the order of the selection.
std::expected<std::vector<core::Column>, Error> read_column(
    const Wanted& w, const nc::VarInfo& var, RowRole role) {
  switch (w.opened.s.layout) {
    case CfDsgLayout::orthogonal:
    case CfDsgLayout::incomplete:
      return station_major(w, var) ? station_major_columns(w, var, role)
                                   : time_major_columns(w, var, role);
    case CfDsgLayout::contiguous_ragged:
    case CfDsgLayout::single_station:
      return run_columns(w, var, role);
    case CfDsgLayout::indexed_ragged:
      return indexed_columns(w, var, role);
  }
  return std::vector<core::Column>{};
}

/// The time axes: one shared axis (orthogonal) or one per selected station.
std::expected<std::vector<core::TimeAxis>, Error> read_axes(
    const Wanted& w, const CfClock& clock) {
  const ForeignStructure& s = w.opened.s;
  if (s.layout == CfDsgLayout::orthogonal) {
    return read_time_axis(w.file, s.time, clock, w.stop)
        .transform([](core::TimeAxis axis) {
          return std::vector<core::TimeAxis>{std::move(axis)};
        });
  }
  auto columns = read_column(w, s.time, RowRole::times);
  if (not columns) {
    return std::unexpected{std::move(columns).error()};
  }
  std::vector<core::TimeAxis> axes;
  axes.reserve(columns->size());
  for (std::size_t p = 0; p < columns->size(); ++p) {
    auto axis =
        axis_of((*columns)[p], clock, s.time.name.view(), w.selected[p]);
    if (not axis) {
      return std::unexpected{std::move(axis).error()};
    }
    axes.push_back(*std::move(axis));
  }
  return axes;
}

std::expected<void, Error> check_size(const Wanted& w) {
  const ForeignStructure& s = w.opened.s;
  std::size_t samples = 0;
  for (const std::size_t i : w.selected) {
    samples += w.opened.counts[i];
  }
  return check_result_size(w.file, s.data.front().name.view(), 1, samples,
                           s.data.size() + 1);
}

}  // namespace

std::expected<Read<core::StationTable>, Error> read_foreign(
    const nc::File& file, const ForeignOpened& opened,
    std::span<const std::size_t> selected, const StopToken& stop) {
  const Wanted w{
      .file = file, .opened = opened, .selected = selected, .stop = stop};
  const ForeignStructure& s = opened.s;
  auto clock = check_size(w).and_then([&] { return clock_of(file, s.time); });
  if (not clock) {
    return std::unexpected{std::move(clock).error()};
  }
  auto axes = read_axes(w, clock->value);
  if (not axes) {
    return std::unexpected{std::move(axes).error()};
  }
  std::vector<core::Variable> variables;
  variables.reserve(s.data.size());
  for (std::size_t k = 0; k < s.data.size(); ++k) {
    auto columns = read_column(w, s.data[k], RowRole::values);
    if (not columns) {
      return std::unexpected{std::move(columns).error()};
    }
    variables.push_back(
        {.meta = opened.catalog.schema[k], .per_station = *std::move(columns)});
  }
  std::vector<core::StationRow> rows;
  rows.reserve(selected.size());
  for (std::size_t p = 0; p < selected.size(); ++p) {
    rows.push_back({.station = opened.catalog.stations[selected[p]].station,
                    .axis = s.layout == CfDsgLayout::orthogonal ? 0 : p});
  }
  auto table = core::StationTable::make(std::move(variables), *std::move(axes),
                                        std::move(rows));
  if (not table) {
    return fail(to_format_error(table.error()));
  }
  return Read<core::StationTable>{.value = *std::move(table),
                                  .warnings = std::move(clock->warnings)};
}

}  // namespace mov::io::detail::station_nc
