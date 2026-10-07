// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/station_table.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <functional>
#include <numeric>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "mov/core/detail/utf8.hpp"

namespace mov::core {

namespace {

using Axis = std::vector<Time>;

TableError station_error(TableErrc code, std::size_t station,
                         std::optional<std::size_t> index = std::nullopt) {
  return {.code = code, .station = station, .index = index};
}

std::optional<TableError> check_schema(std::span<const SeriesMeta> schema) {
  std::unordered_set<std::string_view> tokens;
  for (std::size_t k = 0; k < schema.size(); ++k) {
    if (not tokens.insert(token(schema[k].quantity())).second) {
      return TableError{.code = TableErrc::duplicate_quantity,
                        .station = std::nullopt,
                        .index = k};
    }
  }
  return std::nullopt;
}

bool has_nul(std::string_view s) noexcept {
  return s.find('\0') != std::string_view::npos;
}

std::optional<TableErrc> check_text(const FileStation& s) noexcept {
  if (s.id.empty()) {
    return TableErrc::empty_station_id;
  }
  if (has_nul(s.id) or has_nul(s.name)) {
    return TableErrc::embedded_nul;
  }
  if (not detail::is_valid_utf8(s.id) or not detail::is_valid_utf8(s.name)) {
    return TableErrc::invalid_utf8;
  }
  return std::nullopt;
}

bool in_range(Time t) noexcept {
  const auto ms = t.time_since_epoch().count();
  return ms >= -max_abs_time_ms and ms <= max_abs_time_ms;
}

// The first bad element of an axis and what is wrong with it.
std::optional<std::pair<TableErrc, std::size_t>> check_axis(const Axis& axis) {
  for (std::size_t j = 0; j < axis.size(); ++j) {
    if (not in_range(axis[j])) {
      return std::pair{TableErrc::time_out_of_range, j};
    }
    if (j > 0 and not(axis[j - 1] < axis[j])) {
      return std::pair{TableErrc::time_not_increasing, j};
    }
  }
  return std::nullopt;
}

// Checks rows one by one against the schema size and the axis pool. Each
// axis is checked once, on first use.
class RowChecker {
 public:
  RowChecker(std::size_t schema_size, const std::vector<Axis>& axes)
      : schema_size_{schema_size}, axes_{axes}, axis_checked_(axes.size()) {}

  std::optional<TableError> check(const StationRow& row, std::size_t i) {
    if (const auto code = check_station(row.station)) {
      return station_error(*code, i);
    }
    if (row.axis >= axes_.size()) {
      return station_error(TableErrc::axis_out_of_range, i);
    }
    if (const auto bad = check_axis_once(row.axis)) {
      return station_error(bad->first, i, bad->second);
    }
    return check_columns(row, i);
  }

 private:
  std::optional<TableErrc> check_station(const FileStation& s) {
    if (const auto code = check_text(s)) {
      return code;
    }
    if (not ids_.insert(s.id).second) {
      return TableErrc::duplicate_station_id;
    }
    return std::nullopt;
  }

  std::optional<std::pair<TableErrc, std::size_t>> check_axis_once(
      std::size_t a) {
    if (axis_checked_[a]) {
      return std::nullopt;
    }
    axis_checked_[a] = true;
    return check_axis(axes_[a]);
  }

  std::optional<TableError> check_columns(const StationRow& row,
                                          std::size_t i) const {
    if (row.columns.size() != schema_size_) {
      return station_error(TableErrc::column_count_mismatch, i);
    }
    const std::size_t length = axes_[row.axis].size();
    const auto bad = std::ranges::find_if(
        row.columns, [length](const Column& c) { return c.size() != length; });
    if (bad != row.columns.end()) {
      return station_error(TableErrc::column_length_mismatch, i,
                           static_cast<std::size_t>(bad - row.columns.begin()));
    }
    return std::nullopt;
  }

  std::size_t schema_size_;
  const std::vector<Axis>& axes_;
  std::vector<bool> axis_checked_;
  std::unordered_set<std::string_view> ids_;
};

// Keeps only the axes some row uses, numbered in order of first use.
std::vector<Axis> compact_axes(std::vector<Axis> axes,
                               std::vector<StationRow>& rows) {
  std::vector<std::optional<std::size_t>> renumbered(axes.size());
  std::vector<Axis> used;
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
  return StationSelection{std::move(indices)};
}

StationSelection StationSelection::all(std::size_t station_count) {
  std::vector<std::size_t> indices(station_count);
  for (std::size_t i = 0; i < station_count; ++i) {
    indices[i] = i;
  }
  return StationSelection{std::move(indices)};
}

std::expected<StationTable, TableError> StationTable::make(
    std::vector<SeriesMeta> schema, std::vector<std::vector<Time>> axes,
    std::vector<StationRow> rows) {
  if (const auto e = check_schema(schema)) {
    return std::unexpected{*e};
  }
  RowChecker checker{schema.size(), axes};
  for (std::size_t i = 0; i < rows.size(); ++i) {
    if (const auto e = checker.check(rows[i], i)) {
      return std::unexpected{*e};
    }
  }
  std::vector<Axis> used = compact_axes(std::move(axes), rows);
  return StationTable{std::move(schema), std::move(used), std::move(rows)};
}

std::expected<StationTable, TableError> StationTable::from_series(
    std::vector<AtStation<FileStation, TimeSeries>> series) {
  if (series.empty()) {
    return StationTable{};
  }
  const SeriesMeta& meta = series.front().data.meta();
  std::vector<Axis> axes;
  std::vector<StationRow> rows;
  axes.reserve(series.size());
  rows.reserve(series.size());
  for (std::size_t i = 0; i < series.size(); ++i) {
    auto& [station, data] = series[i];
    if (data.meta() != meta) {
      return std::unexpected{station_error(TableErrc::schema_mismatch, i)};
    }
    axes.emplace_back(data.times().begin(), data.times().end());
    rows.push_back(
        {.station = std::move(station),
         .axis = i,
         .columns = {Column(data.samples().begin(), data.samples().end())}});
  }
  return make({meta}, std::move(axes), std::move(rows));
}

TimeSeries StationTable::series(std::size_t i, std::size_t k) const {
  const std::span<const Time> t = times(i);
  const std::span<const Sample> c = column(i, k);
  return detail::trusted_series(std::vector<Time>(t.begin(), t.end()),
                                std::vector<Sample>(c.begin(), c.end()),
                                schema_[k]);
}

bool StationTable::single_axis() const noexcept {
  if (rows_.empty() or times(0).empty()) {
    return false;
  }
  return std::ranges::all_of(rows_, [this](const StationRow& row) {
    return row.axis == rows_.front().axis or
           std::ranges::equal(axes_[row.axis], times(0));
  });
}

std::size_t StationTable::total_samples() const noexcept {
  return std::transform_reduce(rows_.begin(), rows_.end(), std::size_t{0},
                               std::plus{}, [this](const StationRow& row) {
                                 return axes_[row.axis].size() * schema_.size();
                               });
}

bool operator==(const StationTable& a, const StationTable& b) {
  if (a.schema_ != b.schema_ or a.size() != b.size()) {
    return false;
  }
  for (std::size_t i = 0; i < a.size(); ++i) {
    const StationRow& x = a.rows_[i];
    const StationRow& y = b.rows_[i];
    if (x.station != y.station or x.columns != y.columns or
        not std::ranges::equal(a.times(i), b.times(i))) {
      return false;
    }
  }
  return true;
}

}  // namespace mov::core
