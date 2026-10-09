// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <optional>

#include "mov/core/detail/numeric.hpp"

// Small vocabulary of the tide engine (docs/harmonics-engine.md section 2.1):
// an angle kept in [0, 360) degrees, and the high/low pair that subordinate
// offsets and extremes are expressed in.

namespace mov::core::detail {

/// fmod(x, y) for finite x and finite y > 0, exact like std::fmod (the result
/// is always representable) and usable in constant expressions, where
/// std::fmod is not on every supported toolchain (core-design section 1.1).
/// Binary long division: chunk runs down from the largest y * 2^k <= |x| to
/// y, and r < 2 * chunk holds throughout, so each r - chunk with r >= chunk
/// is exact by Sterbenz's lemma, and so is each halving (chunk >= y).
/// About 2 log2(|x| / y) steps: 2 for a phase, about 26 for a prediction
/// argument of 1e6 degrees.
[[nodiscard]] constexpr double exact_fmod(double x, double y) noexcept {
  double r = magnitude(x);
  double chunk = y;
  while (chunk <= r - chunk) {  // 2 * chunk <= r, without overflowing
    chunk *= 2.0;
  }
  while (chunk >= y) {
    if (r >= chunk) {
      r -= chunk;
    }
    chunk /= 2.0;
  }
  // The sign of x, as std::fmod keeps it (also for -0 and -360).
  return (std::bit_cast<std::uint64_t>(x) & double_sign_bit) != 0 ? -r : r;
}

}  // namespace mov::core::detail

namespace mov::core::tide {

/// An angle in degrees, always in [0, 360). Regular and trivially copyable.
class Phase {
 public:
  /// nullopt iff `deg` is NaN or infinite; otherwise wrap(deg).
  [[nodiscard]] static constexpr std::optional<Phase> of(double deg) noexcept {
    if (not detail::is_finite(deg)) {
      return std::nullopt;
    }
    return wrap(deg);
  }

  /// `deg` reduced into [0, 360): r = fmod(deg, 360); r += 360 if r < 0;
  /// r = 0 if that rounded up to 360 (deg = -1e-14 gives 0, not 360); -0
  /// becomes +0. The reduction itself is exact. Precondition: `deg` is finite
  /// (Phase::of checks it).
  [[nodiscard]] static constexpr Phase wrap(double deg) noexcept {
    assert(detail::is_finite(deg));
    // Both paths are exact, so they agree bit for bit.
    double r = 0.0;
    if consteval {
      r = detail::exact_fmod(deg, full_turn);
    } else {
      r = std::fmod(deg, full_turn);
    }
    if (r < 0.0) {
      r += full_turn;
    }
    if (r == full_turn or r == 0.0) {
      r = 0.0;  // also turns -0 into +0
    }
    return Phase{r};
  }

  [[nodiscard]] constexpr double degrees() const noexcept { return deg_; }
  [[nodiscard]] constexpr double radians() const noexcept {
    return deg_ * (std::numbers::pi / 180.0);
  }

  friend constexpr bool operator==(Phase, Phase) = default;

 private:
  static constexpr double full_turn = 360.0;
  explicit constexpr Phase(double d) noexcept : deg_{d} {}
  double deg_;
};

/// The kind of a tidal extremum.
enum class Tide : std::uint8_t { high, low };

/// A value for high water and one for low water (subordinate time and height
/// offsets, datums of each kind).
template <class T>
struct HighLow {
  T high;
  T low;
  friend constexpr bool operator==(const HighLow&, const HighLow&) = default;
};

/// v.high or v.low.
template <class T>
[[nodiscard]] constexpr const T& at(const HighLow<T>& v, Tide k) noexcept {
  return k == Tide::high ? v.high : v.low;
}
template <class T>
const T& at(const HighLow<T>&& v, Tide k) = delete;  // would dangle

}  // namespace mov::core::tide
