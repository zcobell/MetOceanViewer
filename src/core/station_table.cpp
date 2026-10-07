// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/station_table.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <functional>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core_access.hpp"

namespace mov::core {

namespace {

std::optional<TableError> check_variables(std::span<const Variable> variables,
                                          std::size_t station_count) {
  std::unordered_set<std::string_view> tokens;
  for (std::size_t k = 0; k < variables.size(); ++k) {
    const Variable& v = variables[k];
    if (not tokens.insert(token(v.meta.quantity())).second) {
      return SchemaError{.code = SchemaErrc::duplicate_quantity,
                         .column = ColumnIndex{k}};
    }
    if (v.per_station.size() != station_count) {
      return SchemaError{.code = SchemaErrc::station_count_mismatch,
                         .column = ColumnIndex{k}};
    }
  }
  return std::nullopt;
}

bool in_file_range(Time t) noexcept {
  const auto ms = t.time_since_epoch().count();
  return ms >= -max_abs_time_ms and ms <= max_abs_time_ms;
}

// The first bad element of an axis: out of the file range, or not above its
// predecessor (whichever comes first).
std::optional<StationFault> check_axis(std::span<const Time> axis) {
  const auto out = std::ranges::find_if_not(axis, in_file_range);
  const auto out_index = static_cast<std::size_t>(out - axis.begin());
  const auto disorder = detail::first_not_increasing(axis.first(out_index));
  if (disorder) {
    return TimeNotIncreasing{.index = *disorder};
  }
  if (out != axis.end()) {
    return TimeOutOfRange{.index = out_index};
  }
  return std::nullopt;
}

// Checks stations one by one against the variables and the axis pool. Each
// axis is checked once, on first use.
class StationChecker {
 public:
  StationChecker(std::span<const Variable> variables,
                 std::span<const TimeAxis> axes)
      : variables_{variables}, axes_{axes}, axis_checked_(axes.size()) {}

  std::optional<StationFault> check(const StationRow& row, std::size_t i) {
    if (not ids_.insert(row.station.id.view()).second) {
      return DuplicateStationId{};
    }
    if (row.axis >= axes_.size()) {
      return AxisOutOfRange{};
    }
    if (auto fault = check_axis_once(row.axis)) {
      return fault;
    }
    return check_columns(axes_[row.axis].size(), i);
  }

 private:
  std::optional<StationFault> check_axis_once(std::size_t a) {
    if (axis_checked_[a]) {
      return std::nullopt;
    }
    axis_checked_[a] = true;
    return check_axis(axes_[a]);
  }

  std::optional<StationFault> check_columns(std::size_t length,
                                            std::size_t i) const {
    const auto bad =
        std::ranges::find_if(variables_, [length, i](const Variable& v) {
          return v.per_station[i].size() != length;
        });
    if (bad != variables_.end()) {
      return ColumnLengthMismatch{
          .column =
              ColumnIndex{static_cast<std::size_t>(bad - variables_.begin())}};
    }
    return std::nullopt;
  }

  std::span<const Variable> variables_;
  std::span<const TimeAxis> axes_;
  std::vector<bool> axis_checked_;
  std::unordered_set<std::string_view> ids_;
};

// Keeps only the axes some row uses, numbered in order of first use.
std::vector<TimeAxis> compact_axes(std::vector<TimeAxis> axes,
                                   std::vector<StationRow>& rows) {
  std::vector<std::optional<std::size_t>> renumbered(axes.size());
  std::vector<TimeAxis> used;
  for (StationRow& row : rows) {
    std::optional<std::size_t>& slot = renumbered[row.axis];
    if (not slot) {
      slot = used.size();
      used.push_back(std::move(axes[row.axis]));
    }
    row.axis = *slot;
  }
  return used;
}

}  // namespace

std::expected<StationSelection, SelectionError> StationSelection::make(
    std::vector<std::size_t> indices, std::size_t station_count) {
  if (std::ranges::any_of(indices, [station_count](std::size_t i) {
        return i >= station_count;
      })) {
    return std::unexpected{SelectionError::out_of_range};
  }
  std::vector<std::size_t> sorted = indices;
  std::ranges::sort(sorted);
  if (std::ranges::adjacent_find(sorted) != sorted.end()) {
    return std::unexpected{SelectionError::duplicate_index};
  }
  return StationSelection{std::move(indices), station_count};
}

StationSelection StationSelection::all(std::size_t station_count) {
  std::vector<std::size_t> indices(station_count);
  std::ranges::copy(std::views::iota(std::size_t{0}, station_count),
                    indices.begin());
  return StationSelection{std::move(indices), station_count};
}

std::expected<StationTable, TableError> StationTable::make(
    std::vector<Variable> variables, std::vector<TimeAxis> axes,
    std::vector<StationRow> stations) {
  if (auto e = check_variables(variables, stations.size())) {
    return std::unexpected{*std::move(e)};
  }
  StationChecker checker{variables, axes};
  for (std::size_t i = 0; i < stations.size(); ++i) {
    if (auto fault = checker.check(stations[i], i)) {
      return std::unexpected{TableError{StationError{
          .station = StationIndex{i}, .fault = *std::move(fault)}}};
    }
  }
  std::vector<SeriesMeta> schema;
  std::vector<std::vector<Column>> columns;
  schema.reserve(variables.size());
  columns.reserve(variables.size());
  for (Variable& v : variables) {
    schema.push_back(std::move(v.meta));
    columns.push_back(std::move(v.per_station));
  }
  std::vector<TimeAxis> used = compact_axes(std::move(axes), stations);
  return StationTable{std::move(schema), std::move(columns), std::move(used),
                      std::move(stations)};
}

std::expected<StationTable, TableError> StationTable::from_series(
    SeriesMeta schema, std::vector<AtStation<FileStation, TimeSeries>> series) {
  const auto differs = std::ranges::find_if(
      series, [&schema](const AtStation<FileStation, TimeSeries>& s) {
        return s.data.meta() != schema;
      });
  if (differs != series.end()) {
    return std::unexpected{TableError{StationError{
        .station =
            StationIndex{static_cast<std::size_t>(differs - series.begin())},
        .fault = SchemaMismatch{}}}};
  }
  Variable variable{.meta = std::move(schema), .per_station = {}};
  std::vector<TimeAxis> axes;
  std::vector<StationRow> rows;
  variable.per_station.reserve(series.size());
  axes.reserve(series.size());
  rows.reserve(series.size());
  for (auto& [station, data] : series) {
    TimeSeriesParts parts = std::move(data).into_parts();
    rows.push_back({.station = std::move(station), .axis = axes.size()});
    axes.push_back(std::move(parts.times));
    variable.per_station.push_back(std::move(parts.samples));
  }
  std::vector<Variable> variables;
  variables.push_back(std::move(variable));
  return make(std::move(variables), std::move(axes), std::move(rows));
}

std::optional<ColumnIndex> StationTable::column_of(
    const QuantityId& q) const noexcept {
  const auto it =
      std::ranges::find_if(schema_, [wanted = token(q)](const SeriesMeta& m) {
        return token(m.quantity()) == wanted;
      });
  if (it == schema_.end()) {
    return std::nullopt;
  }
  return ColumnIndex{static_cast<std::size_t>(it - schema_.begin())};
}

TimeSeries StationTable::series(StationIndex i, ColumnIndex k) const {
  const std::span<const Time> t = times(i);
  const std::span<const Sample> c = column(i, k);
  return TimeSeries{detail::CoreAccess::key(), TimeAxis(t.begin(), t.end()),
                    std::vector<Sample>(c.begin(), c.end()),
                    schema_[k.value()]};
}

bool StationTable::single_axis() const noexcept {
  if (rows_.empty() or axes_[rows_.front().axis].empty()) {
    return false;
  }
  const TimeAxis& first = axes_[rows_.front().axis];
  return std::ranges::all_of(rows_, [this, &first](const StationRow& row) {
    return std::ranges::equal(axes_[row.axis], first);
  });
}

std::size_t StationTable::total_samples() const noexcept {
  return std::transform_reduce(rows_.begin(), rows_.end(), std::size_t{0},
                               std::plus{}, [this](const StationRow& row) {
                                 return axes_[row.axis].size() * schema_.size();
                               });
}

bool operator==(const StationTable& a, const StationTable& b) {
  const auto same_station = [&a, &b](StationIndex i) {
    return a.station(i) == b.station(i) and
           std::ranges::equal(a.times(i), b.times(i));
  };
  return a.schema_ == b.schema_ and a.columns_ == b.columns_ and
         a.size() == b.size() and
         std::ranges::all_of(a.stations(), same_station);
}

}  // namespace mov::core
