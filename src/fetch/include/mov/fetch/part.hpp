// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// Parts and merging (docs/providers-design.md §5.4): one unit's data by
// column, and the left-biased union that folds a fetch's parts in plan
// order.

#include <cstddef>
#include <expected>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "mov/core/meta.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/fetch/error.hpp"
#include "mov/io/read.hpp"

namespace mov::fetch {

/// One column of a part: its metadata and its rows, strictly increasing in
/// time. Built only by make.
class Column {
 public:
  /// TimeNotIncreasing at the first row whose time is not after the one
  /// before it.
  [[nodiscard]] static std::expected<Column, core::TimeNotIncreasing> make(
      core::SeriesMeta meta, std::vector<core::Point> rows);

  [[nodiscard]] const core::SeriesMeta& meta() const& noexcept { return meta_; }
  const core::SeriesMeta& meta() && = delete;
  [[nodiscard]] std::span<const core::Point> rows() const& noexcept {
    return rows_;
  }
  std::span<const core::Point> rows() && = delete;
  /// The rows, moved out.
  [[nodiscard]] std::vector<core::Point> into_rows() && noexcept {
    return std::move(rows_);
  }

  friend bool operator==(const Column&, const Column&) = default;

 private:
  friend struct ColumnAccess;
  Column(core::SeriesMeta meta, std::vector<core::Point> rows)
      : meta_{std::move(meta)}, rows_{std::move(rows)} {}

  core::SeriesMeta meta_;
  std::vector<core::Point> rows_;
};

/// One unit's data by column key (the parser's choice: a quantity token, or
/// `observed`/`predicted` for a paired product). The empty part is the
/// identity of combine.
struct Part {
  std::map<std::string, Column, std::less<>> columns;
  friend bool operator==(const Part&, const Part&) = default;
};

/// Why two parts do not merge.
using MergeError = std::variant<UnitMismatch, MetaMismatch>;

/// Lifts a merge error into fetch's Error.
[[nodiscard]] Error lift_merge(MergeError e);

/// The union of two parts, column by column. A column in one part only is
/// taken as it is. A column in both: b's values are converted exactly to
/// a's unit (core::conversion; a value that becomes non-finite is Missing),
/// an incompatible or one-sided unit is UnitMismatch; after that the
/// quantity, label and datum must agree (MetaMismatch otherwise; the metas-
/// agree law); the rows are the union by time where a's row wins on equal
/// times (left bias), so the result keeps a's metadata. Every row of b that
/// lost to a row of a is counted as io's duplicate_times_dropped, subject
/// the column key. With one unit per column, combine is associative and
/// Part{} is its identity; with a conversion, folding in plan order (always
/// to the first part's unit) is what defines the result.
[[nodiscard]] std::expected<io::Read<Part>, MergeError> combine(Part a, Part b);

/// combine folded over `parts` from the left, starting from Part{}.
[[nodiscard]] std::expected<io::Read<Part>, MergeError> fold(
    std::vector<Part> parts);

}  // namespace mov::fetch
