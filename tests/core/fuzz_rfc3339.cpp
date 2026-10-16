// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: core::parse_rfc3339 (docs/providers-design.md section
// 10.4). Laws:
//  - no crash or UB;
//  - an error's column lies inside the text (or at its end);
//  - an instant within the four-digit years re-parses from its canonical
//    form (yyyy-mm-ddThh:mm:ss.mmmZ) to itself;
//  - differential: a text with a ±hh:mm offset names the instant its Z form
//    (the same fields, the offset replaced by Z) names, minus the offset;
//  - a text with an upper-case T and Z and at most three fraction digits
//    names the instant core::parse_utc_datetime reads.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>

#include "mov/core/time.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

[[noreturn]] void fail() { std::abort(); }

constexpr std::size_t seconds_end = 19;  // "yyyy-mm-ddThh:mm:ss"
constexpr std::size_t offset_size = 6;   // "+hh:mm"

bool in_four_digit_years(mov::core::Time t) {
  using namespace std::chrono;
  const year_month_day ymd{floor<days>(t)};
  return ymd.year() >= year{0} and ymd.year() <= year{9999};
}

std::string canonical(mov::core::Time t) { return std::format("{:%FT%T}Z", t); }

// "yyyy-mm-ddThh:mm:ss[.fff]Z" with upper-case T and Z: what
// parse_utc_datetime reads too.
bool core_form(std::string_view text) {
  if (text.size() < seconds_end + 1 or text[10] != 'T' or text.back() != 'Z') {
    return false;
  }
  const std::size_t fraction = text.size() - 1 - seconds_end;
  return fraction == 0 or (text[seconds_end] == '.' and fraction <= 4);
}

// The offset of an accepted text that ends "±hh:mm", in minutes east.
bool numeric_offset(std::string_view text, std::int64_t& east) {
  if (text.size() < seconds_end + offset_size) {
    return false;
  }
  const std::string_view tail = text.substr(text.size() - offset_size);
  if (tail[0] != '+' and tail[0] != '-') {
    return false;
  }
  const auto digit = [&](std::size_t i) {
    return static_cast<std::int64_t>(tail[i] - '0');
  };
  const std::int64_t minutes =
      ((digit(1) * 10 + digit(2)) * 60) + (digit(4) * 10 + digit(5));
  east = tail[0] == '-' ? -minutes : minutes;
  return true;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string storage(data, data + size);
  const std::string_view text{storage};
  const auto parsed = mov::core::parse_rfc3339(text);
  if (not parsed) {
    if (parsed.error().column > text.size()) {
      fail();
    }
    return 0;
  }
  if (in_four_digit_years(*parsed)) {
    const auto again = mov::core::parse_rfc3339(canonical(*parsed));
    if (not again or *again != *parsed) {
      fail();
    }
  }
  std::int64_t east = 0;
  if (numeric_offset(text, east)) {
    const std::string z_form =
        std::string{text.substr(0, text.size() - offset_size)} + "Z";
    const auto at_z = mov::core::parse_rfc3339(z_form);
    if (not at_z or
        *at_z - std::chrono::milliseconds{east * 60'000} != *parsed) {
      fail();
    }
  }
  if (core_form(text)) {
    const auto core = mov::core::parse_utc_datetime(text);
    if (not core or *core != *parsed) {
      fail();
    }
  }
  return 0;
}
