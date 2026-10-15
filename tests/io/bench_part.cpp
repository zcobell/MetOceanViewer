// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The cost of folding a long fetch's parts: 457 windows of 5760 one-minute
// rows (four days of CO-OPS one-minute water level each, five years).
// Hidden ([.]): run it on a release build with
//   mov_io_part_tests "[benchmark]"

#include <catch2/benchmark/catch_benchmark.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <vector>

#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/part.hpp"

TEST_CASE("fold of 457 disjoint 5760-row parts", "[.][benchmark]") {
  namespace core = mov::core;
  constexpr std::int64_t windows = 457;
  constexpr std::int64_t rows_per_window = 5760;
  std::vector<mov::io::Part> parts;
  parts.reserve(windows);
  for (std::int64_t w = 0; w < windows; ++w) {
    core::TimeAxis times;
    std::vector<core::Sample> samples;
    for (std::int64_t i = 0; i < rows_per_window; ++i) {
      times.emplace_back(std::chrono::minutes{(w * rows_per_window) + i});
      samples.push_back(core::finite_or_missing(1.0));
    }
    mov::io::Part part;
    auto series = core::TimeSeries::make(
        std::move(times), std::move(samples),
        core::SeriesMeta::make({.quantity = core::Quantity::water_level}));
    REQUIRE(series);
    const auto inserted = part.insert(*std::move(series));
    REQUIRE(inserted);
    parts.push_back(std::move(part));
  }
  BENCHMARK("fold") { return mov::io::fold(parts); };
}
