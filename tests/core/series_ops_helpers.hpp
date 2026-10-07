// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Builders shared by the series-operation and datum-shift tests.

#pragma once

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/series_ops.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "test_helpers.hpp"

namespace mov::test {

/// A generator with a fixed seed: a failure must reproduce.
[[nodiscard]] inline std::mt19937 fixed_rng() {
  std::seed_seq seed{2026, 10, 7};
  return std::mt19937{seed};
}

/// The ValueStats of a QuickStats that must have them.
[[nodiscard]] inline core::ValueStats stats_of(const core::QuickStats& q) {
  REQUIRE(q.stats.has_value());
  const core::Extreme none{.value = 0.0, .time = at_ms(0)};
  return q.stats.value_or(
      core::ValueStats{.count = 0, .min = none, .max = none, .mean = 0.0});
}

/// A Calibration that must be valid.
[[nodiscard]] inline core::Calibration calibration(double scale,
                                                   double offset) {
  const auto c = core::Calibration::make({.scale = scale, .offset = offset});
  REQUIRE(c.has_value());
  return c.value_or(core::Calibration{});
}

/// parse_unit of text that must be a unit.
[[nodiscard]] inline core::Unit unit_of(std::string_view text) {
  const std::optional<core::Unit> unit = core::parse_unit(text);
  REQUIRE(unit.has_value());
  return unit.value_or(core::Unit{});
}

[[nodiscard]] inline core::Sample val(double v) {
  return core::Sample::of(v).value_or(core::Sample{});
}

[[nodiscard]] inline core::TimeAxis axis_of(
    std::initializer_list<std::int64_t> ms) {
  core::TimeAxis out;
  for (const std::int64_t m : ms) {
    out.push_back(at_ms(m));
  }
  return out;
}

/// The metadata of a water level in `unit` with an optional datum.
[[nodiscard]] inline core::SeriesMeta level_meta(
    std::optional<core::Unit> unit = core::LengthUnit::meter,
    std::optional<core::VerticalDatum> datum = std::nullopt,
    std::string label = "level",
    core::QuantityId quantity = core::Quantity::water_level) {
  const core::SeriesMeta base =
      core::SeriesMeta::make({.quantity = std::move(quantity),
                              .label = std::move(label),
                              .unit = std::move(unit)});
  if (not datum) {
    return base;
  }
  auto with_datum = base.assume_datum(*datum);
  REQUIRE(with_datum.has_value());
  return with_datum.value_or(base);
}

/// A series that must be valid.
[[nodiscard]] inline core::TimeSeries make_series(
    core::TimeAxis times, std::vector<core::Sample> samples,
    core::SeriesMeta meta = core::SeriesMeta{}) {
  auto s = core::TimeSeries::make(std::move(times), std::move(samples),
                                  std::move(meta));
  REQUIRE(s.has_value());
  return s.value_or(core::TimeSeries{});
}

/// Rows of random strictly increasing times (steps of 1 to 5 ms) and samples
/// that are multiples of 0.25 in [-8, 8] (exact in binary, with many ties),
/// with some Missing and some Dry.
[[nodiscard]] inline std::vector<core::Point> random_rows(std::mt19937& rng,
                                                          std::size_t n) {
  std::uniform_int_distribution<int> step{1, 5};
  std::uniform_int_distribution<int> quarter{-32, 32};
  std::uniform_int_distribution<int> kind{0, 9};
  std::vector<std::int64_t> ms(n);
  std::ranges::generate(ms, [&] { return std::int64_t{step(rng)}; });
  std::partial_sum(ms.begin(), ms.end(), ms.begin());
  std::vector<core::Point> rows(n);
  std::ranges::transform(ms, rows.begin(), [&](std::int64_t t) {
    const int k = kind(rng);
    const core::Sample s = k == 0   ? core::Sample{core::Missing{}}
                           : k == 1 ? core::Sample{core::Dry{}}
                                    : val(quarter(rng) * 0.25);
    return core::Point{.time = at_ms(t), .sample = s};
  });
  return rows;
}

[[nodiscard]] inline core::TimeSeries series_of_rows(
    const std::vector<core::Point>& rows,
    core::SeriesMeta meta = core::SeriesMeta{}) {
  core::TimeAxis times(rows.size());
  std::vector<core::Sample> samples(rows.size());
  std::ranges::transform(rows, times.begin(), &core::Point::time);
  std::ranges::transform(rows, samples.begin(), &core::Point::sample);
  return make_series(std::move(times), std::move(samples), std::move(meta));
}

/// The Bucket of rows: the ordered left fold.
[[nodiscard]] inline core::Bucket bucket_of(std::span<const core::Point> rows) {
  return std::accumulate(rows.begin(), rows.end(), core::Bucket{},
                         [](const core::Bucket& acc, const core::Point& p) {
                           return acc + core::Bucket::of(p.time, p.sample);
                         });
}

// What a Bucket knows about its values, as optionals (no unchecked access in
// the tests).
[[nodiscard]] inline std::optional<core::Extreme> min_of(
    const core::Bucket& b) {
  return b.summary().transform(&core::ValueSummary::min);
}
[[nodiscard]] inline std::optional<core::Extreme> max_of(
    const core::Bucket& b) {
  return b.summary().transform(&core::ValueSummary::max);
}
[[nodiscard]] inline std::optional<core::Extreme> first_of(
    const core::Bucket& b) {
  return b.summary().transform(&core::ValueSummary::first);
}
[[nodiscard]] inline std::optional<core::Extreme> last_of(
    const core::Bucket& b) {
  return b.summary().transform(&core::ValueSummary::last);
}
[[nodiscard]] inline std::optional<double> sum_of(const core::Bucket& b) {
  return b.summary().transform(&core::ValueSummary::sum);
}
[[nodiscard]] inline std::optional<double> mean_of(const core::Bucket& b) {
  return b.summary().transform(&core::ValueSummary::mean);
}

}  // namespace mov::test
