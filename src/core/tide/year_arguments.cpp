// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/tide/year_arguments.hpp"

#include <algorithm>
#include <chrono>
#include <expected>
#include <iterator>
#include <utility>
#include <vector>

#include "mov/core/tide/astronomy.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/constituent_set.hpp"
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
  return Equilibrium{
      .argument = Phase::wrap(a.v_deg(y.at_start()) + a.u_deg(y.at_mid_year())),
      .node_factor = a.f(y.node_factors())};
}

YearArguments year_arguments(const ConstituentSet& set,
                             const YearAstronomy& y) {
  std::vector<Equilibrium> table;
  table.reserve(set.size());
  std::ranges::transform(
      set.indices(), std::back_inserter(table),
      [&](ConstituentIndex i) { return equilibrium(set.argument(i), y); });
  return YearArguments{set, y.year(), std::move(table)};
}

std::expected<YearArguments, YearOutOfRange> year_arguments(
    const ConstituentSet& set, std::chrono::year y) {
  return YearAstronomy::make(y).transform(
      [&set](const YearAstronomy& astronomy) {
        return year_arguments(set, astronomy);
      });
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
