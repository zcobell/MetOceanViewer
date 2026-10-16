// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: io::parse_rfc3339 (docs/providers-design.md section
// 10.4). Laws:
//  - no crash or UB;
//  - an error's column lies inside the text (or at its end);
//  - an instant re-parses from its canonical form
//    (yyyy-mm-ddThh:mm:ss.mmmZ, within the four-digit years) to itself;
//  - a text with a Z and at most three fraction digits names the instant
//    core::parse_utc_datetime reads.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>

#include "mov/core/time.hpp"
#include "mov/io/detail/civil_time.hpp"
#include "mov/io/rfc3339.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

[[noreturn]] void fail() { std::abort(); }

std::string canonical(mov::core::Time t) {
  const auto f = mov::io::detail::civil_fields(t);
  return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", f.year,
                     f.month, f.day, f.hour, f.minute, f.second, f.millisecond);
}

// "yyyy-mm-ddThh:mm:ss[.fff]Z" with uppercase T and Z: what
// parse_utc_datetime reads too.
bool core_form(std::string_view text) {
  constexpr std::size_t seconds_end = 19;
  if (text.size() < seconds_end + 1 or text[10] != 'T' or text.back() != 'Z') {
    return false;
  }
  const std::size_t fraction = text.size() - 1 - seconds_end;
  return fraction == 0 or (text[seconds_end] == '.' and fraction <= 4);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string storage(data, data + size);
  const std::string_view text{storage};
  const auto parsed = mov::io::parse_rfc3339(text);
  if (not parsed) {
    if (parsed.error().column > text.size()) {
      fail();
    }
    return 0;
  }
  if (mov::io::detail::in_imeds_range(*parsed)) {
    const auto again = mov::io::parse_rfc3339(canonical(*parsed));
    if (not again or *again != *parsed) {
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
