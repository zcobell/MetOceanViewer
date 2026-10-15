// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <array>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "mov/core/tide/node_factor.hpp"
#include "mov/core/tide/phase.hpp"
#include "mov/core/time.hpp"

// SP 98 astronomy (docs/harmonics-json.md Appendix A): the mean longitudes of
// Table 1, their rates, and the nodal terms derived from them. Everything
// that is polynomial arithmetic is constexpr; the nodal terms and node
// factors need trigonometry, which is constexpr only from C++26, so they are
// run time.

namespace mov::core::tide {

/// The SP 98 Table 1 epoch, 1899-12-31T12:00:00Z.
inline constexpr Time sp98_epoch =
    std::chrono::sys_days{std::chrono::year{1899} / 12 / 31} +
    std::chrono::hours{12};

/// Julian centuries (36,525 days) from sp98_epoch to `t`. The millisecond
/// difference is exact as an integer, but it can exceed 2^53 by up to the
/// epoch's own offset (2.2e12 ms), so its conversion to double may round
/// (2^-53 relative), and the division rounds once more.
/// Precondition (asserted): |t| <= max_abs_time_ms.
[[nodiscard]] constexpr double julian_centuries(Time t) noexcept {
  assert(t.time_since_epoch().count() <= max_abs_time_ms and
         t.time_since_epoch().count() >= -max_abs_time_ms);
  constexpr double ms_per_century = 36525.0 * 86'400'000.0;
  return static_cast<double>((t - sp98_epoch).count()) / ms_per_century;
}

/// The astronomical arguments of V (SP 98 Table 1) and the longitude of the
/// moon's node at one instant, as angles: T, the hour angle of the mean sun;
/// s, h, p, p1, the mean longitudes of the moon, the sun, the lunar perigee
/// and the solar perigee; N, the lunar node. Each is a Phase, so reduced into
/// [0, 360), and V, a small integer combination of them, keeps its
/// precision.
struct Elements {
  Phase T;
  Phase s;
  Phase h;
  Phase p;
  Phase p1;
  Phase N;
  friend constexpr bool operator==(const Elements&, const Elements&) = default;
};

/// The rates of the same elements, in degrees per hour. Not angles: a
/// separate type, so a rate cannot be passed where a position is meant.
struct ElementRates {
  double T;
  double s;
  double h;
  double p;
  double p1;
  double N;
  friend constexpr bool operator==(const ElementRates&,
                                   const ElementRates&) = default;
};

}  // namespace mov::core::tide

namespace mov::core::detail {

/// c0 + c1 t + c2 t^2 + c3 t^3, t in Julian centuries since sp98_epoch, in
/// degrees (SP 98 Table 1; the arcseconds converted as libcongen does).
struct Sp98Polynomial {
  double c0;
  double c1;
  double c2;
  double c3;

  [[nodiscard]] constexpr double value(double t) const noexcept {
    const double t2 = t * t;
    return c0 + (t * c1) + (t2 * c2) + (t2 * t * c3);
  }
  /// d/dt, degrees per Julian century.
  [[nodiscard]] constexpr double rate(double t) const noexcept {
    return c1 + (2.0 * t * c2) + (3.0 * t * t * c3);
  }
};

inline constexpr double arcsec = 1.0 / 3600.0;
inline constexpr double hours_per_century = 876'600.0;

inline constexpr Sp98Polynomial moon_s{
    .c0 = 270.0 + (26.0 / 60.0) + (14.72 * arcsec),
    .c1 = (1336.0 * 360.0) + (1'108'411.2 * arcsec),
    .c2 = 9.09 * arcsec,
    .c3 = 0.0068 * arcsec};
inline constexpr Sp98Polynomial sun_h{
    .c0 = 279.0 + (41.0 / 60.0) + (48.04 * arcsec),
    .c1 = 129'602'768.13 * arcsec,
    .c2 = 1.089 * arcsec,
    .c3 = 0.0};
inline constexpr Sp98Polynomial lunar_perigee_p{
    .c0 = 334.0 + (19.0 / 60.0) + (40.87 * arcsec),
    .c1 = (11.0 * 360.0) + (392'515.94 * arcsec),
    .c2 = -37.24 * arcsec,
    .c3 = -0.045 * arcsec};
inline constexpr Sp98Polynomial solar_perigee_p1{
    .c0 = 281.0 + (13.0 / 60.0) + (15.0 * arcsec),
    .c1 = 6'189.03 * arcsec,
    .c2 = 1.63 * arcsec,
    .c3 = 0.012 * arcsec};
inline constexpr Sp98Polynomial lunar_node_N{
    .c0 = 259.0 + (10.0 / 60.0) + (57.12 * arcsec),
    .c1 = -((5.0 * 360.0) + (482'912.63 * arcsec)),
    .c2 = 7.58 * arcsec,
    .c3 = 0.008 * arcsec};

/// T = 360 degrees per day since sp98_epoch (noon), so 180 at 00:00 UTC.
/// Taken from the exact millisecond of the day, not from the centuries.
[[nodiscard]] constexpr tide::Phase mean_sun_hour_angle(Time t) noexcept {
  constexpr std::int64_t ms_per_day = 86'400'000;
  const std::int64_t ms = t.time_since_epoch().count();
  const std::int64_t of_day = ((ms % ms_per_day) + ms_per_day) % ms_per_day;
  return tide::Phase::wrap(180.0 + (360.0 * static_cast<double>(of_day) /
                                    static_cast<double>(ms_per_day)));
}

}  // namespace mov::core::detail

namespace mov::core::tide {

/// The elements at `t`. Precondition as julian_centuries.
[[nodiscard]] constexpr Elements elements(Time t) noexcept {
  const double c = julian_centuries(t);
  const auto at = [c](const detail::Sp98Polynomial& poly) {
    return Phase::wrap(poly.value(c));
  };
  return Elements{.T = detail::mean_sun_hour_angle(t),
                  .s = at(detail::moon_s),
                  .h = at(detail::sun_h),
                  .p = at(detail::lunar_perigee_p),
                  .p1 = at(detail::solar_perigee_p1),
                  .N = at(detail::lunar_node_N)};
}

/// The rates of the elements at `t`, degrees per hour. T is exactly 15.
/// Precondition as julian_centuries.
[[nodiscard]] constexpr ElementRates rates_at(Time t) noexcept {
  const double c = julian_centuries(t);
  const auto per_hour = [c](const detail::Sp98Polynomial& poly) {
    return poly.rate(c) / detail::hours_per_century;
  };
  return ElementRates{.T = 36525.0 * 360.0 / detail::hours_per_century,
                      .s = per_hour(detail::moon_s),
                      .h = per_hour(detail::sun_h),
                      .p = per_hour(detail::lunar_perigee_p),
                      .p1 = per_hour(detail::solar_perigee_p1),
                      .N = per_hour(detail::lunar_node_N)};
}

/// The NOS speed epoch, 1900-01-01T00:00Z (docs/harmonics-json.md 5.2), and
/// the rates there, from which every constituent speed is derived.
inline constexpr Time nos_speed_epoch =
    std::chrono::sys_days{std::chrono::year{1900} / std::chrono::January / 1};
inline constexpr ElementRates rates_1900 = rates_at(nos_speed_epoch);

/// The terms of u and of the node factors (docs/harmonics-json.md A.2), in
/// degrees except the two amplitude terms Qa and Ra: I, the inclination of
/// the moon's orbit to the equator; nu, xi, nu1 (nu'), two_nu2 (2nu''), the
/// u terms of SP 98; P = p - xi; Q, R and Qu, the M1 and L2 terms. Angles
/// other than I and P are in (-180, 180], P is in [0, 360).
struct NodalTerms {
  double I;
  double nu;
  double xi;
  double nu1;
  double two_nu2;
  double P;
  double Q;
  double R;
  double Qu;
  double Qa;
  double Ra;
  friend constexpr bool operator==(const NodalTerms&,
                                   const NodalTerms&) = default;
};

/// The nodal terms from N and p of `at` (the elements at mid-year in the
/// yearly scheme). Run time only: trigonometry.
[[nodiscard]] NodalTerms nodal_terms(const Elements& at) noexcept;

/// The value of node-factor formula `f` for `n` (A.3); 1 for unity.
[[nodiscard]] double node_factor(NodeFactor f, const NodalTerms& n) noexcept;

/// The value of every node-factor formula for one set of nodal terms: the
/// 14 trigonometric evaluations a year needs, done once (YearAstronomy holds
/// them), so that each constituent's f is a product of cached values.
class NodeFactorValues {
 public:
  /// node_factor(f, n) for every f. Run time only: trigonometry.
  [[nodiscard]] static NodeFactorValues of(const NodalTerms& n) noexcept;

  [[nodiscard]] constexpr double operator[](NodeFactor f) const noexcept {
    return values_[static_cast<std::size_t>(f)];
  }

  friend constexpr bool operator==(const NodeFactorValues&,
                                   const NodeFactorValues&) = default;

 private:
  explicit constexpr NodeFactorValues(
      const std::array<double, node_factor_count>& values) noexcept
      : values_{values} {}

  std::array<double, node_factor_count> values_;
};
static_assert(std::is_trivially_copyable_v<NodeFactorValues>);

}  // namespace mov::core::tide
