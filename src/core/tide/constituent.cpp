// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/tide/constituent.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <optional>
#include <string_view>

#include "mov/core/tide/astronomy.hpp"
#include "mov/core/tide/builtin.hpp"
#include "mov/core/tide/catalogue.hpp"
#include "mov/core/tide/node_factor.hpp"

namespace mov::core::tide {

double Argument::f(const NodalTerms& n) const noexcept {
  return f(NodeFactorValues::of(n));
}

std::optional<Argument> builtin_argument(std::string_view name) noexcept {
  // Every entry flattened once, in catalogue order (the invariants below
  // guarantee each flattens).
  static const std::array<Argument, nos_catalogue.size()> arguments = [] {
    std::array<Argument, nos_catalogue.size()> all{};
    std::ranges::transform(
        nos_catalogue, all.begin(), [](const CatalogueEntry& e) {
          const auto a =
              flatten(e.definition, detail::lookup_in(nos_catalogue));
          assert(a.has_value());  // compound_terms_resolve, asserted below
          return *a;
        });
    return all;
  }();
  const auto canonical = canonical_name(name);
  if (not canonical) {
    return std::nullopt;
  }
  const auto i = catalogue_index(*canonical);
  return i ? std::optional{arguments[*i]} : std::nullopt;
}

// The catalogue's invariants, checked once per build here rather than in the
// header, so that its includers do not pay for the evaluation. Valid names
// and well-formed compounds need no check: the catalogue's consteval
// builders refuse anything else at compile time.
static_assert(nos_catalogue.size() == 120);
static_assert(nos_standard_count <= nos_catalogue.size());
static_assert(detail::names_unique(nos_catalogue, catalogue_aliases),
              "catalogue names and aliases are unique ignoring case");
static_assert(detail::compound_terms_resolve(nos_catalogue),
              "every compound term names a Darwin entry");
static_assert(detail::species_match_names(nos_catalogue),
              "the T coefficient is the species the name states");
static_assert(detail::notes_plain(nos_catalogue),
              "notes need no JSON escaping");
static_assert(detail::aliases_resolve(nos_catalogue, catalogue_aliases),
              "every alias names a catalogue entry");

// The worst Argument flattening can build under the file's bounds (HE 2.2,
// the schema): 8 compound terms of coefficient 16, each a Darwin entry with
// every coefficient at its bound. It fits; + and scale would refuse it
// otherwise.
static_assert([] {
  const Argument extreme =
      Argument::darwin(Darwin{.v = {16, 64, 64, 64, 64},
                              .c_deg = 360,
                              .u = {64, 64, 64, 64, 64, 64, 64},
                              .f = NodeFactor::f78});
  Argument sum;
  for (int i = 0; i < 8; ++i) {
    sum = sum + scale(16, extreme);
  }
  return sum.c_deg() == 8 * 16 * 360 and sum.v()[1] == 8 * 16 * 64 and
         sum.exponent(NodeFactor::f78) == 8 * 16;
}());

// And under the types' own bounds, which make() accepts beyond the schema:
// 8 terms of -128 times int8 and int16 extremes.
static_assert([] {
  const Argument extreme =
      Argument::darwin(Darwin{.v = {-128, -128, -128, -128, -128},
                              .c_deg = -32768,
                              .u = {-128, -128, -128, -128, -128, -128, -128},
                              .f = NodeFactor::f78});
  Argument sum;
  for (int i = 0; i < 8; ++i) {
    sum = sum + scale(-128, extreme);
  }
  return sum.c_deg() == 8 * 128 * 32768 and
         sum.exponent(NodeFactor::f78) == 8 * 128;
}());

}  // namespace mov::core::tide
