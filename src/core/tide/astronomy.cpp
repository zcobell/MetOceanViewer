// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/tide/astronomy.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
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

/// The trigonometry of I that the node-factor formulas share.
struct ITrig {
  double sin_i;
  double sin_half;
  double cos_half;
  double sin_2i;
};

ITrig trig_of(double i) noexcept {
  return ITrig{.sin_i = sind(i),
               .sin_half = sind(i / 2.0),
               .cos_half = cosd(i / 2.0),
               .sin_2i = sind(2.0 * i)};
}

// The SP 98 formulas (docs/harmonics-json.md A.3), one per number, all of one
// signature so that a table can select them. The divisors normalize each to
// its mean value, as SP 98 tabulates them.
double unity(const ITrig& /*t*/, const NodalTerms& /*n*/) noexcept {
  return 1.0;
}
double f73(const ITrig& t, const NodalTerms& /*n*/) noexcept {
  return ((2.0 / 3.0) - (t.sin_i * t.sin_i)) / 0.5021;
}
double f74(const ITrig& t, const NodalTerms& /*n*/) noexcept {
  return t.sin_i * t.sin_i / 0.1578;
}
double f75(const ITrig& t, const NodalTerms& /*n*/) noexcept {
  return t.sin_i * t.cos_half * t.cos_half / 0.38;
}
double f76(const ITrig& t, const NodalTerms& /*n*/) noexcept {
  return t.sin_2i / 0.7214;
}
double f77(const ITrig& t, const NodalTerms& /*n*/) noexcept {
  return t.sin_i * t.sin_half * t.sin_half / 0.0164;
}
double f78(const ITrig& t, const NodalTerms& /*n*/) noexcept {
  const double c2 = t.cos_half * t.cos_half;
  return c2 * c2 / 0.9154;
}
double f79(const ITrig& t, const NodalTerms& /*n*/) noexcept {
  return t.sin_i * t.sin_i / 0.1565;
}
double f144(const ITrig& t, const NodalTerms& /*n*/) noexcept {
  const double s2 = t.sin_half * t.sin_half;
  return (1.0 - (10.0 * s2) + (15.0 * s2 * s2)) * t.cos_half * t.cos_half /
         0.5873;
}
double f149(const ITrig& t, const NodalTerms& /*n*/) noexcept {
  const double c2 = t.cos_half * t.cos_half;
  return c2 * c2 * c2 / 0.8758;
}
double f206(const ITrig& t, const NodalTerms& n) noexcept {
  return f75(t, n) / n.Qa;
}
double f215(const ITrig& t, const NodalTerms& n) noexcept {
  return f78(t, n) / n.Ra;
}
double f227(const ITrig& t, const NodalTerms& n) noexcept {
  return std::sqrt((0.8965 * t.sin_2i * t.sin_2i) +
                   (0.6001 * t.sin_2i * cosd(n.nu)) + 0.1006);
}
double f235(const ITrig& t, const NodalTerms& n) noexcept {
  const double s2 = t.sin_i * t.sin_i;
  return std::sqrt((19.0444 * s2 * s2) + (2.7702 * s2 * cosd(2.0 * n.nu)) +
                   0.0981);
}

/// Each enumerator beside its formula, in enumerator order (checked below):
/// a table rather than a 14-way switch, which sat at the complexity cap.
struct Formula {
  NodeFactor factor;
  double (*value)(const ITrig&, const NodalTerms&) noexcept;
};
constexpr std::array<Formula, node_factor_count> formulas{{
    {.factor = NodeFactor::unity, .value = unity},
    {.factor = NodeFactor::f73, .value = f73},
    {.factor = NodeFactor::f74, .value = f74},
    {.factor = NodeFactor::f75, .value = f75},
    {.factor = NodeFactor::f76, .value = f76},
    {.factor = NodeFactor::f77, .value = f77},
    {.factor = NodeFactor::f78, .value = f78},
    {.factor = NodeFactor::f79, .value = f79},
    {.factor = NodeFactor::f144, .value = f144},
    {.factor = NodeFactor::f149, .value = f149},
    {.factor = NodeFactor::f206, .value = f206},
    {.factor = NodeFactor::f215, .value = f215},
    {.factor = NodeFactor::f227, .value = f227},
    {.factor = NodeFactor::f235, .value = f235},
}};
static_assert(std::ranges::equal(formulas, all_node_factors, {},
                                 &Formula::factor));

}  // namespace

// SP 98 Figure 1 as libcongen solves it (docs/harmonics-json.md A.2). I lies
// in [omega - i, omega + i] (18 to 29 degrees) and nu in [-13.02, 13.02], so
// the square roots below take the positive branch.
NodalTerms nodal_terms(const Elements& at) noexcept {
  const double n = at.N.degrees();
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

  const double p_term = Phase::wrap(at.p.degrees() - xi).degrees();
  const double q = atan2d(0.483 * sind(p_term), cosd(p_term));
  const double tan_half_i = tand(i / 2.0);
  const double tan2_half_i = tan_half_i * tan_half_i;
  const double cos_2p = cosd(2.0 * p_term);
  const double r =
      atan2d(sind(2.0 * p_term), (1.0 / (tan2_half_i * 6.0)) - cos_2p);
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
      .Qa = 1.0 / std::sqrt(2.31 + (1.435 * cos_2p)),
      .Ra = 1.0 / std::sqrt(1.0 - (12.0 * tan2_half_i * cos_2p) +
                            (36.0 * tan2_half_i * tan2_half_i)),
  };
}

double node_factor(NodeFactor f, const NodalTerms& n) noexcept {
  const auto i = static_cast<std::size_t>(f);
  if (i >= formulas.size()) {
    assert(false and "not a NodeFactor enumerator");
    return 1.0;
  }
  return formulas[i].value(trig_of(n.I), n);
}

NodeFactorValues NodeFactorValues::of(const NodalTerms& n) noexcept {
  const ITrig trig = trig_of(n.I);  // once for all 14 formulas
  std::array<double, node_factor_count> values{};
  std::ranges::transform(formulas, values.begin(),
                         [&](const Formula& f) { return f.value(trig, n); });
  return NodeFactorValues{values};
}

}  // namespace mov::core::tide
