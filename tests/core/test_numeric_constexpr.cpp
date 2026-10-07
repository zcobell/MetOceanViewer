// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/detail/numeric.hpp.

#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstdint>
#include <limits>

#include "mov/core/detail/numeric.hpp"
#include "test_helpers.hpp"

using mov::core::detail::is_finite;
using mov::core::detail::is_nan;
using mov::core::detail::magnitude;
using mov::core::detail::round_half_away;
using mov::test::infinity;
using mov::test::quiet_nan;

namespace {

constexpr double max_double = std::numeric_limits<double>::max();
// Built from bits: negating a NaN is an operation GCC rejects at compile time.
constexpr double negative_nan = std::bit_cast<double>(0xFFF8'0000'0000'0000U);
constexpr double smallest = std::numeric_limits<double>::denorm_min();

constexpr bool has_sign_bit(double v) {
  return (std::bit_cast<std::uint64_t>(v) >> 63U) != 0U;
}

}  // namespace

TEST_CASE("is_finite is false exactly for NaN and the infinities",
          "[core][numeric][constexpr]") {
  STATIC_REQUIRE(is_finite(0.0));
  STATIC_REQUIRE(is_finite(-0.0));
  STATIC_REQUIRE(is_finite(1.5));
  STATIC_REQUIRE(is_finite(max_double));
  STATIC_REQUIRE(is_finite(-max_double));
  STATIC_REQUIRE(is_finite(smallest));  // subnormal
  STATIC_REQUIRE(is_finite(std::numeric_limits<double>::min()));
  STATIC_REQUIRE_FALSE(is_finite(infinity));
  STATIC_REQUIRE_FALSE(is_finite(-infinity));
  STATIC_REQUIRE_FALSE(is_finite(quiet_nan));
  STATIC_REQUIRE(is_nan(quiet_nan));
  STATIC_REQUIRE(is_nan(negative_nan));
  STATIC_REQUIRE(is_nan(std::numeric_limits<double>::signaling_NaN()));
  STATIC_REQUIRE_FALSE(is_nan(std::numeric_limits<double>::infinity()));
  STATIC_REQUIRE_FALSE(is_nan(-std::numeric_limits<double>::infinity()));
  STATIC_REQUIRE_FALSE(is_nan(0.0));
  STATIC_REQUIRE_FALSE(is_nan(-0.0));
  STATIC_REQUIRE_FALSE(is_nan(std::numeric_limits<double>::denorm_min()));
  STATIC_REQUIRE_FALSE(is_finite(negative_nan));
  STATIC_REQUIRE_FALSE(is_finite(std::numeric_limits<double>::signaling_NaN()));
}

TEST_CASE("magnitude clears the sign bit, including that of -0.0",
          "[core][numeric][constexpr]") {
  STATIC_REQUIRE(magnitude(-2.5) == 2.5);
  STATIC_REQUIRE(magnitude(2.5) == 2.5);
  STATIC_REQUIRE(magnitude(-infinity) == infinity);
  STATIC_REQUIRE(magnitude(-max_double) == max_double);
  STATIC_REQUIRE(magnitude(-smallest) == smallest);
  // -0.0 == 0.0, so the sign bit is what tells them apart.
  STATIC_REQUIRE(has_sign_bit(-0.0));
  STATIC_REQUIRE_FALSE(has_sign_bit(0.0));
  STATIC_REQUIRE_FALSE(has_sign_bit(magnitude(-0.0)));
  STATIC_REQUIRE_FALSE(has_sign_bit(magnitude(-1.0)));
  // A NaN stays a NaN, with the sign cleared.
  STATIC_REQUIRE_FALSE(is_finite(magnitude(negative_nan)));
  STATIC_REQUIRE_FALSE(has_sign_bit(magnitude(negative_nan)));
  STATIC_REQUIRE(magnitude(quiet_nan) != magnitude(quiet_nan));
}

TEST_CASE("round_half_away rounds halves away from zero",
          "[core][numeric][constexpr]") {
  STATIC_REQUIRE(round_half_away(0.0) == 0);
  STATIC_REQUIRE(round_half_away(-0.0) == 0);
  STATIC_REQUIRE(round_half_away(0.49999999999999994) == 0);  // not 1
  STATIC_REQUIRE(round_half_away(0.5) == 1);
  STATIC_REQUIRE(round_half_away(1.5) == 2);
  STATIC_REQUIRE(round_half_away(2.5) == 3);
  STATIC_REQUIRE(round_half_away(-0.5) == -1);
  STATIC_REQUIRE(round_half_away(-1.5) == -2);
  STATIC_REQUIRE(round_half_away(-2.5) == -3);
  STATIC_REQUIRE(round_half_away(-2.4999999999999996) == -2);
  STATIC_REQUIRE(round_half_away(9007199254740991.0) == 9007199254740991);
  STATIC_REQUIRE(round_half_away(-9007199254740991.0) == -9007199254740991);
  // Halves that only just fit a double's mantissa.
  STATIC_REQUIRE(round_half_away(4503599627370495.5) == 4503599627370496);
  STATIC_REQUIRE(round_half_away(-4503599627370495.5) == -4503599627370496);
  // The documented precondition's upper end.
  STATIC_REQUIRE(round_half_away(4.0e18) == 4'000'000'000'000'000'000);
}
