// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <cassert>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/detail/core_key.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"

namespace mov::core {

/// A station of a StationTable (0-based).
class StationIndex {
 public:
  constexpr StationIndex() noexcept = default;
  explicit constexpr StationIndex(std::size_t i) noexcept : i_{i} {}
  [[nodiscard]] constexpr std::size_t value() const noexcept { return i_; }
  friend constexpr auto operator<=>(StationIndex, StationIndex) = default;

 private:
  std::size_t i_{};
};

/// A schema entry (column) of a StationTable (0-based).
class ColumnIndex {
 public:
  constexpr ColumnIndex() noexcept = default;
  explicit constexpr ColumnIndex(std::size_t k) noexcept : k_{k} {}
  [[nodiscard]] constexpr std::size_t value() const noexcept { return k_; }
  friend constexpr auto operator<=>(ColumnIndex, ColumnIndex) = default;

 private:
  std::size_t k_{};
};

enum class SelectionError : std::uint8_t {
  duplicate_index,
  out_of_range,
  selection_mismatch,  // applies_to: made for a different station count
};

/// Which stations of a model file to read (C12): distinct 0-based indices,
/// each below the station count it was made for, in the caller's order.
/// Every netCDF model read requires one; there is no default and no implicit
/// "all". The count is part of the value, so a reader checks in O(1) that a
/// selection was made for its file (applies_to).
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
  [[nodiscard]] std::size_t station_count() const noexcept {
    return station_count_;
  }

  /// selection_mismatch unless the selection was made for station_count.
  [[nodiscard]] std::expected<void, SelectionError> applies_to(
      std::size_t station_count) const noexcept {
    if (station_count != station_count_) {
      return std::unexpected{SelectionError::selection_mismatch};
    }
    return {};
  }

  friend bool operator==(const StationSelection&,
                         const StationSelection&) = default;

 private:
  StationSelection(std::vector<std::size_t> indices,
                   std::size_t station_count) noexcept
      : indices_{std::move(indices)}, station_count_{station_count} {}

  std::vector<std::size_t> indices_;
  std::size_t station_count_;
};

/// The samples of one quantity at one station, parallel to the station's
/// time axis.
using Column = std::vector<Sample>;

/// One schema entry of a StationTable with its data: per_station[i] is the
/// column of station i.
struct Variable {
  SeriesMeta meta;
  std::vector<Column> per_station;
  friend bool operator==(const Variable&, const Variable&) = default;
};

/// One station of a StationTable and the index of its time axis in the
/// axis pool.
struct StationRow {
  FileStation station;
  std::size_t axis;
  friend bool operator==(const StationRow&, const StationRow&) = default;
};

enum class SchemaErrc : std::uint8_t {
  duplicate_quantity,      // a schema entry repeats an earlier token
  station_count_mismatch,  // per_station.size() != number of stations
};

struct SchemaError {
  SchemaErrc code;
  ColumnIndex column;
  friend constexpr bool operator==(SchemaError, SchemaError) = default;
};

/// The station's id repeats an earlier station's.
struct DuplicateStationId {
  friend constexpr bool operator==(DuplicateStationId,
                                   DuplicateStationId) = default;
};
/// The station's axis index is not in the pool.
struct AxisOutOfRange {
  friend constexpr bool operator==(AxisOutOfRange, AxisOutOfRange) = default;
};
/// |t| > max_abs_time_ms at this element of the station's axis (SN 7: file
/// times are doubles of milliseconds and must be exact).
struct TimeOutOfRange {
  std::size_t index;
  friend constexpr bool operator==(TimeOutOfRange, TimeOutOfRange) = default;
};
/// The station's column of this variable is not as long as its axis.
struct ColumnLengthMismatch {
  ColumnIndex column;
  friend constexpr bool operator==(ColumnLengthMismatch,
                                   ColumnLengthMismatch) = default;
};
/// from_series: the station's series meta differs from the declared schema.
struct SchemaMismatch {
  friend constexpr bool operator==(SchemaMismatch, SchemaMismatch) = default;
};

using StationFault =
    std::variant<DuplicateStationId, AxisOutOfRange, TimeOutOfRange,
                 TimeNotIncreasing, ColumnLengthMismatch, SchemaMismatch>;

struct StationError {
  StationIndex station;
  StationFault fault;
  friend constexpr bool operator==(const StationError&,
                                   const StationError&) = default;
};

using TableError = std::variant<SchemaError, StationError>;

/// The record every file reader returns and every file writer takes (C4).
/// A schema of unique quantities (by token), stations, and per station a
/// time axis plus one sample column per schema entry. Axes live in a pool,
/// so model output, where every station shares one axis, stores its times
/// once.
///
/// make() reports exactly the structural constraints of SN section 12.8
/// that the types do not already rule out (station ids and names are
/// StationKey/StationText, samples are finite by construction), in this
/// order: each variable in order (duplicate_quantity,
/// station_count_mismatch), then each station in order (DuplicateStationId,
/// AxisOutOfRange, the first axis element that is TimeOutOfRange or
/// TimeNotIncreasing, the first ColumnLengthMismatch). Axes no station uses
/// are dropped unchecked.
///
/// Equality is by value: the same stations with the same times and samples
/// are equal however the axes are pooled.
class StationTable {
 public:
  /// No schema and no stations.
  StationTable() = default;

  [[nodiscard]] static std::expected<StationTable, TableError> make(
      std::vector<Variable> variables, std::vector<TimeAxis> axes,
      std::vector<StationRow> stations);

  /// A one-column table with schema {schema} from series that all carry
  /// exactly that meta (provider exports). A series whose meta differs is
  /// StationError{i, SchemaMismatch}. Then make's checks run, so the
  /// conversion from TimeSeries is partial: a TimeSeries may hold times
  /// beyond the file bound of +-max_abs_time_ms, which is TimeOutOfRange.
  /// No series gives a table with the schema and no stations.
  [[nodiscard]] static std::expected<StationTable, TableError> from_series(
      SeriesMeta schema,
      std::vector<AtStation<FileStation, TimeSeries>> series);

  [[nodiscard]] std::span<const SeriesMeta> schema() const& noexcept {
    return schema_;
  }
  std::span<const SeriesMeta> schema() const&& = delete;

  /// The column whose quantity has this token, if any.
  [[nodiscard]] std::optional<ColumnIndex> column_of(
      const QuantityId& q) const noexcept;

  /// The number of stations.
  [[nodiscard]] std::size_t size() const noexcept { return rows_.size(); }

  /// StationIndex{0}, ..., StationIndex{size() - 1}.
  [[nodiscard]] auto stations() const noexcept {
    return std::views::iota(std::size_t{0}, size()) |
           std::views::transform([](std::size_t i) { return StationIndex{i}; });
  }

  // Precondition for the accessors below: i.value() < size(),
  // k.value() < schema().size().

  [[nodiscard]] const FileStation& station(StationIndex i) const& {
    return rows_[i.value()].station;
  }
  const FileStation& station(StationIndex i) const&& = delete;
  [[nodiscard]] std::span<const Time> times(StationIndex i) const& {
    return axes_[rows_[i.value()].axis];
  }
  std::span<const Time> times(StationIndex i) const&& = delete;
  [[nodiscard]] std::span<const Sample> column(StationIndex i,
                                               ColumnIndex k) const& {
    return columns_[k.value()][i.value()];
  }
  std::span<const Sample> column(StationIndex i,
                                 ColumnIndex k) const&& = delete;

  /// A copy of station i's column k as a series with meta schema()[k].
  [[nodiscard]] TimeSeries series(StationIndex i, ColumnIndex k) const;

  /// At least one station, and every station has the same non-empty times
  /// (SN layout L1).
  [[nodiscard]] bool single_axis() const noexcept;

  /// The number of samples over all stations and columns.
  [[nodiscard]] std::size_t total_samples() const noexcept;

  /// Core only (convert): column k with metadata `meta` and f applied to
  /// every sample at every station. The caller keeps the schema's tokens
  /// unique (convert changes only the unit). Precondition:
  /// k.value() < schema().size().
  template <std::invocable<Sample> F>
    requires std::same_as<std::invoke_result_t<F&, Sample>, Sample>
  [[nodiscard]] StationTable rewrite_column(const detail::CoreKey& /*key*/,
                                            ColumnIndex k, SeriesMeta meta,
                                            F f) && {
    assert(k.value() < schema_.size());
    schema_[k.value()] = std::move(meta);
    for (Column& column : columns_[k.value()]) {
      std::ranges::transform(column, column.begin(), f);
    }
    return std::move(*this);
  }

  friend bool operator==(const StationTable& a, const StationTable& b);

 private:
  StationTable(std::vector<SeriesMeta> schema,
               std::vector<std::vector<Column>> columns,
               std::vector<TimeAxis> axes,
               std::vector<StationRow> rows) noexcept
      : schema_{std::move(schema)},
        columns_{std::move(columns)},
        axes_{std::move(axes)},
        rows_{std::move(rows)} {}

  std::vector<SeriesMeta> schema_;
  std::vector<std::vector<Column>> columns_;  // [column][station]
  std::vector<TimeAxis> axes_;
  std::vector<StationRow> rows_;
};

}  // namespace mov::core
