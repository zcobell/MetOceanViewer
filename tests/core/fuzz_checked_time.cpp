// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: checked_time, fed a raw double, integer, unit and epoch,
// must never overflow, whatever it returns is inside +-max_abs_time_ms, and
// for an integral double the double and integer paths agree. Under the fuzz
// preset's UBSan this also covers the double-to-integer casts.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>

#include "mov/core/detail/numeric.hpp"
#include "mov/core/time.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

constexpr std::size_t input_size = 4 * sizeof(std::uint64_t);

bool within_bounds(const mov::core::Time& t) {
  const std::int64_t ms = t.time_since_epoch().count();
  return ms <= mov::core::max_abs_time_ms and ms >= -mov::core::max_abs_time_ms;
}

bool result_ok(const std::optional<mov::core::Time>& r) {
  return not r or within_bounds(*r);
}

// 2^53: the doubles below it are exactly the integers a double holds.
constexpr double exact_limit = 9007199254740992.0;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size < input_size) {
    return 0;
  }
  double value = 0.0;
  std::int64_t integer = 0;
  std::int64_t unit_count = 0;
  std::int64_t epoch_count = 0;
  std::memcpy(&value, data, sizeof value);
  std::memcpy(&integer, data + sizeof value, sizeof integer);
  std::memcpy(&unit_count, data + (2 * sizeof value), sizeof unit_count);
  std::memcpy(&epoch_count, data + (3 * sizeof value), sizeof epoch_count);
  const std::chrono::milliseconds unit{unit_count};
  const mov::core::Time epoch{std::chrono::milliseconds{epoch_count}};

  if (not result_ok(mov::core::checked_time(value, unit, epoch)) or
      not result_ok(mov::core::checked_time(integer, unit, epoch)) or
      not result_ok(mov::core::checked_time(static_cast<std::uint64_t>(integer),
                                            unit, epoch)) or
      not result_ok(
          mov::core::checked_time(static_cast<float>(value), unit, epoch))) {
    std::abort();
  }

  // The differential law: an integral double below 2^53 in magnitude is the
  // same query as the integer it holds.
  if (mov::core::detail::is_finite(value) and
      mov::core::detail::magnitude(value) < exact_limit) {
    const auto as_integer = static_cast<std::int64_t>(value);
    if (static_cast<double>(as_integer) == value and
        mov::core::checked_time(value, unit, epoch) !=
            mov::core::checked_time(as_integer, unit, epoch)) {
      std::abort();
    }
  }
  return 0;
}
