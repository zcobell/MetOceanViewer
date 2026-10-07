// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/hwm_stats.hpp: the Moments monoid on
// small exact cases, and the shape of the statistics API.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>

#include "hwm_helpers.hpp"
#include "mov/core/hwm.hpp"
#include "mov/core/hwm_stats.hpp"
#include "mov/core/units.hpp"

using mov::core::DegenerateObserved;
using mov::core::Free;
using mov::core::HighWaterMark;
using mov::core::HwmStats;
using mov::core::HwmStatsError;
using mov::core::Intercept;
using mov::core::LinearFit;
using mov::core::Moments;
using mov::core::NonFiniteMoments;
using mov::core::NoWetMarks;
using mov::core::ThroughOrigin;
using mov::core::TooFewForFreeFit;
using mov::core::wet_moments;
using mov::test::mark_m;

namespace {

// Dyadic data: every sum below is exact in binary, so == is the right check.
// (x, y) = (1, 2) and (3, 6): n = 2, means (2, 4), e = y - x = 1 and 3.
constexpr Moments pair() {
  return Moments::of(mark_m(1.0, 2.0)) + Moments::of(mark_m(3.0, 6.0));
}

constexpr std::array<HighWaterMark, 5> with_dry{
    mark_m(1.0, 2.0), mark_m(2.0, -99999.0), mark_m(3.0, 6.0),
    mark_m(9.0, -999.0), mark_m(5.0, 5.5)};

constexpr bool identity_is_exact(const Moments& m) {
  return Moments{} + m == m and m + Moments{} == m;
}

// The ordered left fold, written out: what wet_moments must equal bit for bit.
constexpr Moments explicit_fold(std::span<const HighWaterMark> marks) {
  Moments so_far{};
  for (const HighWaterMark& h : marks) {
    so_far = so_far + Moments::of(h);
  }
  return so_far;
}

// The wet part of two Moments, ignoring how many marks were seen.
constexpr bool same_wet_part(const Moments& a, const Moments& b) {
  return a.n() == b.n() and a.mean_x() == b.mean_x() and
         a.mean_y() == b.mean_y() and a.mean_e() == b.mean_e() and
         a.m2x() == b.m2x() and a.m2y() == b.m2y() and a.cxy() == b.cxy() and
         a.m2e() == b.m2e();
}

template <class... Args>
concept CallsHwmStats = requires(Args&&... args) {
  mov::core::hwm_stats(std::forward<Args>(args)...);
};

template <class Fit>
concept HasIntercept = requires(Fit fit) { fit.intercept; };

using Marks = std::span<const HighWaterMark>;

}  // namespace

TEST_CASE("Moments is a regular, cheap value", "[core][hwm_stats][constexpr]") {
  STATIC_REQUIRE(std::regular<Moments>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Moments>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<Moments>);
  // Two counts and seven sums, private: the invariants (n == 0 means every
  // sum is zero; seen >= n) hold by construction.
  STATIC_REQUIRE(sizeof(Moments) == 9 * sizeof(double));
  STATIC_REQUIRE(std::regular<HwmStats>);
  STATIC_REQUIRE(std::regular<LinearFit>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<HwmStats>);
  STATIC_REQUIRE(std::regular<HwmStatsError>);
}

TEST_CASE("Moments has no raw-number constructor",
          "[core][hwm_stats][constexpr]") {
  // Only the identity and of() make one; the invariants cannot be skipped.
  STATIC_REQUIRE_FALSE(std::is_constructible_v<Moments, double>);
  STATIC_REQUIRE_FALSE(std::is_constructible_v<Moments, std::size_t, double>);
}

TEST_CASE("Moments identity", "[core][hwm_stats][constexpr]") {
  constexpr Moments zero{};
  STATIC_REQUIRE(zero.n() == 0);
  STATIC_REQUIRE(zero.seen() == 0);
  STATIC_REQUIRE(zero.mean_x() == 0.0);
  STATIC_REQUIRE(zero.mean_y() == 0.0);
  STATIC_REQUIRE(zero.mean_e() == 0.0);
  STATIC_REQUIRE(zero.m2x() == 0.0);
  STATIC_REQUIRE(zero.m2y() == 0.0);
  STATIC_REQUIRE(zero.cxy() == 0.0);
  STATIC_REQUIRE(zero.m2e() == 0.0);
  STATIC_REQUIRE(zero + zero == zero);
  STATIC_REQUIRE(identity_is_exact(Moments::of(mark_m(1.0, 2.0))));
  STATIC_REQUIRE(identity_is_exact(pair()));
}

TEST_CASE("Moments::of a wet mark is one observation",
          "[core][hwm_stats][constexpr]") {
  constexpr Moments one = Moments::of(mark_m(2.0, 3.5));
  STATIC_REQUIRE(one.seen() == 1);
  STATIC_REQUIRE(one.n() == 1);
  STATIC_REQUIRE(one.mean_x() == 2.0);
  STATIC_REQUIRE(one.mean_y() == 3.5);
  STATIC_REQUIRE(one.mean_e() == 1.5);
  STATIC_REQUIRE(one.m2x() == 0.0);
  STATIC_REQUIRE(one.m2y() == 0.0);
  STATIC_REQUIRE(one.cxy() == 0.0);
  STATIC_REQUIRE(one.m2e() == 0.0);
}

TEST_CASE("Moments::of a dry mark is seen but not wet",
          "[core][hwm_stats][constexpr]") {
  constexpr Moments dry = Moments::of(mark_m(2.0, -99999.0));
  STATIC_REQUIRE(dry.seen() == 1);
  STATIC_REQUIRE(dry.n() == 0);
  STATIC_REQUIRE(same_wet_part(dry, Moments{}));
  STATIC_REQUIRE(dry != Moments{});  // it was seen
  STATIC_REQUIRE(Moments::of(mark_m(2.0, -999.0)) == dry);
  // Dry marks change `seen` and nothing else, from either side.
  constexpr Moments wet = Moments::of(mark_m(1.0, 2.0));
  constexpr Moments both = wet + Moments::of(mark_m(7.0, -999.0));
  STATIC_REQUIRE(both.seen() == 2);
  STATIC_REQUIRE(same_wet_part(both, wet));
  constexpr Moments before = Moments::of(mark_m(7.0, -999.0)) + wet;
  STATIC_REQUIRE(before.seen() == 2);
  STATIC_REQUIRE(same_wet_part(before, wet));
  STATIC_REQUIRE((dry + dry).seen() == 2);
  STATIC_REQUIRE((dry + dry).n() == 0);
}

TEST_CASE("Moments combine of two observations (exact case)",
          "[core][hwm_stats][constexpr]") {
  constexpr Moments m = pair();
  STATIC_REQUIRE(m.n() == 2);
  STATIC_REQUIRE(m.seen() == 2);
  STATIC_REQUIRE(m.mean_x() == 2.0);
  STATIC_REQUIRE(m.mean_y() == 4.0);
  STATIC_REQUIRE(m.mean_e() == 2.0);
  STATIC_REQUIRE(m.m2x() == 2.0);  // (1-2)^2 + (3-2)^2
  STATIC_REQUIRE(m.m2y() == 8.0);  // (2-4)^2 + (6-4)^2
  STATIC_REQUIRE(m.cxy() == 4.0);  // (-1)(-2) + (1)(2)
  STATIC_REQUIRE(m.m2e() == 2.0);  // (1-2)^2 + (3-2)^2
}

TEST_CASE("wet_moments is the ordered left fold over the marks",
          "[core][hwm_stats][constexpr]") {
  STATIC_REQUIRE(wet_moments(Marks{}) == Moments{});
  STATIC_REQUIRE(wet_moments(with_dry).n() == 3);
  STATIC_REQUIRE(wet_moments(with_dry).seen() == 5);
  STATIC_REQUIRE(wet_moments(with_dry) == explicit_fold(with_dry));
  constexpr std::array<HighWaterMark, 2> two{mark_m(1.0, 2.0),
                                             mark_m(3.0, 6.0)};
  STATIC_REQUIRE(wet_moments(two) == pair());
}

TEST_CASE("the fit is a variant, never a flag",
          "[core][hwm_stats][constexpr][regression][N1]") {
  STATIC_REQUIRE(std::variant_size_v<LinearFit> == 2);
  STATIC_REQUIRE(
      std::is_same_v<std::variant_alternative_t<0, LinearFit>, ThroughOrigin>);
  STATIC_REQUIRE(
      std::is_same_v<std::variant_alternative_t<1, LinearFit>, Free>);
  // The origin fit has no intercept member to ignore.
  STATIC_REQUIRE_FALSE(HasIntercept<ThroughOrigin>);
  STATIC_REQUIRE(HasIntercept<Free>);
  STATIC_REQUIRE(std::variant_size_v<HwmStatsError> == 4);
}

TEST_CASE("the intercept mode must be named",
          "[core][hwm_stats][constexpr][regression][N1]") {
  STATIC_REQUIRE(CallsHwmStats<Marks, Intercept>);
  STATIC_REQUIRE(CallsHwmStats<std::array<HighWaterMark, 2>&, Intercept>);
  // v4 passed a QCheckBox* that became `true`: no default, no bool, no pointer.
  STATIC_REQUIRE_FALSE(CallsHwmStats<Marks>);
  STATIC_REQUIRE_FALSE(CallsHwmStats<Marks, bool>);
  STATIC_REQUIRE_FALSE(CallsHwmStats<Marks, int>);
  STATIC_REQUIRE_FALSE(CallsHwmStats<Marks, const void*>);
  STATIC_REQUIRE_FALSE(std::is_convertible_v<bool, Intercept>);
  STATIC_REQUIRE_FALSE(std::is_convertible_v<int, Intercept>);
  // Moments take the same explicit mode, and carry their own total.
  STATIC_REQUIRE(CallsHwmStats<Moments, Intercept>);
  STATIC_REQUIRE_FALSE(CallsHwmStats<Moments>);
  STATIC_REQUIRE_FALSE(CallsHwmStats<Moments, bool>);
  STATIC_REQUIRE_FALSE(CallsHwmStats<Moments, std::size_t, Intercept>);
}

TEST_CASE("error alternatives carry their counts",
          "[core][hwm_stats][constexpr]") {
  STATIC_REQUIRE(NoWetMarks{.total = 4} == NoWetMarks{.total = 4});
  STATIC_REQUIRE(NoWetMarks{.total = 4} != NoWetMarks{.total = 5});
  STATIC_REQUIRE(TooFewForFreeFit{.wet = 1} == TooFewForFreeFit{.wet = 1});
  STATIC_REQUIRE(DegenerateObserved{} == DegenerateObserved{});
  STATIC_REQUIRE(NonFiniteMoments{} == NonFiniteMoments{});
  STATIC_REQUIRE(HwmStatsError{NoWetMarks{.total = 3}} !=
                 HwmStatsError{DegenerateObserved{}});
}
