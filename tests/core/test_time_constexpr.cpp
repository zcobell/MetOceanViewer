// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/time.hpp.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <type_traits>

#include "mov/core/time.hpp"
#include "test_helpers.hpp"

using mov::core::checked_time;
using mov::core::DateTimeError;
using mov::core::max_abs_time_ms;
using mov::core::parse_utc_datetime;
using mov::core::Time;
using mov::core::TimeRange;
using mov::core::TimeRangeError;
using mov::core::ValidRange;
using mov::core::ValidRangeError;
using mov::test::at_ms;
using mov::test::infinity;
using mov::test::quiet_nan;

namespace {

using std::chrono::day;
using std::chrono::month;
using std::chrono::sys_days;
using std::chrono::year;

constexpr std::int64_t int64_max = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t int64_min = std::numeric_limits<std::int64_t>::min();
constexpr std::chrono::seconds unit_second{1};
using millis = std::chrono::milliseconds;

constexpr Time utc(int y, unsigned m, unsigned d, int hh = 0, int mm = 0,
                   int ss = 0, int ms = 0) {
  using namespace std::chrono;
  return time_point_cast<milliseconds>(sys_days{year{y} / month{m} / day{d}}) +
         hours{hh} + minutes{mm} + seconds{ss} + milliseconds{ms};
}

constexpr std::optional<std::size_t> bad_column(std::string_view text) {
  const auto parsed = parse_utc_datetime(text);
  if (parsed) {
    return std::nullopt;
  }
  return parsed.error().column;
}

}  // namespace

TEST_CASE("time types are value types", "[core][time][constexpr]") {
  STATIC_REQUIRE(std::copyable<TimeRange>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<TimeRange>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<ValidRange>);
  STATIC_REQUIRE(std::equality_comparable<TimeRange>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<TimeRange>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<TimeRange>);
  STATIC_REQUIRE(std::regular<ValidRange>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<ValidRange>);
  STATIC_REQUIRE(std::regular<DateTimeError>);
}

TEST_CASE("TimeRange make and contains", "[core][time][constexpr]") {
  constexpr auto range = TimeRange::make(at_ms(100), at_ms(200));
  STATIC_REQUIRE(range.has_value());
  STATIC_REQUIRE(range->begin() == at_ms(100));
  STATIC_REQUIRE(range->end() == at_ms(200));
  // Half-open: begin is in, end is out.
  STATIC_REQUIRE(range->contains(at_ms(100)));
  STATIC_REQUIRE(range->contains(at_ms(199)));
  STATIC_REQUIRE_FALSE(range->contains(at_ms(200)));
  STATIC_REQUIRE_FALSE(range->contains(at_ms(99)));

  STATIC_REQUIRE(TimeRange::make(at_ms(5), at_ms(5)).error() ==
                 TimeRangeError::empty_or_inverted);
  STATIC_REQUIRE(TimeRange::make(at_ms(6), at_ms(5)).error() ==
                 TimeRangeError::empty_or_inverted);
  STATIC_REQUIRE(*TimeRange::make(at_ms(1), at_ms(2)) ==
                 *TimeRange::make(at_ms(1), at_ms(2)));
  STATIC_REQUIRE_FALSE(*TimeRange::make(at_ms(1), at_ms(2)) ==
                       *TimeRange::make(at_ms(1), at_ms(3)));
}

TEST_CASE("ValidRange make", "[core][time][constexpr]") {
  constexpr sys_days a{year{2000} / 1 / 1};
  constexpr sys_days b{year{2010} / 6 / 15};

  constexpr auto unknown_ongoing =
      ValidRange::make({.first = std::nullopt, .last = std::nullopt});
  STATIC_REQUIRE(unknown_ongoing.has_value());
  STATIC_REQUIRE_FALSE(unknown_ongoing->first().has_value());
  STATIC_REQUIRE_FALSE(unknown_ongoing->last().has_value());
  STATIC_REQUIRE(*unknown_ongoing == ValidRange{});

  constexpr auto closed = ValidRange::make({.first = a, .last = b});
  STATIC_REQUIRE(closed.has_value());
  STATIC_REQUIRE(closed->first() == a);
  STATIC_REQUIRE(closed->last() == b);

  STATIC_REQUIRE(
      ValidRange::make({.first = a, .last = std::nullopt}).has_value());
  STATIC_REQUIRE(
      ValidRange::make({.first = std::nullopt, .last = b}).has_value());
  STATIC_REQUIRE(
      ValidRange::make({.first = a, .last = a}).has_value());  // one valid day
  STATIC_REQUIRE(ValidRange::make({.first = b, .last = a}).error() ==
                 ValidRangeError::inverted);
  STATIC_REQUIRE_FALSE(*ValidRange::make({.first = a, .last = b}) ==
                       *ValidRange::make({.first = a, .last = a}));
}

TEST_CASE("checked_time double overload", "[core][time][constexpr]") {
  constexpr Time epoch = at_ms(1'000);
  STATIC_REQUIRE(checked_time(1.5, unit_second, epoch) == at_ms(2'500));
  STATIC_REQUIRE(checked_time(0.0, unit_second, epoch) == epoch);
  // Negative values.
  STATIC_REQUIRE(checked_time(-1.5, unit_second, epoch) == at_ms(-500));
  // Rounds half away from zero, like llround.
  STATIC_REQUIRE(checked_time(0.4, millis{1}, at_ms(0)) == at_ms(0));
  STATIC_REQUIRE(checked_time(0.5, millis{1}, at_ms(0)) == at_ms(1));
  STATIC_REQUIRE(checked_time(2.5, millis{1}, at_ms(0)) == at_ms(3));
  STATIC_REQUIRE(checked_time(-0.5, millis{1}, at_ms(0)) == at_ms(-1));
  STATIC_REQUIRE(checked_time(-2.4, millis{1}, at_ms(0)) == at_ms(-2));
  // A day fraction that is not exact in binary still lands on the millisecond.
  STATIC_REQUIRE(checked_time(0.1, millis{86'400'000}, at_ms(0)) ==
                 at_ms(8'640'000));
}

TEST_CASE("checked_time rejects non-finite values", "[core][time][constexpr]") {
  STATIC_REQUIRE_FALSE(
      checked_time(quiet_nan, millis{1}, at_ms(0)).has_value());
  STATIC_REQUIRE_FALSE(checked_time(infinity, millis{1}, at_ms(0)).has_value());
  STATIC_REQUIRE_FALSE(
      checked_time(-infinity, millis{1}, at_ms(0)).has_value());
  // The product overflows to infinity.
  STATIC_REQUIRE_FALSE(checked_time(1e308, millis{1000}, at_ms(0)).has_value());
}

TEST_CASE("checked_time 2^53 edge", "[core][time][constexpr]") {
  STATIC_REQUIRE(max_abs_time_ms == 9'007'199'254'740'991);
  // 2^53 - 1 is exact in a double and accepted, in either sign.
  STATIC_REQUIRE(checked_time(9'007'199'254'740'991.0, millis{1}, at_ms(0)) ==
                 at_ms(max_abs_time_ms));
  STATIC_REQUIRE(checked_time(-9'007'199'254'740'991.0, millis{1}, at_ms(0)) ==
                 at_ms(-max_abs_time_ms));
  // 2^53 and 2^53 + 1 (which a double rounds to 2^53) are rejected.
  STATIC_REQUIRE_FALSE(
      checked_time(9'007'199'254'740'992.0, millis{1}, at_ms(0)).has_value());
  STATIC_REQUIRE_FALSE(
      checked_time(9'007'199'254'740'993.0, millis{1}, at_ms(0)).has_value());
  // The offset itself is bounded, whatever the unit.
  STATIC_REQUIRE_FALSE(
      checked_time(1.0e13, millis{1000}, at_ms(0)).has_value());
  STATIC_REQUIRE(checked_time(9.0e12, millis{1000}, at_ms(0)) ==
                 at_ms(9'000'000'000'000'000));
}

TEST_CASE("checked_time rejects epoch plus offset overflow",
          "[core][time][constexpr]") {
  // Result beyond +-2^53 - 1.
  STATIC_REQUIRE_FALSE(
      checked_time(1.0, millis{1}, at_ms(max_abs_time_ms)).has_value());
  STATIC_REQUIRE_FALSE(
      checked_time(-1.0, millis{1}, at_ms(-max_abs_time_ms)).has_value());
  STATIC_REQUIRE(checked_time(-1.0, millis{1}, at_ms(max_abs_time_ms)) ==
                 at_ms(max_abs_time_ms - 1));
  // Epoch at the int64 limits: the sum overflows int64 before any range check.
  STATIC_REQUIRE_FALSE(
      checked_time(1.0, millis{1}, at_ms(int64_max)).has_value());
  STATIC_REQUIRE_FALSE(
      checked_time(-1.0, millis{1}, at_ms(int64_min)).has_value());
  // An epoch outside the bound stays outside it.
  STATIC_REQUIRE_FALSE(
      checked_time(-1.0, millis{1}, at_ms(int64_max)).has_value());
  STATIC_REQUIRE_FALSE(
      checked_time(std::int64_t{1}, millis{1}, at_ms(int64_max)).has_value());
}

TEST_CASE("checked_time int64 overload", "[core][time][constexpr]") {
  constexpr Time epoch = at_ms(1'000);
  STATIC_REQUIRE(checked_time(std::int64_t{3}, unit_second, epoch) ==
                 at_ms(4'000));
  STATIC_REQUIRE(checked_time(std::int64_t{-3}, unit_second, epoch) ==
                 at_ms(-2'000));
  STATIC_REQUIRE(checked_time(std::int64_t{0}, unit_second, epoch) == epoch);
  // Exact 2^53 bounds.
  STATIC_REQUIRE(checked_time(max_abs_time_ms, millis{1}, at_ms(0)) ==
                 at_ms(max_abs_time_ms));
  STATIC_REQUIRE(checked_time(-max_abs_time_ms, millis{1}, at_ms(0)) ==
                 at_ms(-max_abs_time_ms));
  STATIC_REQUIRE_FALSE(
      checked_time(max_abs_time_ms + 1, millis{1}, at_ms(0)).has_value());
  STATIC_REQUIRE_FALSE(
      checked_time(-max_abs_time_ms - 1, millis{1}, at_ms(0)).has_value());
  // |value| * unit would overflow int64: the bound check rejects it before any
  // multiply happens.
  STATIC_REQUIRE_FALSE(
      checked_time(int64_max, millis{2}, at_ms(0)).has_value());
  STATIC_REQUIRE_FALSE(
      checked_time(int64_min, millis{2}, at_ms(0)).has_value());
  STATIC_REQUIRE_FALSE(
      checked_time(int64_max, millis{86'400'000}, at_ms(0)).has_value());
  // The product fits int64 but not the 2^53 bound.
  STATIC_REQUIRE_FALSE(
      checked_time(std::int64_t{1} << 50, millis{86'400'000}, at_ms(0))
          .has_value());
  STATIC_REQUIRE(checked_time(std::int64_t{100'000}, millis{86'400'000},
                              at_ms(0)) == at_ms(8'640'000'000'000));
  // Epoch overflow.
  STATIC_REQUIRE_FALSE(
      checked_time(std::int64_t{1}, millis{1}, at_ms(max_abs_time_ms))
          .has_value());
  STATIC_REQUIRE_FALSE(
      checked_time(std::int64_t{1}, millis{1}, at_ms(int64_max)).has_value());
}

TEST_CASE("checked_time rejects a non-positive unit",
          "[core][time][constexpr]") {
  STATIC_REQUIRE_FALSE(checked_time(1.0, millis{0}, at_ms(0)).has_value());
  STATIC_REQUIRE_FALSE(checked_time(1.0, millis{-1000}, at_ms(0)).has_value());
  STATIC_REQUIRE_FALSE(
      checked_time(std::int64_t{1}, millis{0}, at_ms(0)).has_value());
  STATIC_REQUIRE_FALSE(
      checked_time(std::int64_t{1}, millis{-1000}, at_ms(0)).has_value());
  STATIC_REQUIRE_FALSE(
      checked_time(std::int64_t{1}, millis{int64_min}, at_ms(0)).has_value());
}

// ---- the value type is any floating-point or integer type ------------------

template <class T>
concept CheckedTimeCallable = requires(T v) {
  { checked_time(v, millis{1}, Time{}) } -> std::same_as<std::optional<Time>>;
};

TEST_CASE("checked_time accepts every arithmetic value type but bool",
          "[core][time][constexpr]") {
  STATIC_REQUIRE(CheckedTimeCallable<double>);
  STATIC_REQUIRE(CheckedTimeCallable<float>);
  STATIC_REQUIRE(CheckedTimeCallable<long double>);
  STATIC_REQUIRE(CheckedTimeCallable<int>);
  STATIC_REQUIRE(CheckedTimeCallable<long long>);
  STATIC_REQUIRE(CheckedTimeCallable<std::int32_t>);
  STATIC_REQUIRE(CheckedTimeCallable<std::uint32_t>);
  STATIC_REQUIRE(CheckedTimeCallable<std::int64_t>);
  STATIC_REQUIRE(CheckedTimeCallable<std::uint64_t>);
  STATIC_REQUIRE(CheckedTimeCallable<std::size_t>);
  STATIC_REQUIRE_FALSE(CheckedTimeCallable<bool>);
  STATIC_REQUIRE_FALSE(CheckedTimeCallable<const char*>);
}

TEST_CASE("checked_time gives the same answer whatever the integer type",
          "[core][time][constexpr]") {
  constexpr Time epoch = at_ms(500);
  constexpr auto expected = std::optional<Time>{at_ms(3'500)};
  STATIC_REQUIRE(checked_time(3, unit_second, epoch) == expected);
  STATIC_REQUIRE(checked_time(3U, unit_second, epoch) == expected);
  STATIC_REQUIRE(checked_time(3L, unit_second, epoch) == expected);
  STATIC_REQUIRE(checked_time(3LL, unit_second, epoch) == expected);
  STATIC_REQUIRE(checked_time(3UL, unit_second, epoch) == expected);
  STATIC_REQUIRE(checked_time(3ULL, unit_second, epoch) == expected);
  STATIC_REQUIRE(checked_time(std::int32_t{3}, unit_second, epoch) == expected);
  STATIC_REQUIRE(checked_time(std::uint32_t{3}, unit_second, epoch) ==
                 expected);
  STATIC_REQUIRE(checked_time(std::size_t{3}, unit_second, epoch) == expected);
  STATIC_REQUIRE(checked_time(std::int8_t{3}, unit_second, epoch) == expected);
  STATIC_REQUIRE(checked_time(3.0F, unit_second, epoch) == expected);
  STATIC_REQUIRE(checked_time(3.0L, unit_second, epoch) == expected);
  STATIC_REQUIRE(checked_time(-3, unit_second, epoch) ==
                 std::optional<Time>{at_ms(-2'500)});
}

TEST_CASE("checked_time rejects an unsigned value above INT64_MAX",
          "[core][time][constexpr]") {
  constexpr std::uint64_t huge = std::uint64_t{1} << 63;  // INT64_MAX + 1
  STATIC_REQUIRE_FALSE(checked_time(huge, millis{1}, at_ms(0)).has_value());
  STATIC_REQUIRE_FALSE(checked_time(std::numeric_limits<std::uint64_t>::max(),
                                    millis{1}, at_ms(0))
                           .has_value());
  // It must not wrap to a negative number that passes the bound.
  STATIC_REQUIRE_FALSE(checked_time(huge + 5, millis{1}, at_ms(0)).has_value());
  // Unsigned values inside the bound are fine.
  STATIC_REQUIRE(checked_time(std::uint64_t{max_abs_time_ms}, millis{1},
                              at_ms(0)) == at_ms(max_abs_time_ms));
  STATIC_REQUIRE_FALSE(
      checked_time(std::uint64_t{max_abs_time_ms} + 1, millis{1}, at_ms(0))
          .has_value());
}

TEST_CASE("checked_time bounds float and long double by their own range",
          "[core][time][constexpr]") {
  STATIC_REQUIRE(checked_time(1.5F, unit_second, at_ms(0)) == at_ms(1'500));
  STATIC_REQUIRE(checked_time(1.5L, unit_second, at_ms(0)) == at_ms(1'500));
  STATIC_REQUIRE_FALSE(checked_time(1.0e30F, millis{1}, at_ms(0)).has_value());
  STATIC_REQUIRE_FALSE(checked_time(1.0e30L, millis{1}, at_ms(0)).has_value());
  STATIC_REQUIRE_FALSE(
      checked_time(std::numeric_limits<long double>::max(), millis{1}, at_ms(0))
          .has_value());
  // 2^53 is not a valid offset even though a float rounds the limit up to it.
  STATIC_REQUIRE_FALSE(
      checked_time(9007199254740992.0F, millis{1}, at_ms(0)).has_value());
}

// ---- the two paths agree ---------------------------------------------------

namespace {

constexpr bool paths_agree() {
  constexpr std::array<std::int64_t, 21> values{
      0,
      1,
      -1,
      2,
      59,
      -86'399,
      1'000'000,
      -1'000'000'007,
      max_abs_time_ms / 86'400'000,
      max_abs_time_ms / 86'400'000 + 1,
      -(max_abs_time_ms / 86'400'000),
      -(max_abs_time_ms / 86'400'000) - 1,
      max_abs_time_ms / 1000,
      max_abs_time_ms / 1000 + 1,
      max_abs_time_ms - 1,
      max_abs_time_ms,  // the last exact double below 2^53
      -max_abs_time_ms,
      -max_abs_time_ms + 1,
      4'503'599'627'370'496,
      -4'503'599'627'370'497,
      123'456'789'012'345};
  constexpr std::array<std::int64_t, 4> units{1, 1000, 3'600'000, 86'400'000};
  constexpr std::array<std::int64_t, 4> epochs{
      0, 1'700'000'000'000, -max_abs_time_ms / 2, max_abs_time_ms};
  for (const std::int64_t v : values) {
    for (const std::int64_t u : units) {
      for (const std::int64_t e : epochs) {
        const auto from_integer = checked_time(v, millis{u}, at_ms(e));
        const auto from_double =
            checked_time(static_cast<double>(v), millis{u}, at_ms(e));
        if (from_integer != from_double) {
          return false;
        }
      }
    }
  }
  return true;
}

}  // namespace

// For an integral double with |v| < 2^53 both overloads give the same answer:
// the same value, or both reject.
TEST_CASE("checked_time: the double and integer paths agree on integral values",
          "[core][time][constexpr]") {
  STATIC_REQUIRE(paths_agree());
}

TEST_CASE("parse_utc_datetime accepts the documented forms",
          "[core][time][constexpr]") {
  constexpr Time day_start = utc(2005, 8, 28);
  STATIC_REQUIRE(parse_utc_datetime("2005-08-28") == day_start);
  STATIC_REQUIRE(parse_utc_datetime("2005-08-28Z") == day_start);
  STATIC_REQUIRE(parse_utc_datetime("2005-08-28 12:30") ==
                 utc(2005, 8, 28, 12, 30));
  STATIC_REQUIRE(parse_utc_datetime("2005-08-28T12:30") ==
                 utc(2005, 8, 28, 12, 30));
  STATIC_REQUIRE(parse_utc_datetime("2005-08-28 12:30:45") ==
                 utc(2005, 8, 28, 12, 30, 45));
  STATIC_REQUIRE(parse_utc_datetime("2005-08-28T12:30:45Z") ==
                 utc(2005, 8, 28, 12, 30, 45));
  STATIC_REQUIRE(parse_utc_datetime("2005-08-28T12:30Z") ==
                 utc(2005, 8, 28, 12, 30));
  STATIC_REQUIRE(parse_utc_datetime("2005-08-28T12:30:45.123Z") ==
                 utc(2005, 8, 28, 12, 30, 45, 123));
  STATIC_REQUIRE(parse_utc_datetime("2005-08-28 12:30:45.5") ==
                 utc(2005, 8, 28, 12, 30, 45, 500));
  STATIC_REQUIRE(parse_utc_datetime("2005-08-28 12:30:45.05") ==
                 utc(2005, 8, 28, 12, 30, 45, 50));
  STATIC_REQUIRE(parse_utc_datetime("2005-08-28 23:59:59.999") ==
                 utc(2005, 8, 28, 23, 59, 59, 999));
  // Calendar edges.
  STATIC_REQUIRE(parse_utc_datetime("2004-02-29") == utc(2004, 2, 29));
  STATIC_REQUIRE(parse_utc_datetime("1970-01-01") == at_ms(0));
  STATIC_REQUIRE(parse_utc_datetime("1969-12-31 23:59:59.999") == at_ms(-1));
  STATIC_REQUIRE(parse_utc_datetime("0000-01-01") == utc(0, 1, 1));
  STATIC_REQUIRE(parse_utc_datetime("9999-12-31 23:59:59.999") ==
                 utc(9999, 12, 31, 23, 59, 59, 999));
}

TEST_CASE("parse_utc_datetime reports the column of the first bad character",
          "[core][time][constexpr]") {
  // Truncated input points just past the end.
  STATIC_REQUIRE(bad_column("") == 0U);
  STATIC_REQUIRE(bad_column("2005") == 4U);
  STATIC_REQUIRE(bad_column("2005-0") == 6U);
  STATIC_REQUIRE(bad_column("2005-08-28T") == 11U);
  STATIC_REQUIRE(bad_column("2005-08-28 12") == 13U);
  STATIC_REQUIRE(bad_column("2005-08-28 12:30:") == 17U);
  STATIC_REQUIRE(bad_column("2005-08-28 12:30:45.") == 20U);
  // Malformed characters.
  STATIC_REQUIRE(bad_column(" 2005-08-28") == 0U);
  STATIC_REQUIRE(bad_column("2005/08/28") == 4U);
  STATIC_REQUIRE(bad_column("2005-8-28") == 6U);
  STATIC_REQUIRE(bad_column("20x5-08-28") == 2U);
  STATIC_REQUIRE(bad_column("2005-08-28X") == 10U);
  STATIC_REQUIRE(bad_column("2005-08-28 ") == 11U);
  STATIC_REQUIRE(bad_column("2005-08-28  12:30") == 11U);
  STATIC_REQUIRE(bad_column("2005-08-28ZZ") == 11U);
  STATIC_REQUIRE(bad_column("2005-08-28 12:30 ") == 16U);
  STATIC_REQUIRE(bad_column("2005-08-28 12:30:45.1234") == 23U);
  STATIC_REQUIRE(bad_column("2005-08-28 12:30:45.12Z0") == 23U);
  STATIC_REQUIRE(bad_column("2005-08-28 12:30:45,5") == 19U);
  STATIC_REQUIRE(bad_column("+005-08-28") == 0U);
  STATIC_REQUIRE(bad_column("2005-08-28 12:30+01:00") == 16U);
  // Out-of-range fields point at the start of the field.
  STATIC_REQUIRE(bad_column("2005-00-10") == 5U);
  STATIC_REQUIRE(bad_column("2005-13-01") == 5U);
  STATIC_REQUIRE(bad_column("2005-02-30") == 8U);
  STATIC_REQUIRE(bad_column("2005-02-29") == 8U);  // 2005 is not a leap year
  STATIC_REQUIRE(bad_column("2005-04-31") == 8U);
  STATIC_REQUIRE(bad_column("2005-08-00") == 8U);
  STATIC_REQUIRE(bad_column("2005-08-28 24:00") == 11U);
  STATIC_REQUIRE(bad_column("2005-08-28 12:60") == 14U);
  STATIC_REQUIRE(bad_column("2005-08-28 12:30:60") == 17U);
  // Embedded NUL and non-ASCII bytes are just bad characters.
  STATIC_REQUIRE(bad_column(std::string_view{"2005-08-28\0", 11}) == 10U);
  STATIC_REQUIRE(bad_column("2005-08-28\xC2\xA0") == 10U);
}
