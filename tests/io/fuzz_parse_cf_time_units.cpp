// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: parse_cf_time_units must never crash and report errors
// inside the text; an accepted text
//  - round-trips through its own canonical form (without a warning);
//  - means the same under a different spelling and a zone offset (a
//    metamorphic check: the same instant written as local time + offset, with
//    the unit's abbreviation, parses to the same units);
//  - gives a CfClock whose `at` returns nullopt or an instant within
//    +-max_abs_time_ms, for a double and for an integer, and the two agree on
//    an integral value.
// Input: 8 bytes of double, 8 bytes of int64, then the units text.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>
#include <string>

#include "mov/core/time.hpp"
#include "mov/io/cf_time.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace io = mov::io;

[[noreturn]] void fail() { std::abort(); }

bool in_bounds(const std::optional<mov::core::Time>& t) {
  if (not t) {
    return true;
  }
  const std::int64_t ms = t->time_since_epoch().count();
  return ms <= mov::core::max_abs_time_ms and ms >= -mov::core::max_abs_time_ms;
}

const char* unit_name(io::CfTimeUnit unit) {
  switch (unit) {
    case io::CfTimeUnit::millisecond:
      return "milliseconds";
    case io::CfTimeUnit::second:
      return "seconds";
    case io::CfTimeUnit::minute:
      return "minutes";
    case io::CfTimeUnit::hour:
      return "hours";
    case io::CfTimeUnit::day:
      return "days";
  }
  return "";
}

// Another spelling of the same unit (the UDUNITS abbreviations).
const char* unit_abbreviation(io::CfTimeUnit unit) {
  switch (unit) {
    case io::CfTimeUnit::millisecond:
      return "millisecond";
    case io::CfTimeUnit::second:
      return "sec";
    case io::CfTimeUnit::minute:
      return "min";
    case io::CfTimeUnit::hour:
      return "hr";
    case io::CfTimeUnit::day:
      return "d";
  }
  return "";
}

// The canonical text of an epoch is parseable only for the four-digit years
// of the grammar; a zone offset can carry the epoch just outside them.
bool has_four_digit_year(mov::core::Time t) {
  const std::chrono::year_month_day date{
      std::chrono::floor<std::chrono::days>(t)};
  return date.year() >= std::chrono::year{0} and
         date.year() <= std::chrono::year{9999};
}

void check_canonical(const io::CfTimeUnits& units) {
  const std::string canonical = std::format("{} since {:%F %T} +00:00",
                                            unit_name(units.unit), units.epoch);
  const auto again = io::parse_cf_time_units(canonical);
  if (not again or again->value != units or not again->warnings.empty()) {
    fail();
  }
}

// The same instant as a local time five and a half hours ahead, with that
// offset, and an abbreviated unit.
void check_zone_metamorphosis(const io::CfTimeUnits& units) {
  constexpr std::chrono::minutes offset{5 * 60 + 30};
  const mov::core::Time local = units.epoch + offset;
  if (not has_four_digit_year(local)) {
    return;
  }
  const std::string text = std::format("{} since {:%F %T} +05:30",
                                       unit_abbreviation(units.unit), local);
  const auto again = io::parse_cf_time_units(text);
  if (not again or again->value != units) {
    fail();
  }
}

void check_clock(const io::CfTimeUnits& units, double real,
                 std::int64_t integer) {
  for (const auto calendar :
       {io::CfCalendar::standard, io::CfCalendar::proleptic_gregorian}) {
    const auto clock = io::CfClock::make(units, calendar);
    if (not clock) {
      continue;
    }
    if (not in_bounds(clock->at(real)) or not in_bounds(clock->at(integer))) {
      fail();
    }
    // An integral double below 2^53 is the same query as the integer.
    constexpr double exact_limit = 9007199254740992.0;
    const auto as_integer = static_cast<std::int64_t>(
        real < exact_limit and real > -exact_limit ? real : 0.0);
    if (static_cast<double>(as_integer) == real and
        clock->at(real) != clock->at(as_integer)) {
      fail();
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  constexpr std::size_t header = sizeof(double) + sizeof(std::int64_t);
  if (size < header) {
    return 0;
  }
  double real = 0.0;
  std::int64_t integer = 0;
  std::memcpy(&real, data, sizeof real);
  std::memcpy(&integer, data + sizeof real, sizeof integer);
  const std::string text(data + header, data + size);

  const auto parsed = io::parse_cf_time_units(text);
  if (not parsed) {
    const auto& error = parsed.error();
    if (not error.column() or *error.column() > text.size() or
        error.line() != 1 or
        error.context().size() > io::ParseError::max_context_bytes) {
      fail();
    }
    return 0;
  }
  if (parsed->warnings.size() > 1) {
    fail();
  }
  check_clock(parsed->value, real, integer);
  if (has_four_digit_year(parsed->value.epoch)) {
    check_canonical(parsed->value);
    check_zone_metamorphosis(parsed->value);
  }
  return 0;
}
