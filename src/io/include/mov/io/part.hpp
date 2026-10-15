// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// Parts and their merge (docs/providers-design.md §4.3, §5.4): what a
// provider parser returns for one unit of a fetch, column by column, and the
// left-biased union that folds a fetch's parts in plan order. In io because
// the parsers build parts; fetch re-exports the names.

#include <compare>
#include <cstddef>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/quantity.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "mov/io/read.hpp"

namespace mov::io {

/// A part's column: the identity of its series' quantity (the registry
/// Quantity, or the GenericQuantity's token and standard name), so a
/// predicted water level (water_level_prediction) and an observed one
/// (water_level) are two columns. Ordered by kind (generic before
/// registry), token and standard name, so it is a map key.
struct ColumnKey {
  core::QuantityId quantity;

  /// The key of a series: its metadata's quantity.
  [[nodiscard]] static ColumnKey of(const core::TimeSeries& series) {
    return ColumnKey{.quantity = series.meta().quantity()};
  }

  friend bool operator==(const ColumnKey&, const ColumnKey&) = default;
  friend std::strong_ordering operator<=>(const ColumnKey& a,
                                          const ColumnKey& b);
};

/// The quantity's token ("water_level"): for a Warning's subject only.
[[nodiscard]] std::string format(const ColumnKey& key);

/// A second series of a quantity the part already has.
struct DuplicateColumn {
  ColumnKey column;
  friend bool operator==(const DuplicateColumn&,
                         const DuplicateColumn&) = default;
};

/// One unit's data by column: each column a core::TimeSeries (strictly
/// increasing times, its own metadata), keyed by its own quantity, so a key
/// cannot disagree with its series. The empty part is the identity of
/// combine.
class Part {
 public:
  using Columns = std::map<ColumnKey, core::TimeSeries>;

  /// Adds a series under ColumnKey::of(series); DuplicateColumn when the
  /// part has its quantity already (the part is unchanged).
  [[nodiscard]] std::expected<void, DuplicateColumn> insert(
      core::TimeSeries series);

  [[nodiscard]] const Columns& columns() const& noexcept { return columns_; }
  const Columns& columns() && = delete;
  /// The columns, moved out.
  [[nodiscard]] Columns into_columns() && noexcept {
    return std::move(columns_);
  }

  [[nodiscard]] bool empty() const noexcept { return columns_.empty(); }

  friend bool operator==(const Part&, const Part&) = default;

 private:
  friend struct PartAccess;
  Columns columns_;
};

/// Two parts disagree on a column's unit and neither converts to the other
/// (either may be unset).
struct UnitMismatch {
  ColumnKey column;
  std::optional<core::Unit> first{};
  std::optional<core::Unit> other{};
  friend bool operator==(const UnitMismatch&, const UnitMismatch&) = default;
};

/// Two parts disagree on a column's quantity, label or datum.
struct MetaMismatch {
  ColumnKey column;
  friend bool operator==(const MetaMismatch&, const MetaMismatch&) = default;
};

/// Why two parts do not merge.
using MergeError = std::variant<UnitMismatch, MetaMismatch>;

/// Rows that lost to an earlier part's row at the same time, by column: a
/// monoid under + with {} as identity, so the count of a fold does not
/// depend on how it is bracketed.
struct DropTally {
  std::map<ColumnKey, std::size_t> dropped;

  friend DropTally operator+(DropTally a, const DropTally& b);
  friend bool operator==(const DropTally&, const DropTally&) = default;
};

/// A combine's result: the union and the rows it dropped.
struct Combined {
  Part part;
  DropTally dropped;
};

/// The union of two parts, column by column. A column in one part only is
/// taken as it is. A column in both: first the quantity, label and datum
/// must agree (MetaMismatch; the metas-agree law), then b's values are
/// converted exactly to a's unit (core::conversion; a value that becomes
/// non-finite is Missing; an incompatible or one-sided unit is
/// UnitMismatch); the rows are the union by time where a's row wins on equal
/// times (left bias), so the result keeps a's metadata. Associative, with
/// Part{} as identity, when each column has one unit; with a conversion,
/// folding in plan order (always to the first part's unit) defines the
/// result. When every row of b is after every row of a the union is an
/// append (the common case: disjoint windows in order). O(rows of a and b).
[[nodiscard]] std::expected<Combined, MergeError> combine(Part a, Part b);

/// combine folded over `parts` from the left, starting from Part{}, with
/// the same result; one duplicate_times_dropped warning per column that
/// dropped rows (subject: format(key)), in key order. Each column is built
/// once at the end, so parts in time order cost O(total rows).
[[nodiscard]] std::expected<Read<Part>, MergeError> fold(
    std::vector<Part> parts);

/// The part with only the rows in `range` (core::slice): for a file whose
/// name does not say which year it holds (NDBC's latest-month file), so rows
/// outside the request neither reach the merge nor count as duplicates.
[[nodiscard]] Part clip(Part part, core::TimeRange range);

}  // namespace mov::io
