// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/windows.hpp and mov/core/enum_set.hpp.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>
#include <type_traits>

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
using mov::core::Windows;
using mov::core::WindowSpan;
using mov::test::at_ms;

enum class Colour : std::uint8_t { red, green, blue };
enum class Gappy : std::uint8_t { a, c = 2 };
enum class Shuffled : std::uint8_t { a, b };

template <>
struct mov::core::Enumerators<Colour> {
  static constexpr std::array values{Colour::red, Colour::green, Colour::blue};
};
template <>
struct mov::core::Enumerators<Gappy> {
  static constexpr std::array values{Gappy::a, Gappy::c};
};
template <>
struct mov::core::Enumerators<Shuffled> {
  static constexpr std::array values{Shuffled::b, Shuffled::a};
};

namespace {

using std::chrono::days;
using std::chrono::local_days;
using std::chrono::milliseconds;
using std::chrono::minutes;

constexpr std::int64_t int64_max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t int64_min = std::numeric_limits<std::int64_t>::min();
constexpr std::int64_t ms_per_minute = 60'000;
constexpr std::int64_t ms_per_day = 86'400'000;

constexpr TimeRange range_ms(std::int64_t b, std::int64_t e) {
  return *TimeRange::make(at_ms(b), at_ms(e));
}

/// A span of n >= 1 milliseconds (1 ms for a smaller n, which no test
/// passes).
constexpr WindowSpan span_ms(std::int64_t n) {
  return WindowSpan::make(milliseconds{n}).value_or(WindowSpan::of<1>());
}

constexpr local_days day_number(int n) { return local_days{days{n}}; }

/// The windows are consecutive, cover r exactly, number ceil(length / span)
/// and are `span` long but the last, which is at most `span`.
constexpr bool split_laws_hold(TimeRange r, WindowSpan span) {
  const Windows w = split(r, span);
  const milliseconds step = span.duration();
  if (w.empty() or w.front().begin() != r.begin() or
      w.back().end() != r.end() or w.back().end() - w.back().begin() > step) {
    return false;
  }
  for (std::size_t i = 0; i + 1 < w.size(); ++i) {
    if (w[i].end() != w[i + 1].begin() or w[i].end() - w[i].begin() != step) {
      return false;
    }
  }
  return true;
}

constexpr bool on_grain(Time t, WindowSpan grain) {
  return t.time_since_epoch().count() % grain.duration().count() == 0;
}

/// aligned(r, grain) contains r, sits on the grain, and widens each side by
/// less than one grain.
constexpr bool aligned_laws_hold(TimeRange r, WindowSpan grain) {
  const auto a = aligned(r, grain);
  return a and on_grain(a->begin(), grain) and on_grain(a->end(), grain) and
         a->begin() <= r.begin() and
         r.begin() - a->begin() < grain.duration() and r.end() <= a->end() and
         a->end() - r.end() < grain.duration();
}

constexpr WindowSpan one_minute = WindowSpan::of<1, minutes>();

/// Windows split from a minute-aligned range by a whole number of minutes
/// begin and end on minutes (S8: no window shares a boundary sample).
constexpr bool minute_windows(TimeRange r, WindowSpan span) {
  const auto a = aligned(r, one_minute);
  if (not a) {
    return false;
  }
  return std::ranges::all_of(split(*a, span), [](const TimeRange& w) {
    return on_grain(w.begin(), one_minute) and on_grain(w.end(), one_minute);
  });
}

using ColourSet = mov::core::EnumSet<Colour>;

}  // namespace

TEST_CASE("window types are values", "[core][windows][constexpr]") {
  STATIC_REQUIRE(std::copyable<DayRange>);
  STATIC_REQUIRE(std::equality_comparable<DayRange>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<DayRange>);
  // Parse, don't validate: no default and no way around make.
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<DayRange>);
  STATIC_REQUIRE_FALSE(
      std::is_constructible_v<DayRange, local_days, local_days>);
  // Days are not instants.
  STATIC_REQUIRE_FALSE(
      std::is_invocable_v<decltype(&DayRange::make), std::chrono::sys_days,
                          std::chrono::sys_days>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<WindowSpan>);
  STATIC_REQUIRE(std::totally_ordered<WindowSpan>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<WindowSpan>);
  STATIC_REQUIRE_FALSE(std::is_constructible_v<WindowSpan, milliseconds>);
  STATIC_REQUIRE(std::regular<ColourSet>);
}

TEST_CASE("a window span is positive", "[core][windows][constexpr]") {
  STATIC_REQUIRE(WindowSpan::make(milliseconds{1}).has_value());
  STATIC_REQUIRE_FALSE(WindowSpan::make(milliseconds{0}).has_value());
  STATIC_REQUIRE_FALSE(WindowSpan::make(milliseconds{-60}).has_value());
  STATIC_REQUIRE(WindowSpan::of<31, days>().duration() == days{31});
  STATIC_REQUIRE(WindowSpan::of<1, std::chrono::seconds>().duration() ==
                 milliseconds{1000});
  STATIC_REQUIRE(WindowSpan::of<5>().duration() == milliseconds{5});
  STATIC_REQUIRE(WindowSpan::of<1, minutes>() < WindowSpan::of<1, days>());
  STATIC_REQUIRE(WindowSpan::of<60, std::chrono::seconds>() ==
                 WindowSpan::of<1, minutes>());
}

TEST_CASE("the window view is a non-allocating random-access range",
          "[core][windows][constexpr]") {
  STATIC_REQUIRE(std::ranges::random_access_range<Windows>);
  STATIC_REQUIRE(std::ranges::sized_range<Windows>);
  STATIC_REQUIRE(std::ranges::view<Windows>);
  STATIC_REQUIRE(std::random_access_iterator<Windows::iterator>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<Windows>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<Windows::iterator>);
  STATIC_REQUIRE(std::same_as<std::ranges::range_value_t<Windows>, TimeRange>);
}

TEST_CASE("split cuts a range into consecutive windows",
          "[core][windows][constexpr]") {
  // An exact multiple: three full windows.
  STATIC_REQUIRE(split(range_ms(0, 30), span_ms(10)).size() == 3);
  STATIC_REQUIRE(split(range_ms(0, 30), span_ms(10))[1] == range_ms(10, 20));
  // A remainder: the last window is cut at the end.
  STATIC_REQUIRE(split(range_ms(0, 31), span_ms(10)).size() == 4);
  STATIC_REQUIRE(split(range_ms(0, 31), span_ms(10)).back() ==
                 range_ms(30, 31));
  // A span at least the range's length gives the range itself.
  STATIC_REQUIRE(split(range_ms(5, 6), span_ms(1)).size() == 1);
  STATIC_REQUIRE(split(range_ms(-7, 3), span_ms(10)).front() ==
                 range_ms(-7, 3));
  STATIC_REQUIRE(split(range_ms(-7, 3), WindowSpan::of<365, days>()).size() ==
                 1);
  // Iterators: distance, indexing, reverse order.
  STATIC_REQUIRE(std::ranges::distance(split(range_ms(0, 100), span_ms(7))) ==
                 15);
  STATIC_REQUIRE(split(range_ms(0, 100), span_ms(7)).begin()[14] ==
                 range_ms(98, 100));
  STATIC_REQUIRE(
      *std::ranges::prev(split(range_ms(0, 100), span_ms(7)).end()) ==
      range_ms(98, 100));
  STATIC_REQUIRE(std::ranges::is_sorted(split(range_ms(-50, 50), span_ms(3)),
                                        {}, &TimeRange::begin));
  // Negative times and odd spans.
  STATIC_REQUIRE(split_laws_hold(range_ms(-1'000, 999), span_ms(7)));
  STATIC_REQUIRE(split_laws_hold(range_ms(-3, 3), span_ms(1)));
  STATIC_REQUIRE(split_laws_hold(range_ms(0, 100), span_ms(99)));
  STATIC_REQUIRE(split_laws_hold(range_ms(0, 100), span_ms(100)));
  STATIC_REQUIRE(split_laws_hold(range_ms(0, 100), span_ms(101)));
}

TEST_CASE("split handles the whole time line without overflow",
          "[core][windows][constexpr]") {
  // The range is longer than INT64_MAX ms; one span reaches past it.
  STATIC_REQUIRE(
      split(range_ms(int64_min, int64_max), span_ms(int64_max)).size() == 3);
  STATIC_REQUIRE(
      split_laws_hold(range_ms(int64_min, int64_max), span_ms(int64_max)));
  STATIC_REQUIRE(
      split(range_ms(int64_max - 10, int64_max), span_ms(4)).back() ==
      range_ms(int64_max - 2, int64_max));
  STATIC_REQUIRE(
      split_laws_hold(range_ms(int64_min, int64_min + 10), span_ms(3)));
  // 2^64 - 1 one-millisecond windows: counted, not built.
  STATIC_REQUIRE(split(range_ms(int64_min, int64_max), span_ms(1)).size() ==
                 std::numeric_limits<std::uint64_t>::max());
  STATIC_REQUIRE(split(range_ms(int64_min, int64_max), span_ms(1)).back() ==
                 range_ms(int64_max - 1, int64_max));
}

TEST_CASE("aligned widens a range to the grain", "[core][windows][constexpr]") {
  // Already on the grain: unchanged.
  STATIC_REQUIRE(aligned(range_ms(0, ms_per_minute), one_minute) ==
                 range_ms(0, ms_per_minute));
  // The begin is floored, the end ceiled.
  STATIC_REQUIRE(aligned(range_ms(1, ms_per_minute + 1), one_minute) ==
                 range_ms(0, 2 * ms_per_minute));
  // Floor and ceiling go toward minus and plus infinity for negative times.
  STATIC_REQUIRE(aligned(range_ms(-1, 0), one_minute) ==
                 range_ms(-ms_per_minute, 0));
  STATIC_REQUIRE(aligned(range_ms(-ms_per_minute - 1, -1), one_minute) ==
                 range_ms(-2 * ms_per_minute, 0));
  STATIC_REQUIRE(aligned_laws_hold(range_ms(-12'345, 67'891), span_ms(7)));
  STATIC_REQUIRE(aligned_laws_hold(range_ms(-999, -998),
                                   WindowSpan::of<1, std::chrono::seconds>()));
  STATIC_REQUIRE(aligned_laws_hold(range_ms(5, 6), span_ms(1)));
}

TEST_CASE("aligned refuses to widen past the time line",
          "[core][windows][constexpr]") {
  STATIC_REQUIRE_FALSE(
      aligned(range_ms(int64_min, int64_min + 5), one_minute).has_value());
  STATIC_REQUIRE_FALSE(aligned(range_ms(0, int64_max), one_minute).has_value());
  // A bound already on the grain needs no room.
  STATIC_REQUIRE(aligned(range_ms(0, int64_max), span_ms(1)) ==
                 range_ms(0, int64_max));
  STATIC_REQUIRE(aligned(range_ms(int64_min, 0), span_ms(2)) ==
                 range_ms(int64_min, 0));
}

TEST_CASE("windows of a minute-aligned range sit on minutes",
          "[core][windows][constexpr]") {
  STATIC_REQUIRE(minute_windows(range_ms(1, (40 * ms_per_day) + 17),
                                WindowSpan::of<31, days>()));
  STATIC_REQUIRE(minute_windows(range_ms(-ms_per_day - 3, (9 * ms_per_day) + 1),
                                WindowSpan::of<4, days>()));
  STATIC_REQUIRE(minute_windows(range_ms(59'999, 60'001), one_minute));
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

TEST_CASE("DayRange holds the days std::chrono::year can name",
          "[core][windows][constexpr]") {
  constexpr local_days earliest = DayRange::earliest_day;
  constexpr local_days latest = DayRange::latest_day;
  STATIC_REQUIRE(earliest == local_days{std::chrono::year{-32767} /
                                        std::chrono::January / 1});
  STATIC_REQUIRE(latest == local_days{std::chrono::year{32767} /
                                      std::chrono::December / 31});
  // The whole span, on every standard library (days::rep may be 32 bits).
  STATIC_REQUIRE(
      DayRange::make(earliest, latest + days{1}).transform(&DayRange::size) ==
      latest - earliest + days{1});
  STATIC_REQUIRE(DayRange::make(earliest - days{1}, latest) ==
                 std::unexpected{DayRangeError::out_of_range});
  STATIC_REQUIRE(DayRange::make(earliest, latest + days{2}) ==
                 std::unexpected{DayRangeError::out_of_range});
  // Emptiness is reported before the bounds.
  STATIC_REQUIRE(DayRange::make(latest + days{3}, latest + days{2}) ==
                 std::unexpected{DayRangeError::empty_or_inverted});
}

TEST_CASE("EnumSet holds members without order or repeats",
          "[core][enum_set][constexpr]") {
  STATIC_REQUIRE(ColourSet::capacity == 3);
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

TEST_CASE("EnumSet is a monoid under union", "[core][enum_set][constexpr]") {
  constexpr ColourSet r{Colour::red};
  constexpr ColourSet g{Colour::green};
  constexpr ColourSet rb{Colour::red, Colour::blue};
  // Identity, associativity, commutativity, idempotence.
  STATIC_REQUIRE((r | ColourSet{}) == r);
  STATIC_REQUIRE((ColourSet{} | r) == r);
  STATIC_REQUIRE(((r | g) | rb) == (r | (g | rb)));
  STATIC_REQUIRE((g | rb) == (rb | g));
  STATIC_REQUIRE((rb | rb) == rb);
  STATIC_REQUIRE((r | g | rb) ==
                 ColourSet{Colour::red, Colour::green, Colour::blue});
  // Intersection.
  STATIC_REQUIRE((rb & r) == r);
  STATIC_REQUIRE((rb & g).empty());
}

TEST_CASE("EnumSet takes only dense enumerator lists",
          "[core][enum_set][constexpr]") {
  STATIC_REQUIRE(mov::core::DenseEnum<Colour>);
  // A gap or a list out of order is not the whole enum.
  STATIC_REQUIRE_FALSE(mov::core::DenseEnum<Gappy>);
  STATIC_REQUIRE_FALSE(mov::core::DenseEnum<Shuffled>);
  // No list at all.
  STATIC_REQUIRE_FALSE(mov::core::DenseEnum<std::byte>);
}
