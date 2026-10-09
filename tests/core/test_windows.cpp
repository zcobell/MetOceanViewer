// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Run-time properties of split, aligned and DayRange over seeded random
// ranges (the constant-expression cases are in test_windows_constexpr.cpp).

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <random>
#include <utility>
#include <vector>

#include "mov/core/time.hpp"
#include "mov/core/windows.hpp"
#include "test_helpers.hpp"

using mov::core::aligned;
using mov::core::DayRange;
using mov::core::DayRangeError;
using mov::core::split;
using mov::core::Time;
using mov::core::TimeRange;
using mov::test::at_ms;

namespace {

using std::chrono::milliseconds;

constexpr std::int64_t int64_max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t int64_min = std::numeric_limits<std::int64_t>::min();

/// A random non-empty range anywhere on the time line, of length below
/// max_length.
TimeRange random_range(std::mt19937_64& rng, std::int64_t max_length) {
  std::uniform_int_distribution<std::int64_t> begin_dist{int64_min,
                                                         int64_max - 1};
  const std::int64_t begin = begin_dist(rng);
  // int64_max - begin, in unsigned arithmetic: it exceeds INT64_MAX when
  // begin is negative.
  const std::uint64_t room =
      static_cast<std::uint64_t>(int64_max) - static_cast<std::uint64_t>(begin);
  const auto cap = static_cast<std::uint64_t>(max_length);
  std::uniform_int_distribution<std::uint64_t> length_dist{
      1, room < cap ? room : cap};
  const std::uint64_t length = length_dist(rng);
  // begin + length <= INT64_MAX, computed without signed overflow.
  const auto end =
      static_cast<std::int64_t>(static_cast<std::uint64_t>(begin) + length);
  return *TimeRange::make(at_ms(begin), at_ms(end));
}

std::int64_t count(Time t) { return t.time_since_epoch().count(); }

/// A generator with a fixed seed: a failure must reproduce.
std::mt19937_64 fixed_rng(std::uint32_t stream) {
  std::seed_seq seed{2026U, 10U, 9U, stream};
  return std::mt19937_64{seed};
}

}  // namespace

TEST_CASE("split laws hold on random ranges", "[core][windows]") {
  std::mt19937_64 rng = fixed_rng(1);
  std::uniform_int_distribution<std::int64_t> span_dist{1, 1'000'000};
  for (int i = 0; i < 2'000; ++i) {
    const milliseconds span{span_dist(rng)};
    const TimeRange r = random_range(rng, span.count() * 50);
    const std::vector<TimeRange> w = split(r, span);
    REQUIRE(not w.empty());
    CHECK(w.front().begin() == r.begin());
    CHECK(w.back().end() == r.end());
    CHECK(w.back().end() - w.back().begin() <= span);
    for (std::size_t k = 0; k + 1 < w.size(); ++k) {
      CHECK(w[k].end() == w[k + 1].begin());
      CHECK(w[k].end() - w[k].begin() == span);
    }
  }
}

TEST_CASE("aligned laws hold on random ranges", "[core][windows]") {
  std::mt19937_64 rng = fixed_rng(2);
  std::uniform_int_distribution<std::int64_t> grain_dist{1, 86'400'000};
  int refused = 0;
  for (int i = 0; i < 5'000; ++i) {
    const milliseconds grain{grain_dist(rng)};
    const TimeRange r = random_range(rng, int64_max);
    const auto a = aligned(r, grain);
    if (not a) {
      // Only when a bound has no multiple of the grain inside the time line.
      const bool low = count(r.begin()) < int64_min + grain.count();
      const bool high = count(r.end()) > int64_max - grain.count();
      CHECK((low or high));
      ++refused;
      continue;
    }
    CHECK(count(a->begin()) % grain.count() == 0);
    CHECK(count(a->end()) % grain.count() == 0);
    CHECK(a->begin() <= r.begin());
    CHECK(r.begin() - a->begin() < grain);
    CHECK(r.end() <= a->end());
    CHECK(a->end() - r.end() < grain);
  }
  // Random bounds anywhere on the line sit near an end only rarely.
  CHECK(refused < 100);
}

TEST_CASE("CO-OPS windows of a month split at minutes, not at the request",
          "[core][windows][regression][L1]") {
  using namespace std::chrono_literals;
  // 2026-01-01T00:00:30Z to 2026-03-15T12:00:10Z, in 31-day windows.
  const Time begin =
      std::chrono::sys_days{std::chrono::year{2026} / 1 / 1} + 30s;
  const Time end =
      std::chrono::sys_days{std::chrono::year{2026} / 3 / 15} + 12h + 10s;
  const auto r = TimeRange::make(begin, end);
  REQUIRE(r.has_value());
  const auto widened = aligned(*r, std::chrono::minutes{1});
  REQUIRE(widened.has_value());
  const std::vector<TimeRange> w =
      split(widened.value_or(*r), std::chrono::days{31});
  REQUIRE(w.size() == 3);
  CHECK(w[0].begin() == std::chrono::sys_days{std::chrono::year{2026} / 1 / 1});
  CHECK(w[1].begin() == std::chrono::sys_days{std::chrono::year{2026} / 2 / 1});
  CHECK(w[2].begin() == std::chrono::sys_days{std::chrono::year{2026} / 3 / 4});
  CHECK(w[2].end() ==
        std::chrono::sys_days{std::chrono::year{2026} / 3 / 15} + 12h + 1min);
}

TEST_CASE("DayRange::covering refuses days std::chrono::days cannot hold",
          "[core][windows]") {
  using Rep = std::chrono::days::rep;
  constexpr std::int64_t ms_per_day = 86'400'000;
  // The furthest whole day a Time can hold.
  const std::int64_t last_day = int64_max / ms_per_day;
  const auto r = TimeRange::make(at_ms(0), at_ms(last_day * ms_per_day));
  REQUIRE(r.has_value());
  const auto covered = DayRange::covering(*r);
  if (std::in_range<Rep>(last_day)) {
    // libstdc++: a 64-bit rep holds every day a Time can name.
    CHECK(covered.transform(&DayRange::size) ==
          std::chrono::days{static_cast<Rep>(last_day)});
  } else {
    // MSVC and libc++: 32 bits.
    CHECK(covered == std::unexpected{DayRangeError::out_of_range});
  }
}
