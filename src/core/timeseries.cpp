// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/timeseries.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <expected>
#include <functional>
#include <iterator>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

namespace mov::core {

namespace {

constexpr auto not_increasing = [](Time a, Time b) noexcept {
  return not(a < b);
};

constexpr auto point_not_increasing = [](const Point& a,
                                         const Point& b) noexcept {
  return not(a.time < b.time);
};

std::size_t count_descents(const std::vector<Point>& rows) {
  if (rows.size() < 2) {
    return 0;
  }
  return std::transform_reduce(
      rows.begin(), std::prev(rows.end()), std::next(rows.begin()),
      std::size_t{0}, std::plus{}, [](const Point& a, const Point& b) {
        return b.time < a.time ? std::size_t{1} : std::size_t{0};
      });
}

// Of each run of equal times, keeps the first and counts the others.
std::vector<Point> keep_first_of_equal_times(std::vector<Point> sorted,
                                             NormalizeReport& report) {
  auto kept = sorted.begin();
  for (auto it = sorted.begin(); it != sorted.end(); ++it) {
    if (it != sorted.begin() and it->time == std::prev(kept)->time) {
      ++report.duplicates_dropped;
      if (it->sample != std::prev(kept)->sample) {
        ++report.conflicting_duplicates;
      }
      continue;
    }
    *kept++ = *it;
  }
  sorted.erase(kept, sorted.end());
  return sorted;
}

TimeSeries series_from_rows(const std::vector<Point>& rows, SeriesMeta meta) {
  std::vector<Time> times;
  std::vector<Sample> samples;
  times.reserve(rows.size());
  samples.reserve(rows.size());
  for (const Point& p : rows) {
    times.push_back(p.time);
    samples.push_back(p.sample);
  }
  return detail::trusted_series(std::move(times), std::move(samples),
                                std::move(meta));
}

}  // namespace

TimeSeries detail::trusted_series(std::vector<Time> times,
                                  std::vector<Sample> samples,
                                  SeriesMeta meta) {
  assert(times.size() == samples.size());
  assert(std::ranges::adjacent_find(times, not_increasing) == times.end());
  return TimeSeries{std::move(times), std::move(samples), std::move(meta)};
}

std::expected<TimeSeries, ConstructionError> TimeSeries::make(
    std::vector<Time> times, std::vector<Sample> samples, SeriesMeta meta) {
  if (times.size() != samples.size()) {
    return std::unexpected{
        ConstructionError{.code = ConstructionErrc::length_mismatch,
                          .index = std::min(times.size(), samples.size())}};
  }
  const auto bad = std::ranges::adjacent_find(times, not_increasing);
  if (bad != times.end()) {
    return std::unexpected{ConstructionError{
        .code = ConstructionErrc::time_not_increasing,
        .index = static_cast<std::size_t>(bad - times.begin()) + 1}};
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

std::expected<TimeSeries, MetaError> TimeSeries::assume_unit(Unit u) const& {
  TimeSeries copy = *this;
  return std::move(copy).assume_unit(std::move(u));
}

std::expected<TimeSeries, MetaError> TimeSeries::assume_unit(Unit u) && {
  return meta_.assume_unit(std::move(u)).transform([this](SeriesMeta m) {
    meta_ = std::move(m);
    return std::move(*this);
  });
}

std::expected<TimeSeries, MetaError> TimeSeries::assume_datum(
    VerticalDatum d) const& {
  TimeSeries copy = *this;
  return std::move(copy).assume_datum(d);
}

std::expected<TimeSeries, MetaError> TimeSeries::assume_datum(
    VerticalDatum d) && {
  return meta_.assume_datum(d).transform([this](SeriesMeta m) {
    meta_ = std::move(m);
    return std::move(*this);
  });
}

Normalized normalize(std::vector<Point> rows, SeriesMeta meta) {
  if (std::ranges::adjacent_find(rows, point_not_increasing) == rows.end()) {
    return {.series = series_from_rows(rows, std::move(meta)), .report = {}};
  }
  NormalizeReport report{.descents = count_descents(rows)};
  if (report.descents > 0) {
    std::ranges::stable_sort(rows, std::less{}, &Point::time);
  }
  rows = keep_first_of_equal_times(std::move(rows), report);
  return {.series = series_from_rows(rows, std::move(meta)), .report = report};
}

}  // namespace mov::core
