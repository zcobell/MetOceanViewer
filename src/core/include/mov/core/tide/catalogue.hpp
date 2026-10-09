// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/ascii.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/node_factor.hpp"

// The built-in constituent catalogue (docs/harmonics-engine.md section 2.3):
// the 37 constituents NOAA CO-OPS publishes for most stations
// (docs/harmonics-json.md Appendix B), then the 83 further ones it publishes
// for 9455920 Anchorage, all in NOAA's spelling and NOAA's order. The
// definitions are SP 98 Table 2 as libcongen 1.7 (congen_input.txt) codes
// them. A harmonics file's own dictionary is authoritative at run time
// (docs/harmonics-json.md 7.3); this catalogue is the source of
// docs/schemas/constituents-nos.json, which the builder reads.
//
// The entries are built by consteval functions that call value() on every
// make(), so an invalid name or a malformed compound does not compile. The
// other invariants are detail:: predicates over any catalogue span,
// static_asserted in src/core/tide/constituent.cpp and shown in
// tests/core/tide to reject a catalogue that breaks each one: names and
// aliases unique ignoring case; compound terms name Darwin entries; the T
// coefficient equals the species the name ends in; notes need no escaping;
// aliases name catalogue entries.

namespace mov::core::tide {

/// One catalogue constituent. `note` says why an entry is defined as it is,
/// where that is not plain SP 98 Table 2; it becomes the JSON `description`.
struct CatalogueEntry {
  ConstituentName name;
  Definition definition;
  std::optional<std::string_view> note;
  friend constexpr bool operator==(const CatalogueEntry&,
                                   const CatalogueEntry&) = default;
};

/// A spelling another source uses for a catalogue constituent with the same
/// definition (checked against congen_input.txt; docs/harmonics-engine.md
/// section 2.3). Exact spellings: the builder folds case per source.
struct CatalogueAlias {
  ConstituentName alias;
  ConstituentName name;
  friend constexpr bool operator==(const CatalogueAlias&,
                                   const CatalogueAlias&) = default;
};

}  // namespace mov::core::tide

namespace mov::core::detail {

/// A catalogue entry for a Darwin constituent; a bad name does not compile.
[[nodiscard]] consteval tide::CatalogueEntry darwin_entry(
    std::string_view name, tide::Darwin d,
    std::optional<std::string_view> note = std::nullopt) {
  return tide::CatalogueEntry{
      .name = tide::constituent_name(name), .definition = d, .note = note};
}

/// A catalogue entry for a compound; a bad name or term does not compile.
// A table coefficient as a Term's int8_t; out of range does not compile
// (std::abort is not a constant expression). The table spells coefficients as
// int: building a pair<string_view, int8_t> from the literal would narrow
// inside std::pair, which MSVC reports as C4242.
[[nodiscard]] consteval std::int8_t coefficient_of(int c) {
  if (c < std::numeric_limits<std::int8_t>::min() or
      c > std::numeric_limits<std::int8_t>::max()) {
    std::abort();
  }
  return static_cast<std::int8_t>(c);
}

[[nodiscard]] consteval tide::CatalogueEntry compound_entry(
    std::string_view name,
    std::initializer_list<std::pair<std::string_view, int>> terms,
    std::optional<std::string_view> note = std::nullopt) {
  // No std::vector: its allocator machinery costs constexpr steps, and the
  // catalogue's initializer is evaluated in every TU that includes it. More
  // than max_terms terms index past the array, which does not compile.
  using Term = tide::CompoundTerms::Term;
  const Term first{.base = tide::constituent_name(terms.begin()->first),
                   .coefficient = coefficient_of(terms.begin()->second)};
  std::array<Term, tide::CompoundTerms::max_terms> parsed{
      first, first, first, first, first, first, first, first};
  std::size_t count = 0;
  for (const auto& [base, coefficient] : terms) {
    parsed[count] = Term{.base = tide::constituent_name(base),
                         .coefficient = coefficient_of(coefficient)};
    ++count;
  }
  return tide::CatalogueEntry{
      .name = tide::constituent_name(name),
      .definition =
          tide::CompoundTerms::make(std::span{parsed}.first(count)).value(),
      .note = note};
}

[[nodiscard]] consteval tide::CatalogueAlias catalogue_alias(
    std::string_view alias, std::string_view name) {
  return tide::CatalogueAlias{.alias = tide::constituent_name(alias),
                              .name = tide::constituent_name(name)};
}

}  // namespace mov::core::detail

namespace mov::core::tide {

/// The catalogue, in NOAA's order: entries 0-36 are the NOS 37 (Appendix B
/// numbering), the rest the further constituents of 9455920.
inline constexpr std::array nos_catalogue{
    detail::darwin_entry("M2", Darwin{.v = {2, -2, 2, 0, 0},
                                      .c_deg = 0,
                                      .u = {2, -2, 0, 0, 0, 0, 0},
                                      .f = NodeFactor::f78}),
    detail::darwin_entry("S2", Darwin{.v = {2, 0, 0, 0, 0},
                                      .c_deg = 0,
                                      .u = {0, 0, 0, 0, 0, 0, 0},
                                      .f = NodeFactor::unity}),
    detail::darwin_entry("N2", Darwin{.v = {2, -3, 2, 1, 0},
                                      .c_deg = 0,
                                      .u = {2, -2, 0, 0, 0, 0, 0},
                                      .f = NodeFactor::f78}),
    detail::darwin_entry("K1", Darwin{.v = {1, 0, 1, 0, 0},
                                      .c_deg = -90,
                                      .u = {0, 0, -1, 0, 0, 0, 0},
                                      .f = NodeFactor::f227}),
    detail::compound_entry("M4", {{"M2", 2}}),
    detail::darwin_entry("O1", Darwin{.v = {1, -2, 1, 0, 0},
                                      .c_deg = 90,
                                      .u = {2, -1, 0, 0, 0, 0, 0},
                                      .f = NodeFactor::f75}),
    detail::compound_entry("M6", {{"M2", 3}}),
    detail::compound_entry("MK3", {{"K1", 1}, {"M2", 1}}),
    detail::compound_entry("S4", {{"S2", 2}}),
    detail::compound_entry("MN4", {{"M2", 1}, {"N2", 1}}),
    detail::darwin_entry("NU2", Darwin{.v = {2, -3, 4, -1, 0},
                                       .c_deg = 0,
                                       .u = {2, -2, 0, 0, 0, 0, 0},
                                       .f = NodeFactor::f78}),
    detail::compound_entry("S6", {{"S2", 3}}),
    detail::darwin_entry("MU2", Darwin{.v = {2, -4, 4, 0, 0},
                                       .c_deg = 0,
                                       .u = {2, -2, 0, 0, 0, 0, 0},
                                       .f = NodeFactor::f78}),
    detail::darwin_entry("2N2", Darwin{.v = {2, -4, 2, 2, 0},
                                       .c_deg = 0,
                                       .u = {2, -2, 0, 0, 0, 0, 0},
                                       .f = NodeFactor::f78}),
    detail::darwin_entry("OO1", Darwin{.v = {1, 2, 1, 0, 0},
                                       .c_deg = -90,
                                       .u = {-2, -1, 0, 0, 0, 0, 0},
                                       .f = NodeFactor::f77}),
    detail::darwin_entry("LAM2", Darwin{.v = {2, -1, 0, 1, 0},
                                        .c_deg = 180,
                                        .u = {2, -2, 0, 0, 0, 0, 0},
                                        .f = NodeFactor::f78}),
    detail::darwin_entry("S1", Darwin{.v = {1, 0, 0, 0, 0},
                                      .c_deg = 0,
                                      .u = {0, 0, 0, 0, 0, 0, 0},
                                      .f = NodeFactor::unity}),
    detail::darwin_entry(
        "M1",
        Darwin{.v = {1, -1, 1, 0, 0},
               .c_deg = -90,
               .u = {1, -1, 0, 0, 1, 0, 0},
               .f = NodeFactor::f206},
        "NOS form (SP 98 Table 2, second formula): u includes Q, so the speed "
        "includes the p rate (SP 98 paragraph 124)"),
    detail::darwin_entry("J1", Darwin{.v = {1, 1, 1, -1, 0},
                                      .c_deg = -90,
                                      .u = {0, -1, 0, 0, 0, 0, 0},
                                      .f = NodeFactor::f76}),
    detail::darwin_entry("MM", Darwin{.v = {0, 1, 0, -1, 0},
                                      .c_deg = 0,
                                      .u = {0, 0, 0, 0, 0, 0, 0},
                                      .f = NodeFactor::f73}),
    detail::darwin_entry("SSA", Darwin{.v = {0, 0, 2, 0, 0},
                                       .c_deg = 0,
                                       .u = {0, 0, 0, 0, 0, 0, 0},
                                       .f = NodeFactor::unity}),
    detail::darwin_entry("SA", Darwin{.v = {0, 0, 1, 0, 0},
                                      .c_deg = 0,
                                      .u = {0, 0, 0, 0, 0, 0, 0},
                                      .f = NodeFactor::unity}),
    detail::compound_entry("MSF", {{"M2", -1}, {"S2", 1}},
                           "S2 - M2 (SP 98 p. 48), so u = -u(M2)"),
    detail::darwin_entry("MF", Darwin{.v = {0, 2, 0, 0, 0},
                                      .c_deg = 0,
                                      .u = {-2, 0, 0, 0, 0, 0, 0},
                                      .f = NodeFactor::f74}),
    detail::darwin_entry("RHO", Darwin{.v = {1, -3, 3, -1, 0},
                                       .c_deg = 90,
                                       .u = {2, -1, 0, 0, 0, 0, 0},
                                       .f = NodeFactor::f75}),
    detail::darwin_entry("Q1", Darwin{.v = {1, -3, 1, 1, 0},
                                      .c_deg = 90,
                                      .u = {2, -1, 0, 0, 0, 0, 0},
                                      .f = NodeFactor::f75}),
    detail::darwin_entry("T2", Darwin{.v = {2, 0, -1, 0, 1},
                                      .c_deg = 0,
                                      .u = {0, 0, 0, 0, 0, 0, 0},
                                      .f = NodeFactor::unity}),
    detail::darwin_entry("R2", Darwin{.v = {2, 0, 1, 0, -1},
                                      .c_deg = 180,
                                      .u = {0, 0, 0, 0, 0, 0, 0},
                                      .f = NodeFactor::unity}),
    detail::darwin_entry("2Q1", Darwin{.v = {1, -4, 1, 2, 0},
                                       .c_deg = 90,
                                       .u = {2, -1, 0, 0, 0, 0, 0},
                                       .f = NodeFactor::f75}),
    detail::darwin_entry("P1", Darwin{.v = {1, 0, -1, 0, 0},
                                      .c_deg = 90,
                                      .u = {0, 0, 0, 0, 0, 0, 0},
                                      .f = NodeFactor::unity}),
    detail::compound_entry("2SM2", {{"M2", -1}, {"S2", 2}}),
    detail::darwin_entry("M3", Darwin{.v = {3, -3, 3, 0, 0},
                                      .c_deg = 0,
                                      .u = {3, -3, 0, 0, 0, 0, 0},
                                      .f = NodeFactor::f149}),
    detail::darwin_entry("L2", Darwin{.v = {2, -1, 2, -1, 0},
                                      .c_deg = 180,
                                      .u = {2, -2, 0, 0, 0, -1, 0},
                                      .f = NodeFactor::f215}),
    detail::compound_entry("2MK3", {{"K1", -1}, {"M2", 2}}),
    detail::darwin_entry("K2", Darwin{.v = {2, 0, 2, 0, 0},
                                      .c_deg = 0,
                                      .u = {0, 0, 0, -1, 0, 0, 0},
                                      .f = NodeFactor::f235}),
    detail::compound_entry("M8", {{"M2", 4}}),
    detail::compound_entry("MS4", {{"M2", 1}, {"S2", 1}}),
    detail::darwin_entry("SIGMA1", Darwin{.v = {1, -4, 3, 0, 0},
                                          .c_deg = 90,
                                          .u = {2, -1, 0, 0, 0, 0, 0},
                                          .f = NodeFactor::f75}),
    detail::darwin_entry("MP1", Darwin{.v = {1, -2, 3, 0, 0},
                                       .c_deg = -90,
                                       .u = {0, -1, 0, 0, 0, 0, 0},
                                       .f = NodeFactor::f76}),
    detail::darwin_entry("CHI1", Darwin{.v = {1, -1, 3, -1, 0},
                                        .c_deg = -90,
                                        .u = {0, -1, 0, 0, 0, 0, 0},
                                        .f = NodeFactor::f76}),
    detail::compound_entry("2PO1", {{"O1", -1}, {"P1", 2}}),
    detail::darwin_entry("SO1", Darwin{.v = {1, 2, -1, 0, 0},
                                       .c_deg = -90,
                                       .u = {0, -1, 0, 0, 0, 0, 0},
                                       .f = NodeFactor::f76}),
    detail::compound_entry("MSN2", {{"M2", 1}, {"N2", -1}, {"S2", 1}}),
    detail::compound_entry("MNS2", {{"M2", 1}, {"N2", 1}, {"S2", -1}}),
    detail::compound_entry("OP2", {{"O1", 1}, {"P1", 1}}),
    detail::compound_entry("MKS2", {{"K2", 1}, {"M2", 1}, {"S2", -1}}),
    detail::compound_entry("2NS2", {{"N2", 2}, {"S2", -1}}),
    detail::compound_entry("MLN2S2",
                           {{"L2", 1}, {"M2", 1}, {"N2", 1}, {"S2", -2}}),
    detail::compound_entry("2ML2S2", {{"L2", 1}, {"M2", 2}, {"S2", -2}}),
    detail::compound_entry("SKM2", {{"K2", 1}, {"M2", -1}, {"S2", 1}}),
    detail::compound_entry("2MS2K2", {{"K2", -2}, {"M2", 2}, {"S2", 1}}),
    detail::compound_entry("MKL2S2",
                           {{"K2", 1}, {"L2", 1}, {"M2", 1}, {"S2", -2}}),
    detail::compound_entry("M2KS2", {{"K2", 2}, {"M2", 1}, {"S2", -2}}),
    detail::compound_entry("2SNMK2",
                           {{"K2", -1}, {"M2", -1}, {"N2", 1}, {"S2", 2}}),
    detail::compound_entry("2KMSN2",
                           {{"K2", 2}, {"M2", 1}, {"N2", -1}, {"S2", -1}}),
    detail::compound_entry("SO3", {{"O1", 1}, {"S2", 1}}),
    detail::compound_entry("SK3", {{"K1", 1}, {"S2", 1}}),
    detail::compound_entry("NO3", {{"N2", 1}, {"O1", 1}}),
    detail::compound_entry("MK4", {{"K2", 1}, {"M2", 1}}),
    detail::compound_entry("SN4", {{"N2", 1}, {"S2", 1}}),
    detail::compound_entry("2MLS4", {{"L2", 1}, {"M2", 2}, {"S2", -1}}),
    detail::compound_entry("3MS4", {{"M2", 3}, {"S2", -1}}),
    detail::compound_entry("ML4", {{"L2", 1}, {"M2", 1}}),
    detail::compound_entry("N4", {{"N2", 2}}),
    detail::compound_entry("SL4", {{"L2", 1}, {"S2", 1}}),
    detail::compound_entry("MNO5", {{"M2", 1}, {"N2", 1}, {"O1", 1}}),
    detail::compound_entry("2MO5", {{"M2", 2}, {"O1", 1}}),
    detail::compound_entry("2MK5", {{"K1", 1}, {"M2", 2}}),
    detail::compound_entry("MSK5", {{"K1", 1}, {"M2", 1}, {"S2", 1}}),
    detail::compound_entry("3KM5", {{"K1", 1}, {"K2", 1}, {"M2", 1}},
                           "K1 + M2 + K2 (Zetler and Cummings 1967; congen "
                           "since 2018), not 3 K1 + M2"),
    detail::compound_entry("2MP5", {{"M2", 2}, {"P1", 1}}),
    detail::compound_entry("3MP5", {{"M2", 3}, {"P1", -1}}),
    detail::compound_entry("MNK5", {{"K1", 1}, {"M2", 1}, {"N2", 1}}),
    detail::compound_entry("2SM6", {{"M2", 1}, {"S2", 2}}),
    detail::compound_entry("2MN6", {{"M2", 2}, {"N2", 1}}),
    detail::compound_entry("MSN6", {{"M2", 1}, {"N2", 1}, {"S2", 1}}),
    detail::compound_entry("2MS6", {{"M2", 2}, {"S2", 1}}),
    detail::compound_entry("2NMLS6",
                           {{"L2", 1}, {"M2", 1}, {"N2", 2}, {"S2", -1}}),
    detail::compound_entry("2NM6", {{"M2", 1}, {"N2", 2}}),
    detail::compound_entry("MSL6", {{"L2", 1}, {"M2", 1}, {"S2", 1}}),
    detail::compound_entry("2ML6", {{"L2", 1}, {"M2", 2}}),
    detail::compound_entry("MSK6", {{"K2", 1}, {"M2", 1}, {"S2", 1}}),
    detail::compound_entry("2MLNS6",
                           {{"L2", 1}, {"M2", 2}, {"N2", 1}, {"S2", -1}}),
    detail::compound_entry("3MLS6", {{"L2", 1}, {"M2", 3}, {"S2", -1}}),
    detail::compound_entry("2MK6", {{"K2", 1}, {"M2", 2}}),
    detail::compound_entry("2MNO7", {{"M2", 2}, {"N2", 1}, {"O1", 1}}),
    detail::compound_entry("2NMK7", {{"K1", 1}, {"M2", 1}, {"N2", 2}}),
    detail::compound_entry("2MSO7", {{"M2", 2}, {"O1", 1}, {"S2", 1}}),
    detail::compound_entry("MSKO7",
                           {{"K2", 1}, {"M2", 1}, {"O1", 1}, {"S2", 1}}),
    detail::compound_entry("2MSN8", {{"M2", 2}, {"N2", 1}, {"S2", 1}}),
    detail::compound_entry("3MS8", {{"M2", 3}, {"S2", 1}}),
    detail::compound_entry("2MS8", {{"M2", 2}, {"S2", 2}}),
    detail::compound_entry("2MN8", {{"M2", 2}, {"N2", 2}}),
    detail::compound_entry("3MN8", {{"M2", 3}, {"N2", 1}}),
    detail::compound_entry("2MSL8", {{"L2", 1}, {"M2", 2}, {"S2", 1}}),
    detail::compound_entry("4MLS8", {{"L2", 1}, {"M2", 4}, {"S2", -1}}),
    detail::compound_entry("3ML8", {{"L2", 1}, {"M2", 3}}),
    detail::compound_entry("3MK8", {{"K2", 1}, {"M2", 3}}),
    detail::compound_entry("2MSK8", {{"K2", 1}, {"M2", 2}, {"S2", 1}}),
    detail::compound_entry("2M2NK9", {{"K1", 1}, {"M2", 2}, {"N2", 2}}),
    detail::compound_entry("3MNK9", {{"K1", 1}, {"M2", 3}, {"N2", 1}}),
    detail::compound_entry("4MK9", {{"K1", 1}, {"M2", 4}}),
    detail::compound_entry("3MSK9", {{"K1", 1}, {"M2", 3}, {"S2", 1}}),
    detail::compound_entry("4MN10", {{"M2", 4}, {"N2", 1}}),
    detail::compound_entry("M10", {{"M2", 5}}),
    detail::compound_entry("3MNS10", {{"M2", 3}, {"N2", 1}, {"S2", 1}}),
    detail::compound_entry("4MS10", {{"M2", 4}, {"S2", 1}}),
    detail::compound_entry("3MSL10", {{"L2", 1}, {"M2", 3}, {"S2", 1}}),
    detail::compound_entry("3M2S10", {{"M2", 3}, {"S2", 2}}),
    detail::compound_entry("4MSK11", {{"K1", 1}, {"M2", 4}, {"S2", 1}}),
    detail::compound_entry("4MNS12", {{"M2", 4}, {"N2", 1}, {"S2", 1}}),
    detail::compound_entry("5MS12", {{"M2", 5}, {"S2", 1}}),
    detail::compound_entry("4MSL12", {{"L2", 1}, {"M2", 4}, {"S2", 1}}),
    detail::compound_entry("4M2S12", {{"M2", 4}, {"S2", 2}}),
    detail::darwin_entry("TK1", Darwin{.v = {1, 0, -2, 0, 1},
                                       .c_deg = 90,
                                       .u = {0, 0, 1, 0, 0, 0, 0},
                                       .f = NodeFactor::f227}),
    detail::darwin_entry(
        "RP1",
        Darwin{.v = {1, 0, 2, 0, -1},
               .c_deg = -90,
               .u = {0, 0, 0, 0, 0, 0, 0},
               .f = NodeFactor::unity},
        "congen PSI1, the phase NOAA appears to publish RP1 in; provisional: "
        "vetted by a one-off comparison with CO-OPS at 9455920 on 2026-10-09 "
        "(6-minute predictions 2025-12-30 to 2026-01-02, 2026-06-29 to 07-04, "
        "2026-10-01 to 10-08) until the H3 Anchorage golden exists; congen RP1 "
        "is 180 degrees from it"),
    detail::compound_entry("KP1", {{"K2", 1}, {"P1", -1}}),
    detail::darwin_entry("THETA1", Darwin{.v = {1, 1, -1, 1, 0},
                                          .c_deg = -90,
                                          .u = {0, -1, 0, 0, 0, 0, 0},
                                          .f = NodeFactor::f76}),
    detail::darwin_entry(
        "KJ2",
        Darwin{.v = {2, 1, 2, -1, 0},
               .c_deg = 180,
               .u = {0, -2, 0, 0, 0, 0, 0},
               .f = NodeFactor::f79},
        "congen KJ2-IHO, the IHO phase NOAA appears to publish KJ2 in; "
        "provisional: vetted by a one-off comparison with CO-OPS at 9455920 on "
        "2026-10-09 (the same three windows) until the H3 Anchorage golden "
        "exists; SP 98 KJ2 is 180 degrees from it"),
    detail::compound_entry("OO2", {{"O1", 1}, {"Q1", 1}},
                           "O1 + Q1, congen OQ2-HORN"),
};

/// How many leading entries are the NOS 37.
inline constexpr std::size_t nos_standard_count = 37;

inline constexpr std::array catalogue_aliases{
    // congen and XTide spellings.
    detail::catalogue_alias("LDA2", "LAM2"),
    detail::catalogue_alias("RHO1", "RHO"),
    detail::catalogue_alias("SIG1", "SIGMA1"),
    detail::catalogue_alias("THE1", "THETA1"),
    detail::catalogue_alias("OQ2-HORN", "OO2"),
    detail::catalogue_alias("2(MS)8", "2MS8"),
    detail::catalogue_alias("2(MN)8", "2MN8"),
    detail::catalogue_alias("2MNLS6", "2MLNS6"),
    detail::catalogue_alias("M2(KS)2", "M2KS2"),
    detail::catalogue_alias("2SN(MK)2", "2SNMK2"),
    detail::catalogue_alias("2KM(SN)2", "2KMSN2"),
    detail::catalogue_alias("KJ2-IHO", "KJ2"),
    detail::catalogue_alias("PSI1", "RP1"),
    // TICON-4 spellings (docs/harmonics-json.md 7.3).
    detail::catalogue_alias("LAMBDA2", "LAM2"),
    detail::catalogue_alias("SGM", "SIGMA1"),
};

}  // namespace mov::core::tide

// The lookups and the invariants, over any catalogue span.
namespace mov::core::detail {

using CatalogueSpan = std::span<const tide::CatalogueEntry>;

[[nodiscard]] constexpr std::optional<std::size_t> index_in(
    CatalogueSpan catalogue, std::string_view name) noexcept {
  // A plain loop, the length checked first: the invariants call this a few
  // hundred times in constant evaluation, where std::ranges costs several
  // times more steps.
  for (std::size_t i = 0; i < catalogue.size(); ++i) {
    const std::string_view candidate = catalogue[i].name.view();
    if (candidate.size() == name.size() and candidate == name) {
      return i;
    }
  }
  return std::nullopt;
}

/// The DefinitionLookup of a catalogue, for flatten.
[[nodiscard]] constexpr auto lookup_in(CatalogueSpan catalogue) noexcept {
  return [catalogue](const tide::ConstituentName& n) -> tide::DefinitionRef {
    const auto i = index_in(catalogue, n.view());
    if (not i) {
      return std::nullopt;
    }
    return std::cref(catalogue[*i].definition);
  };
}

/// The species a name states: its trailing digits (M2 2, 4MSK11 11), 0 when
/// it ends in a letter (SA, MSF), except RHO (NOAA's spelling of RHO1): 1.
[[nodiscard]] constexpr std::int32_t species_of_name(
    std::string_view name) noexcept {
  if (name == "RHO") {
    return 1;
  }
  std::size_t first_digit = name.size();
  while (first_digit > 0 and ascii::is_digit(name[first_digit - 1])) {
    --first_digit;
  }
  std::int32_t species = 0;
  for (const char c : name.substr(first_digit)) {
    species = (species * 10) + (c - '0');
  }
  return species;
}

[[nodiscard]] constexpr bool less_ignoring_case(std::string_view a,
                                                std::string_view b) noexcept {
  const std::size_t n = a.size() < b.size() ? a.size() : b.size();
  for (std::size_t i = 0; i < n; ++i) {
    const char x = ascii::to_lower(a[i]);
    const char y = ascii::to_lower(b[i]);
    if (x != y) {
      return x < y;
    }
  }
  return a.size() < b.size();
}

/// No two names, catalogue names and aliases together, are equal ignoring
/// case: sorted, then neighbours compared. std::sort with a plain comparator
/// (n log n) is the cheapest form in constant evaluation, where these
/// predicates run; std::ranges and an all-pairs loop cost several times more.
[[nodiscard]] constexpr bool names_unique(
    CatalogueSpan catalogue,
    std::span<const tide::CatalogueAlias> aliases) noexcept {
  std::vector<std::string_view> names;
  names.reserve(catalogue.size() + aliases.size());
  for (const auto& e : catalogue) {
    names.push_back(e.name.view());
  }
  for (const auto& a : aliases) {
    names.push_back(a.alias.view());
  }
  std::sort(names.begin(), names.end(), less_ignoring_case);
  for (std::size_t i = 1; i < names.size(); ++i) {
    if (not less_ignoring_case(names[i - 1], names[i])) {
      return false;
    }
  }
  return true;
}

/// The Darwin definition of the entry named `name`, if it is one.
[[nodiscard]] constexpr std::optional<tide::Darwin> darwin_in(
    CatalogueSpan catalogue, std::string_view name) noexcept {
  const auto i = index_in(catalogue, name);
  if (not i or
      not std::holds_alternative<tide::Darwin>(catalogue[*i].definition)) {
    return std::nullopt;
  }
  return std::get<tide::Darwin>(catalogue[*i].definition);
}

/// Every compound term names a Darwin entry: every entry flattens. Checked
/// by lookups alone, without building the Arguments.
[[nodiscard]] constexpr bool compound_terms_resolve(
    CatalogueSpan catalogue) noexcept {
  for (const auto& e : catalogue) {
    if (not std::holds_alternative<tide::CompoundTerms>(e.definition)) {
      continue;
    }
    for (const auto& t : std::get<tide::CompoundTerms>(e.definition).terms()) {
      if (not darwin_in(catalogue, t.base.view())) {
        return false;
      }
    }
  }
  return true;
}

/// The T coefficient of every entry, the species of its flattened Argument,
/// is the species its name states. Only T is summed, which is what flatten
/// sums for it.
[[nodiscard]] constexpr bool species_match_names(
    CatalogueSpan catalogue) noexcept {
  for (const auto& e : catalogue) {
    std::int32_t species = 0;
    if (std::holds_alternative<tide::Darwin>(e.definition)) {
      species = std::get<tide::Darwin>(e.definition).v[0];
    } else {
      for (const auto& t :
           std::get<tide::CompoundTerms>(e.definition).terms()) {
        const auto base = darwin_in(catalogue, t.base.view());
        if (not base) {
          return false;
        }
        species += t.coefficient * base->v[0];
      }
    }
    if (species != species_of_name(e.name.view())) {
      return false;
    }
  }
  return true;
}

/// Notes are printable ASCII without quotes or backslashes.
[[nodiscard]] constexpr bool notes_plain(CatalogueSpan catalogue) noexcept {
  return std::ranges::all_of(catalogue, [](const auto& e) {
    return not e.note or std::ranges::all_of(*e.note, [](char c) {
      return c >= ' ' and c <= '~' and c != '"' and c != '\\';
    });
  });
}

/// Every alias names a catalogue entry.
[[nodiscard]] constexpr bool aliases_resolve(
    CatalogueSpan catalogue,
    std::span<const tide::CatalogueAlias> aliases) noexcept {
  return std::ranges::all_of(aliases, [catalogue](const auto& a) {
    return index_in(catalogue, a.name.view()).has_value();
  });
}

}  // namespace mov::core::detail

namespace mov::core::tide {

/// The index of the entry named exactly `name`.
[[nodiscard]] constexpr std::optional<std::size_t> catalogue_index(
    std::string_view name) noexcept {
  return detail::index_in(nos_catalogue, name);
}

/// The definition of the entry named exactly `name`: the catalogue as a
/// DefinitionLookup.
[[nodiscard]] constexpr DefinitionRef catalogue_definition(
    const ConstituentName& name) noexcept {
  return detail::lookup_in(nos_catalogue)(name);
}

/// The flattened Argument of the entry named exactly `name`; nullopt if
/// there is none. (Every entry flattens: constituent.cpp asserts it.)
[[nodiscard]] constexpr std::optional<Argument> catalogue_argument(
    std::string_view name) noexcept {
  const auto i = catalogue_index(name);
  if (not i) {
    return std::nullopt;
  }
  const auto a =
      flatten(nos_catalogue[*i].definition, detail::lookup_in(nos_catalogue));
  return a ? std::optional<Argument>{*a} : std::nullopt;
}

/// `name` if it is a catalogue name, its catalogue name if it is an alias,
/// else nullopt. Exact spelling. The view is into static storage.
[[nodiscard]] constexpr std::optional<std::string_view> canonical_name(
    std::string_view name) noexcept {
  if (const auto i = catalogue_index(name)) {
    return nos_catalogue[*i].name.view();
  }
  const auto found = std::ranges::find_if(
      catalogue_aliases,
      [name](const auto& a) { return a.alias.view() == name; });
  if (found == catalogue_aliases.end()) {
    return std::nullopt;
  }
  return found->name.view();
}

/// The note on the entry named exactly `name`, if it has one.
[[nodiscard]] constexpr std::optional<std::string_view> catalogue_note(
    std::string_view name) noexcept {
  const auto i = catalogue_index(name);
  return i ? nos_catalogue[*i].note : std::nullopt;
}

}  // namespace mov::core::tide
