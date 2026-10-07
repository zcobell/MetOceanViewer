// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "mov/core/meta.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"

namespace mov::core {

enum class SelectionError : std::uint8_t { duplicate_index, out_of_range };

/// Which stations of a model file to read (C12): distinct 0-based indices,
/// each below the file's station count, in the caller's order. Every netCDF
/// model read requires one; there is no default and no implicit "all".
class StationSelection {
 public:
  /// out_of_range is checked over all indices before duplicate_index. An
  /// empty selection is valid and reads no station.
  [[nodiscard]] static std::expected<StationSelection, SelectionError> make(
      std::vector<std::size_t> indices, std::size_t station_count);

  /// 0, 1, ..., station_count - 1.
  [[nodiscard]] static StationSelection all(std::size_t station_count);

  [[nodiscard]] std::span<const std::size_t> indices() const& noexcept {
    return indices_;
  }
  std::span<const std::size_t> indices() const&& = delete;

  friend bool operator==(const StationSelection&,
                         const StationSelection&) = default;

 private:
  explicit StationSelection(std::vector<std::size_t> indices) noexcept
      : indices_{std::move(indices)} {}

  std::vector<std::size_t> indices_;
};

/// The samples of one quantity at one station, parallel to the station's
/// time axis.
using Column = std::vector<Sample>;

/// One station of a StationTable: the index of its time axis in the axis
/// pool and one column per schema entry.
struct StationRow {
  FileStation station;
  std::size_t axis;
  std::vector<Column> columns;
  friend bool operator==(const StationRow&, const StationRow&) = default;
};

enum class TableErrc : std::uint8_t {
  duplicate_quantity,      // index: the later schema entry with a seen token
  axis_out_of_range,       // station
  time_not_increasing,     // station, index: element of its axis
  time_out_of_range,       // station, index: element with |t| > max_abs_time_ms
  column_count_mismatch,   // station
  column_length_mismatch,  // station, index: schema column
  empty_station_id,        // station
  duplicate_station_id,    // station: the later one
  embedded_nul,            // station: in its id or name
  invalid_utf8,            // station: in its id or name
  schema_mismatch,         // from_series only, station: meta differs from #0
};

struct TableError {
  TableErrc code;
  std::optional<std::size_t> station;
  std::optional<std::size_t> index;
  friend constexpr bool operator==(const TableError&,
                                   const TableError&) = default;
};

/// The record every file reader returns and every file writer takes (C4).
/// A schema of unique quantities (by token), stations, and per station a
/// time axis plus one sample column per schema entry. Axes live in a pool,
/// so model output, where every station shares one axis, stores its times
/// once.
///
/// make() reports exactly the structural constraints of SN section 12.8
/// that the types do not already rule out, in this order: the schema
/// (duplicate_quantity), then each station in order: its id and name
/// (empty_station_id, embedded_nul, invalid_utf8, duplicate_station_id),
/// its axis (axis_out_of_range, then the first element that is out of
/// range or not above its predecessor), its columns (column_count_mismatch,
/// column_length_mismatch). Axes no station uses are dropped unchecked.
///
/// Equality is by value: the same stations with the same times and samples
/// are equal however the axes are pooled.
class StationTable {
 public:
  /// No schema and no stations.
  StationTable() = default;

  [[nodiscard]] static std::expected<StationTable, TableError> make(
      std::vector<SeriesMeta> schema, std::vector<std::vector<Time>> axes,
      std::vector<StationRow> rows);

  /// A one-column table from series that share one SeriesMeta (provider
  /// exports): schema_mismatch for the first series whose meta differs from
  /// the first one's, then make's checks. No series gives an empty table.
  [[nodiscard]] static std::expected<StationTable, TableError> from_series(
      std::vector<AtStation<FileStation, TimeSeries>> series);

  [[nodiscard]] std::span<const SeriesMeta> schema() const& noexcept {
    return schema_;
  }
  std::span<const SeriesMeta> schema() const&& = delete;

  /// The number of stations.
  [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }

  // Precondition for the accessors below: i < size(), k < schema().size().

  [[nodiscard]] const FileStation& station(std::size_t i) const& {
    return rows_[i].station;
  }
  const FileStation& station(std::size_t i) const&& = delete;
  [[nodiscard]] std::span<const Time> times(std::size_t i) const& {
    return axes_[rows_[i].axis];
  }
  std::span<const Time> times(std::size_t i) const&& = delete;
  [[nodiscard]] std::span<const Sample> column(std::size_t i,
                                               std::size_t k) const& {
    return rows_[i].columns[k];
  }
  std::span<const Sample> column(std::size_t i, std::size_t k) const&& = delete;

  /// A copy of station i's column k as a series with meta schema()[k].
  [[nodiscard]] TimeSeries series(std::size_t i, std::size_t k) const;

  /// At least one station, and every station has the same non-empty times
  /// (SN layout L1).
  [[nodiscard]] bool single_axis() const noexcept;

  /// The number of samples over all stations and columns.
  [[nodiscard]] std::size_t total_samples() const noexcept;

  friend bool operator==(const StationTable& a, const StationTable& b);

 private:
  StationTable(std::vector<SeriesMeta> schema,
               std::vector<std::vector<Time>> axes,
               std::vector<StationRow> rows) noexcept
      : schema_{std::move(schema)},
        axes_{std::move(axes)},
        rows_{std::move(rows)} {}

  std::vector<SeriesMeta> schema_;
  std::vector<std::vector<Time>> axes_;
  std::vector<StationRow> rows_;
};

}  // namespace mov::core
