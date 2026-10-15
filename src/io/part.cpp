// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/part.hpp"

#include <algorithm>
#include <compare>
#include <cstddef>
#include <expected>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/series_ops.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "mov/io/warning.hpp"

namespace mov::io {

/// The accumulator's and clip's access to a part's map: they keep each key
/// with its own series (a unit conversion keeps the quantity).
struct PartAccess {
  [[nodiscard]] static Part::Columns& columns(Part& p) { return p.columns_; }
};

namespace {

[[nodiscard]] std::string_view standard_name(const core::QuantityId& q) {
  const auto* generic = std::get_if<core::GenericQuantity>(&q);
  return generic != nullptr ? generic->standard_name() : std::string_view{};
}

[[nodiscard]] bool metas_agree(const core::SeriesMeta& a,
                               const core::SeriesMeta& b) {
  return a.quantity() == b.quantity() and a.label() == b.label() and
         a.datum() == b.datum();
}

/// One column being folded: its metadata (the first part's) and its rows,
/// strictly increasing by construction; the TimeSeries is built once.
struct Rows {
  core::SeriesMeta meta;
  core::TimeAxis times;
  std::vector<core::Sample> samples;
};

/// The left-biased union of every part added, column by column.
class Accumulator {
 public:
  /// Folds `p` in after the parts before it.
  [[nodiscard]] std::expected<void, MergeError> add(Part p) {
    for (auto& [key, series] : std::move(p).into_columns()) {
      const auto found = columns_.find(key);
      if (found == columns_.end()) {
        auto parts = std::move(series).into_parts();
        columns_.emplace(key, Rows{.meta = std::move(parts.meta),
                                   .times = std::move(parts.times),
                                   .samples = std::move(parts.samples)});
        continue;
      }
      if (not metas_agree(found->second.meta, series.meta())) {
        return std::unexpected{MetaMismatch{.column = key}};
      }
      auto converted = in_unit_of(key, found->second.meta, std::move(series));
      if (not converted) {
        return std::unexpected{std::move(converted).error()};
      }
      unite(key, found->second, std::move(*converted));
    }
    return {};
  }

  /// The union and the rows dropped.
  [[nodiscard]] Combined finish() && {
    Part out;
    for (auto& [key, rows] : columns_) {
      // Strictly increasing by construction: make cannot fail.
      auto series = core::TimeSeries::make(
          std::move(rows.times), std::move(rows.samples), std::move(rows.meta));
      PartAccess::columns(out).emplace(key, *std::move(series));
    }
    return Combined{.part = std::move(out), .dropped = std::move(tally_)};
  }

 private:
  /// `s` in the unit of `meta`, or UnitMismatch: equal units (or none on
  /// both sides) need nothing; one-sided or incompatible units do not
  /// convert.
  [[nodiscard]] static std::expected<core::TimeSeries, UnitMismatch> in_unit_of(
      const ColumnKey& key, const core::SeriesMeta& meta, core::TimeSeries s) {
    const auto& to = meta.unit();
    const auto& from = s.meta().unit();
    if (to == from) {
      return s;
    }
    const UnitMismatch mismatch{.column = key, .first = to, .other = from};
    if (not to or not from) {
      return std::unexpected{mismatch};
    }
    auto converted = core::convert(std::move(s), *to);
    if (not converted) {
      return std::unexpected{mismatch};
    }
    return *std::move(converted);
  }

  /// The rows of `s` into `rows`, the existing rows winning ties.
  void unite(const ColumnKey& key, Rows& rows, core::TimeSeries s) {
    auto incoming = std::move(s).into_parts();
    if (incoming.times.empty()) {
      return;
    }
    if (rows.times.empty() or rows.times.back() < incoming.times.front()) {
      // Disjoint and in order: an append, nothing dropped.
      rows.times.insert(rows.times.end(), incoming.times.begin(),
                        incoming.times.end());
      rows.samples.insert(rows.samples.end(), incoming.samples.begin(),
                          incoming.samples.end());
      return;
    }
    const std::size_t total = rows.times.size() + incoming.times.size();
    core::TimeAxis times;
    std::vector<core::Sample> samples;
    times.reserve(total);
    samples.reserve(total);
    std::size_t i = 0;
    std::size_t j = 0;
    // The merge step of a merge sort: on equal times the existing row is
    // kept and the incoming one skipped (left bias).
    while (i < rows.times.size() or j < incoming.times.size()) {
      const bool take_existing =
          j == incoming.times.size() or
          (i < rows.times.size() and not(incoming.times[j] < rows.times[i]));
      if (take_existing) {
        if (j < incoming.times.size() and incoming.times[j] == rows.times[i]) {
          ++j;  // a duplicate of the existing row
        }
        times.push_back(rows.times[i]);
        samples.push_back(rows.samples[i]);
        ++i;
      } else {
        times.push_back(incoming.times[j]);
        samples.push_back(incoming.samples[j]);
        ++j;
      }
    }
    const std::size_t dropped = total - times.size();
    if (dropped > 0) {
      tally_.dropped[key] += dropped;
    }
    rows.times = std::move(times);
    rows.samples = std::move(samples);
  }

  std::map<ColumnKey, Rows> columns_;
  DropTally tally_;
};

}  // namespace

std::strong_ordering operator<=>(const ColumnKey& a, const ColumnKey& b) {
  if (const auto c = a.quantity.index() <=> b.quantity.index(); c != 0) {
    return c;
  }
  if (const auto c = core::token(a.quantity) <=> core::token(b.quantity);
      c != 0) {
    return c;
  }
  return standard_name(a.quantity) <=> standard_name(b.quantity);
}

std::string format(const ColumnKey& key) {
  return std::string{core::token(key.quantity)};
}

std::expected<void, DuplicateColumn> Part::insert(core::TimeSeries series) {
  ColumnKey key = ColumnKey::of(series);
  if (columns_.contains(key)) {
    return std::unexpected{DuplicateColumn{.column = std::move(key)}};
  }
  columns_.emplace(std::move(key), std::move(series));
  return {};
}

DropTally operator+(DropTally a, const DropTally& b) {
  for (const auto& [key, count] : b.dropped) {
    a.dropped[key] += count;
  }
  return a;
}

std::expected<Combined, MergeError> combine(Part a, Part b) {
  Accumulator acc;
  if (auto added = acc.add(std::move(a)); not added) {
    return std::unexpected{std::move(added).error()};
  }
  if (auto added = acc.add(std::move(b)); not added) {
    return std::unexpected{std::move(added).error()};
  }
  return std::move(acc).finish();
}

std::expected<Read<Part>, MergeError> fold(std::vector<Part> parts) {
  Accumulator acc;
  for (Part& p : parts) {
    if (auto added = acc.add(std::move(p)); not added) {
      return std::unexpected{std::move(added).error()};
    }
  }
  Combined all = std::move(acc).finish();
  std::vector<Warning> warnings;
  warnings.reserve(all.dropped.dropped.size());
  for (const auto& [key, count] : all.dropped.dropped) {
    warnings.push_back(Warning{.code = WarningCode::duplicate_times_dropped,
                               .subject = format(key),
                               .count = count});
  }
  return Read<Part>{.value = std::move(all.part),
                    .warnings = std::move(warnings)};
}

Part clip(Part part, core::TimeRange range) {
  for (auto& [key, series] : PartAccess::columns(part)) {
    series = core::slice(std::move(series), range);
  }
  return part;
}

}  // namespace mov::io
