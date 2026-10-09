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

namespace {

bool in_range(std::chrono::year y) noexcept {
  return y >= first_year and y <= last_year;
}

}  // namespace

std::expected<YearAstronomy, YearOutOfRange> YearAstronomy::make(
    std::chrono::year y) noexcept {
  if (not in_range(y)) {
    return std::unexpected(YearOutOfRange{.year = y});
  }
  return YearAstronomy{y, elements(year_start(y)),
                       nodal_terms(elements(mid_year(y)))};
}

Equilibrium equilibrium(const Argument& a, const YearAstronomy& y) noexcept {
  const NodalTerms mid = y.at_mid_year();
  return Equilibrium{
      .argument = Phase::wrap(a.v_deg(y.at_start()) + a.u_deg(mid)),
      .node_factor = a.f(mid)};
}

namespace diagnostic {

std::expected<InstantAstronomy, YearOutOfRange> InstantAstronomy::make(
    Time t) noexcept {
  const std::chrono::year y = year_of(t);
  if (not in_range(y)) {
    return std::unexpected(YearOutOfRange{.year = y});
  }
  const Elements e = elements(t);
  return InstantAstronomy{t, e, nodal_terms(e)};
}

Equilibrium equilibrium(const Argument& a,
                        const InstantAstronomy& now) noexcept {
  const NodalTerms n = now.nodal_at_instant();
  return Equilibrium{
      .argument = Phase::wrap(a.v_deg(now.at_instant()) + a.u_deg(n)),
      .node_factor = a.f(n)};
}

}  // namespace diagnostic

}  // namespace mov::core::tide
