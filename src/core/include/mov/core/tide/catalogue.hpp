// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <string_view>
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
// Invariants (static_asserts in src/core/tide/constituent.cpp, and the same
// predicates in tests/core/tide): names are valid and unique ignoring case;
// compound terms are sorted, non-zero and name Darwin entries; the T
// coefficient equals the species the name ends in; notes are plain ASCII
// text; aliases name catalogue entries and collide with none.

namespace mov::core::tide {

/// One term of a catalogue compound: base name and coefficient.
class CatalogueTerm {
 public:
  constexpr CatalogueTerm(std::string_view base,
                          std::int8_t coefficient) noexcept
      : base_{base}, coefficient_{coefficient} {}

  [[nodiscard]] constexpr std::string_view base() const noexcept {
    return base_;
  }
  [[nodiscard]] constexpr std::int8_t coefficient() const noexcept {
    return coefficient_;
  }

  friend constexpr bool operator==(const CatalogueTerm&,
                                   const CatalogueTerm&) = default;

 private:
  std::string_view base_;
  std::int8_t coefficient_;
};

/// The catalogue's compounds have at most four terms (the format allows 8).
inline constexpr std::size_t max_catalogue_terms = 4;

/// A catalogue compound: 1 to max_catalogue_terms terms, held inline.
class CatalogueCompound {
 public:
  template <std::same_as<CatalogueTerm>... Terms>
    requires(sizeof...(Terms) >= 1 and sizeof...(Terms) <= max_catalogue_terms)
  explicit constexpr CatalogueCompound(Terms... t) noexcept
      : storage_{padded(t...)}, size_{sizeof...(Terms)} {}

  [[nodiscard]] constexpr std::span<const CatalogueTerm> terms()
      const& noexcept {
    return {storage_.data(), size_};
  }
  std::span<const CatalogueTerm> terms() const&& = delete;  // would dangle

  friend constexpr bool operator==(const CatalogueCompound& a,
                                   const CatalogueCompound& b) noexcept {
    return std::ranges::equal(a.terms(), b.terms());
  }

 private:
  // Unused slots repeat the first term; only terms() is observable.
  template <std::same_as<CatalogueTerm>... Rest>
  [[nodiscard]] static constexpr std::array<CatalogueTerm, max_catalogue_terms>
  padded(CatalogueTerm first, Rest... rest) noexcept {
    std::array<CatalogueTerm, max_catalogue_terms> out{first, first, first,
                                                       first};
    std::size_t i = 1;
    ((out[i++] = rest), ...);
    return out;
  }

  std::array<CatalogueTerm, max_catalogue_terms> storage_;
  std::size_t size_;
};

using CatalogueDefinition = std::variant<Darwin, CatalogueCompound>;

/// One catalogue constituent: a name and its Darwin or compound definition.
class CatalogueEntry {
 public:
  constexpr CatalogueEntry(std::string_view name, const Darwin& d) noexcept
      : name_{name}, definition_{d} {}

  template <std::same_as<CatalogueTerm>... Terms>
    requires(sizeof...(Terms) >= 1 and sizeof...(Terms) <= max_catalogue_terms)
  constexpr CatalogueEntry(std::string_view name, Terms... t) noexcept
      : name_{name}, definition_{CatalogueCompound{t...}} {}

  [[nodiscard]] constexpr std::string_view name() const noexcept {
    return name_;
  }
  [[nodiscard]] constexpr const CatalogueDefinition& definition()
      const& noexcept {
    return definition_;
  }
  const CatalogueDefinition& definition() const&& = delete;  // would dangle

  friend constexpr bool operator==(const CatalogueEntry&,
                                   const CatalogueEntry&) = default;

 private:
  std::string_view name_;
  CatalogueDefinition definition_;
};

/// The catalogue, in NOAA's order: entries 0-36 are the NOS 37 (Appendix B
/// numbering), the rest the further constituents of 9455920.
inline constexpr auto nos_catalogue = std::to_array<CatalogueEntry>({
    {"M2", Darwin{.v = {2, -2, 2, 0, 0},
                  .c_deg = 0,
                  .u = {2, -2, 0, 0, 0, 0, 0},
                  .f = NodeFactor::f78}},
    {"S2", Darwin{.v = {2, 0, 0, 0, 0},
                  .c_deg = 0,
                  .u = {0, 0, 0, 0, 0, 0, 0},
                  .f = NodeFactor::unity}},
    {"N2", Darwin{.v = {2, -3, 2, 1, 0},
                  .c_deg = 0,
                  .u = {2, -2, 0, 0, 0, 0, 0},
                  .f = NodeFactor::f78}},
    {"K1", Darwin{.v = {1, 0, 1, 0, 0},
                  .c_deg = -90,
                  .u = {0, 0, -1, 0, 0, 0, 0},
                  .f = NodeFactor::f227}},
    {"M4", CatalogueTerm{"M2", 2}},
    {"O1", Darwin{.v = {1, -2, 1, 0, 0},
                  .c_deg = 90,
                  .u = {2, -1, 0, 0, 0, 0, 0},
                  .f = NodeFactor::f75}},
    {"M6", CatalogueTerm{"M2", 3}},
    {"MK3", CatalogueTerm{"K1", 1}, CatalogueTerm{"M2", 1}},
    {"S4", CatalogueTerm{"S2", 2}},
    {"MN4", CatalogueTerm{"M2", 1}, CatalogueTerm{"N2", 1}},
    {"NU2", Darwin{.v = {2, -3, 4, -1, 0},
                   .c_deg = 0,
                   .u = {2, -2, 0, 0, 0, 0, 0},
                   .f = NodeFactor::f78}},
    {"S6", CatalogueTerm{"S2", 3}},
    {"MU2", Darwin{.v = {2, -4, 4, 0, 0},
                   .c_deg = 0,
                   .u = {2, -2, 0, 0, 0, 0, 0},
                   .f = NodeFactor::f78}},
    {"2N2", Darwin{.v = {2, -4, 2, 2, 0},
                   .c_deg = 0,
                   .u = {2, -2, 0, 0, 0, 0, 0},
                   .f = NodeFactor::f78}},
    {"OO1", Darwin{.v = {1, 2, 1, 0, 0},
                   .c_deg = -90,
                   .u = {-2, -1, 0, 0, 0, 0, 0},
                   .f = NodeFactor::f77}},
    {"LAM2", Darwin{.v = {2, -1, 0, 1, 0},
                    .c_deg = 180,
                    .u = {2, -2, 0, 0, 0, 0, 0},
                    .f = NodeFactor::f78}},
    {"S1", Darwin{.v = {1, 0, 0, 0, 0},
                  .c_deg = 0,
                  .u = {0, 0, 0, 0, 0, 0, 0},
                  .f = NodeFactor::unity}},
    {"M1", Darwin{.v = {1, -1, 1, 0, 0},
                  .c_deg = -90,
                  .u = {1, -1, 0, 0, 1, 0, 0},
                  .f = NodeFactor::f206}},
    {"J1", Darwin{.v = {1, 1, 1, -1, 0},
                  .c_deg = -90,
                  .u = {0, -1, 0, 0, 0, 0, 0},
                  .f = NodeFactor::f76}},
    {"MM", Darwin{.v = {0, 1, 0, -1, 0},
                  .c_deg = 0,
                  .u = {0, 0, 0, 0, 0, 0, 0},
                  .f = NodeFactor::f73}},
    {"SSA", Darwin{.v = {0, 0, 2, 0, 0},
                   .c_deg = 0,
                   .u = {0, 0, 0, 0, 0, 0, 0},
                   .f = NodeFactor::unity}},
    {"SA", Darwin{.v = {0, 0, 1, 0, 0},
                  .c_deg = 0,
                  .u = {0, 0, 0, 0, 0, 0, 0},
                  .f = NodeFactor::unity}},
    {"MSF", CatalogueTerm{"M2", -1}, CatalogueTerm{"S2", 1}},
    {"MF", Darwin{.v = {0, 2, 0, 0, 0},
                  .c_deg = 0,
                  .u = {-2, 0, 0, 0, 0, 0, 0},
                  .f = NodeFactor::f74}},
    {"RHO", Darwin{.v = {1, -3, 3, -1, 0},
                   .c_deg = 90,
                   .u = {2, -1, 0, 0, 0, 0, 0},
                   .f = NodeFactor::f75}},
    {"Q1", Darwin{.v = {1, -3, 1, 1, 0},
                  .c_deg = 90,
                  .u = {2, -1, 0, 0, 0, 0, 0},
                  .f = NodeFactor::f75}},
    {"T2", Darwin{.v = {2, 0, -1, 0, 1},
                  .c_deg = 0,
                  .u = {0, 0, 0, 0, 0, 0, 0},
                  .f = NodeFactor::unity}},
    {"R2", Darwin{.v = {2, 0, 1, 0, -1},
                  .c_deg = 180,
                  .u = {0, 0, 0, 0, 0, 0, 0},
                  .f = NodeFactor::unity}},
    {"2Q1", Darwin{.v = {1, -4, 1, 2, 0},
                   .c_deg = 90,
                   .u = {2, -1, 0, 0, 0, 0, 0},
                   .f = NodeFactor::f75}},
    {"P1", Darwin{.v = {1, 0, -1, 0, 0},
                  .c_deg = 90,
                  .u = {0, 0, 0, 0, 0, 0, 0},
                  .f = NodeFactor::unity}},
    {"2SM2", CatalogueTerm{"M2", -1}, CatalogueTerm{"S2", 2}},
    {"M3", Darwin{.v = {3, -3, 3, 0, 0},
                  .c_deg = 0,
                  .u = {3, -3, 0, 0, 0, 0, 0},
                  .f = NodeFactor::f149}},
    {"L2", Darwin{.v = {2, -1, 2, -1, 0},
                  .c_deg = 180,
                  .u = {2, -2, 0, 0, 0, -1, 0},
                  .f = NodeFactor::f215}},
    {"2MK3", CatalogueTerm{"K1", -1}, CatalogueTerm{"M2", 2}},
    {"K2", Darwin{.v = {2, 0, 2, 0, 0},
                  .c_deg = 0,
                  .u = {0, 0, 0, -1, 0, 0, 0},
                  .f = NodeFactor::f235}},
    {"M8", CatalogueTerm{"M2", 4}},
    {"MS4", CatalogueTerm{"M2", 1}, CatalogueTerm{"S2", 1}},
    {"SIGMA1", Darwin{.v = {1, -4, 3, 0, 0},
                      .c_deg = 90,
                      .u = {2, -1, 0, 0, 0, 0, 0},
                      .f = NodeFactor::f75}},
    {"MP1", Darwin{.v = {1, -2, 3, 0, 0},
                   .c_deg = -90,
                   .u = {0, -1, 0, 0, 0, 0, 0},
                   .f = NodeFactor::f76}},
    {"CHI1", Darwin{.v = {1, -1, 3, -1, 0},
                    .c_deg = -90,
                    .u = {0, -1, 0, 0, 0, 0, 0},
                    .f = NodeFactor::f76}},
    {"2PO1", CatalogueTerm{"O1", -1}, CatalogueTerm{"P1", 2}},
    {"SO1", Darwin{.v = {1, 2, -1, 0, 0},
                   .c_deg = -90,
                   .u = {0, -1, 0, 0, 0, 0, 0},
                   .f = NodeFactor::f76}},
    {"MSN2", CatalogueTerm{"M2", 1}, CatalogueTerm{"N2", -1},
     CatalogueTerm{"S2", 1}},
    {"MNS2", CatalogueTerm{"M2", 1}, CatalogueTerm{"N2", 1},
     CatalogueTerm{"S2", -1}},
    {"OP2", CatalogueTerm{"O1", 1}, CatalogueTerm{"P1", 1}},
    {"MKS2", CatalogueTerm{"K2", 1}, CatalogueTerm{"M2", 1},
     CatalogueTerm{"S2", -1}},
    {"2NS2", CatalogueTerm{"N2", 2}, CatalogueTerm{"S2", -1}},
    {"MLN2S2", CatalogueTerm{"L2", 1}, CatalogueTerm{"M2", 1},
     CatalogueTerm{"N2", 1}, CatalogueTerm{"S2", -2}},
    {"2ML2S2", CatalogueTerm{"L2", 1}, CatalogueTerm{"M2", 2},
     CatalogueTerm{"S2", -2}},
    {"SKM2", CatalogueTerm{"K2", 1}, CatalogueTerm{"M2", -1},
     CatalogueTerm{"S2", 1}},
    {"2MS2K2", CatalogueTerm{"K2", -2}, CatalogueTerm{"M2", 2},
     CatalogueTerm{"S2", 1}},
    {"MKL2S2", CatalogueTerm{"K2", 1}, CatalogueTerm{"L2", 1},
     CatalogueTerm{"M2", 1}, CatalogueTerm{"S2", -2}},
    {"M2KS2", CatalogueTerm{"K2", 2}, CatalogueTerm{"M2", 1},
     CatalogueTerm{"S2", -2}},
    {"2SNMK2", CatalogueTerm{"K2", -1}, CatalogueTerm{"M2", -1},
     CatalogueTerm{"N2", 1}, CatalogueTerm{"S2", 2}},
    {"2KMSN2", CatalogueTerm{"K2", 2}, CatalogueTerm{"M2", 1},
     CatalogueTerm{"N2", -1}, CatalogueTerm{"S2", -1}},
    {"SO3", CatalogueTerm{"O1", 1}, CatalogueTerm{"S2", 1}},
    {"SK3", CatalogueTerm{"K1", 1}, CatalogueTerm{"S2", 1}},
    {"NO3", CatalogueTerm{"N2", 1}, CatalogueTerm{"O1", 1}},
    {"MK4", CatalogueTerm{"K2", 1}, CatalogueTerm{"M2", 1}},
    {"SN4", CatalogueTerm{"N2", 1}, CatalogueTerm{"S2", 1}},
    {"2MLS4", CatalogueTerm{"L2", 1}, CatalogueTerm{"M2", 2},
     CatalogueTerm{"S2", -1}},
    {"3MS4", CatalogueTerm{"M2", 3}, CatalogueTerm{"S2", -1}},
    {"ML4", CatalogueTerm{"L2", 1}, CatalogueTerm{"M2", 1}},
    {"N4", CatalogueTerm{"N2", 2}},
    {"SL4", CatalogueTerm{"L2", 1}, CatalogueTerm{"S2", 1}},
    {"MNO5", CatalogueTerm{"M2", 1}, CatalogueTerm{"N2", 1},
     CatalogueTerm{"O1", 1}},
    {"2MO5", CatalogueTerm{"M2", 2}, CatalogueTerm{"O1", 1}},
    {"2MK5", CatalogueTerm{"K1", 1}, CatalogueTerm{"M2", 2}},
    {"MSK5", CatalogueTerm{"K1", 1}, CatalogueTerm{"M2", 1},
     CatalogueTerm{"S2", 1}},
    {"3KM5", CatalogueTerm{"K1", 1}, CatalogueTerm{"K2", 1},
     CatalogueTerm{"M2", 1}},
    {"2MP5", CatalogueTerm{"M2", 2}, CatalogueTerm{"P1", 1}},
    {"3MP5", CatalogueTerm{"M2", 3}, CatalogueTerm{"P1", -1}},
    {"MNK5", CatalogueTerm{"K1", 1}, CatalogueTerm{"M2", 1},
     CatalogueTerm{"N2", 1}},
    {"2SM6", CatalogueTerm{"M2", 1}, CatalogueTerm{"S2", 2}},
    {"2MN6", CatalogueTerm{"M2", 2}, CatalogueTerm{"N2", 1}},
    {"MSN6", CatalogueTerm{"M2", 1}, CatalogueTerm{"N2", 1},
     CatalogueTerm{"S2", 1}},
    {"2MS6", CatalogueTerm{"M2", 2}, CatalogueTerm{"S2", 1}},
    {"2NMLS6", CatalogueTerm{"L2", 1}, CatalogueTerm{"M2", 1},
     CatalogueTerm{"N2", 2}, CatalogueTerm{"S2", -1}},
    {"2NM6", CatalogueTerm{"M2", 1}, CatalogueTerm{"N2", 2}},
    {"MSL6", CatalogueTerm{"L2", 1}, CatalogueTerm{"M2", 1},
     CatalogueTerm{"S2", 1}},
    {"2ML6", CatalogueTerm{"L2", 1}, CatalogueTerm{"M2", 2}},
    {"MSK6", CatalogueTerm{"K2", 1}, CatalogueTerm{"M2", 1},
     CatalogueTerm{"S2", 1}},
    {"2MLNS6", CatalogueTerm{"L2", 1}, CatalogueTerm{"M2", 2},
     CatalogueTerm{"N2", 1}, CatalogueTerm{"S2", -1}},
    {"3MLS6", CatalogueTerm{"L2", 1}, CatalogueTerm{"M2", 3},
     CatalogueTerm{"S2", -1}},
    {"2MK6", CatalogueTerm{"K2", 1}, CatalogueTerm{"M2", 2}},
    {"2MNO7", CatalogueTerm{"M2", 2}, CatalogueTerm{"N2", 1},
     CatalogueTerm{"O1", 1}},
    {"2NMK7", CatalogueTerm{"K1", 1}, CatalogueTerm{"M2", 1},
     CatalogueTerm{"N2", 2}},
    {"2MSO7", CatalogueTerm{"M2", 2}, CatalogueTerm{"O1", 1},
     CatalogueTerm{"S2", 1}},
    {"MSKO7", CatalogueTerm{"K2", 1}, CatalogueTerm{"M2", 1},
     CatalogueTerm{"O1", 1}, CatalogueTerm{"S2", 1}},
    {"2MSN8", CatalogueTerm{"M2", 2}, CatalogueTerm{"N2", 1},
     CatalogueTerm{"S2", 1}},
    {"3MS8", CatalogueTerm{"M2", 3}, CatalogueTerm{"S2", 1}},
    {"2MS8", CatalogueTerm{"M2", 2}, CatalogueTerm{"S2", 2}},
    {"2MN8", CatalogueTerm{"M2", 2}, CatalogueTerm{"N2", 2}},
    {"3MN8", CatalogueTerm{"M2", 3}, CatalogueTerm{"N2", 1}},
    {"2MSL8", CatalogueTerm{"L2", 1}, CatalogueTerm{"M2", 2},
     CatalogueTerm{"S2", 1}},
    {"4MLS8", CatalogueTerm{"L2", 1}, CatalogueTerm{"M2", 4},
     CatalogueTerm{"S2", -1}},
    {"3ML8", CatalogueTerm{"L2", 1}, CatalogueTerm{"M2", 3}},
    {"3MK8", CatalogueTerm{"K2", 1}, CatalogueTerm{"M2", 3}},
    {"2MSK8", CatalogueTerm{"K2", 1}, CatalogueTerm{"M2", 2},
     CatalogueTerm{"S2", 1}},
    {"2M2NK9", CatalogueTerm{"K1", 1}, CatalogueTerm{"M2", 2},
     CatalogueTerm{"N2", 2}},
    {"3MNK9", CatalogueTerm{"K1", 1}, CatalogueTerm{"M2", 3},
     CatalogueTerm{"N2", 1}},
    {"4MK9", CatalogueTerm{"K1", 1}, CatalogueTerm{"M2", 4}},
    {"3MSK9", CatalogueTerm{"K1", 1}, CatalogueTerm{"M2", 3},
     CatalogueTerm{"S2", 1}},
    {"4MN10", CatalogueTerm{"M2", 4}, CatalogueTerm{"N2", 1}},
    {"M10", CatalogueTerm{"M2", 5}},
    {"3MNS10", CatalogueTerm{"M2", 3}, CatalogueTerm{"N2", 1},
     CatalogueTerm{"S2", 1}},
    {"4MS10", CatalogueTerm{"M2", 4}, CatalogueTerm{"S2", 1}},
    {"3MSL10", CatalogueTerm{"L2", 1}, CatalogueTerm{"M2", 3},
     CatalogueTerm{"S2", 1}},
    {"3M2S10", CatalogueTerm{"M2", 3}, CatalogueTerm{"S2", 2}},
    {"4MSK11", CatalogueTerm{"K1", 1}, CatalogueTerm{"M2", 4},
     CatalogueTerm{"S2", 1}},
    {"4MNS12", CatalogueTerm{"M2", 4}, CatalogueTerm{"N2", 1},
     CatalogueTerm{"S2", 1}},
    {"5MS12", CatalogueTerm{"M2", 5}, CatalogueTerm{"S2", 1}},
    {"4MSL12", CatalogueTerm{"L2", 1}, CatalogueTerm{"M2", 4},
     CatalogueTerm{"S2", 1}},
    {"4M2S12", CatalogueTerm{"M2", 4}, CatalogueTerm{"S2", 2}},
    {"TK1", Darwin{.v = {1, 0, -2, 0, 1},
                   .c_deg = 90,
                   .u = {0, 0, 1, 0, 0, 0, 0},
                   .f = NodeFactor::f227}},
    {"RP1", Darwin{.v = {1, 0, 2, 0, -1},
                   .c_deg = -90,
                   .u = {0, 0, 0, 0, 0, 0, 0},
                   .f = NodeFactor::unity}},
    {"KP1", CatalogueTerm{"K2", 1}, CatalogueTerm{"P1", -1}},
    {"THETA1", Darwin{.v = {1, 1, -1, 1, 0},
                      .c_deg = -90,
                      .u = {0, -1, 0, 0, 0, 0, 0},
                      .f = NodeFactor::f76}},
    {"KJ2", Darwin{.v = {2, 1, 2, -1, 0},
                   .c_deg = 180,
                   .u = {0, -2, 0, 0, 0, 0, 0},
                   .f = NodeFactor::f79}},
    {"OO2", CatalogueTerm{"O1", 1}, CatalogueTerm{"Q1", 1}},
});

/// How many leading entries are the NOS 37.
inline constexpr std::size_t nos_standard_count = 37;

/// Why an entry is defined as it is, where that is not plain SP 98 Table 2.
/// Printable ASCII without quotes or backslashes: each becomes the entry's
/// JSON `description` in docs/schemas/constituents-nos.json.
struct CatalogueNote {
  std::string_view name;
  std::string_view note;
  friend constexpr bool operator==(const CatalogueNote&,
                                   const CatalogueNote&) = default;
};

inline constexpr std::array catalogue_notes{
    CatalogueNote{.name = "MSF",
                  .note = "S2 - M2 (SP 98 p. 48), so u = -u(M2)"},
    CatalogueNote{
        .name = "M1",
        .note = "NOS form (SP 98 Table 2, second formula): u includes Q, so "
                "the speed includes the p rate (SP 98 paragraph 124)"},
    CatalogueNote{.name = "3KM5",
                  .note = "K1 + M2 + K2 (Zetler and Cummings 1967; congen "
                          "since 2018), not 3 K1 + M2"},
    CatalogueNote{.name = "RP1",
                  .note = "congen PSI1: NOAA publishes RP1 in this phase "
                          "(vetted at 9455920); congen RP1 is 180 degrees "
                          "from it"},
    CatalogueNote{.name = "KJ2",
                  .note = "congen KJ2-IHO, the IHO phase: NOAA publishes KJ2 "
                          "in it (vetted at 9455920); SP 98 KJ2 is 180 "
                          "degrees from it"},
    CatalogueNote{.name = "OO2", .note = "O1 + Q1, congen OQ2-HORN"},
};

/// A spelling another source uses for a catalogue constituent with the same
/// definition (checked against congen_input.txt; docs/harmonics-engine.md
/// section 2.3). Exact spellings: the builder folds case per source.
struct CatalogueAlias {
  std::string_view alias;
  std::string_view name;
  friend constexpr bool operator==(const CatalogueAlias&,
                                   const CatalogueAlias&) = default;
};

inline constexpr std::array catalogue_aliases{
    // congen and XTide spellings.
    CatalogueAlias{.alias = "LDA2", .name = "LAM2"},
    CatalogueAlias{.alias = "RHO1", .name = "RHO"},
    CatalogueAlias{.alias = "SIG1", .name = "SIGMA1"},
    CatalogueAlias{.alias = "THE1", .name = "THETA1"},
    CatalogueAlias{.alias = "OQ2-HORN", .name = "OO2"},
    CatalogueAlias{.alias = "2(MS)8", .name = "2MS8"},
    CatalogueAlias{.alias = "2(MN)8", .name = "2MN8"},
    CatalogueAlias{.alias = "2MNLS6", .name = "2MLNS6"},
    CatalogueAlias{.alias = "M2(KS)2", .name = "M2KS2"},
    CatalogueAlias{.alias = "2SN(MK)2", .name = "2SNMK2"},
    CatalogueAlias{.alias = "2KM(SN)2", .name = "2KMSN2"},
    CatalogueAlias{.alias = "KJ2-IHO", .name = "KJ2"},
    CatalogueAlias{.alias = "PSI1", .name = "RP1"},
    // TICON-4 spellings (docs/harmonics-json.md 7.3).
    CatalogueAlias{.alias = "LAMBDA2", .name = "LAM2"},
    CatalogueAlias{.alias = "SGM", .name = "SIGMA1"},
};

}  // namespace mov::core::tide

// The lookups and the invariants, over any catalogue, so that the tests can
// show each invariant rejecting a catalogue that breaks it.
namespace mov::core::detail {

using CatalogueSpan = std::span<const tide::CatalogueEntry>;

[[nodiscard]] constexpr std::optional<std::size_t> index_in(
    CatalogueSpan catalogue, std::string_view name) noexcept {
  const auto found =
      std::ranges::find(catalogue, name, &tide::CatalogueEntry::name);
  if (found == catalogue.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(found - catalogue.begin());
}

/// The Darwin definition of the entry named `name`, if it is one.
[[nodiscard]] constexpr std::optional<tide::Darwin> darwin_in(
    CatalogueSpan catalogue, std::string_view name) noexcept {
  const auto i = index_in(catalogue, name);
  if (not i) {
    return std::nullopt;
  }
  // holds_alternative and get, not get_if: GCC 14 with -fsanitize=address
  // cannot constant-evaluate get_if's null check of a static object's
  // address.
  const auto& definition = catalogue[*i].definition();
  if (not std::holds_alternative<tide::Darwin>(definition)) {
    return std::nullopt;
  }
  return std::get<tide::Darwin>(definition);
}

/// The flattened Argument of `e` (a compound is the fold of its scaled
/// Darwin terms); nullopt if a compound term is not a Darwin entry of
/// `catalogue`.
[[nodiscard]] constexpr std::optional<tide::Argument> flatten(
    CatalogueSpan catalogue, const tide::CatalogueEntry& e) noexcept {
  const auto& definition = e.definition();
  if (std::holds_alternative<tide::Darwin>(definition)) {
    return tide::Argument::darwin(std::get<tide::Darwin>(definition));
  }
  tide::Argument sum;
  for (const auto& t : std::get<tide::CatalogueCompound>(definition).terms()) {
    const auto base = darwin_in(catalogue, t.base());
    if (not base) {
      return std::nullopt;
    }
    sum = sum + scale(t.coefficient(), tide::Argument::darwin(*base));
  }
  return sum;
}

/// The flattened Argument of the entry named `name`, if it flattens.
[[nodiscard]] constexpr std::optional<tide::Argument> argument_in(
    CatalogueSpan catalogue, std::string_view name) noexcept {
  const auto i = index_in(catalogue, name);
  if (not i) {
    return std::nullopt;
  }
  return flatten(catalogue, catalogue[*i]);
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

/// Every name passes ConstituentName::make.
[[nodiscard]] constexpr bool names_valid(CatalogueSpan catalogue) noexcept {
  return std::ranges::all_of(catalogue, [](const auto& e) {
    return tide::ConstituentName::make(e.name()).has_value();
  });
}

[[nodiscard]] constexpr bool less_ignoring_case(std::string_view a,
                                                std::string_view b) noexcept {
  return std::ranges::lexicographical_compare(a, b, [](char x, char y) {
    return ascii::to_lower(x) < ascii::to_lower(y);
  });
}

/// No two names, catalogue names and aliases together, are equal ignoring
/// case. Sorted, then neighbours compared: n log n comparisons, which keeps
/// the static_assert within MSVC's default constexpr step budget.
[[nodiscard]] constexpr bool names_unique(
    CatalogueSpan catalogue,
    std::span<const tide::CatalogueAlias> aliases) noexcept {
  std::vector<std::string_view> names;
  names.reserve(catalogue.size() + aliases.size());
  std::ranges::transform(catalogue, std::back_inserter(names),
                         &tide::CatalogueEntry::name);
  std::ranges::transform(aliases, std::back_inserter(names),
                         &tide::CatalogueAlias::alias);
  std::ranges::sort(names, less_ignoring_case);
  return std::ranges::adjacent_find(names, ascii::equal_ignore_case) ==
         names.end();
}

/// Compound terms are sorted strictly by base, have non-zero coefficients
/// and name Darwin entries.
[[nodiscard]] constexpr bool compounds_resolve(
    CatalogueSpan catalogue) noexcept {
  const auto well_formed = [catalogue](const tide::CatalogueCompound& c) {
    const auto terms = c.terms();
    const bool sorted =
        std::ranges::adjacent_find(terms, [](const auto& x, const auto& y) {
          return not(x.base() < y.base());
        }) == terms.end();
    return sorted and std::ranges::all_of(terms, [catalogue](const auto& t) {
             return t.coefficient() != 0 and
                    darwin_in(catalogue, t.base()).has_value();
           });
  };
  return std::ranges::all_of(catalogue, [&](const auto& e) {
    const auto& definition = e.definition();
    return not std::holds_alternative<tide::CatalogueCompound>(definition) or
           well_formed(std::get<tide::CatalogueCompound>(definition));
  });
}

/// The T coefficient of every flattened entry is the species its name
/// states.
[[nodiscard]] constexpr bool species_match_names(
    CatalogueSpan catalogue) noexcept {
  return std::ranges::all_of(catalogue, [catalogue](const auto& e) {
    const auto a = flatten(catalogue, e);
    return a and a->species() == species_of_name(e.name());
  });
}

/// Notes name catalogue entries and are printable ASCII without quotes or
/// backslashes.
[[nodiscard]] constexpr bool notes_plain(
    CatalogueSpan catalogue,
    std::span<const tide::CatalogueNote> notes) noexcept {
  return std::ranges::all_of(notes, [catalogue](const auto& n) {
    return index_in(catalogue, n.name).has_value() and
           std::ranges::all_of(n.note, [](char c) {
             return c >= ' ' and c <= '~' and c != '"' and c != '\\';
           });
  });
}

/// Every alias names a catalogue entry.
[[nodiscard]] constexpr bool aliases_resolve(
    CatalogueSpan catalogue,
    std::span<const tide::CatalogueAlias> aliases) noexcept {
  return std::ranges::all_of(aliases, [catalogue](const auto& a) {
    return index_in(catalogue, a.name).has_value();
  });
}

}  // namespace mov::core::detail

namespace mov::core::tide {

/// The index of the entry named exactly `name`.
[[nodiscard]] constexpr std::optional<std::size_t> catalogue_index(
    std::string_view name) noexcept {
  return detail::index_in(nos_catalogue, name);
}

/// The flattened Argument of the entry named exactly `name` (a compound is
/// the fold of its scaled Darwin terms); nullopt if there is none.
[[nodiscard]] constexpr std::optional<Argument> catalogue_argument(
    std::string_view name) noexcept {
  return detail::argument_in(nos_catalogue, name);
}

/// `name` if it is a catalogue name, its catalogue name if it is an alias,
/// else nullopt. Exact spelling.
[[nodiscard]] constexpr std::optional<std::string_view> canonical_name(
    std::string_view name) noexcept {
  if (const auto i = catalogue_index(name)) {
    return nos_catalogue[*i].name();
  }
  const auto found =
      std::ranges::find(catalogue_aliases, name, &CatalogueAlias::alias);
  if (found == catalogue_aliases.end()) {
    return std::nullopt;
  }
  return found->name;
}

/// The note on the entry named exactly `name`, if it has one.
[[nodiscard]] constexpr std::optional<std::string_view> catalogue_note(
    std::string_view name) noexcept {
  const auto found =
      std::ranges::find(catalogue_notes, name, &CatalogueNote::name);
  if (found == catalogue_notes.end()) {
    return std::nullopt;
  }
  return found->note;
}

}  // namespace mov::core::tide
