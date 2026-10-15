// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/fetch/part.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <iterator>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/sample.hpp"
#include "mov/core/units.hpp"

namespace mov::fetch {

/// Builds a Column whose rows are known to increase (a union of two
/// increasing columns).
struct ColumnAccess {
  [[nodiscard]] static Column trusted(core::SeriesMeta meta,
                                      std::vector<core::Point> rows) {
    return Column{std::move(meta), std::move(rows)};
  }
};

namespace {

[[nodiscard]] bool metas_agree(const core::SeriesMeta& a,
                               const core::SeriesMeta& b) {
  return a.quantity() == b.quantity() and a.label() == b.label() and
         a.datum() == b.datum();
}

/// b's rows in a's unit, or UnitMismatch.
[[nodiscard]] std::expected<std::vector<core::Point>, UnitMismatch> in_unit_of(
    const std::string& key, const Column& a, Column b) {
  const auto& to = a.meta().unit();
  const auto& from = b.meta().unit();
  if (to == from) {
    return std::move(b).into_rows();
  }
  const auto mismatch = [&] {
    return UnitMismatch{.column = key, .first = to, .other = from};
  };
  if (not to or not from) {
    return std::unexpected{mismatch()};
  }
  const auto affine = core::conversion(*from, *to);
  if (not affine) {
    return std::unexpected{mismatch()};
  }
  std::vector<core::Point> rows = std::move(b).into_rows();
  for (core::Point& p : rows) {
    if (const auto v = p.sample.value()) {
      p.sample = core::finite_or_missing((*affine)(*v));
    }
  }
  return rows;
}

struct Merged {
  Column column;
  std::size_t dropped;
};

/// The union of two columns of one key: a's rows win on equal times.
[[nodiscard]] std::expected<Merged, MergeError> merge_column(
    const std::string& key, const Column& a, Column b) {
  auto b_rows = in_unit_of(key, a, std::move(b));
  if (not b_rows) {
    return std::unexpected{std::move(b_rows).error()};
  }
  const std::size_t total = a.rows().size() + b_rows->size();
  std::vector<core::Point> rows;
  rows.reserve(total);
  // set_union copies the first range's element of an equivalent pair, and
  // each column has unique times: exactly the left-biased union.
  std::ranges::set_union(a.rows(), *b_rows, std::back_inserter(rows), {},
                         &core::Point::time, &core::Point::time);
  const std::size_t dropped = total - rows.size();
  return Merged{.column = ColumnAccess::trusted(a.meta(), std::move(rows)),
                .dropped = dropped};
}

}  // namespace

std::expected<Column, core::TimeNotIncreasing> Column::make(
    core::SeriesMeta meta, std::vector<core::Point> rows) {
  const auto it = std::ranges::adjacent_find(
      rows, [](const core::Point& x, const core::Point& y) {
        return not(x.time < y.time);
      });
  if (it != rows.end()) {
    return std::unexpected{core::TimeNotIncreasing{
        .index = static_cast<std::size_t>(it - rows.begin()) + 1}};
  }
  return Column{std::move(meta), std::move(rows)};
}

Error lift_merge(MergeError e) {
  return std::visit(
      [](auto&& alternative) -> Error {
        return Error{std::forward<decltype(alternative)>(alternative)};
      },
      std::move(e));
}

std::expected<io::Read<Part>, MergeError> combine(Part a, Part b) {
  std::vector<io::Warning> warnings;
  for (auto& [key, column] : b.columns) {
    const auto found = a.columns.find(key);
    if (found == a.columns.end()) {
      a.columns.emplace(key, std::move(column));
      continue;
    }
    if (not metas_agree(found->second.meta(), column.meta())) {
      return std::unexpected{MetaMismatch{.column = key}};
    }
    auto merged = merge_column(key, found->second, std::move(column));
    if (not merged) {
      return std::unexpected{std::move(merged).error()};
    }
    found->second = std::move(merged->column);
    io::append_if_counted(
        warnings, io::Warning{.code = io::WarningCode::duplicate_times_dropped,
                              .subject = key,
                              .count = merged->dropped});
  }
  return io::Read<Part>{.value = std::move(a), .warnings = std::move(warnings)};
}

std::expected<io::Read<Part>, MergeError> fold(std::vector<Part> parts) {
  io::Read<Part> acc = io::pure(Part{});
  for (Part& p : parts) {
    auto next = combine(std::move(acc.value), std::move(p));
    if (not next) {
      return std::unexpected{std::move(next).error()};
    }
    io::append(acc.warnings, std::move(next->warnings));
    acc.value = std::move(next->value);
  }
  return acc;
}

}  // namespace mov::fetch
