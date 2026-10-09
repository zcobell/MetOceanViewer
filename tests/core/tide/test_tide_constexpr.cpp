// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/tide: Phase, the node-factor tokens,
// ConstituentName, CompoundTerms, the Argument monoid and flatten, speeds,
// Doodson numbers, the catalogue invariants on small catalogues, and the
// SP 98 elements. The invariants over the whole catalogue, and the NOAA
// speeds, are run-time tests (test_tide_catalogue.cpp): as STATIC_REQUIREs
// they would repeat constituent.cpp's static_asserts at a large constexpr
// cost.

#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <span>
#include <string_view>
#include <type_traits>
#include <variant>

#include "../test_helpers.hpp"
#include "mov/core/tide/astronomy.hpp"
#include "mov/core/tide/catalogue.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/node_factor.hpp"
#include "mov/core/tide/phase.hpp"
#include "mov/core/tide/year_arguments.hpp"

using mov::core::Time;
using mov::core::tide::all_node_factors;
using mov::core::tide::Argument;
using mov::core::tide::canonical_name;
using mov::core::tide::catalogue_argument;
using mov::core::tide::catalogue_index;
using mov::core::tide::catalogue_note;
using mov::core::tide::CatalogueAlias;
using mov::core::tide::CatalogueEntry;
using mov::core::tide::CompoundTerms;
using mov::core::tide::CompoundTermsError;
using mov::core::tide::constituent_name;
using mov::core::tide::ConstituentName;
using mov::core::tide::ConstituentNameError;
using mov::core::tide::Darwin;
using mov::core::tide::Definition;
using mov::core::tide::ElementRates;
using mov::core::tide::Elements;
using mov::core::tide::HighLow;
using mov::core::tide::NodeFactor;
using mov::core::tide::nos_catalogue;
using mov::core::tide::nos_standard_count;
using mov::core::tide::Phase;
using mov::core::tide::TermError;
using mov::core::tide::TermNotDarwin;
using mov::core::tide::Tide;
using mov::core::tide::UnknownTerm;
using mov::core::tide::UTerm;
using mov::test::near_abs;

namespace detail = mov::core::detail;
namespace tide = mov::core::tide;
namespace chr = std::chrono;

namespace {

using Term = CompoundTerms::Term;

constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

constexpr bool sign_bit(double v) {
  return (std::bit_cast<std::uint64_t>(v) >> 63U) != 0U;
}

/// The flattened argument of a catalogue name the test knows exists; an
/// unknown name is not a constant expression.
constexpr Argument arg(std::string_view name) {
  const auto a = catalogue_argument(name);
  if (not a) {
    std::abort();  // not a constant expression: an unknown name fails
  }
  return *a;
}

constexpr Term term(std::string_view base, std::int8_t coefficient) {
  return Term{.base = ConstituentName::make(base).value(),
              .coefficient = coefficient};
}

/// Whether `a`'s Doodson number is the six digits `want`.
constexpr bool doodson_is(const Argument& a, std::string_view want) {
  const auto d = tide::doodson_number(a);
  return d and std::string_view{d->data(), d->size()} == want;
}

// Four catalogue arguments for the monoid laws: with and without a node
// factor, the M1 Q term, a compound with a negative coefficient. Few, so the
// cubic associativity check stays small against MSVC's default budget of
// 100,000 constexpr steps.
constexpr std::array law_args{arg("M2"), arg("S2"), arg("M1"), arg("2MK3")};

constexpr bool identity_holds() {
  return std::ranges::all_of(law_args, [](const Argument& a) {
    return a + Argument{} == a and Argument{} + a == a;
  });
}

/// The laws with `a` in first place, over every b and c of law_args. One
/// call per a, so each STATIC_REQUIRE is a separate, smaller evaluation.
constexpr bool associative_and_commutative(const Argument& a) {
  for (const Argument& b : law_args) {
    if (a + b != b + a) {
      return false;
    }
    for (const Argument& c : law_args) {
      if ((a + b) + c != a + (b + c)) {
        return false;
      }
    }
  }
  return true;
}

constexpr bool scale_laws_hold() {
  return std::ranges::all_of(law_args, [](const Argument& a) {
    return scale(1, a) == a and scale(2, a) == a + a and
           scale(3, a) == a + a + a and scale(0, a) == Argument{};
  });
}

// scale distributes over + and composes multiplicatively, also with
// negative factors (the exponents take |k| in both).
constexpr bool scale_is_a_homomorphism(const Argument& a) {
  constexpr std::array<std::int8_t, 4> factors{-3, -1, 2, 5};
  for (const std::int8_t j : factors) {
    if (scale(j, a + law_args[0]) != scale(j, a) + scale(j, law_args[0])) {
      return false;
    }
    for (const std::int8_t k : factors) {
      if (scale(j, scale(k, a)) != scale(static_cast<std::int8_t>(j * k), a)) {
        return false;
      }
    }
  }
  return true;
}

// scale(-1, a) + a cancels V, c and u but doubles every exponent: f is
// multiplied, never divided, so scaling is not a module action.
constexpr bool negation_doubles_exponents() {
  return std::ranges::all_of(law_args, [](const Argument& a) {
    const Argument sum = scale(-1, a) + a;
    const bool cancelled = sum.v() == std::array<std::int32_t, 5>{} and
                           sum.c_deg() == 0 and
                           sum.u() == std::array<std::int32_t, 7>{};
    const bool doubled = std::ranges::all_of(
        all_node_factors,
        [&](NodeFactor f) { return sum.exponent(f) == 2 * a.exponent(f); });
    return cancelled and doubled;
  });
}

// Small catalogues, each breaking one invariant, for the predicates to
// reject.
constexpr Darwin m2_darwin{.v = {2, -2, 2, 0, 0},
                           .c_deg = 0,
                           .u = {2, -2, 0, 0, 0, 0, 0},
                           .f = NodeFactor::f78};
constexpr Darwin s2_darwin{
    .v = {2, 0, 0, 0, 0}, .c_deg = 0, .u = {}, .f = NodeFactor::unity};
constexpr std::array good_catalogue{
    detail::darwin_entry("M2", m2_darwin),
    detail::darwin_entry("S2", s2_darwin),
    detail::compound_entry("MS4", {{"M2", 1}, {"S2", 1}}, "plain note")};
constexpr std::array duplicate_ignoring_case{
    detail::darwin_entry("M2", m2_darwin),
    detail::darwin_entry("m2", m2_darwin)};
constexpr std::array nested_compound{detail::darwin_entry("M2", m2_darwin),
                                     detail::compound_entry("M4", {{"M2", 2}}),
                                     detail::compound_entry("M8", {{"M4", 2}})};
constexpr std::array dangling_term{detail::compound_entry("M4", {{"M2", 2}})};
constexpr std::array wrong_species{detail::darwin_entry("M3", m2_darwin)};
constexpr std::array note_with_quote{
    detail::darwin_entry("M2", m2_darwin, "say \"hi\"")};
constexpr std::array<CatalogueAlias, 0> no_aliases{};
constexpr std::array alias_shadows_name{detail::catalogue_alias("ms4", "MS4")};
constexpr std::array alias_to_nothing{detail::catalogue_alias("MKS", "MK4")};

}  // namespace

TEST_CASE("tide vocabulary types are value types", "[core][tide][constexpr]") {
  STATIC_REQUIRE(std::copyable<Phase>);
  STATIC_REQUIRE(std::equality_comparable<Phase>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<Phase>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<Phase>);
  STATIC_REQUIRE_FALSE(std::is_constructible_v<Phase, double>);
  STATIC_REQUIRE(std::regular<HighLow<int>>);
  STATIC_REQUIRE(std::regular<Argument>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<Argument>);
  STATIC_REQUIRE(std::regular<Darwin>);
  STATIC_REQUIRE(std::copyable<Elements>);
  STATIC_REQUIRE(std::equality_comparable<Elements>);
  STATIC_REQUIRE(std::regular<ElementRates>);
  STATIC_REQUIRE_FALSE(std::is_convertible_v<ElementRates, Elements>);
  STATIC_REQUIRE(std::copyable<ConstituentName>);
  STATIC_REQUIRE(std::totally_ordered<ConstituentName>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<ConstituentName>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<ConstituentName>);
  STATIC_REQUIRE_FALSE(
      std::is_constructible_v<ConstituentName, std::string_view>);
  // No empty compound exists, so no default one either: copyable and
  // equality comparable, the core convention for such types (CD 7).
  STATIC_REQUIRE(std::copyable<CompoundTerms>);
  STATIC_REQUIRE(std::equality_comparable<CompoundTerms>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<CompoundTerms>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<CompoundTerms>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<Definition>);
  STATIC_REQUIRE(std::copyable<CatalogueEntry>);
  STATIC_REQUIRE(std::equality_comparable<CatalogueEntry>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<CatalogueEntry>);
}

TEST_CASE("Phase::wrap reduces into [0, 360)", "[core][tide][constexpr]") {
  STATIC_REQUIRE(Phase::wrap(0.0).degrees() == 0.0);
  STATIC_REQUIRE(Phase::wrap(-0.0).degrees() == 0.0);
  STATIC_REQUIRE_FALSE(sign_bit(Phase::wrap(-0.0).degrees()));
  // -1e-14 + 360 rounds to 360: the result is 0, never 360.
  STATIC_REQUIRE(Phase::wrap(-1e-14).degrees() == 0.0);
  STATIC_REQUIRE(Phase::wrap(360.0).degrees() == 0.0);
  STATIC_REQUIRE(Phase::wrap(-360.0).degrees() == 0.0);
  STATIC_REQUIRE(Phase::wrap(720.5).degrees() == 0.5);
  STATIC_REQUIRE(Phase::wrap(-359.9).degrees() == -359.9 + 360.0);
  STATIC_REQUIRE(Phase::wrap(359.75).degrees() == 359.75);
  STATIC_REQUIRE(Phase::wrap(1'000'000.25).degrees() == 280.25);
  STATIC_REQUIRE(Phase::wrap(-1'000'000.25).degrees() == 79.75);
  STATIC_REQUIRE(Phase::wrap(1e300).degrees() >= 0.0);
  STATIC_REQUIRE(Phase::wrap(1e300).degrees() < 360.0);
  STATIC_REQUIRE(Phase::wrap(370.0) == Phase::wrap(10.0));
  STATIC_REQUIRE(
      near_abs(Phase::wrap(180.0).radians(), 3.141592653589793, 1e-15));
}

TEST_CASE("Phase::of refuses NaN and the infinities",
          "[core][tide][constexpr]") {
  STATIC_REQUIRE_FALSE(Phase::of(quiet_nan).has_value());
  STATIC_REQUIRE_FALSE(Phase::of(infinity).has_value());
  STATIC_REQUIRE_FALSE(Phase::of(-infinity).has_value());
  STATIC_REQUIRE(Phase::of(-90.0) == Phase::wrap(270.0));
}

TEST_CASE("exact_fmod is fmod", "[core][tide][constexpr]") {
  STATIC_REQUIRE(detail::exact_fmod(0.0, 360.0) == 0.0);
  STATIC_REQUIRE(detail::exact_fmod(359.0, 360.0) == 359.0);
  STATIC_REQUIRE(detail::exact_fmod(360.0, 360.0) == 0.0);
  STATIC_REQUIRE(detail::exact_fmod(-725.0, 360.0) == -5.0);
  STATIC_REQUIRE(detail::exact_fmod(0.1, 360.0) == 0.1);
  STATIC_REQUIRE(sign_bit(detail::exact_fmod(-0.0, 360.0)));
  STATIC_REQUIRE(sign_bit(detail::exact_fmod(-720.0, 360.0)));
  // 2^60 = 360 * q + 136 for an integer q: exact even far beyond 2^53.
  STATIC_REQUIRE(detail::exact_fmod(1152921504606846976.0, 360.0) == 136.0);
}

TEST_CASE("HighLow selects by tide kind", "[core][tide][constexpr]") {
  constexpr HighLow<int> v{.high = 245, .low = 249};
  STATIC_REQUIRE(at(v, Tide::high) == 245);
  STATIC_REQUIRE(at(v, Tide::low) == 249);
  STATIC_REQUIRE(v == HighLow<int>{.high = 245, .low = 249});
}

TEST_CASE("node-factor tokens round trip", "[core][tide][constexpr]") {
  STATIC_REQUIRE(std::ranges::all_of(all_node_factors, [](NodeFactor f) {
    return tide::parse_node_factor(tide::to_token(f)) == f;
  }));
  STATIC_REQUIRE(tide::to_token(NodeFactor::unity) == "unity");
  STATIC_REQUIRE(tide::to_token(NodeFactor::f78) == "sp98-78");
  STATIC_REQUIRE(tide::to_token(NodeFactor::f235) == "sp98-235");
  STATIC_REQUIRE_FALSE(tide::parse_node_factor("SP98-78").has_value());
  STATIC_REQUIRE_FALSE(tide::parse_node_factor("sp98-80").has_value());
  STATIC_REQUIRE_FALSE(tide::parse_node_factor("").has_value());
  STATIC_REQUIRE_FALSE(tide::parse_node_factor("1").has_value());
}

TEST_CASE("ConstituentName follows the format's grammar",
          "[core][tide][constexpr]") {
  STATIC_REQUIRE(ConstituentName::make("M2")->view() == "M2");
  constexpr ConstituentName parenthesised = constituent_name("2(MS)8");
  STATIC_REQUIRE(parenthesised.view() == "2(MS)8");
  STATIC_REQUIRE(ConstituentName::make("OQ2-HORN").has_value());
  STATIC_REQUIRE(ConstituentName::make("a.b_c+d").has_value());
  STATIC_REQUIRE(ConstituentName::make("").error() ==
                 ConstituentNameError::empty);
  STATIC_REQUIRE(ConstituentName::make("-M2").error() ==
                 ConstituentNameError::bad_char);
  STATIC_REQUIRE(ConstituentName::make("M\xc3\xa9").error() ==
                 ConstituentNameError::bad_char);
  // Every byte at positions 0 and 1, and the lengths 32 and 33, are
  // test_tide_catalogue.cpp's exhaustive run-time test.
}

TEST_CASE("ConstituentName orders by bytes", "[core][tide][constexpr]") {
  STATIC_REQUIRE(constituent_name("2N2") < constituent_name("M2"));
  STATIC_REQUIRE(constituent_name("M2") < constituent_name("MS4"));
  STATIC_REQUIRE(constituent_name("M2") < constituent_name("m2"));
  STATIC_REQUIRE(constituent_name("M2") == *ConstituentName::make("M2"));
  STATIC_REQUIRE(constituent_name("M2") != constituent_name("m2"));
}

TEST_CASE("CompoundTerms::make sorts and refuses bad terms",
          "[core][tide][constexpr]") {
  constexpr std::array s2_m2{term("S2", 1), term("M2", -1)};
  constexpr auto made = CompoundTerms::make(s2_m2);
  STATIC_REQUIRE(made.has_value());
  STATIC_REQUIRE(made->terms().size() == 2);
  STATIC_REQUIRE(made->terms()[0] == term("M2", -1));
  STATIC_REQUIRE(made->terms()[1] == term("S2", 1));
  constexpr std::array m2_s2{term("M2", -1), term("S2", 1)};
  STATIC_REQUIRE(*made == *CompoundTerms::make(m2_s2));
  constexpr std::array m2_s2_other{term("M2", 1), term("S2", 1)};
  STATIC_REQUIRE(*made != *CompoundTerms::make(m2_s2_other));

  STATIC_REQUIRE(CompoundTerms::make(std::span<const Term>{}).error() ==
                 CompoundTermsError::empty);
  constexpr std::array nine{term("A1", 1), term("B1", 1), term("C1", 1),
                            term("D1", 1), term("E1", 1), term("F1", 1),
                            term("G1", 1), term("H1", 1), term("I1", 1)};
  STATIC_REQUIRE(CompoundTerms::make(nine).error() ==
                 CompoundTermsError::too_many);
  STATIC_REQUIRE(CompoundTerms::make(std::span{nine}.first(8)).has_value());
  constexpr std::array zero{term("M2", 0)};
  STATIC_REQUIRE(CompoundTerms::make(zero).error() ==
                 CompoundTermsError::zero_coefficient);
  constexpr std::array twice{term("M2", 1), term("S2", 1), term("M2", 2)};
  STATIC_REQUIRE(CompoundTerms::make(twice).error() ==
                 CompoundTermsError::duplicate_base);
  // Exact names: m2 and M2 are different bases here; a dictionary refuses
  // the pair (names unique ignoring case).
  constexpr std::array cases{term("M2", 1), term("m2", 1)};
  STATIC_REQUIRE(CompoundTerms::make(cases).has_value());
}

TEST_CASE("Argument is a commutative monoid", "[core][tide][constexpr]") {
  STATIC_REQUIRE(identity_holds());
  STATIC_REQUIRE(associative_and_commutative(law_args[0]));
  STATIC_REQUIRE(associative_and_commutative(law_args[1]));
  STATIC_REQUIRE(associative_and_commutative(law_args[2]));
  STATIC_REQUIRE(associative_and_commutative(law_args[3]));
}

TEST_CASE("scale multiplies coefficients by k and exponents by |k|",
          "[core][tide][constexpr]") {
  STATIC_REQUIRE(scale_laws_hold());
  STATIC_REQUIRE(scale_is_a_homomorphism(law_args[0]));
  STATIC_REQUIRE(scale_is_a_homomorphism(law_args[1]));
  STATIC_REQUIRE(scale_is_a_homomorphism(law_args[2]));
  STATIC_REQUIRE(scale_is_a_homomorphism(law_args[3]));
  STATIC_REQUIRE(negation_doubles_exponents());
  // Without a node factor, scale(-1, a) is a's inverse.
  STATIC_REQUIRE(scale(-1, arg("S2")) + arg("S2") == Argument{});
  STATIC_REQUIRE(scale(-1, arg("M2")).exponent(NodeFactor::f78) == 1);
  STATIC_REQUIRE(scale(-1, arg("M2")).v()[0] == -2);
}

TEST_CASE("Argument::darwin copies the definition", "[core][tide][constexpr]") {
  constexpr Argument m1 = Argument::darwin(Darwin{.v = {1, -1, 1, 0, 0},
                                                  .c_deg = -90,
                                                  .u = {1, -1, 0, 0, 1, 0, 0},
                                                  .f = NodeFactor::f206});
  STATIC_REQUIRE(m1.v() == std::array<std::int32_t, 5>{1, -1, 1, 0, 0});
  STATIC_REQUIRE(m1.c_deg() == -90);
  STATIC_REQUIRE(m1.u()[static_cast<std::size_t>(UTerm::Q)] == 1);
  STATIC_REQUIRE(m1.exponent(NodeFactor::f206) == 1);
  STATIC_REQUIRE(m1.exponent(NodeFactor::unity) == 0);
  STATIC_REQUIRE(m1.species() == 1);
  STATIC_REQUIRE(arg("S2").exponent(NodeFactor::unity) == 0);
}

TEST_CASE("compounds flatten to sums of scaled Darwin terms",
          "[core][tide][constexpr]") {
  STATIC_REQUIRE(arg("M4") == arg("M2") + arg("M2"));
  STATIC_REQUIRE(arg("MS4") == arg("M2") + arg("S2"));
  STATIC_REQUIRE(arg("MSF") == arg("S2") + scale(-1, arg("M2")));
  STATIC_REQUIRE(arg("2MK3") == scale(2, arg("M2")) + scale(-1, arg("K1")));
  STATIC_REQUIRE(arg("3KM5") == arg("K1") + arg("M2") + arg("K2"));
  STATIC_REQUIRE(arg("M4").exponent(NodeFactor::f78) == 2);
  STATIC_REQUIRE(arg("2MK3").exponent(NodeFactor::f227) == 1);
  STATIC_REQUIRE(arg("MSF").exponent(NodeFactor::f78) == 1);
}

TEST_CASE("flatten names the term it cannot resolve",
          "[core][tide][constexpr]") {
  constexpr auto in_nested = detail::lookup_in(nested_compound);
  STATIC_REQUIRE(tide::flatten(nested_compound[1].definition, in_nested) ==
                 scale(2, Argument::darwin(m2_darwin)));
  STATIC_REQUIRE(
      tide::flatten(nested_compound[2].definition, in_nested).error() ==
      TermError{TermNotDarwin{.term = constituent_name("M4")}});
  STATIC_REQUIRE(tide::flatten(dangling_term[0].definition,
                               detail::lookup_in(dangling_term))
                     .error() ==
                 TermError{UnknownTerm{.term = constituent_name("M2")}});
  STATIC_REQUIRE(tide::flatten(Definition{m2_darwin}, in_nested) ==
                 Argument::darwin(m2_darwin));
}

TEST_CASE("speeds follow from the 1900 rates", "[core][tide][constexpr]") {
  STATIC_REQUIRE(tide::rates_1900 == tide::rates_at(tide::nos_speed_epoch));
  STATIC_REQUIRE(tide::rates_1900.T == 15.0);
  STATIC_REQUIRE(arg("S2").speed_deg_per_hour() == 30.0);
  STATIC_REQUIRE(arg("S1").speed_deg_per_hour() == 15.0);
  STATIC_REQUIRE(near_abs(arg("M2").speed_deg_per_hour(), 28.9841042, 1e-7));
  STATIC_REQUIRE(near_abs(arg("K1").speed_deg_per_hour(), 15.0410686, 1e-7));
  // M1's speed carries b_Q * p-dot (SP 98 paragraph 124): half of M2's.
  STATIC_REQUIRE(near_abs(arg("M1").speed_deg_per_hour(), 14.4966939, 1e-7));
}

TEST_CASE("Doodson numbers follow from V", "[core][tide][constexpr]") {
  STATIC_REQUIRE(doodson_is(arg("M2"), "255555"));
  STATIC_REQUIRE(doodson_is(arg("K1"), "165555"));
  STATIC_REQUIRE(doodson_is(arg("O1"), "145555"));
  STATIC_REQUIRE(doodson_is(arg("SA"), "056555"));
  STATIC_REQUIRE(doodson_is(arg("M4"), "455555"));
  // MSF: s + T = 2, h - T = -2: digits 0, 7, 3.
  STATIC_REQUIRE(doodson_is(arg("MSF"), "073555"));
  STATIC_REQUIRE(tide::doodson_coefficients(arg("N2")) ==
                 std::array<std::int32_t, 6>{2, -1, 0, 1, 0, 0});
  // tau = 10 is not a digit.
  STATIC_REQUIRE_FALSE(tide::doodson_number(arg("M10")).has_value());
}

TEST_CASE("each catalogue invariant rejects a catalogue that breaks it",
          "[core][tide][constexpr]") {
  STATIC_REQUIRE(detail::names_unique(good_catalogue, no_aliases));
  STATIC_REQUIRE(detail::compound_terms_resolve(good_catalogue));
  STATIC_REQUIRE(detail::species_match_names(good_catalogue));
  STATIC_REQUIRE(detail::notes_plain(good_catalogue));
  STATIC_REQUIRE(detail::aliases_resolve(good_catalogue, alias_shadows_name));
  STATIC_REQUIRE_FALSE(
      detail::names_unique(duplicate_ignoring_case, no_aliases));
  STATIC_REQUIRE_FALSE(
      detail::names_unique(good_catalogue, alias_shadows_name));
  STATIC_REQUIRE_FALSE(detail::compound_terms_resolve(nested_compound));
  STATIC_REQUIRE_FALSE(detail::compound_terms_resolve(dangling_term));
  STATIC_REQUIRE_FALSE(detail::species_match_names(wrong_species));
  STATIC_REQUIRE_FALSE(detail::species_match_names(dangling_term));
  STATIC_REQUIRE_FALSE(detail::notes_plain(note_with_quote));
  STATIC_REQUIRE_FALSE(
      detail::aliases_resolve(good_catalogue, alias_to_nothing));
}

TEST_CASE("the species is read from the name", "[core][tide][constexpr]") {
  STATIC_REQUIRE(detail::species_of_name("M2") == 2);
  STATIC_REQUIRE(detail::species_of_name("4MSK11") == 11);
  STATIC_REQUIRE(detail::species_of_name("2MS2K2") == 2);
  STATIC_REQUIRE(detail::species_of_name("SA") == 0);
  STATIC_REQUIRE(detail::species_of_name("MSF") == 0);
  STATIC_REQUIRE(detail::species_of_name("RHO") == 1);
}

TEST_CASE("catalogue lookups", "[core][tide][constexpr]") {
  STATIC_REQUIRE(catalogue_index("M2") == 0);
  STATIC_REQUIRE(nos_catalogue[nos_standard_count - 1].name.view() == "MS4");
  STATIC_REQUIRE(nos_catalogue[nos_standard_count].name.view() == "SIGMA1");
  STATIC_REQUIRE_FALSE(catalogue_index("m2").has_value());
  STATIC_REQUIRE_FALSE(catalogue_index("LDA2").has_value());
  STATIC_REQUIRE(canonical_name("M2") == "M2");
  STATIC_REQUIRE(canonical_name("LDA2") == "LAM2");
  STATIC_REQUIRE(canonical_name("2(MS)8") == "2MS8");
  STATIC_REQUIRE(canonical_name("SGM") == "SIGMA1");
  STATIC_REQUIRE_FALSE(canonical_name("EPS2").has_value());
  STATIC_REQUIRE(catalogue_note("MSF").has_value());
  STATIC_REQUIRE_FALSE(catalogue_note("M2").has_value());
  STATIC_REQUIRE_FALSE(catalogue_argument("NOPE").has_value());
  STATIC_REQUIRE(
      tide::catalogue_definition(constituent_name("M2")).has_value());
  STATIC_REQUIRE_FALSE(
      tide::catalogue_definition(constituent_name("NOPE")).has_value());
  // The vetted NOAA readings (docs/harmonics-engine.md section 2.6): RP1 is
  // congen's PSI1 and KJ2 the IHO phase of SP 98's KJ2.
  STATIC_REQUIRE(arg("RP1").c_deg() == -90);
  STATIC_REQUIRE(arg("KJ2").c_deg() == 180);
}

TEST_CASE("elements at 2026-01-01T00:00Z match the prototype",
          "[core][tide][constexpr]") {
  // HJ 13.2, from the prototype (tools/tide/proto_predict.py, vterms and
  // node, %.10f): T is exact.
  constexpr Elements e =
      tide::elements(chr::sys_days{chr::year{2026} / chr::January / 1});
  STATIC_REQUIRE(e.T.degrees() == 180.0);
  STATIC_REQUIRE(near_abs(e.s.degrees(), 67.9853546907, 1e-9));
  STATIC_REQUIRE(near_abs(e.h.degrees(), 280.6660035268, 1e-9));
  STATIC_REQUIRE(near_abs(e.p.degrees(), 61.2921505997, 1e-9));
  STATIC_REQUIRE(near_abs(e.p1.degrees(), 283.3877193313, 1e-9));
  STATIC_REQUIRE(near_abs(e.N.degrees(), 342.1664600586, 1e-9));
}

TEST_CASE("elements: T is the hour angle of the mean sun",
          "[core][tide][constexpr]") {
  constexpr Time midnight = chr::sys_days{chr::year{2026} / 7 / 2};
  STATIC_REQUIRE(tide::elements(midnight).T.degrees() == 180.0);
  STATIC_REQUIRE(tide::elements(midnight + chr::hours{12}).T.degrees() == 0.0);
  STATIC_REQUIRE(tide::elements(midnight + chr::hours{6}).T.degrees() == 270.0);
  STATIC_REQUIRE(tide::elements(midnight - chr::hours{6}).T.degrees() == 90.0);
  // Before 1970 the day fraction is taken with floor, not truncation.
  constexpr Time old = chr::sys_days{chr::year{1900} / 1 / 1};
  STATIC_REQUIRE(tide::elements(old + chr::hours{3}).T.degrees() == 225.0);
  STATIC_REQUIRE(tide::elements(old - chr::hours{3}).T.degrees() == 135.0);
}

TEST_CASE("Julian centuries count from the SP 98 epoch",
          "[core][tide][constexpr]") {
  STATIC_REQUIRE(tide::julian_centuries(tide::sp98_epoch) == 0.0);
  STATIC_REQUIRE(tide::julian_centuries(tide::sp98_epoch + chr::days{36525}) ==
                 1.0);
  STATIC_REQUIRE(tide::julian_centuries(tide::sp98_epoch - chr::days{36525}) ==
                 -1.0);
}

TEST_CASE("the year scheme's instants", "[core][tide][constexpr]") {
  using tide::mid_year;
  using tide::year_start;
  STATIC_REQUIRE(year_start(chr::year{2026}) ==
                 Time{chr::sys_days{chr::year{2026} / 1 / 1}});
  // 2 July 12:00 in a common year, 00:00 in a leap year (HJ 5.2).
  STATIC_REQUIRE(mid_year(chr::year{2026}) ==
                 chr::sys_days{chr::year{2026} / 7 / 2} + chr::hours{12});
  STATIC_REQUIRE(mid_year(chr::year{2024}) ==
                 Time{chr::sys_days{chr::year{2024} / 7 / 2}});
  STATIC_REQUIRE(mid_year(chr::year{1900}) ==
                 chr::sys_days{chr::year{1900} / 7 / 2} + chr::hours{12});
  STATIC_REQUIRE(mid_year(chr::year{2000}) ==
                 Time{chr::sys_days{chr::year{2000} / 7 / 2}});
  STATIC_REQUIRE(tide::year_of(year_start(chr::year{2026}) -
                               chr::milliseconds{1}) == chr::year{2025});
  STATIC_REQUIRE(tide::year_of(year_start(chr::year{1700})) == chr::year{1700});
  STATIC_REQUIRE(tide::first_year == chr::year{1700});
  STATIC_REQUIRE(tide::last_year == chr::year{2200});
}

TEST_CASE("V is linear in the Argument", "[core][tide][constexpr]") {
  constexpr Elements e =
      tide::elements(chr::sys_days{chr::year{2026} / chr::January / 1});
  STATIC_REQUIRE(near_abs(arg("M4").v_deg(e), 2.0 * arg("M2").v_deg(e), 1e-9));
  STATIC_REQUIRE(near_abs(arg("MSF").v_deg(e),
                          arg("S2").v_deg(e) - arg("M2").v_deg(e), 1e-9));
  STATIC_REQUIRE(Argument{}.v_deg(e) == 0.0);
}
