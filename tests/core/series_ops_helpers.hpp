// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Builders shared by the series-operation and datum-shift tests.

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "test_helpers.hpp"

namespace mov::test {

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
  std::vector<core::Point> rows;
  std::int64_t t = 0;
  for (std::size_t i = 0; i < n; ++i) {
    t += step(rng);
    const int k = kind(rng);
    const core::Sample s = k == 0   ? core::Sample{core::Missing{}}
                           : k == 1 ? core::Sample{core::Dry{}}
                                    : val(quarter(rng) * 0.25);
    rows.push_back({.time = at_ms(t), .sample = s});
  }
  return rows;
}

[[nodiscard]] inline core::TimeSeries series_of_rows(
    const std::vector<core::Point>& rows,
    core::SeriesMeta meta = core::SeriesMeta{}) {
  core::TimeAxis times;
  std::vector<core::Sample> samples;
  for (const core::Point& p : rows) {
    times.push_back(p.time);
    samples.push_back(p.sample);
  }
  return make_series(std::move(times), std::move(samples), std::move(meta));
}

}  // namespace mov::test
