// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/tide: Phase, the node-factor tokens,
// ConstituentName, the Argument monoid, speeds, Doodson numbers, the
// catalogue and its invariants, and the SP 98 elements.

#include <algorithm>
#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

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
using mov::core::tide::catalogue_aliases;
using mov::core::tide::catalogue_argument;
using mov::core::tide::catalogue_index;
using mov::core::tide::catalogue_note;
using mov::core::tide::catalogue_notes;
using mov::core::tide::CatalogueAlias;
using mov::core::tide::CatalogueEntry;
using mov::core::tide::CatalogueNote;
using mov::core::tide::CatalogueTerm;
using mov::core::tide::ConstituentName;
using mov::core::tide::ConstituentNameError;
using mov::core::tide::Darwin;
using mov::core::tide::Elements;
using mov::core::tide::HighLow;
using mov::core::tide::NodeFactor;
using mov::core::tide::nos_catalogue;
using mov::core::tide::nos_standard_count;
using mov::core::tide::Phase;
using mov::core::tide::Tide;
using mov::core::tide::UTerm;
using mov::test::near_abs;

namespace detail = mov::core::detail;
namespace tide = mov::core::tide;
namespace chr = std::chrono;

namespace {

constexpr double quiet_nan = std::numeric_limits<double>::quiet_NaN();
constexpr double infinity = std::numeric_limits<double>::infinity();

constexpr bool sign_bit(double v) {
  return (std::bit_cast<std::uint64_t>(v) >> 63U) != 0U;
}

/// The flattened argument of a catalogue name the test knows exists.
constexpr Argument arg(std::string_view name) {
  return catalogue_argument(name).value_or(Argument{});
}

constexpr bool has_entry(std::string_view name) {
  return catalogue_argument(name).has_value();
}

// NOAA CO-OPS published speeds (deg/h) for the 120 constituents of 9455920
// Anchorage, harcon.json?units=metric (field `speed`), retrieved 2026-10-08
// for docs/harmonics-engine.md section 1 (the prototype's input). The first
// 37 are the NOS standard set, which every CO-OPS station shares.
struct PublishedSpeed {
  std::string_view name;
  double speed;
};
constexpr std::array noaa_speeds{
    PublishedSpeed{.name = "M2", .speed = 28.984104},
    PublishedSpeed{.name = "S2", .speed = 30.0},
    PublishedSpeed{.name = "N2", .speed = 28.43973},
    PublishedSpeed{.name = "K1", .speed = 15.041069},
    PublishedSpeed{.name = "M4", .speed = 57.96821},
    PublishedSpeed{.name = "O1", .speed = 13.943035},
    PublishedSpeed{.name = "M6", .speed = 86.95232},
    PublishedSpeed{.name = "MK3", .speed = 44.025173},
    PublishedSpeed{.name = "S4", .speed = 60.0},
    PublishedSpeed{.name = "MN4", .speed = 57.423832},
    PublishedSpeed{.name = "NU2", .speed = 28.512583},
    PublishedSpeed{.name = "S6", .speed = 90.0},
    PublishedSpeed{.name = "MU2", .speed = 27.968208},
    PublishedSpeed{.name = "2N2", .speed = 27.895355},
    PublishedSpeed{.name = "OO1", .speed = 16.139101},
    PublishedSpeed{.name = "LAM2", .speed = 29.455626},
    PublishedSpeed{.name = "S1", .speed = 15.0},
    PublishedSpeed{.name = "M1", .speed = 14.496694},
    PublishedSpeed{.name = "J1", .speed = 15.5854435},
    PublishedSpeed{.name = "MM", .speed = 0.5443747},
    PublishedSpeed{.name = "SSA", .speed = 0.0821373},
    PublishedSpeed{.name = "SA", .speed = 0.0410686},
    PublishedSpeed{.name = "MSF", .speed = 1.0158958},
    PublishedSpeed{.name = "MF", .speed = 1.0980331},
    PublishedSpeed{.name = "RHO", .speed = 13.471515},
    PublishedSpeed{.name = "Q1", .speed = 13.398661},
    PublishedSpeed{.name = "T2", .speed = 29.958933},
    PublishedSpeed{.name = "R2", .speed = 30.041067},
    PublishedSpeed{.name = "2Q1", .speed = 12.854286},
    PublishedSpeed{.name = "P1", .speed = 14.958931},
    PublishedSpeed{.name = "2SM2", .speed = 31.015896},
    PublishedSpeed{.name = "M3", .speed = 43.47616},
    PublishedSpeed{.name = "L2", .speed = 29.528479},
    PublishedSpeed{.name = "2MK3", .speed = 42.92714},
    PublishedSpeed{.name = "K2", .speed = 30.082138},
    PublishedSpeed{.name = "M8", .speed = 115.93642},
    PublishedSpeed{.name = "MS4", .speed = 58.984104},
    PublishedSpeed{.name = "SIGMA1", .speed = 12.92714},
    PublishedSpeed{.name = "MP1", .speed = 14.025173},
    PublishedSpeed{.name = "CHI1", .speed = 14.569548},
    PublishedSpeed{.name = "2PO1", .speed = 15.974827},
    PublishedSpeed{.name = "SO1", .speed = 16.056965},
    PublishedSpeed{.name = "MSN2", .speed = 30.544374},
    PublishedSpeed{.name = "MNS2", .speed = 27.423834},
    PublishedSpeed{.name = "OP2", .speed = 28.901966},
    PublishedSpeed{.name = "MKS2", .speed = 29.066242},
    PublishedSpeed{.name = "2NS2", .speed = 26.87946},
    PublishedSpeed{.name = "MLN2S2", .speed = 26.952312},
    PublishedSpeed{.name = "2ML2S2", .speed = 27.496687},
    PublishedSpeed{.name = "SKM2", .speed = 31.098034},
    PublishedSpeed{.name = "2MS2K2", .speed = 27.803934},
    PublishedSpeed{.name = "MKL2S2", .speed = 28.59472},
    PublishedSpeed{.name = "M2KS2", .speed = 29.148378},
    PublishedSpeed{.name = "2SNMK2", .speed = 29.373487},
    PublishedSpeed{.name = "2KMSN2", .speed = 30.708649},
    PublishedSpeed{.name = "SO3", .speed = 43.943035},
    PublishedSpeed{.name = "SK3", .speed = 45.04107},
    PublishedSpeed{.name = "NO3", .speed = 42.382767},
    PublishedSpeed{.name = "MK4", .speed = 59.066242},
    PublishedSpeed{.name = "SN4", .speed = 58.439728},
    PublishedSpeed{.name = "2MLS4", .speed = 57.49669},
    PublishedSpeed{.name = "3MS4", .speed = 56.952312},
    PublishedSpeed{.name = "ML4", .speed = 58.512585},
    PublishedSpeed{.name = "N4", .speed = 56.87946},
    PublishedSpeed{.name = "SL4", .speed = 59.52848},
    PublishedSpeed{.name = "MNO5", .speed = 71.36687},
    PublishedSpeed{.name = "2MO5", .speed = 71.91125},
    PublishedSpeed{.name = "2MK5", .speed = 73.00928},
    PublishedSpeed{.name = "MSK5", .speed = 74.02517},
    PublishedSpeed{.name = "3KM5", .speed = 74.10731},
    PublishedSpeed{.name = "2MP5", .speed = 72.92714},
    PublishedSpeed{.name = "3MP5", .speed = 71.99338},
    PublishedSpeed{.name = "MNK5", .speed = 72.464905},
    PublishedSpeed{.name = "2SM6", .speed = 88.98411},
    PublishedSpeed{.name = "2MN6", .speed = 86.407936},
    PublishedSpeed{.name = "MSN6", .speed = 87.423836},
    PublishedSpeed{.name = "2MS6", .speed = 87.96821},
    PublishedSpeed{.name = "2NMLS6", .speed = 85.392044},
    PublishedSpeed{.name = "2NM6", .speed = 85.86356},
    PublishedSpeed{.name = "MSL6", .speed = 88.51258},
    PublishedSpeed{.name = "2ML6", .speed = 87.49669},
    PublishedSpeed{.name = "MSK6", .speed = 89.06624},
    PublishedSpeed{.name = "2MLNS6", .speed = 85.93642},
    PublishedSpeed{.name = "3MLS6", .speed = 86.48079},
    PublishedSpeed{.name = "2MK6", .speed = 88.05035},
    PublishedSpeed{.name = "2MNO7", .speed = 100.350975},
    PublishedSpeed{.name = "2NMK7", .speed = 100.90463},
    PublishedSpeed{.name = "2MSO7", .speed = 101.91125},
    PublishedSpeed{.name = "MSKO7", .speed = 103.00928},
    PublishedSpeed{.name = "2MSN8", .speed = 116.407936},
    PublishedSpeed{.name = "3MS8", .speed = 116.95232},
    PublishedSpeed{.name = "2MS8", .speed = 117.96821},
    PublishedSpeed{.name = "2MN8", .speed = 114.847664},
    PublishedSpeed{.name = "3MN8", .speed = 115.392044},
    PublishedSpeed{.name = "2MSL8", .speed = 117.49669},
    PublishedSpeed{.name = "4MLS8", .speed = 115.4649},
    PublishedSpeed{.name = "3ML8", .speed = 116.48079},
    PublishedSpeed{.name = "3MK8", .speed = 117.03445},
    PublishedSpeed{.name = "2MSK8", .speed = 118.05035},
    PublishedSpeed{.name = "2M2NK9", .speed = 129.88873},
    PublishedSpeed{.name = "3MNK9", .speed = 130.4331},
    PublishedSpeed{.name = "4MK9", .speed = 130.97748},
    PublishedSpeed{.name = "3MSK9", .speed = 131.99338},
    PublishedSpeed{.name = "4MN10", .speed = 144.37614},
    PublishedSpeed{.name = "M10", .speed = 144.92052},
    PublishedSpeed{.name = "3MNS10", .speed = 145.39204},
    PublishedSpeed{.name = "4MS10", .speed = 145.93642},
    PublishedSpeed{.name = "3MSL10", .speed = 146.48079},
    PublishedSpeed{.name = "3M2S10", .speed = 146.95232},
    PublishedSpeed{.name = "4MSK11", .speed = 160.97748},
    PublishedSpeed{.name = "4MNS12", .speed = 174.37614},
    PublishedSpeed{.name = "5MS12", .speed = 174.92052},
    PublishedSpeed{.name = "4MSL12", .speed = 175.46489},
    PublishedSpeed{.name = "4M2S12", .speed = 175.93642},
    PublishedSpeed{.name = "TK1", .speed = 14.917865},
    PublishedSpeed{.name = "RP1", .speed = 15.082135},
    PublishedSpeed{.name = "KP1", .speed = 15.123206},
    PublishedSpeed{.name = "THETA1", .speed = 15.512589},
    PublishedSpeed{.name = "KJ2", .speed = 30.626513},
    PublishedSpeed{.name = "OO2", .speed = 27.341696},
};
static_assert(noaa_speeds.size() == nos_catalogue.size());

// NOAA rounds speeds to 7-8 significant digits (HJ 7.1). For the NOS 37 the
// format's checksum tolerance, 1e-5 deg/h, holds (largest: M6, 7.4e-6). The
// higher compounds of 9455920 are rounded to fewer decimals: 3MNK9 differs by
// 1.08e-5, so they get 2e-5, still five orders below the smallest speed gap
// a definition error could cause through T, s or h (0.04 deg/h).
constexpr bool speeds_match_noaa() {
  for (std::size_t i = 0; i < noaa_speeds.size(); ++i) {
    const auto& published = noaa_speeds[i];
    const double tolerance = i < nos_standard_count ? 1e-5 : 2e-5;
    const auto a = detail::flatten(nos_catalogue, nos_catalogue[i]);
    if (nos_catalogue[i].name() != published.name or not a or
        not near_abs(a->speed_deg_per_hour(), published.speed, tolerance)) {
      return false;
    }
  }
  return true;
}

// A few catalogue arguments for the monoid laws: with and without node
// factors, Darwin and compound, the M1 Q term, a negative coefficient. Looked
// up once, so the cubic associativity check stays within the constexpr
// step budgets (Clang and MSVC: about 1M).
constexpr std::array law_args{arg("M2"), arg("K1"),  arg("L2"),  arg("M1"),
                              arg("S2"), arg("MSF"), arg("2MK3")};

constexpr bool identity_holds() {
  return std::ranges::all_of(law_args, [](const Argument& a) {
    return a + Argument{} == a and Argument{} + a == a;
  });
}

constexpr bool associative_and_commutative() {
  for (const Argument& a : law_args) {
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
  }
  return true;
}

constexpr bool scale_laws_hold() {
  return std::ranges::all_of(law_args, [](const Argument& a) {
    return scale(1, a) == a and scale(2, a) == a + a and
           scale(3, a) == a + a + a and scale(0, a) == Argument{};
  });
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

// Catalogues that each break one invariant, for the predicates to reject.
constexpr Darwin m2_darwin{.v = {2, -2, 2, 0, 0},
                           .c_deg = 0,
                           .u = {2, -2, 0, 0, 0, 0, 0},
                           .f = NodeFactor::f78};
constexpr Darwin s2_darwin{
    .v = {2, 0, 0, 0, 0}, .c_deg = 0, .u = {}, .f = NodeFactor::unity};
constexpr std::array good_catalogue{
    CatalogueEntry{"M2", m2_darwin}, CatalogueEntry{"S2", s2_darwin},
    CatalogueEntry{"MS4", CatalogueTerm{"M2", 1}, CatalogueTerm{"S2", 1}}};
constexpr std::array duplicate_ignoring_case{CatalogueEntry{"M2", m2_darwin},
                                             CatalogueEntry{"m2", m2_darwin}};
constexpr std::array bad_name{CatalogueEntry{"M 2", m2_darwin}};
constexpr std::array unsorted_terms{
    CatalogueEntry{"M2", m2_darwin}, CatalogueEntry{"S2", s2_darwin},
    CatalogueEntry{"MS4", CatalogueTerm{"S2", 1}, CatalogueTerm{"M2", 1}}};
constexpr std::array zero_term{
    CatalogueEntry{"M2", m2_darwin}, CatalogueEntry{"S2", s2_darwin},
    CatalogueEntry{"MS4", CatalogueTerm{"M2", 0}, CatalogueTerm{"S2", 1}}};
constexpr std::array nested_compound{
    CatalogueEntry{"M2", m2_darwin},
    CatalogueEntry{"M4", CatalogueTerm{"M2", 2}},
    CatalogueEntry{"M8", CatalogueTerm{"M4", 2}}};
constexpr std::array dangling_term{
    CatalogueEntry{"M4", CatalogueTerm{"M2", 2}}};
constexpr std::array wrong_species{CatalogueEntry{"M3", m2_darwin}};
constexpr std::array no_aliases = std::array<CatalogueAlias, 0>{};
constexpr std::array alias_shadows_name{
    CatalogueAlias{.alias = "ms4", .name = "MS4"}};
constexpr std::array alias_to_nothing{
    CatalogueAlias{.alias = "MKS", .name = "MK4"}};
constexpr std::array note_to_nothing{
    CatalogueNote{.name = "X1", .note = "plain"}};
constexpr std::array note_with_quote{
    CatalogueNote{.name = "M2", .note = "say \"hi\""}};

}  // namespace

TEST_CASE("tide vocabulary types are value types", "[core][tide][constexpr]") {
  STATIC_REQUIRE(std::copyable<Phase>);
  STATIC_REQUIRE(std::equality_comparable<Phase>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<Phase>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<Phase>);
  STATIC_REQUIRE_FALSE(std::is_constructible_v<Phase, double>);
  STATIC_REQUIRE(std::regular<HighLow<int>>);
  STATIC_REQUIRE(std::regular<Argument>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Argument>);
  STATIC_REQUIRE(std::regular<Darwin>);
  STATIC_REQUIRE(std::regular<Elements>);
  STATIC_REQUIRE(std::copyable<ConstituentName>);
  STATIC_REQUIRE(std::totally_ordered<ConstituentName>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<ConstituentName>);
  STATIC_REQUIRE(std::copyable<CatalogueEntry>);
  STATIC_REQUIRE(std::equality_comparable<CatalogueEntry>);
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
  STATIC_REQUIRE(ConstituentName::make("2(MS)8").has_value());
  STATIC_REQUIRE(ConstituentName::make("OQ2-HORN").has_value());
  STATIC_REQUIRE(ConstituentName::make("a.b_c+d").has_value());
  STATIC_REQUIRE(
      ConstituentName::make("A1234567890123456789012345678901").has_value());
  STATIC_REQUIRE(ConstituentName::make("").error() ==
                 ConstituentNameError::empty);
  STATIC_REQUIRE(
      ConstituentName::make("A12345678901234567890123456789012").error() ==
      ConstituentNameError::too_long);
  STATIC_REQUIRE(ConstituentName::make("-M2").error() ==
                 ConstituentNameError::bad_char);
  STATIC_REQUIRE(ConstituentName::make("(M2)").error() ==
                 ConstituentNameError::bad_char);
  STATIC_REQUIRE(ConstituentName::make("M 2").error() ==
                 ConstituentNameError::bad_char);
  STATIC_REQUIRE(ConstituentName::make("M2/").error() ==
                 ConstituentNameError::bad_char);
  STATIC_REQUIRE(ConstituentName::make("M\xc3\xa9").error() ==
                 ConstituentNameError::bad_char);
}

TEST_CASE("ConstituentName orders by bytes", "[core][tide][constexpr]") {
  constexpr auto n2n2 = ConstituentName::make("2N2");
  constexpr auto m2 = ConstituentName::make("M2");
  constexpr auto ms4 = ConstituentName::make("MS4");
  constexpr auto lower_m2 = ConstituentName::make("m2");
  STATIC_REQUIRE(*n2n2 < *m2);
  STATIC_REQUIRE(*m2 < *ms4);
  STATIC_REQUIRE(*m2 < *lower_m2);
  STATIC_REQUIRE(*m2 == *ConstituentName::make("M2"));
  STATIC_REQUIRE(*m2 != *lower_m2);
}

TEST_CASE("Argument is a commutative monoid", "[core][tide][constexpr]") {
  STATIC_REQUIRE(identity_holds());
  STATIC_REQUIRE(associative_and_commutative());
}

TEST_CASE("scale multiplies coefficients by k and exponents by |k|",
          "[core][tide][constexpr]") {
  STATIC_REQUIRE(scale_laws_hold());
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
  STATIC_REQUIRE(arg("M4").exponent(NodeFactor::f78) == 2);
  STATIC_REQUIRE(arg("2MK3").exponent(NodeFactor::f227) == 1);
  STATIC_REQUIRE(arg("MSF").exponent(NodeFactor::f78) == 1);
}

TEST_CASE("speeds follow from the 1900 rates", "[core][tide][constexpr]") {
  STATIC_REQUIRE(tide::rates_1900.T == 15.0);
  STATIC_REQUIRE(arg("S2").speed_deg_per_hour() == 30.0);
  STATIC_REQUIRE(arg("S1").speed_deg_per_hour() == 15.0);
  STATIC_REQUIRE(near_abs(arg("M2").speed_deg_per_hour(), 28.9841042, 1e-7));
  STATIC_REQUIRE(near_abs(arg("K1").speed_deg_per_hour(), 15.0410686, 1e-7));
  // M1's speed carries b_Q * p-dot (SP 98 paragraph 124): half of M2's.
  STATIC_REQUIRE(near_abs(arg("M1").speed_deg_per_hour(), 14.4966939, 1e-7));
  STATIC_REQUIRE(speeds_match_noaa());
}

TEST_CASE("Doodson numbers follow from V", "[core][tide][constexpr]") {
  STATIC_REQUIRE(tide::doodson_number(arg("M2")) == 255555);
  STATIC_REQUIRE(tide::doodson_number(arg("K1")) == 165555);
  STATIC_REQUIRE(tide::doodson_number(arg("O1")) == 145555);
  STATIC_REQUIRE(tide::doodson_number(arg("SA")) == 56555);
  STATIC_REQUIRE(tide::doodson_number(arg("M4")) == 455555);
  STATIC_REQUIRE(tide::doodson_coefficients(arg("N2")) ==
                 std::array<std::int32_t, 6>{2, -1, 0, 1, 0, 0});
  // tau = 10 is not a digit.
  STATIC_REQUIRE_FALSE(tide::doodson_number(arg("M10")).has_value());
  // MSF: s + T = 2, h - T = -2: digits 0, 7, 3.
  STATIC_REQUIRE(tide::doodson_number(arg("MSF")) == 73555);
}

TEST_CASE("the catalogue holds its invariants", "[core][tide][constexpr]") {
  STATIC_REQUIRE(nos_catalogue.size() == 120);
  STATIC_REQUIRE(detail::names_valid(nos_catalogue));
  STATIC_REQUIRE(detail::names_unique(nos_catalogue, catalogue_aliases));
  STATIC_REQUIRE(detail::compounds_resolve(nos_catalogue));
  STATIC_REQUIRE(detail::species_match_names(nos_catalogue));
  STATIC_REQUIRE(detail::notes_plain(nos_catalogue, catalogue_notes));
  STATIC_REQUIRE(detail::aliases_resolve(nos_catalogue, catalogue_aliases));
}

TEST_CASE("each catalogue invariant rejects a catalogue that breaks it",
          "[core][tide][constexpr]") {
  STATIC_REQUIRE(detail::names_valid(good_catalogue));
  STATIC_REQUIRE(detail::names_unique(good_catalogue, no_aliases));
  STATIC_REQUIRE(detail::compounds_resolve(good_catalogue));
  STATIC_REQUIRE(detail::species_match_names(good_catalogue));
  STATIC_REQUIRE_FALSE(detail::names_valid(bad_name));
  STATIC_REQUIRE_FALSE(
      detail::names_unique(duplicate_ignoring_case, no_aliases));
  STATIC_REQUIRE_FALSE(
      detail::names_unique(good_catalogue, alias_shadows_name));
  STATIC_REQUIRE_FALSE(detail::compounds_resolve(unsorted_terms));
  STATIC_REQUIRE_FALSE(detail::compounds_resolve(zero_term));
  STATIC_REQUIRE_FALSE(detail::compounds_resolve(nested_compound));
  STATIC_REQUIRE_FALSE(detail::compounds_resolve(dangling_term));
  STATIC_REQUIRE_FALSE(detail::species_match_names(wrong_species));
  STATIC_REQUIRE_FALSE(detail::species_match_names(dangling_term));
  STATIC_REQUIRE_FALSE(
      detail::aliases_resolve(good_catalogue, alias_to_nothing));
  STATIC_REQUIRE_FALSE(detail::notes_plain(good_catalogue, note_to_nothing));
  STATIC_REQUIRE_FALSE(detail::notes_plain(good_catalogue, note_with_quote));
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
  STATIC_REQUIRE(nos_catalogue[nos_standard_count - 1].name() == "MS4");
  STATIC_REQUIRE(nos_catalogue[nos_standard_count].name() == "SIGMA1");
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
  STATIC_REQUIRE(has_entry("2KMSN2"));
  // The vetted NOAA readings (docs/harmonics-engine.md section 2.3): RP1 is
  // congen's PSI1 and KJ2 the IHO phase of SP 98's KJ2.
  STATIC_REQUIRE(arg("RP1").c_deg() == -90);
  STATIC_REQUIRE(arg("KJ2").c_deg() == 180);
  STATIC_REQUIRE(arg("3KM5") == arg("K1") + arg("M2") + arg("K2"));
}

TEST_CASE("elements at 2026-01-01T00:00Z match the prototype",
          "[core][tide][constexpr]") {
  // HJ 13.2, from the prototype (proto_predict.py vterms, %.10f): T is exact.
  constexpr Elements e =
      tide::elements(chr::sys_days{chr::year{2026} / chr::January / 1});
  STATIC_REQUIRE(e.T == 180.0);
  STATIC_REQUIRE(near_abs(e.s, 67.9853546907, 1e-9));
  STATIC_REQUIRE(near_abs(e.h, 280.6660035268, 1e-9));
  STATIC_REQUIRE(near_abs(e.p, 61.2921505997, 1e-9));
  STATIC_REQUIRE(near_abs(e.p1, 283.3877193313, 1e-9));
  STATIC_REQUIRE(near_abs(e.N, 342.1664600586, 1e-9));
}

TEST_CASE("elements: T is the hour angle of the mean sun",
          "[core][tide][constexpr]") {
  constexpr Time midnight = chr::sys_days{chr::year{2026} / 7 / 2};
  STATIC_REQUIRE(tide::elements(midnight).T == 180.0);
  STATIC_REQUIRE(tide::elements(midnight + chr::hours{12}).T == 0.0);
  STATIC_REQUIRE(tide::elements(midnight + chr::hours{6}).T == 270.0);
  STATIC_REQUIRE(tide::elements(midnight - chr::hours{6}).T == 90.0);
  // Before 1970 the day fraction is taken with floor, not truncation.
  constexpr Time old = chr::sys_days{chr::year{1900} / 1 / 1};
  STATIC_REQUIRE(tide::elements(old + chr::hours{3}).T == 225.0);
  STATIC_REQUIRE(tide::elements(old - chr::hours{3}).T == 135.0);
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
