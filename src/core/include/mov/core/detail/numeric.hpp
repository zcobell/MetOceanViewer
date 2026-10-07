// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstdint>
#include <limits>

// Floating-point helpers that are constexpr on every supported toolchain.
// std::fabs, std::llround and friends (P0533) are not constant expressions on
// Clang 20 with libstdc++ 14 (tests/core/test_toolchain_probe.cpp), so the
// core does not depend on them.

namespace mov::core::detail {

/// True for every double except NaN and the infinities.
[[nodiscard]] constexpr bool is_finite(double v) noexcept {
  // Comparisons only: GCC rejects an operation that yields NaN (inf - inf) in
  // a constant expression, and every comparison with NaN is false.
  constexpr double largest = std::numeric_limits<double>::max();
  return v >= -largest and v <= largest;
}

/// |v|; also maps -0.0 to 0.0 and keeps NaN as NaN.
[[nodiscard]] constexpr double magnitude(double v) noexcept {
  return v < 0.0 ? -v : v;
}

/// v rounded to the nearest integer, halves away from zero (like llround).
/// Precondition: |v| < 2^62. Exact: the fraction is taken after truncation.
[[nodiscard]] constexpr std::int64_t round_half_away(double v) noexcept {
  const auto truncated = static_cast<std::int64_t>(v);
  const double fraction = v - static_cast<double>(truncated);
  if (fraction >= 0.5) {
    return truncated + 1;
  }
  if (fraction <= -0.5) {
    return truncated - 1;
  }
  return truncated;
}

}  // namespace mov::core::detail
