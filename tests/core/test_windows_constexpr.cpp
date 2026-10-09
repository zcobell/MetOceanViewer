// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/windows.hpp and mov/core/enum_set.hpp.

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <type_traits>
#include <vector>

#include "mov/core/enum_set.hpp"
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

using std::chrono::days;
using std::chrono::milliseconds;
using std::chrono::minutes;
using std::chrono::sys_days;

constexpr std::int64_t int64_max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t int64_min = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t ms_per_minute = 60'000;
constexpr std::int64_t ms_per_day = 86'400'000;

constexpr TimeRange range_ms(std::int64_t b, std::int64_t e) {
  return *TimeRange::make(at_ms(b), at_ms(e));
}

constexpr sys_days day_number(int n) { return sys_days{days{n}}; }

/// The windows are consecutive, cover r exactly and are `span` long but the
/// last, which is at most `span`.
constexpr bool split_laws_hold(TimeRange r, milliseconds span) {
  const std::vector<TimeRange> w = split(r, span);
  if (w.empty() or w.front().begin() != r.begin() or
      w.back().end() != r.end() or w.back().end() - w.back().begin() > span) {
    return false;
  }
  for (std::size_t i = 0; i + 1 < w.size(); ++i) {
    if (w[i].end() != w[i + 1].begin() or w[i].end() - w[i].begin() != span) {
      return false;
    }
  }
  return true;
}

constexpr bool on_grain(Time t, milliseconds grain) {
  return t.time_since_epoch().count() % grain.count() == 0;
}

/// aligned(r, grain) contains r, sits on the grain, and widens each side by
/// less than one grain.
constexpr bool aligned_laws_hold(TimeRange r, milliseconds grain) {
  const auto a = aligned(r, grain);
  return a and on_grain(a->begin(), grain) and on_grain(a->end(), grain) and
         a->begin() <= r.begin() and r.begin() - a->begin() < grain and
         r.end() <= a->end() and a->end() - r.end() < grain;
}

/// Windows split from a minute-aligned range by a whole number of minutes
/// begin and end on minutes (S8: no window shares a boundary sample).
constexpr bool minute_windows(TimeRange r, milliseconds span) {
  const auto a = aligned(r, minutes{1});
  if (not a) {
    return false;
  }
  return std::ranges::all_of(split(*a, span), [](const TimeRange& w) {
    return on_grain(w.begin(), minutes{1}) and on_grain(w.end(), minutes{1});
  });
}

constexpr std::size_t window_count(TimeRange r, milliseconds span) {
  return split(r, span).size();
}

enum class Colour : std::uint8_t { red, green, blue };
using ColourSet = mov::core::EnumSet<Colour, 3>;

}  // namespace

TEST_CASE("window types are values", "[core][windows][constexpr]") {
  STATIC_REQUIRE(std::copyable<DayRange>);
  STATIC_REQUIRE(std::equality_comparable<DayRange>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<DayRange>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<DayRange>);
  // Parse, don't validate: no default and no way around make.
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<DayRange>);
  STATIC_REQUIRE_FALSE(std::is_constructible_v<DayRange, sys_days, sys_days>);
  STATIC_REQUIRE(std::regular<ColourSet>);
}

TEST_CASE("split cuts a range into consecutive windows",
          "[core][windows][constexpr]") {
  // An exact multiple: three full windows.
  STATIC_REQUIRE(window_count(range_ms(0, 30), milliseconds{10}) == 3);
  STATIC_REQUIRE(split(range_ms(0, 30), milliseconds{10})[1] ==
                 range_ms(10, 20));
  // A remainder: the last window is cut at the end.
  STATIC_REQUIRE(window_count(range_ms(0, 31), milliseconds{10}) == 4);
  STATIC_REQUIRE(split(range_ms(0, 31), milliseconds{10}).back() ==
                 range_ms(30, 31));
  // A span at least the range's length gives the range itself.
  STATIC_REQUIRE(split(range_ms(5, 6), milliseconds{1}) ==
                 std::vector{range_ms(5, 6)});
  STATIC_REQUIRE(split(range_ms(-7, 3), milliseconds{10}) ==
                 std::vector{range_ms(-7, 3)});
  STATIC_REQUIRE(split(range_ms(-7, 3), days{365}) ==
                 std::vector{range_ms(-7, 3)});
  // Negative times and odd spans.
  STATIC_REQUIRE(split_laws_hold(range_ms(-1'000, 999), milliseconds{7}));
  STATIC_REQUIRE(split_laws_hold(range_ms(-3, 3), milliseconds{1}));
  STATIC_REQUIRE(split_laws_hold(range_ms(0, 100), milliseconds{99}));
  STATIC_REQUIRE(split_laws_hold(range_ms(0, 100), milliseconds{100}));
  STATIC_REQUIRE(split_laws_hold(range_ms(0, 100), milliseconds{101}));
}

TEST_CASE("split handles the whole time line without overflow",
          "[core][windows][constexpr]") {
  // The range is longer than INT64_MAX ms; one span reaches past it.
  STATIC_REQUIRE(window_count(range_ms(int64_min, int64_max),
                              milliseconds{int64_max}) == 3);
  STATIC_REQUIRE(
      split_laws_hold(range_ms(int64_min, int64_max), milliseconds{int64_max}));
  STATIC_REQUIRE(
      split(range_ms(int64_max - 10, int64_max), milliseconds{4}).back() ==
      range_ms(int64_max - 2, int64_max));
  STATIC_REQUIRE(
      split_laws_hold(range_ms(int64_min, int64_min + 10), milliseconds{3}));
}

TEST_CASE("aligned widens a range to the grain", "[core][windows][constexpr]") {
  // Already on the grain: unchanged.
  STATIC_REQUIRE(aligned(range_ms(0, ms_per_minute), minutes{1}) ==
                 range_ms(0, ms_per_minute));
  // The begin is floored, the end ceiled.
  STATIC_REQUIRE(aligned(range_ms(1, ms_per_minute + 1), minutes{1}) ==
                 range_ms(0, 2 * ms_per_minute));
  // Floor and ceiling go toward minus and plus infinity for negative times.
  STATIC_REQUIRE(aligned(range_ms(-1, 0), minutes{1}) ==
                 range_ms(-ms_per_minute, 0));
  STATIC_REQUIRE(aligned(range_ms(-ms_per_minute - 1, -1), minutes{1}) ==
                 range_ms(-2 * ms_per_minute, 0));
  STATIC_REQUIRE(aligned_laws_hold(range_ms(-12'345, 67'891), milliseconds{7}));
  STATIC_REQUIRE(
      aligned_laws_hold(range_ms(-999, -998), std::chrono::seconds{1}));
  STATIC_REQUIRE(aligned_laws_hold(range_ms(5, 6), milliseconds{1}));
  // A grain below 1 ms aligns nothing.
  STATIC_REQUIRE_FALSE(aligned(range_ms(0, 1), milliseconds{0}).has_value());
  STATIC_REQUIRE_FALSE(aligned(range_ms(0, 1), milliseconds{-60}).has_value());
}

TEST_CASE("aligned refuses to widen past the time line",
          "[core][windows][constexpr]") {
  STATIC_REQUIRE_FALSE(
      aligned(range_ms(int64_min, int64_min + 5), minutes{1}).has_value());
  STATIC_REQUIRE_FALSE(aligned(range_ms(0, int64_max), minutes{1}).has_value());
  // A bound already on the grain needs no room.
  STATIC_REQUIRE(aligned(range_ms(0, int64_max), milliseconds{1}) ==
                 range_ms(0, int64_max));
  STATIC_REQUIRE(aligned(range_ms(int64_min, 0), milliseconds{2}) ==
                 range_ms(int64_min, 0));
}

TEST_CASE("windows of a minute-aligned range sit on minutes",
          "[core][windows][constexpr]") {
  STATIC_REQUIRE(minute_windows(range_ms(1, 40 * ms_per_day + 17), days{31}));
  STATIC_REQUIRE(
      minute_windows(range_ms(-ms_per_day - 3, 9 * ms_per_day + 1), days{4}));
  STATIC_REQUIRE(minute_windows(range_ms(59'999, 60'001), minutes{1}));
}

TEST_CASE("DayRange is a non-empty half-open range of days",
          "[core][windows][constexpr]") {
  STATIC_REQUIRE(DayRange::make(day_number(10), day_number(12))
                     .transform(&DayRange::size) == days{2});
  STATIC_REQUIRE(DayRange::make(day_number(-3), day_number(-2))
                     .transform(&DayRange::first) == day_number(-3));
  STATIC_REQUIRE(DayRange::make(day_number(-3), day_number(-2))
                     .transform(&DayRange::end) == day_number(-2));
  STATIC_REQUIRE(DayRange::make(day_number(5), day_number(5)) ==
                 std::unexpected{DayRangeError::empty_or_inverted});
  STATIC_REQUIRE(DayRange::make(day_number(6), day_number(5)) ==
                 std::unexpected{DayRangeError::empty_or_inverted});
  STATIC_REQUIRE(DayRange::make(day_number(1), day_number(3)) !=
                 DayRange::make(day_number(1), day_number(4)));
}

TEST_CASE("DayRange refuses more days than std::chrono::days holds",
          "[core][windows][constexpr]") {
  using Rep = days::rep;
  constexpr sys_days earliest{days{std::numeric_limits<Rep>::min()}};
  constexpr sys_days latest{days{std::numeric_limits<Rep>::max()}};
  STATIC_REQUIRE(DayRange::make(earliest, latest) ==
                 std::unexpected{DayRangeError::out_of_range});
  STATIC_REQUIRE(DayRange::make(sys_days{days{-1}}, latest) ==
                 std::unexpected{DayRangeError::out_of_range});
  STATIC_REQUIRE(
      DayRange::make(sys_days{days{0}}, latest).transform(&DayRange::size) ==
      days{std::numeric_limits<Rep>::max()});
}

TEST_CASE("DayRange::covering takes whole UTC days only",
          "[core][windows][constexpr]") {
  STATIC_REQUIRE(DayRange::covering(range_ms(0, 3 * ms_per_day)) ==
                 DayRange::make(day_number(0), day_number(3)));
  STATIC_REQUIRE(DayRange::covering(range_ms(-2 * ms_per_day, ms_per_day)) ==
                 DayRange::make(day_number(-2), day_number(1)));
  STATIC_REQUIRE(DayRange::covering(range_ms(1, ms_per_day)) ==
                 std::unexpected{DayRangeError::not_whole_days});
  STATIC_REQUIRE(DayRange::covering(range_ms(0, ms_per_day - 1)) ==
                 std::unexpected{DayRangeError::not_whole_days});
  STATIC_REQUIRE(DayRange::covering(range_ms(-1, ms_per_day)) ==
                 std::unexpected{DayRangeError::not_whole_days});
}

TEST_CASE("EnumSet holds members without order or repeats",
          "[core][enum_set][constexpr]") {
  STATIC_REQUIRE(ColourSet{}.empty());
  STATIC_REQUIRE_FALSE(ColourSet{Colour::red}.empty());
  STATIC_REQUIRE(ColourSet{Colour::red, Colour::blue}.contains(Colour::blue));
  STATIC_REQUIRE_FALSE(
      ColourSet{Colour::red, Colour::blue}.contains(Colour::green));
  STATIC_REQUIRE(ColourSet{Colour::red, Colour::blue} ==
                 ColourSet{Colour::blue, Colour::red, Colour::blue});
  STATIC_REQUIRE(ColourSet{Colour::red} != ColourSet{Colour::green});
  STATIC_REQUIRE([] {
    ColourSet s;
    s.insert(Colour::green);
    s.insert(Colour::green);
    return s == ColourSet{Colour::green};
  }());
}
