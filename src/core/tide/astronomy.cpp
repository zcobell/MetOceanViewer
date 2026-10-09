// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/tide/astronomy.hpp"

#include <cmath>
#include <numbers>

#include "mov/core/tide/node_factor.hpp"
#include "mov/core/tide/phase.hpp"

namespace mov::core::tide {

namespace {

constexpr double deg_per_rad = 180.0 / std::numbers::pi;
constexpr double rad_per_deg = std::numbers::pi / 180.0;

double sind(double deg) noexcept { return std::sin(deg * rad_per_deg); }
double cosd(double deg) noexcept { return std::cos(deg * rad_per_deg); }
double tand(double deg) noexcept { return std::tan(deg * rad_per_deg); }
double atan2d(double y, double x) noexcept {
  return std::atan2(y, x) * deg_per_rad;
}

/// `deg` in (-180, 180].
double signed_degrees(double deg) noexcept {
  const double r = Phase::wrap(deg).degrees();
  return r > 180.0 ? r - 360.0 : r;
}

// SP 98 Table 1 constants: the obliquity of the ecliptic (1900) and the
// inclination of the moon's orbit to the ecliptic.
constexpr double obliquity_omega = 23.0 + (27.0 / 60.0) + (8.26 / 3600.0);
constexpr double inclination_i = 5.0 + (8.0 / 60.0) + (43.3546 / 3600.0);

}  // namespace

// SP 98 Figure 1 as libcongen solves it (docs/harmonics-json.md A.2). I lies
// in [omega - i, omega + i] (18 to 29 degrees) and nu in [-13.02, 13.02], so
// the square roots below take the positive branch.
NodalTerms nodal_terms(const Elements& at) noexcept {
  const double n = at.N;
  const double cos_i = (cosd(obliquity_omega) * cosd(inclination_i)) -
                       (sind(obliquity_omega) * sind(inclination_i) * cosd(n));
  const double sin_i = std::sqrt(1.0 - (cos_i * cos_i));
  const double i = std::acos(cos_i) * deg_per_rad;

  const double sin_nu = sind(inclination_i) * sind(n) / sin_i;
  const double cos_nu = std::sqrt(1.0 - (sin_nu * sin_nu));
  const double nu = std::asin(sin_nu) * deg_per_rad;

  // The side opposite omega in Figure 1.
  const double sin_side = sind(obliquity_omega) * sind(n) / sin_i;
  const double cos_side =
      (cosd(n) * cos_nu) + (sind(n) * sin_nu * cosd(obliquity_omega));
  const double xi = signed_degrees(n - atan2d(sin_side, cos_side));

  const double sin_2i = sind(2.0 * i);
  const double nu1 = atan2d(sin_2i * sin_nu, (sin_2i * cos_nu) + 0.3347);
  const double sin2_i = sin_i * sin_i;
  const double two_nu2 =
      atan2d(sin2_i * sind(2.0 * nu), (sin2_i * cosd(2.0 * nu)) + 0.0727);

  const double p_term = Phase::wrap(at.p - xi).degrees();
  const double q = atan2d(0.483 * sind(p_term), cosd(p_term));
  const double cot_half_i = 1.0 / tand(i / 2.0);
  const double r =
      atan2d(sind(2.0 * p_term),
             ((cot_half_i * cot_half_i) / 6.0) - cosd(2.0 * p_term));
  const double tan2_half_i = tand(i / 2.0) * tand(i / 2.0);
  return NodalTerms{
      .I = i,
      .nu = nu,
      .xi = xi,
      .nu1 = nu1,
      .two_nu2 = two_nu2,
      .P = p_term,
      .Q = q,
      .R = r,
      .Qu = signed_degrees(p_term - q),
      .Qa = 1.0 / std::sqrt(2.31 + (1.435 * cosd(2.0 * p_term))),
      .Ra = 1.0 / std::sqrt(1.0 - (12.0 * tan2_half_i * cosd(2.0 * p_term)) +
                            (36.0 * tan2_half_i * tan2_half_i)),
  };
}

// docs/harmonics-json.md A.3. The divisors normalize each formula to its
// mean value, as SP 98 tabulates them.
double node_factor(NodeFactor f, const NodalTerms& n) noexcept {
  const double sin_i = sind(n.I);
  const double sin_half = sind(n.I / 2.0);
  const double cos_half = cosd(n.I / 2.0);
  const double sin_2i = sind(2.0 * n.I);
  const auto f75 = [&] { return sin_i * cos_half * cos_half / 0.38; };
  const auto f78 = [&] {
    return cos_half * cos_half * cos_half * cos_half / 0.9154;
  };
  switch (f) {
    case NodeFactor::unity:
      break;
    case NodeFactor::f73:
      return ((2.0 / 3.0) - (sin_i * sin_i)) / 0.5021;
    case NodeFactor::f74:
      return sin_i * sin_i / 0.1578;
    case NodeFactor::f75:
      return f75();
    case NodeFactor::f76:
      return sin_2i / 0.7214;
    case NodeFactor::f77:
      return sin_i * sin_half * sin_half / 0.0164;
    case NodeFactor::f78:
      return f78();
    case NodeFactor::f79:
      return sin_i * sin_i / 0.1565;
    case NodeFactor::f144: {
      const double s2 = sin_half * sin_half;
      return (1.0 - (10.0 * s2) + (15.0 * s2 * s2)) * cos_half * cos_half /
             0.5873;
    }
    case NodeFactor::f149:
      return cos_half * cos_half * cos_half * cos_half * cos_half * cos_half /
             0.8758;
    case NodeFactor::f206:
      return f75() / n.Qa;
    case NodeFactor::f215:
      return f78() / n.Ra;
    case NodeFactor::f227:
      return std::sqrt((0.8965 * sin_2i * sin_2i) +
                       (0.6001 * sin_2i * cosd(n.nu)) + 0.1006);
    case NodeFactor::f235:
      return std::sqrt((19.0444 * sin_i * sin_i * sin_i * sin_i) +
                       (2.7702 * sin_i * sin_i * cosd(2.0 * n.nu)) + 0.0981);
  }
  return 1.0;  // unity
}

}  // namespace mov::core::tide
