// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: parse_cf_time_units must never crash and report errors
// inside the text; an accepted text round-trips through its own canonical
// form; and to_time, given the raw double that opens the input, returns
// either nullopt or an instant within +-max_abs_time_ms. Input: 8 bytes of
// double, then the units text.

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

[[noreturn]] void fail() { std::abort(); }

bool in_bounds(const std::optional<mov::core::Time>& t) {
  if (not t) {
    return true;
  }
  const std::int64_t ms = t->time_since_epoch().count();
  return ms <= mov::core::max_abs_time_ms and ms >= -mov::core::max_abs_time_ms;
}

const char* unit_name(mov::io::CfTimeUnit unit) {
  switch (unit) {
    case mov::io::CfTimeUnit::millisecond:
      return "milliseconds";
    case mov::io::CfTimeUnit::second:
      return "seconds";
    case mov::io::CfTimeUnit::minute:
      return "minutes";
    case mov::io::CfTimeUnit::hour:
      return "hours";
    case mov::io::CfTimeUnit::day:
      return "days";
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

void check_accepted(const mov::io::CfTimeUnits& parsed, double value) {
  if (has_four_digit_year(parsed.epoch)) {
    // The canonical form: unit name, "since", the UTC epoch, a zero offset.
    const std::string canonical = std::format(
        "{} since {:%F %T} +00:00", unit_name(parsed.unit), parsed.epoch);
    const auto again = mov::io::parse_cf_time_units(canonical);
    if (not again or *again != parsed) {
      fail();
    }
  }
  for (const auto calendar : {mov::io::CfCalendar::standard,
                              mov::io::CfCalendar::proleptic_gregorian}) {
    if (not in_bounds(mov::io::to_time(parsed, calendar, value))) {
      fail();
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size < sizeof(double)) {
    return 0;
  }
  double value = 0.0;
  std::memcpy(&value, data, sizeof value);
  const std::string text(data + sizeof value, data + size);

  const auto parsed = mov::io::parse_cf_time_units(text);
  if (not parsed) {
    const auto& error = parsed.error();
    if (not error.column() or *error.column() > text.size() or
        error.line() != 1 or
        error.context().size() > mov::io::ParseError::max_context_bytes) {
      fail();
    }
    return 0;
  }
  check_accepted(*parsed, value);
  return 0;
}
