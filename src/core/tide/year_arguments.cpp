// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/tide/year_arguments.hpp"

#include <chrono>
#include <expected>

#include "mov/core/tide/astronomy.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/phase.hpp"
#include "mov/core/time.hpp"

namespace mov::core::tide {

std::expected<YearAstronomy, YearOutOfRange> YearAstronomy::make(
    std::chrono::year y) noexcept {
  if (y < first_year or y > last_year) {
    return std::unexpected(YearOutOfRange{.year = y});
  }
  return YearAstronomy{y, elements(year_start(y)),
                       nodal_terms(elements(mid_year(y)))};
}

Equilibrium yearly_equilibrium(const Argument& a,
                               const YearAstronomy& y) noexcept {
  const NodalTerms mid = y.at_mid_year();
  return Equilibrium{
      .argument = Phase::wrap(a.v_deg(y.at_start()) + a.u_deg(mid)),
      .node_factor = a.f(mid)};
}

namespace diagnostic {

Equilibrium instantaneous_equilibrium(const Argument& a, Time t) noexcept {
  const Elements now = elements(t);
  const NodalTerms terms = nodal_terms(now);
  return Equilibrium{.argument = Phase::wrap(a.v_deg(now) + a.u_deg(terms)),
                     .node_factor = a.f(terms)};
}

}  // namespace diagnostic

}  // namespace mov::core::tide
