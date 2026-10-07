// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <bit>
#include <cstdint>
#include <limits>

// Floating-point helpers that are constexpr on every supported toolchain.
// std::fabs and std::llround (P0533) are not constant expressions on Clang 20
// with libstdc++ 14 (tests/core/test_toolchain_probe.cpp), and GCC rejects
// any operation that produces NaN (inf - inf) in a constant expression. The
// helpers therefore work on the bit pattern, which is exact and branchless.

namespace mov::core::detail {

static_assert(std::numeric_limits<double>::is_iec559 and
                  sizeof(double) == sizeof(std::uint64_t),
              "the bit-level helpers assume IEEE 754 binary64");

inline constexpr std::uint64_t double_sign_bit = std::uint64_t{1} << 63;
inline constexpr std::uint64_t double_exponent_mask = 0x7FF0'0000'0000'0000;

/// True for every double except NaN and the infinities (all-ones exponent).
[[nodiscard]] constexpr bool is_finite(double v) noexcept {
  return (std::bit_cast<std::uint64_t>(v) & double_exponent_mask) !=
         double_exponent_mask;
}

/// True for any NaN (all-ones exponent, nonzero mantissa). Bit-level, so it
/// is exact in constant expressions on every compiler: MSVC mis-evaluates
/// ordered comparisons with NaN during constant evaluation.
[[nodiscard]] constexpr bool is_nan(double v) noexcept {
  const auto bits = std::bit_cast<std::uint64_t>(v);
  return (bits & double_exponent_mask) == double_exponent_mask and
         (bits & ~(double_exponent_mask | double_sign_bit)) != 0;
}

/// |v| with the sign bit cleared, so -0.0 becomes +0.0. A NaN stays a NaN.
[[nodiscard]] constexpr double magnitude(double v) noexcept {
  return std::bit_cast<double>(std::bit_cast<std::uint64_t>(v) &
                               ~double_sign_bit);
}

/// v rounded to the nearest integer, halves away from zero (like llround).
/// Precondition: v is finite and |v| < 2^62. Exact: the fraction is taken
/// after truncation, so no intermediate rounds.
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
