// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/io/cf_time.hpp and the io value types.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

#include "mov/core/geo.hpp"
#include "mov/core/time.hpp"
#include "mov/io/cf_time.hpp"
#include "mov/io/error.hpp"
#include "mov/io/projection.hpp"
#include "mov/io/read.hpp"
#include "mov/io/warning.hpp"

namespace {

using namespace std::chrono;
using mov::core::Time;
using mov::io::CfCalendar;
using mov::io::CfClock;
using mov::io::CfClockError;
using mov::io::CfTimeUnit;
using mov::io::CfTimeUnits;
using mov::io::gregorian_reform;
using mov::io::parse_cf_calendar;
using mov::io::unit_ms;

// The time of `value` units after the epoch under `calendar`, or nullopt
// when the pair cannot be a clock or the value is out of range.
template <class V>
  requires(std::floating_point<V> or
           (std::integral<V> and not std::same_as<V, bool>))
constexpr std::optional<Time> to_time(const CfTimeUnits& units,
                                      CfCalendar calendar, V value) {
  const auto clock = CfClock::make(units, calendar);
  return clock ? clock->at(value) : std::nullopt;
}

constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

constexpr Time at_ms(std::int64_t n) { return Time{milliseconds{n}}; }

constexpr Time utc(int y, unsigned m, unsigned d, int hh = 0) {
  return time_point_cast<milliseconds>(sys_days{year{y} / month{m} / day{d}}) +
         hours{hh};
}

constexpr CfTimeUnits since_1970(CfTimeUnit unit) {
  return CfTimeUnits{.unit = unit, .epoch = Time{}};
}

template <class V>
concept CanConvert = requires(V v) {
  to_time(CfTimeUnits{.unit = CfTimeUnit::second, .epoch = Time{}},
          CfCalendar::standard, v);
};

}  // namespace

TEST_CASE("unit_ms is the integer factor of each unit",
          "[io][cf_time][constexpr]") {
  STATIC_REQUIRE(unit_ms(CfTimeUnit::millisecond) == 1);
  STATIC_REQUIRE(unit_ms(CfTimeUnit::second) == 1'000);
  STATIC_REQUIRE(unit_ms(CfTimeUnit::minute) == 60'000);
  STATIC_REQUIRE(unit_ms(CfTimeUnit::hour) == 3'600'000);
  STATIC_REQUIRE(unit_ms(CfTimeUnit::day) == 86'400'000);
}

TEST_CASE("parse_cf_calendar knows the calendars a reader supports",
          "[io][cf_time][constexpr]") {
  using std::nullopt;
  // Absent means standard.
  STATIC_REQUIRE(parse_cf_calendar(nullopt) == CfCalendar::standard);
  STATIC_REQUIRE(parse_cf_calendar("standard") == CfCalendar::standard);
  STATIC_REQUIRE(parse_cf_calendar("gregorian") == CfCalendar::standard);
  STATIC_REQUIRE(parse_cf_calendar("Gregorian") == CfCalendar::standard);
  STATIC_REQUIRE(parse_cf_calendar(" standard ") == CfCalendar::standard);
  STATIC_REQUIRE(parse_cf_calendar("proleptic_gregorian") ==
                 CfCalendar::proleptic_gregorian);
  STATIC_REQUIRE(parse_cf_calendar("PROLEPTIC_GREGORIAN") ==
                 CfCalendar::proleptic_gregorian);
  // Everything else is unsupported, including the empty string.
  for (const std::string_view other :
       {"noleap", "365_day", "360_day", "julian", "all_leap", "none", "",
        "proleptic", "gregorian2"}) {
    CHECK(parse_cf_calendar(other) == nullopt);
  }
  STATIC_REQUIRE(parse_cf_calendar("noleap") == nullopt);
  STATIC_REQUIRE(parse_cf_calendar("") == nullopt);
}

TEST_CASE("gregorian_reform is 1582-10-15", "[io][cf_time][constexpr]") {
  STATIC_REQUIRE(gregorian_reform == utc(1582, 10, 15));
}

TEST_CASE("CfClock::at adds value units to the epoch",
          "[io][cf_time][constexpr]") {
  constexpr auto std_cal = CfCalendar::standard;
  STATIC_REQUIRE(to_time(since_1970(CfTimeUnit::second), std_cal, 1.5) ==
                 at_ms(1500));
  STATIC_REQUIRE(to_time(since_1970(CfTimeUnit::second), std_cal, 0.0) ==
                 at_ms(0));
  STATIC_REQUIRE(to_time(since_1970(CfTimeUnit::millisecond), std_cal, 7.0) ==
                 at_ms(7));
  STATIC_REQUIRE(to_time(since_1970(CfTimeUnit::minute), std_cal, 2.0) ==
                 at_ms(120'000));
  STATIC_REQUIRE(to_time(since_1970(CfTimeUnit::hour), std_cal, 1.0) ==
                 at_ms(3'600'000));
  STATIC_REQUIRE(to_time(since_1970(CfTimeUnit::day), std_cal, 1.0) ==
                 at_ms(86'400'000));
  STATIC_REQUIRE(to_time(since_1970(CfTimeUnit::day), std_cal, -1.0) ==
                 at_ms(-86'400'000));
  // Rounded to the nearest millisecond, halves away from zero.
  STATIC_REQUIRE(to_time(since_1970(CfTimeUnit::second), std_cal, 0.0005) ==
                 at_ms(1));
  STATIC_REQUIRE(to_time(since_1970(CfTimeUnit::second), std_cal, -0.0005) ==
                 at_ms(-1));
  STATIC_REQUIRE(to_time(since_1970(CfTimeUnit::second), std_cal, 0.0004) ==
                 at_ms(0));
}

TEST_CASE("CfClock::at takes integers, floats and every width",
          "[io][cf_time][constexpr]") {
  constexpr auto cal = CfCalendar::proleptic_gregorian;
  constexpr auto sec = since_1970(CfTimeUnit::second);
  STATIC_REQUIRE(to_time(sec, cal, std::int64_t{1'700'000'000}) ==
                 at_ms(1'700'000'000'000));
  STATIC_REQUIRE(to_time(sec, cal, 5) == at_ms(5000));
  STATIC_REQUIRE(to_time(sec, cal, std::uint32_t{5}) == at_ms(5000));
  STATIC_REQUIRE(to_time(sec, cal, 1.5F) == at_ms(1500));
  // An unsigned value that does not fit int64 is rejected, not wrapped.
  STATIC_REQUIRE(to_time(sec, cal, std::numeric_limits<std::uint64_t>::max()) ==
                 std::nullopt);
  STATIC_REQUIRE_FALSE(CanConvert<bool>);
  STATIC_REQUIRE(CanConvert<int>);
}

TEST_CASE("CfClock::at rejects what checked_time rejects",
          "[io][cf_time][constexpr]") {
  constexpr auto cal = CfCalendar::proleptic_gregorian;
  constexpr auto sec = since_1970(CfTimeUnit::second);
  constexpr auto daily = since_1970(CfTimeUnit::day);
  STATIC_REQUIRE(to_time(sec, cal, quiet_nan) == std::nullopt);
  STATIC_REQUIRE(to_time(sec, cal, infinity) == std::nullopt);
  STATIC_REQUIRE(to_time(sec, cal, -infinity) == std::nullopt);
  // 2^53 ms is the largest offset: in days that is about 1.04e8.
  STATIC_REQUIRE(to_time(daily, cal, 1e8) ==
                 at_ms(86'400'000LL * 100'000'000LL));
  STATIC_REQUIRE(to_time(daily, cal, 1.1e8) == std::nullopt);
  STATIC_REQUIRE(to_time(daily, cal, std::int64_t{100'000'000}) ==
                 at_ms(86'400'000LL * 100'000'000LL));
  STATIC_REQUIRE(to_time(daily, cal, std::int64_t{1'000'000'000}) ==
                 std::nullopt);
  STATIC_REQUIRE(to_time(sec, cal, 1e300) == std::nullopt);
  // The sum, not just the offset, must stay in range.
  constexpr CfTimeUnits late{.unit = CfTimeUnit::second,
                             .epoch = at_ms(mov::core::max_abs_time_ms)};
  STATIC_REQUIRE(to_time(late, cal, 0.0) == at_ms(mov::core::max_abs_time_ms));
  STATIC_REQUIRE(to_time(late, cal, 1.0) == std::nullopt);
}

// SN section 7: "A pre-1582 date in standard/gregorian => error (mixed
// calendar is not reproduced)".
TEST_CASE("the standard calendar rejects dates before 1582-10-15",
          "[io][cf_time][constexpr]") {
  constexpr auto standard = CfCalendar::standard;
  constexpr auto proleptic = CfCalendar::proleptic_gregorian;
  constexpr CfTimeUnits from_reform{.unit = CfTimeUnit::day,
                                    .epoch = gregorian_reform};
  // The reform instant itself is fine; one millisecond before is not.
  STATIC_REQUIRE(to_time(from_reform, standard, 0.0) == gregorian_reform);
  STATIC_REQUIRE(to_time(from_reform, standard, 1.0) ==
                 gregorian_reform + hours{24});
  STATIC_REQUIRE(to_time(from_reform, standard, -1.0e-9) == gregorian_reform);
  STATIC_REQUIRE(to_time(from_reform, standard, -1.0) == std::nullopt);
  STATIC_REQUIRE(to_time(from_reform, proleptic, -1.0) ==
                 gregorian_reform - hours{24});

  // An epoch before the reform makes every result mixed-calendar.
  constexpr CfTimeUnits old_epoch{.unit = CfTimeUnit::day,
                                  .epoch = utc(1500, 1, 1)};
  STATIC_REQUIRE(to_time(old_epoch, standard, 200'000.0) == std::nullopt);
  STATIC_REQUIRE(to_time(old_epoch, proleptic, 200'000.0).has_value());
}

TEST_CASE("CfClock::make is the reproducibility check",
          "[io][cf_time][constexpr]") {
  constexpr CfTimeUnits old_epoch{.unit = CfTimeUnit::day,
                                  .epoch = utc(1500, 1, 1)};
  STATIC_REQUIRE(CfClock::make(old_epoch, CfCalendar::standard).error() ==
                 CfClockError::epoch_before_gregorian_reform);
  STATIC_REQUIRE(
      CfClock::make(old_epoch, CfCalendar::proleptic_gregorian).has_value());
  constexpr CfTimeUnits at_reform{.unit = CfTimeUnit::day,
                                  .epoch = gregorian_reform};
  STATIC_REQUIRE(CfClock::make(at_reform, CfCalendar::standard).has_value());
  constexpr auto clock = CfClock::make(at_reform, CfCalendar::standard);
  STATIC_REQUIRE(clock->units() == at_reform);
  STATIC_REQUIRE(clock->calendar() == CfCalendar::standard);
}

TEST_CASE("io value types are regular and nothrow-movable",
          "[io][types][constexpr]") {
  using namespace mov::io;
  STATIC_REQUIRE(std::regular<Warning>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Warning>);
  STATIC_REQUIRE(std::regular<FileError>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<FileError>);
  STATIC_REQUIRE(std::regular<NcError>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<NcError>);
  STATIC_REQUIRE(std::regular<FormatError>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<FormatError>);
  STATIC_REQUIRE(std::regular<Cancelled>);
  STATIC_REQUIRE(std::regular<Error>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Error>);
  STATIC_REQUIRE(std::regular<LibraryStatus>);
  STATIC_REQUIRE(std::regular<CfTimeUnits>);
  STATIC_REQUIRE(std::copyable<CfClock>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<CfTimeUnits>);
  STATIC_REQUIRE(std::regular<Read<int>>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Read<int>>);
  // ParseError and ProjectionError have no default (their invariants forbid
  // one).
  STATIC_REQUIRE(std::copyable<ParseError>);
  STATIC_REQUIRE(std::equality_comparable<ParseError>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<ParseError>);
  STATIC_REQUIRE(not std::default_initializable<ParseError>);
  STATIC_REQUIRE(std::copyable<ProjectionError>);
  STATIC_REQUIRE(std::equality_comparable<ProjectionError>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<ProjectionError>);
  // A Projector owns a PROJ context: movable, not copyable.
  STATIC_REQUIRE(std::movable<Projector>);
  STATIC_REQUIRE(not std::copyable<Projector>);
}
