// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Helpers shared by the core tests. Everything is constexpr so STATIC_REQUIRE
// can use it.

#pragma once

#include <catch2/catch_tostring.hpp>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>

#include "mov/core/time.hpp"

namespace mov::test {

inline constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();
inline constexpr double infinity = std::numeric_limits<double>::infinity();

/// The time `n` milliseconds after the epoch.
[[nodiscard]] constexpr mov::core::Time at_ms(std::int64_t n) noexcept {
  return mov::core::Time{std::chrono::milliseconds{n}};
}

/// |a - b| <= rel * max(|a|, |b|), without <cmath> (not constexpr everywhere).
[[nodiscard]] constexpr bool near(double a, double b,
                                  double rel = 1e-12) noexcept {
  const double diff = a < b ? b - a : a - b;
  const double mag_a = a < 0.0 ? -a : a;
  const double mag_b = b < 0.0 ? -b : b;
  return diff <= rel * (mag_a < mag_b ? mag_b : mag_a);
}

}  // namespace mov::test

// TimeRange has begin() and end(), so Catch2 mistakes it for a range of
// iterators and fails to compile its stringification.
template <>
struct Catch::StringMaker<mov::core::TimeRange> {
  static std::string convert(const mov::core::TimeRange& range) {
    return "[" + std::to_string(range.begin().time_since_epoch().count()) +
           ", " + std::to_string(range.end().time_since_epoch().count()) +
           ") ms";
  }
};
