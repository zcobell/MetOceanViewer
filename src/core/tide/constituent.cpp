// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/tide/constituent.hpp"

#include <algorithm>
#include <cstdint>
#include <expected>
#include <utility>
#include <vector>

#include "mov/core/tide/astronomy.hpp"
#include "mov/core/tide/catalogue.hpp"
#include "mov/core/tide/node_factor.hpp"

namespace mov::core::tide {

double Argument::f(const NodalTerms& n) const noexcept {
  double product = 1.0;
  for (const NodeFactor formula : all_node_factors) {
    const std::uint16_t e = exponent(formula);
    if (e == 0) {
      continue;
    }
    const double value = node_factor(formula, n);
    for (std::uint16_t k = 0; k < e; ++k) {
      product *= value;
    }
  }
  return product;
}

std::expected<CompoundTerms, CompoundErrc> CompoundTerms::make(
    std::vector<Term> terms) {
  if (terms.empty()) {
    return std::unexpected(CompoundErrc::empty);
  }
  if (terms.size() > max_terms) {
    return std::unexpected(CompoundErrc::too_many);
  }
  if (std::ranges::any_of(terms,
                          [](const Term& t) { return t.coefficient == 0; })) {
    return std::unexpected(CompoundErrc::zero_coefficient);
  }
  std::ranges::sort(terms, {}, &Term::base);
  if (std::ranges::adjacent_find(terms, {}, &Term::base) != terms.end()) {
    return std::unexpected(CompoundErrc::duplicate_base);
  }
  return CompoundTerms{std::move(terms)};
}

// The catalogue's invariants, checked once per build here rather than in the
// header, so that its includers do not pay for the evaluation.
static_assert(nos_catalogue.size() == 120);
static_assert(nos_standard_count <= nos_catalogue.size());
static_assert(detail::names_valid(nos_catalogue),
              "every catalogue name is a valid ConstituentName");
static_assert(detail::names_unique(nos_catalogue, catalogue_aliases),
              "catalogue names and aliases are unique ignoring case");
static_assert(detail::compounds_resolve(nos_catalogue),
              "compound terms are sorted, non-zero and name Darwin entries");
static_assert(detail::species_match_names(nos_catalogue),
              "the T coefficient is the species the name states");
static_assert(detail::notes_plain(nos_catalogue, catalogue_notes),
              "notes name entries and need no JSON escaping");
static_assert(detail::aliases_resolve(nos_catalogue, catalogue_aliases),
              "every alias names a catalogue entry");

}  // namespace mov::core::tide
