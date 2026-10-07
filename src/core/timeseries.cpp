// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/timeseries.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <functional>
#include <iterator>
#include <numeric>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace mov::core {

namespace {

// Adjacent input pairs with t[i+1] < t[i]. (A transform_reduce over the
// shifted range: views::pairwise is not on every target standard library.)
std::size_t count_descents(std::span<const Point> rows) {
  if (rows.empty()) {
    return 0;
  }
  return std::transform_reduce(
      rows.begin(), std::prev(rows.end()), std::next(rows.begin()),
      std::size_t{0}, std::plus{}, [](const Point& a, const Point& b) {
        return b.time < a.time ? std::size_t{1} : std::size_t{0};
      });
}

// What dropping all but the first row of each run of equal times costs: a
// commutative monoid under +, with {} as identity.
struct DedupCounts {
  std::size_t dropped{};
  std::size_t conflicting{};
  friend constexpr DedupCounts operator+(DedupCounts a,
                                         DedupCounts b) noexcept {
    return {.dropped = a.dropped + b.dropped,
            .conflicting = a.conflicting + b.conflicting};
  }
};

// One run of equal times: everything after the first row is dropped.
DedupCounts run_counts(std::span<const Point> run) {
  const Sample& kept = run.front().sample;
  const auto differs = [&kept](const Point& p) { return p.sample != kept; };
  return {.dropped = run.size() - 1,
          .conflicting = static_cast<std::size_t>(
              std::ranges::count_if(run.subspan(1), differs))};
}

// The sum of run_counts over the runs of equal times of a sorted range.
// (The runs are found by hand: views::chunk_by is not on every target
// standard library.)
DedupCounts dedup_counts(std::span<const Point> sorted) {
  DedupCounts total;
  auto first = sorted.begin();
  while (first != sorted.end()) {
    const auto last = std::ranges::find_if(
        first, sorted.end(),
        [t = first->time](const Point& p) { return p.time != t; });
    total = total + run_counts({first, last});
    first = last;
  }
  return total;
}

// Sorts and deduplicates rows that are not strictly increasing.
NormalizeReport sort_and_deduplicate(std::vector<Point>& rows) {
  const std::size_t descents = count_descents(rows);
  if (descents > 0) {
    std::ranges::stable_sort(rows, std::less{}, &Point::time);
  }
  const DedupCounts counts = dedup_counts(rows);
  const auto duplicates = std::ranges::unique(rows, {}, &Point::time);
  rows.erase(duplicates.begin(), duplicates.end());
  return {.descents = descents,
          .duplicates_dropped = counts.dropped,
          .conflicting_duplicates = counts.conflicting};
}

std::pair<TimeAxis, std::vector<Sample>> unzip(std::span<const Point> rows) {
  TimeAxis times(rows.size());
  std::vector<Sample> samples(rows.size());
  std::ranges::transform(rows, times.begin(), &Point::time);
  std::ranges::transform(rows, samples.begin(), &Point::sample);
  return {std::move(times), std::move(samples)};
}

bool strictly_increasing(std::span<const Point> rows) {
  return std::ranges::adjacent_find(rows, [](const Point& a, const Point& b) {
           return not(a.time < b.time);
         }) == rows.end();
}

}  // namespace

std::expected<TimeSeries, ConstructionError> TimeSeries::make(
    TimeAxis times, std::vector<Sample> samples, SeriesMeta meta) {
  if (times.size() != samples.size()) {
    return std::unexpected{ConstructionError{
        LengthMismatch{.times = times.size(), .samples = samples.size()}}};
  }
  if (const auto bad = detail::first_not_increasing(times)) {
    return std::unexpected{ConstructionError{TimeNotIncreasing{.index = *bad}}};
  }
  return TimeSeries{std::move(times), std::move(samples), std::move(meta)};
}

TimeSeries TimeSeries::with_label(std::string label) const& {
  TimeSeries copy = *this;
  return std::move(copy).with_label(std::move(label));
}

TimeSeries TimeSeries::with_label(std::string label) && {
  meta_ = std::move(meta_).with_label(std::move(label));
  return std::move(*this);
}

std::expected<TimeSeries, AssumeUnitError> TimeSeries::assume_unit(
    Unit u) const& {
  return meta_.assume_unit(std::move(u)).transform([this](SeriesMeta m) {
    return TimeSeries{times_, samples_, std::move(m)};
  });
}

std::expected<TimeSeries, AssumeUnitError> TimeSeries::assume_unit(Unit u) && {
  return meta_.assume_unit(std::move(u)).transform([this](SeriesMeta m) {
    meta_ = std::move(m);
    return std::move(*this);
  });
}

std::expected<TimeSeries, AssumeDatumError> TimeSeries::assume_datum(
    VerticalDatum d) const& {
  return meta_.assume_datum(d).transform([this](SeriesMeta m) {
    return TimeSeries{times_, samples_, std::move(m)};
  });
}

std::expected<TimeSeries, AssumeDatumError> TimeSeries::assume_datum(
    VerticalDatum d) && {
  return meta_.assume_datum(d).transform([this](SeriesMeta m) {
    meta_ = std::move(m);
    return std::move(*this);
  });
}

std::expected<TimeSeries, LengthMismatch> TimeSeries::with_samples(
    std::vector<Sample> samples, SeriesMeta meta) const& {
  if (samples.size() != size()) {
    return std::unexpected{
        LengthMismatch{.times = size(), .samples = samples.size()}};
  }
  return TimeSeries{times_, std::move(samples), std::move(meta)};
}

std::expected<TimeSeries, LengthMismatch> TimeSeries::with_samples(
    std::vector<Sample> samples, SeriesMeta meta) && {
  if (samples.size() != size()) {
    return std::unexpected{
        LengthMismatch{.times = size(), .samples = samples.size()}};
  }
  return TimeSeries{std::move(times_), std::move(samples), std::move(meta)};
}

Normalized normalize(std::vector<Point> rows, SeriesMeta meta) {
  const NormalizeReport report = strictly_increasing(rows)
                                     ? NormalizeReport{}
                                     : sort_and_deduplicate(rows);
  auto [times, samples] = unzip(rows);
  return {.series =
              TimeSeries{std::move(times), std::move(samples), std::move(meta)},
          .report = report};
}

}  // namespace mov::core
