// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: parse_utc_datetime must never crash, report an error
// inside the text, and give back, for every accepted text, the instant that
// its own canonical form parses to.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>

#include "mov/core/time.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

[[noreturn]] void fail() { std::abort(); }

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string text(data, data + size);
  const auto parsed = mov::core::parse_utc_datetime(text);
  if (not parsed) {
    if (parsed.error().column > text.size()) {
      fail();
    }
    return 0;
  }
  const std::int64_t ms = parsed->time_since_epoch().count();
  if (ms > mov::core::max_abs_time_ms or ms < -mov::core::max_abs_time_ms) {
    fail();
  }
  const std::string canonical = std::format("{:%FT%T}Z", *parsed);
  if (mov::core::parse_utc_datetime(canonical) != parsed) {
    fail();
  }
  return 0;
}
