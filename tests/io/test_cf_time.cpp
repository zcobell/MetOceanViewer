// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <array>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "mov/core/time.hpp"
#include "mov/io/cf_time.hpp"
#include "mov/io/error.hpp"
#include "mov/io/warning.hpp"

namespace {

using namespace std::chrono;
using mov::core::Time;
using mov::io::CfTimeUnit;
using mov::io::CfTimeUnits;
using mov::io::parse_cf_time_units;
using mov::io::ParseErrc;

// 2000-01-01 plus the given clock time, in UTC.
Time on(year_month_day date, milliseconds clock = milliseconds{0}) {
  return time_point_cast<milliseconds>(sys_days{date}) + clock;
}

CfTimeUnits units(CfTimeUnit unit, Time epoch) {
  return CfTimeUnits{.unit = unit, .epoch = epoch};
}

// The units of a text that parses, or nullopt.
std::optional<CfTimeUnits> parsed(std::string_view text) {
  const auto result = parse_cf_time_units(text);
  return result ? std::optional<CfTimeUnits>{result->value} : std::nullopt;
}

// The warning codes of a text that parses.
std::vector<mov::io::WarningCode> warning_codes(std::string_view text) {
  std::vector<mov::io::WarningCode> codes;
  if (const auto result = parse_cf_time_units(text)) {
    for (const auto& warning : result->warnings) {
      codes.push_back(warning.code);
    }
  }
  return codes;
}

struct BadCase {
  std::string_view text;
  ParseErrc code;
  std::size_t column;
};

BadCase bad(std::string_view text, ParseErrc code, std::size_t column) {
  return BadCase{.text = text, .code = code, .column = column};
}

}  // namespace

TEST_CASE("parse_cf_time_units reads the v5 and common CF forms",
          "[io][cf_time]") {
  CHECK(parsed("milliseconds since 1970-01-01 00:00:00") ==
        units(CfTimeUnit::millisecond, Time{}));
  CHECK(parsed("seconds since 2000-01-01 00:00:00") ==
        units(CfTimeUnit::second, on(2000y / January / 1)));
  CHECK(parsed("days since 1970-01-01") == units(CfTimeUnit::day, Time{}));
  CHECK(parsed("hours since 1900-01-01T00:00:00Z") ==
        units(CfTimeUnit::hour, on(1900y / January / 1)));
  CHECK(parsed("minutes since 2001-02-03 04:05:06") ==
        units(CfTimeUnit::minute,
              on(2001y / February / 3, hours{4} + minutes{5} + seconds{6})));
}

// legacy-formats.md section 4: the D-Flow FM units, which v4's substr(14, 19)
// only handled by accident.
TEST_CASE("parse_cf_time_units reads the D-Flow FM units with a zone suffix",
          "[io][cf_time][regression][B11]") {
  CHECK(parsed("seconds since 2000-01-01 00:00:00 +00:00") ==
        units(CfTimeUnit::second, on(2000y / January / 1)));
  CHECK(parsed("minutes since 2001-02-03 04:05:06 +00:00") ==
        units(CfTimeUnit::minute,
              on(2001y / February / 3, hours{4} + minutes{5} + seconds{6})));
}

TEST_CASE("parse_cf_time_units accepts any unit spelling and case",
          "[io][cf_time]") {
  const Time epoch = on(2000y / January / 1);
  for (const std::string_view word :
       {"second", "seconds", "SECONDS", "Second"}) {
    CHECK(parsed(std::string{word} + " since 2000-01-01") ==
          units(CfTimeUnit::second, epoch));
  }
  CHECK(parsed("minute since 2000-01-01") == units(CfTimeUnit::minute, epoch));
  CHECK(parsed("hour since 2000-01-01") == units(CfTimeUnit::hour, epoch));
  CHECK(parsed("day since 2000-01-01") == units(CfTimeUnit::day, epoch));
  CHECK(parsed("millisecond since 2000-01-01") ==
        units(CfTimeUnit::millisecond, epoch));
  CHECK(parsed("Days SINCE 2000-01-01") == units(CfTimeUnit::day, epoch));
}

TEST_CASE("parse_cf_time_units ignores surrounding and repeated white space",
          "[io][cf_time]") {
  CHECK(parsed("  seconds   since \t 2000-01-01 12:30  ") ==
        units(CfTimeUnit::second,
              on(2000y / January / 1, hours{12} + minutes{30})));
  CHECK(parsed("seconds since 2000-01-01\r\n") ==
        units(CfTimeUnit::second, on(2000y / January / 1)));
}

TEST_CASE("parse_cf_time_units reads fractional seconds", "[io][cf_time]") {
  CHECK(parsed("seconds since 2000-01-01 00:00:00.5") ==
        units(CfTimeUnit::second, on(2000y / January / 1, milliseconds{500})));
  CHECK(parsed("seconds since 2000-01-01 00:00:01.123") ==
        units(CfTimeUnit::second,
              on(2000y / January / 1, seconds{1} + milliseconds{123})));
}

TEST_CASE("parse_cf_time_units applies the zone offset to give a UTC epoch",
          "[io][cf_time]") {
  const auto utc_midnight = units(CfTimeUnit::second, on(2000y / January / 1));
  CHECK(parsed("seconds since 2000-01-01 00:00:00 UTC") == utc_midnight);
  CHECK(parsed("seconds since 2000-01-01 00:00:00 utc") == utc_midnight);
  CHECK(parsed("seconds since 2000-01-01 00:00:00Z") == utc_midnight);
  CHECK(parsed("seconds since 2000-01-01 00:00:00 Z") == utc_midnight);
  CHECK(parsed("seconds since 2000-01-01 00:00:00 +00") == utc_midnight);
  CHECK(parsed("seconds since 2000-01-01 00:00:00 -00:00") == utc_midnight);

  // 00:00 at UTC-6 is 06:00 UTC; at UTC+5:30 it is 18:30 UTC the day before.
  CHECK(parsed("seconds since 2000-01-01 00:00:00 -06:00") ==
        units(CfTimeUnit::second, on(2000y / January / 1, hours{6})));
  CHECK(parsed("seconds since 2000-01-01 00:00:00 -0600") ==
        units(CfTimeUnit::second, on(2000y / January / 1, hours{6})));
  CHECK(parsed("seconds since 2000-01-01 00:00:00 -06") ==
        units(CfTimeUnit::second, on(2000y / January / 1, hours{6})));
  CHECK(parsed("seconds since 2000-01-01 00:00:00 +05:30") ==
        units(CfTimeUnit::second,
              on(1999y / December / 31, hours{18} + minutes{30})));
  CHECK(parsed("seconds since 2000-01-01 00:00:00 +0530") ==
        units(CfTimeUnit::second,
              on(1999y / December / 31, hours{18} + minutes{30})));
  // The offset may be attached to the time, or follow a bare date.
  CHECK(parsed("seconds since 2000-01-01T00:00:00+01:00") ==
        units(CfTimeUnit::second, on(1999y / December / 31, hours{23})));
  CHECK(parsed("days since 2000-01-01 -06:00") ==
        units(CfTimeUnit::day, on(2000y / January / 1, hours{6})));
  CHECK(parsed("days since 2000-01-01-06:00") ==
        units(CfTimeUnit::day, on(2000y / January / 1, hours{6})));
}

TEST_CASE("parse_cf_time_units reaches the ends of the date range",
          "[io][cf_time]") {
  const auto first = parse_cf_time_units("days since 0000-01-01");
  REQUIRE(first.has_value());
  CHECK(first->value.epoch == on(year{0} / January / 1));
  const auto latest = parse_cf_time_units("days since 9999-12-31 23:59:59.999");
  REQUIRE(latest.has_value());
  CHECK(latest->value.epoch ==
        on(9999y / December / 31,
           hours{23} + minutes{59} + seconds{59} + milliseconds{999}));
}

TEST_CASE("parse_cf_time_units reports what is wrong and where",
          "[io][cf_time]") {
  // Columns are byte offsets into the text.
  const std::array cases{
      bad("", ParseErrc::bad_time_units, 0),
      bad("   ", ParseErrc::bad_time_units, 3),
      bad("since 2000-01-01", ParseErrc::bad_time_units, 0),
      bad("  fortnights since 2000-01-01", ParseErrc::bad_time_units, 2),
      bad("seconds", ParseErrc::bad_time_units, 7),
      bad("seconds after 2000-01-01", ParseErrc::bad_time_units, 8),
      bad("seconds sincere 2000-01-01", ParseErrc::bad_time_units, 8),
      bad("seconds since", ParseErrc::bad_date, 13),
      bad("seconds since  ", ParseErrc::bad_date, 15),
      bad("seconds since yesterday", ParseErrc::bad_date, 14),
      bad("seconds since 00-01-01", ParseErrc::bad_date, 16),
      bad("seconds since 2000/01/01", ParseErrc::bad_date, 18),
      bad("seconds since 2000-02-30", ParseErrc::bad_date, 22),
      bad("seconds since 2000-13-01", ParseErrc::bad_date, 19),
      bad("seconds since 2000-01-01 25:00", ParseErrc::bad_date, 25),
      bad("seconds since 2000-01-01 12", ParseErrc::bad_date, 27),
      // The date and the clock are separated by exactly one space or a T.
      bad("seconds since 2000-01-01  12:30", ParseErrc::bad_time_units, 26),
      bad("seconds since 2000-01-01T12:00:60", ParseErrc::bad_date, 31),
      bad("seconds since 2000-01-01 00:00:00 EST", ParseErrc::bad_time_units,
          34),
      bad("seconds since 2000-01-01 +25:00", ParseErrc::bad_time_units, 25),
      bad("seconds since 2000-01-01 +05:60", ParseErrc::bad_time_units, 25),
      bad("seconds since 2000-01-01 +05:6", ParseErrc::bad_time_units, 25),
      bad("seconds since 2000-01-01 +5", ParseErrc::bad_time_units, 25),
      bad("seconds since 2000-01-01 +", ParseErrc::bad_time_units, 25),
      bad("seconds since 2000-1-32", ParseErrc::bad_date, 21),
      bad("seconds since 2000-01-01 1:2:3.", ParseErrc::bad_date, 31),
      bad("ds since 2000-01-01", ParseErrc::bad_time_units, 0),
      bad("seconds since 2000-01-01 Z extra", ParseErrc::trailing_text, 27),
      bad("seconds since 2000-01-01 00:00:00 UTC+1", ParseErrc::trailing_text,
          37),
      bad("seconds since 2000-01-01 +01:00 +02:00", ParseErrc::trailing_text,
          32),
      bad("seconds since 2000-01-01 Zulu", ParseErrc::trailing_text, 26),
  };
  for (const BadCase& c : cases) {
    CAPTURE(c.text);
    const auto result = parse_cf_time_units(c.text);
    REQUIRE(not result.has_value());
    CHECK(result.error().code() == c.code);
    CHECK(result.error().column() == std::optional<std::size_t>{c.column});
    CHECK(result.error().line() == 1);
    CHECK(result.error().context() == c.text);
  }
}

TEST_CASE("parse_cf_time_units treats NUL and non-ASCII as ordinary bytes",
          "[io][cf_time]") {
  using namespace std::string_view_literals;
  CHECK(not parse_cf_time_units("seconds\0 since 2000-01-01"sv).has_value());
  CHECK(not parse_cf_time_units("seconds since 2000-01-01\0"sv).has_value());
  CHECK(
      not parse_cf_time_units("seconds since 2000-01-01 \xC2\xA0").has_value());
  CHECK(not parse_cf_time_units("s\xC3\xA9"
                                "conds since 2000-01-01")
                .has_value());
}

TEST_CASE("parse_cf_time_units keeps a hostile attribute out of the error",
          "[io][cf_time]") {
  const std::string hostile =
      "seconds since 2000-01-01 " + std::string(10'000, 'x');
  const auto result = parse_cf_time_units(hostile);
  REQUIRE(not result.has_value());
  CHECK(result.error().code() == ParseErrc::bad_time_units);
  CHECK(result.error().context().size() ==
        mov::io::ParseError::max_context_bytes);
}

// ---- foreign files: the lenient forms of CF section 4.4
// ----------------------

TEST_CASE("parse_cf_time_units accepts the UDUNITS abbreviations",
          "[io][cf_time]") {
  const Time epoch = on(2000y / January / 1);
  const std::array<std::pair<std::string_view, CfTimeUnit>, 16> cases{{
      {"d", CfTimeUnit::day},
      {"D", CfTimeUnit::day},
      {"h", CfTimeUnit::hour},
      {"hr", CfTimeUnit::hour},
      {"hrs", CfTimeUnit::hour},
      {"min", CfTimeUnit::minute},
      {"mins", CfTimeUnit::minute},
      {"s", CfTimeUnit::second},
      {"sec", CfTimeUnit::second},
      {"secs", CfTimeUnit::second},
      {"SEC", CfTimeUnit::second},
      {"days", CfTimeUnit::day},
      {"hours", CfTimeUnit::hour},
      {"minutes", CfTimeUnit::minute},
      {"seconds", CfTimeUnit::second},
      {"milliseconds", CfTimeUnit::millisecond},
  }};
  for (const auto& [word, unit] : cases) {
    CAPTURE(word);
    CHECK(parsed(std::string{word} + " since 2000-01-01") ==
          units(unit, epoch));
  }
  // "s" is a unit, but there is no plural of a one-letter abbreviation, and
  // other words are not units.
  for (const std::string_view word :
       {"ds", "hs", "ss", "ms", "msec", "secss", "dayss", "yr", "weeks", "m"}) {
    CAPTURE(word);
    CHECK(not parse_cf_time_units(std::string{word} + " since 2000-01-01")
                  .has_value());
  }
}

// Probes of real files: NCEP and many reanalyses write one-digit fields, and
// some models write microseconds.
TEST_CASE("parse_cf_time_units accepts one- and two-digit fields",
          "[io][cf_time]") {
  CHECK(parsed("hours since 1800-1-1 00:00:0.0") ==
        units(CfTimeUnit::hour, on(1800y / January / 1)));
  CHECK(parsed("days since 2000-1-1") ==
        units(CfTimeUnit::day, on(2000y / January / 1)));
  CHECK(parsed("seconds since 1970-1-1 0:0:0") ==
        units(CfTimeUnit::second, on(1970y / January / 1)));
  CHECK(parsed("seconds since 2001-2-3 4:5:6") ==
        units(CfTimeUnit::second,
              on(2001y / February / 3, hours{4} + minutes{5} + seconds{6})));
  CHECK(parsed("seconds since 2001-12-31T23:59") ==
        units(CfTimeUnit::second,
              on(2001y / December / 31, hours{23} + minutes{59})));
  CHECK(parsed("days since 2000-1-1-06:00") ==
        units(CfTimeUnit::day, on(2000y / January / 1, hours{6})));
}

TEST_CASE("parse_cf_time_units rounds a long fraction to the millisecond",
          "[io][cf_time]") {
  const Time midnight = on(1970y / January / 1);
  CHECK(parsed("seconds since 1970-01-01 00:00:00.000000") ==
        units(CfTimeUnit::second, midnight));
  CHECK(parsed("seconds since 1970-01-01 00:00:00.0") ==
        units(CfTimeUnit::second, midnight));
  CHECK(parsed("seconds since 1970-01-01 00:00:00.5") ==
        units(CfTimeUnit::second, midnight + milliseconds{500}));
  CHECK(parsed("seconds since 1970-01-01 00:00:00.123456") ==
        units(CfTimeUnit::second, midnight + milliseconds{123}));
  CHECK(parsed("seconds since 1970-01-01 00:00:00.1235") ==
        units(CfTimeUnit::second, midnight + milliseconds{124}));
  CHECK(parsed("seconds since 1970-01-01 00:00:00.9996") ==
        units(CfTimeUnit::second, midnight + seconds{1}));
  CHECK(parsed("seconds since 1970-01-01 00:00:00.99949") ==
        units(CfTimeUnit::second, midnight + milliseconds{999}));
  CHECK(parsed("seconds since 1970-01-01 00:00:00.00049999999") ==
        units(CfTimeUnit::second, midnight));
  CHECK(parsed("seconds since 1970-01-01 00:00:00.0005") ==
        units(CfTimeUnit::second, midnight + milliseconds{1}));
  // Very many digits are fine.
  CHECK(parsed("seconds since 1970-01-01 00:00:00." + std::string(500, '0')) ==
        units(CfTimeUnit::second, midnight));
}

TEST_CASE("a dropped nonzero sub-millisecond digit is a warning",
          "[io][cf_time]") {
  using mov::io::WarningCode;
  using Codes = std::vector<WarningCode>;
  const Codes dropped{WarningCode::time_precision_dropped};
  // Nothing lost: no warning.
  CHECK(warning_codes("seconds since 1970-01-01 00:00:00").empty());
  CHECK(warning_codes("seconds since 1970-01-01 00:00:00.000000").empty());
  CHECK(warning_codes("seconds since 1970-01-01 00:00:00.123").empty());
  CHECK(warning_codes("seconds since 1970-01-01 00:00:00.123000000").empty());
  // Something lost: one warning, up or down.
  CHECK(warning_codes("seconds since 1970-01-01 00:00:00.1234") == dropped);
  CHECK(warning_codes("seconds since 1970-01-01 00:00:00.123456") == dropped);
  CHECK(warning_codes("seconds since 1970-01-01 00:00:00.0004") == dropped);
  CHECK(warning_codes("seconds since 1970-01-01 00:00:00.0000000001") ==
        dropped);

  const auto result =
      parse_cf_time_units(" seconds since 1970-01-01 00:00:00.123456 ");
  REQUIRE(result.has_value());
  REQUIRE(result->warnings.size() == 1);
  CHECK(result->warnings.front().subject ==
        "seconds since 1970-01-01 00:00:00.123456");
  CHECK(result->warnings.front().count == 1);
}

TEST_CASE("the strict forms stay strict", "[io][cf_time]") {
  // The year has four digits; a separator is one space or a T; the fraction
  // needs a digit; a clock needs hour and minute.
  CHECK(not parse_cf_time_units("seconds since 99-01-01").has_value());
  CHECK(not parse_cf_time_units("seconds since 20000-01-01").has_value());
  CHECK(not parse_cf_time_units("seconds since 2000-01-001").has_value());
  CHECK(not parse_cf_time_units("seconds since 2000-001-01").has_value());
  CHECK(not parse_cf_time_units("seconds since 2000-01-01 12:30:00.")
                .has_value());
  CHECK(not parse_cf_time_units("seconds since 2000-01-01 12:30:00.x")
                .has_value());
  CHECK(not parse_cf_time_units("seconds since 2000-01-01 123:00").has_value());
  CHECK(not parse_cf_time_units("seconds since 2000-01-01 1:002").has_value());
  CHECK(not parse_cf_time_units("seconds since 2000-01-01T").has_value());
}

// ---- the clock of one variable ---------------------------------------------

TEST_CASE("CfClock refuses an epoch the standard calendar cannot reproduce",
          "[io][cf_time]") {
  using mov::io::CfCalendar;
  using mov::io::CfClock;
  using mov::io::CfClockError;
  const CfTimeUnits old_epoch{.unit = CfTimeUnit::day,
                              .epoch = on(1500y / January / 1)};
  const auto standard = CfClock::make(old_epoch, CfCalendar::standard);
  REQUIRE(not standard.has_value());
  CHECK(standard.error() == CfClockError::epoch_before_gregorian_reform);
  // The same epoch in the proleptic calendar is fine, and so is the reform
  // day itself in the standard one.
  CHECK(CfClock::make(old_epoch, CfCalendar::proleptic_gregorian).has_value());
  CHECK(CfClock::make(units(CfTimeUnit::day, mov::io::gregorian_reform),
                      CfCalendar::standard)
            .has_value());
}

// CfClock::at for every value type, at run time (the constexpr tests cover the
// same logic at compile time).
TEMPLATE_TEST_CASE("CfClock::at follows the calendar rule for every value type",
                   "[io][cf_time]", float, double, long double, int, long,
                   long long, unsigned, unsigned long) {
  using mov::io::CfCalendar;
  using mov::io::CfClock;
  using mov::io::gregorian_reform;
  const CfTimeUnits from_reform{.unit = CfTimeUnit::day,
                                .epoch = gregorian_reform};
  const auto standard = CfClock::make(from_reform, CfCalendar::standard);
  const auto proleptic =
      CfClock::make(from_reform, CfCalendar::proleptic_gregorian);
  REQUIRE(standard.has_value());
  REQUIRE(proleptic.has_value());

  CHECK(standard->at(TestType{1}) == gregorian_reform + hours{24});
  CHECK(standard->at(TestType{0}) == gregorian_reform);
  CHECK(proleptic->at(TestType{1}) == gregorian_reform + hours{24});
  if constexpr (std::is_signed_v<TestType>) {
    // A result before the reform is a mixed-calendar date: out of range for
    // this value only.
    CHECK(standard->at(TestType{-1}) == std::nullopt);
    CHECK(proleptic->at(TestType{-1}) == gregorian_reform - hours{24});
  }
}
