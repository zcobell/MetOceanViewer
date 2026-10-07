// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: checked_time, fed a raw double, unit and epoch, must
// never overflow, and whatever it returns is inside +-max_abs_time_ms. Under
// the fuzz preset's UBSan this also covers the double-to-integer casts.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "mov/core/time.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

constexpr std::size_t input_size = 3 * sizeof(std::uint64_t);

bool within_bounds(const mov::core::Time& t) {
  const std::int64_t ms = t.time_since_epoch().count();
  return ms <= mov::core::max_abs_time_ms and ms >= -mov::core::max_abs_time_ms;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size < input_size) {
    return 0;
  }
  double value = 0.0;
  std::int64_t integer = 0;
  std::int64_t unit_ms = 0;
  std::memcpy(&value, data, sizeof value);
  std::memcpy(&integer, data + sizeof value, sizeof integer);
  std::memcpy(&unit_ms, data + sizeof value + sizeof integer, sizeof unit_ms);
  // The epoch is a free-standing input too: reuse the integer's bit pattern.
  const mov::core::Time epoch{std::chrono::milliseconds{integer}};

  const auto from_double = mov::core::checked_time(value, unit_ms, epoch);
  if (from_double and not within_bounds(*from_double)) {
    std::abort();
  }
  const auto from_integer = mov::core::checked_time(integer, unit_ms, epoch);
  if (from_integer and not within_bounds(*from_integer)) {
    std::abort();
  }
  return 0;
}
