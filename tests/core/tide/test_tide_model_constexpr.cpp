// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for the value types of the harmonics file model
// (WP H2): licence expressions, source tokens and station-id namespaces,
// file datum tokens, Heights, DatumPeriod and Datums, Analysis tokens,
// HarmonicConstant, SubordinateOffsets, a small ConstituentSet, and the
// spelling of the error and warning codes.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <initializer_list>
#include <optional>
#include <span>
#include <string_view>
#include <variant>
#include <vector>

#include "../test_helpers.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/enum_set.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/constituent_set.hpp"
#include "mov/core/tide/datums.hpp"
#include "mov/core/tide/harmonics.hpp"
#include "mov/core/tide/licence.hpp"
#include "mov/core/tide/node_factor.hpp"
#include "mov/core/tide/phase.hpp"
#include "mov/core/tide/source.hpp"
#include "mov/core/tide/station_info.hpp"
#include "mov/core/units.hpp"

using mov::core::Length;
using mov::core::LengthUnit;
using mov::core::VerticalDatum;
using mov::core::tide::all_analyses;
using mov::core::tide::all_computable_datums;
using mov::core::tide::Analysis;
using mov::core::tide::BothOrigins;
using mov::core::tide::CompoundTerms;
using mov::core::tide::ComputableDatum;
using mov::core::tide::ComputedDatums;
using mov::core::tide::ComputedHeights;
using mov::core::tide::constituent_name;
using mov::core::tide::ConstituentName;
using mov::core::tide::ConstituentSet;
using mov::core::tide::Darwin;
using mov::core::tide::DatumHeights;
using mov::core::tide::DatumOrigin;
using mov::core::tide::DatumPeriod;
using mov::core::tide::DatumPeriodError;
using mov::core::tide::Datums;
using mov::core::tide::DictionaryEntry;
using mov::core::tide::DictionaryError;
using mov::core::tide::DuplicateName;
using mov::core::tide::HarmonicConstant;
using mov::core::tide::HarmonicConstantError;
using mov::core::tide::HeightOffset;
using mov::core::tide::HeightRatio;
using mov::core::tide::HeightRow;
using mov::core::tide::HeightsErrc;
using mov::core::tide::Licence;
using mov::core::tide::LicenceErrc;
using mov::core::tide::LicenceError;
using mov::core::tide::NodeFactor;
using mov::core::tide::source_token;
using mov::core::tide::SourceToken;
using mov::core::tide::SourceTokenError;
using mov::core::tide::station_namespace;
using mov::core::tide::SubordinateOffsets;
using mov::core::tide::SubordinateOffsetsError;

namespace tide = mov::core::tide;

namespace {

constexpr Length metres(double m) noexcept {
  return Length::in(m, LengthUnit::meter);
}

/// The error of parsing `text`, or nullopt if it parses.
constexpr std::optional<LicenceError> licence_error(std::string_view text) {
  const auto l = Licence::parse(text);
  return l ? std::nullopt : std::optional{l.error()};
}

constexpr LicenceError at(std::size_t offset, LicenceErrc code) noexcept {
  return LicenceError{.offset = offset, .code = code};
}

constexpr std::chrono::sys_days day(int y, unsigned m, unsigned d) noexcept {
  return std::chrono::sys_days{std::chrono::year{y} / std::chrono::month{m} /
                               std::chrono::day{d}};
}

}  // namespace

TEST_CASE("licence expressions of the SPDX subset parse into identifiers",
          "[core][tide][licence][constexpr]") {
  STATIC_REQUIRE([] {
    const auto l = Licence::parse("LicenseRef-US-Government-Public-Domain");
    return l and l->identifiers().size() == 1 and
           l->identifiers()[0] == "LicenseRef-US-Government-Public-Domain" and
           l->exceptions().empty() and not l->carries_non_commercial() and
           l->text() == "LicenseRef-US-Government-Public-Domain";
  }());
  STATIC_REQUIRE([] {
    const auto l = Licence::parse("CC-BY-4.0 AND (MIT OR CC0-1.0)");
    return l and l->identifiers().size() == 3 and
           l->identifiers()[0] == "CC-BY-4.0" and
           l->identifiers()[1] == "MIT" and l->identifiers()[2] == "CC0-1.0";
  }());
  STATIC_REQUIRE([] {
    const auto l = Licence::parse(
        "(GPL-2.0-or-later WITH Classpath-exception-2.0) OR MIT");
    return l and l->identifiers().size() == 2 and
           l->exceptions().size() == 1 and
           l->exceptions()[0] == "Classpath-exception-2.0";
  }());
  // Spaces are optional around parentheses and repeatable elsewhere.
  STATIC_REQUIRE(Licence::parse("((MIT))AND  (Apache-2.0)").has_value());
  STATIC_REQUIRE(Licence::parse(" GPL-2.0+ ").has_value());
  // An identifier that only contains an operator's letters is an identifier.
  STATIC_REQUIRE(Licence::parse("LicenseRef-AND OR ANDY").has_value());
}

TEST_CASE("a non-commercial component is found by token, not by search",
          "[core][tide][licence][constexpr]") {
  STATIC_REQUIRE(Licence::parse("CC-BY-NC-SA-4.0")->carries_non_commercial());
  STATIC_REQUIRE(
      Licence::parse("MIT OR CC-BY-NC-4.0")->carries_non_commercial());
  STATIC_REQUIRE(Licence::parse("LicenseRef-x-nc")->carries_non_commercial());
  STATIC_REQUIRE(
      Licence::parse("MIT WITH LicenseRef-NC")->carries_non_commercial());
  STATIC_REQUIRE(Licence::parse("NC")->carries_non_commercial());
  // "NC" inside a component is not a component.
  STATIC_REQUIRE_FALSE(
      Licence::parse("LicenseRef-NCAR-data")->carries_non_commercial());
  STATIC_REQUIRE_FALSE(
      Licence::parse("LicenseRef-SYNC")->carries_non_commercial());
}

TEST_CASE("malformed licence expressions report where and why",
          "[core][tide][licence][constexpr]") {
  STATIC_REQUIRE(licence_error("") == at(0, LicenceErrc::empty));
  STATIC_REQUIRE(licence_error("   ") == at(0, LicenceErrc::empty));
  STATIC_REQUIRE(licence_error("MIT/X") == at(3, LicenceErrc::bad_character));
  STATIC_REQUIRE(licence_error("MIT\tOR X") ==
                 at(3, LicenceErrc::bad_character));
  STATIC_REQUIRE(licence_error("LicenseRef-a+b") ==
                 at(12, LicenceErrc::bad_character));
  STATIC_REQUIRE(licence_error("LicenseRef-") ==
                 at(11, LicenceErrc::bad_character));
  STATIC_REQUIRE(licence_error("AND MIT") ==
                 at(0, LicenceErrc::expected_licence));
  STATIC_REQUIRE(licence_error("MIT AND") ==
                 at(7, LicenceErrc::expected_licence));
  STATIC_REQUIRE(licence_error("MIT OR OR X") ==
                 at(7, LicenceErrc::expected_licence));
  STATIC_REQUIRE(licence_error("()") == at(1, LicenceErrc::expected_licence));
  STATIC_REQUIRE(licence_error("MIT Apache-2.0") ==
                 at(4, LicenceErrc::expected_operator));
  STATIC_REQUIRE(licence_error("MIT (X)") ==
                 at(4, LicenceErrc::expected_operator));
  STATIC_REQUIRE(licence_error("MIT and X") ==
                 at(4, LicenceErrc::expected_operator));
  STATIC_REQUIRE(licence_error("WITH X") ==
                 at(0, LicenceErrc::expected_licence));
  STATIC_REQUIRE(licence_error("(MIT) WITH X") ==
                 at(6, LicenceErrc::expected_operator));
  STATIC_REQUIRE(licence_error("MIT WITH X WITH Y") ==
                 at(11, LicenceErrc::expected_operator));
  STATIC_REQUIRE(licence_error("MIT WITH") ==
                 at(8, LicenceErrc::expected_licence));
  STATIC_REQUIRE(licence_error("MIT WITH AND") ==
                 at(9, LicenceErrc::expected_licence));
  STATIC_REQUIRE(licence_error("MIT WITH (X)") ==
                 at(9, LicenceErrc::expected_licence));
  STATIC_REQUIRE(licence_error("MIT)") == at(3, LicenceErrc::unbalanced));
  STATIC_REQUIRE(licence_error("(MIT") == at(4, LicenceErrc::unbalanced));
  STATIC_REQUIRE(licence_error("(") == at(1, LicenceErrc::expected_licence));
}

TEST_CASE("licences compare by text", "[core][tide][licence][constexpr]") {
  STATIC_REQUIRE(Licence::parse("MIT") == Licence::parse("MIT"));
  STATIC_REQUIRE(Licence::parse("MIT OR X") != Licence::parse("MIT  OR X"));
}

TEST_CASE("source tokens follow ^[a-z][a-z0-9-]{0,31}$",
          "[core][tide][source][constexpr]") {
  static constexpr SourceToken noaa = source_token("noaa-coops");
  STATIC_REQUIRE(noaa.view() == "noaa-coops");
  STATIC_REQUIRE(SourceToken::make("ticon-4").has_value());
  STATIC_REQUIRE(SourceToken::make("a").has_value());
  STATIC_REQUIRE(SourceToken::make(std::string_view{"a234567890123456789012"
                                                    "3456789012"})
                     .has_value());
  STATIC_REQUIRE(SourceToken::make("").error() == SourceTokenError::empty);
  STATIC_REQUIRE(
      SourceToken::make("a2345678901234567890123456789012x").error() ==
      SourceTokenError::too_long);
  STATIC_REQUIRE(SourceToken::make("Noaa").error() ==
                 SourceTokenError::bad_char);
  STATIC_REQUIRE(SourceToken::make("4noaa").error() ==
                 SourceTokenError::bad_char);
  STATIC_REQUIRE(SourceToken::make("noaa_coops").error() ==
                 SourceTokenError::bad_char);
  STATIC_REQUIRE(source_token("a") < source_token("b"));
}

TEST_CASE("a station id's namespace is its source token",
          "[core][tide][source][constexpr]") {
  STATIC_REQUIRE(station_namespace("noaa-coops:8720030") ==
                 source_token("noaa-coops"));
  // Only the first ':' separates; the local part may hold more.
  STATIC_REQUIRE(station_namespace("pringle:GESLA/a:b #2") ==
                 source_token("pringle"));
  STATIC_REQUIRE(station_namespace("ticon-4:\xC3\xA9tretat") ==
                 source_token("ticon-4"));
  STATIC_REQUIRE_FALSE(station_namespace("8720030"));
  STATIC_REQUIRE_FALSE(station_namespace("noaa-coops:"));
  STATIC_REQUIRE_FALSE(station_namespace(":8720030"));
  STATIC_REQUIRE_FALSE(station_namespace("NOAA:8720030"));
  STATIC_REQUIRE_FALSE(station_namespace("noaa:87\x1F"));
  STATIC_REQUIRE_FALSE(station_namespace("noaa:87\x7F"));
  STATIC_REQUIRE_FALSE(station_namespace("noaa:\xC3"));
  STATIC_REQUIRE([] {
    std::array<char, 256> id{};
    id.fill('x');
    id[0] = 'a';
    id[1] = ':';
    return station_namespace({id.data(), 255}).has_value() and
           not station_namespace({id.data(), 256}).has_value();
  }());
}

TEST_CASE("the file's datum tokens are core's, spelled exactly",
          "[core][tide][datums][constexpr]") {
  STATIC_REQUIRE(
      std::ranges::all_of(mov::core::detail::all_datums, [](VerticalDatum d) {
        return tide::parse_file_datum(mov::core::to_string(d)) == d;
      }));
  STATIC_REQUIRE(tide::parse_file_datum("MSL") == VerticalDatum::msl);
  STATIC_REQUIRE(tide::parse_file_datum("HAT") == VerticalDatum::hat);
  STATIC_REQUIRE_FALSE(tide::parse_file_datum("mllw"));
  STATIC_REQUIRE_FALSE(tide::parse_file_datum("NAVD"));  // an alias elsewhere
  // Every computable datum is its core datum, and back.
  STATIC_REQUIRE(
      std::ranges::all_of(all_computable_datums, [](ComputableDatum c) {
        return tide::computable(tide::vertical_datum(c)) == c;
      }));
  STATIC_REQUIRE(tide::to_string(ComputableDatum::dtl) == "DTL");
  STATIC_REQUIRE_FALSE(tide::computable(VerticalDatum::navd88));
  STATIC_REQUIRE_FALSE(tide::computable(VerticalDatum::msl));
}

namespace {

constexpr std::array published_rows{
    HeightRow<VerticalDatum>{.datum = VerticalDatum::mllw,
                             .height = metres(-1.004)},
    HeightRow<VerticalDatum>{.datum = VerticalDatum::stnd,
                             .height = metres(-1.522)}};

}  // namespace

TEST_CASE("datum heights hold each datum once, finite, never MSL",
          "[core][tide][datums][constexpr]") {
  using Row = HeightRow<VerticalDatum>;
  using Error = tide::HeightsError<VerticalDatum>;
  STATIC_REQUIRE(
      DatumHeights::make(published_rows)->height(VerticalDatum::mllw) ==
      metres(-1.004));
  STATIC_REQUIRE_FALSE(
      DatumHeights::make(published_rows)->height(VerticalDatum::mhw));
  STATIC_REQUIRE(DatumHeights::make(published_rows)->rows().size() == 2);
  STATIC_REQUIRE(DatumHeights{}.empty());
  STATIC_REQUIRE_FALSE(DatumHeights::make(published_rows)->empty());
  constexpr std::array twice{
      Row{.datum = VerticalDatum::mllw, .height = metres(-1.0)},
      Row{.datum = VerticalDatum::mllw, .height = metres(-1.0)}};
  STATIC_REQUIRE(
      DatumHeights::make(twice).error() ==
      Error{.datum = VerticalDatum::mllw, .code = HeightsErrc::duplicate});
  constexpr std::array infinite{
      Row{.datum = VerticalDatum::hat, .height = metres(mov::test::infinity)}};
  STATIC_REQUIRE(
      DatumHeights::make(infinite).error() ==
      Error{.datum = VerticalDatum::hat, .code = HeightsErrc::not_finite});
  constexpr std::array msl{
      Row{.datum = VerticalDatum::msl, .height = metres(0.0)}};
  STATIC_REQUIRE(DatumHeights::make(msl).error() ==
                 Error{.datum = VerticalDatum::msl, .code = HeightsErrc::msl});
  constexpr std::array computed_rows{HeightRow<ComputableDatum>{
      .datum = ComputableDatum::lat, .height = metres(-1.3)}};
  STATIC_REQUIRE(
      ComputedHeights::make(computed_rows)->height(ComputableDatum::lat) ==
      metres(-1.3));
}

TEST_CASE("a computed period spans at least 18.61 Julian years",
          "[core][tide][datums][constexpr]") {
  // 18.61 x 365.25 = 6797.3025 days: 6797 is short, 6798 is long enough.
  STATIC_REQUIRE(DatumPeriod::make(day(2000, 1, 1),
                                   day(2000, 1, 1) + std::chrono::days{6797})
                     .error() == DatumPeriodError::too_short);
  STATIC_REQUIRE(DatumPeriod::make(day(2000, 1, 1),
                                   day(2000, 1, 1) + std::chrono::days{6798})
                     .has_value());
  STATIC_REQUIRE(DatumPeriod::make(day(2007, 1, 1), day(2026, 1, 1))->end() ==
                 day(2026, 1, 1));
  STATIC_REQUIRE(DatumPeriod::make(day(2026, 1, 1), day(2007, 1, 1)).error() ==
                 DatumPeriodError::inverted);
  STATIC_REQUIRE(DatumPeriod::make(day(2026, 1, 1), day(2026, 1, 1)).error() ==
                 DatumPeriodError::too_short);
}

namespace {

constexpr ComputedDatums computed(
    std::initializer_list<HeightRow<ComputableDatum>> rows) {
  return ComputedDatums{
      .period = DatumPeriod::make(day(2007, 1, 1), day(2026, 1, 1)).value(),
      .heights =
          ComputedHeights::make(std::span{rows.begin(), rows.size()}).value()};
}

}  // namespace

TEST_CASE("Datums knows each height and whether it was published",
          "[core][tide][datums][constexpr]") {
  STATIC_REQUIRE([] {
    const auto d = Datums::make(
        DatumHeights::make(published_rows).value(),
        computed({{.datum = ComputableDatum::lat, .height = metres(-1.3)},
                  {.datum = ComputableDatum::mhw, .height = metres(0.9)}}),
        "1983-2001");
    return d and d->height(VerticalDatum::msl) == metres(0.0) and
           d->height(VerticalDatum::mllw) == metres(-1.004) and
           d->height(VerticalDatum::lat) == metres(-1.3) and
           not d->height(VerticalDatum::navd88) and
           d->origin(VerticalDatum::msl) == DatumOrigin::published and
           d->origin(VerticalDatum::mllw) == DatumOrigin::published and
           d->origin(VerticalDatum::mhw) == DatumOrigin::computed and
           not d->origin(VerticalDatum::hat) and
           d->published_epoch() == "1983-2001" and
           d->computed()->period.start() == day(2007, 1, 1);
  }());
  // The table holds every datum either set has, LAT included.
  STATIC_REQUIRE([] {
    const auto d = Datums::make(
        DatumHeights::make(published_rows).value(),
        computed({{.datum = ComputableDatum::lat, .height = metres(-1.3)},
                  {.datum = ComputableDatum::mhw, .height = metres(0.9)}}),
        std::nullopt);
    const auto t = d->table();
    return t.height_above_msl(VerticalDatum::mllw) == metres(-1.004) and
           t.height_above_msl(VerticalDatum::mhw) == metres(0.9) and
           t.height_above_msl(VerticalDatum::lat) == metres(-1.3) and
           t.height_above_msl(VerticalDatum::stnd) == metres(-1.522) and
           not t.height_above_msl(VerticalDatum::navd88);
  }());
  STATIC_REQUIRE(Datums{}.height(VerticalDatum::msl) == metres(0.0));
  STATIC_REQUIRE_FALSE(Datums{}.height(VerticalDatum::mllw));
  STATIC_REQUIRE_FALSE(Datums{}.origin(VerticalDatum::mllw));
  STATIC_REQUIRE(Datums{}.table() == mov::core::DatumTable{});
}

TEST_CASE("Datums refuses every datum both published and computed",
          "[core][tide][datums][constexpr]") {
  STATIC_REQUIRE(
      Datums::make(
          DatumHeights::make(published_rows).value(),
          computed({{.datum = ComputableDatum::mllw, .height = metres(-1.0)},
                    {.datum = ComputableDatum::lat, .height = metres(-1.3)}}),
          std::nullopt)
          .error() == BothOrigins{.datums = mov::core::EnumSet<VerticalDatum>{
                                      VerticalDatum::mllw}});
  STATIC_REQUIRE([] {
    using enum VerticalDatum;
    constexpr std::array rows{
        HeightRow<VerticalDatum>{.datum = mhw, .height = metres(0.9)},
        HeightRow<VerticalDatum>{.datum = mllw, .height = metres(-1.0)}};
    const auto e =
        Datums::make(
            DatumHeights::make(rows).value(),
            computed({{.datum = ComputableDatum::mllw, .height = metres(-1.0)},
                      {.datum = ComputableDatum::mhw, .height = metres(0.9)}}),
            std::nullopt)
            .error();
    return e.datums == mov::core::EnumSet<VerticalDatum>{mhw, mllw};
  }());
}

TEST_CASE("analysis tokens round-trip", "[core][tide][constexpr]") {
  STATIC_REQUIRE(std::ranges::all_of(all_analyses, [](Analysis a) {
    return tide::parse_analysis(tide::to_token(a)) == a;
  }));
  STATIC_REQUIRE(tide::to_token(Analysis::least_squares) == "least_squares");
  STATIC_REQUIRE_FALSE(tide::parse_analysis("NOS"));
  STATIC_REQUIRE_FALSE(tide::parse_analysis("unknown"));
}

TEST_CASE("a harmonic constant has H >= 0 and G in [0, 360)",
          "[core][tide][constexpr]") {
  using enum HarmonicConstantError;
  constexpr auto make = [](double h, double g) {
    return HarmonicConstant::make(
        {.amplitude_m = h, .phase_deg = g, .sd = std::nullopt});
  };
  STATIC_REQUIRE(make(0.87, 33.7)->amplitude() == metres(0.87));
  STATIC_REQUIRE(make(0.87, 33.7)->phase().degrees() == 33.7);
  STATIC_REQUIRE_FALSE(make(0.87, 33.7)->sd());
  STATIC_REQUIRE(make(0.0, 0.0).has_value());
  STATIC_REQUIRE(make(0.0, 359.999).has_value());
  STATIC_REQUIRE(make(-0.001, 0.0).error() == amplitude);
  STATIC_REQUIRE(make(mov::test::quiet_nan, 0.0).error() == amplitude);
  STATIC_REQUIRE(make(1.0, 360.0).error() == phase);
  STATIC_REQUIRE(make(1.0, -0.5).error() == phase);
  constexpr auto with_sd = [](double h_sd, double g_sd) {
    return HarmonicConstant::make(
        {.amplitude_m = 1.0,
         .phase_deg = 10.0,
         .sd = HarmonicConstant::UncertaintyValues{.amplitude_m = h_sd,
                                                   .phase_deg = g_sd}});
  };
  STATIC_REQUIRE(
      with_sd(0.01, 360.0)->sd() ==
      tide::Uncertainty{.amplitude = metres(0.01), .phase_deg = 360.0});
  STATIC_REQUIRE(
      with_sd(0.02, 2.0)->sd() ==
      tide::Uncertainty{.amplitude = metres(0.02), .phase_deg = 2.0});
  STATIC_REQUIRE(with_sd(-0.01, 2.0).error() == amplitude_sd);
  STATIC_REQUIRE(with_sd(0.01, 360.5).error() == phase_sd);
  STATIC_REQUIRE(with_sd(0.01, -1.0).error() == phase_sd);
}

TEST_CASE("subordinate offsets: minutes to milliseconds, ratios positive",
          "[core][tide][constexpr]") {
  using enum SubordinateOffsetsError;
  using std::chrono::milliseconds;
  constexpr auto ratio = [](double th, double tl, double rh, double rl) {
    return SubordinateOffsets::make(
        {.minutes = {.high = th, .low = tl},
         .height = HeightRatio{.ratio = {.high = rh, .low = rl}},
         .datum = VerticalDatum::mllw});
  };
  STATIC_REQUIRE(ratio(245.0, 249.0, 0.49, 1.16)->time().high ==
                 milliseconds{245 * 60'000});
  STATIC_REQUIRE(ratio(245.0, -249.0, 0.49, 1.16)->time().low ==
                 milliseconds{-249 * 60'000});
  STATIC_REQUIRE(ratio(1.0, 1.0, 0.49, 1.16)->datum() == VerticalDatum::mllw);
  STATIC_REQUIRE(ratio(1.0, 1.0, 0.49, 1.16)->height() ==
                 tide::HeightAdjustment{
                     HeightRatio{.ratio = {.high = 0.49, .low = 1.16}}});
  // Rounded half away from zero; -0 is 0.
  STATIC_REQUIRE(ratio(0.5 / 60'000.0, -0.5 / 60'000.0, 1.0, 1.0)->time() ==
                 tide::HighLow<milliseconds>{.high = milliseconds{1},
                                             .low = milliseconds{-1}});
  STATIC_REQUIRE(ratio(0.4 / 60'000.0, -0.4 / 60'000.0, 1.0, 1.0)->time() ==
                 tide::HighLow<milliseconds>{.high = milliseconds{0},
                                             .low = milliseconds{0}});
  STATIC_REQUIRE(ratio(-0.0, 0.0, 1.0, 1.0)->time().high == milliseconds{0});
  STATIC_REQUIRE(ratio(1440.0, -1440.0, 1.0, 1.0)->time().high ==
                 milliseconds{86'400'000});
  STATIC_REQUIRE(ratio(1440.01, 0.0, 1.0, 1.0).error() == high_time);
  STATIC_REQUIRE(ratio(0.0, -1440.01, 1.0, 1.0).error() == low_time);
  STATIC_REQUIRE(ratio(mov::test::quiet_nan, 0.0, 1.0, 1.0).error() ==
                 high_time);
  STATIC_REQUIRE(ratio(0.0, 0.0, 0.0, 1.0).error() == high_height);
  STATIC_REQUIRE(ratio(0.0, 0.0, 1.0, -1.0).error() == low_height);
  constexpr auto offset = [](double ah, double al) {
    return SubordinateOffsets::make(
        {.minutes = {.high = -17.0, .low = -29.0},
         .height =
             HeightOffset{.offset = {.high = metres(ah), .low = metres(al)}},
         .datum = VerticalDatum::msl});
  };
  STATIC_REQUIRE(offset(-0.06, 0.03).has_value());
  STATIC_REQUIRE(offset(-0.06, 0.03)->datum() == VerticalDatum::msl);
  STATIC_REQUIRE(offset(mov::test::infinity, 0.0).error() == high_height);
  STATIC_REQUIRE(offset(0.0, -mov::test::infinity).error() == low_height);
}

namespace {

constexpr Darwin m2_darwin{.v = {2, -2, 2, 0, 0},
                           .c_deg = 0,
                           .u = {2, -2, 0, 0, 0, 0, 0},
                           .f = NodeFactor::f78};
constexpr Darwin s2_darwin{.v = {2, 0, 0, 0, 0},
                           .c_deg = 0,
                           .u = {0, 0, 0, 0, 0, 0, 0},
                           .f = NodeFactor::unity};
constexpr Darwin k1_darwin{.v = {1, 0, 1, 0, 0},
                           .c_deg = -90,
                           .u = {0, 0, -1, 0, 0, 0, 0},
                           .f = NodeFactor::f227};

constexpr CompoundTerms compound(std::initializer_list<CompoundTerms::Term> t) {
  return CompoundTerms::make(std::span{t.begin(), t.size()}).value();
}

constexpr DictionaryEntry entry(std::string_view name,
                                const tide::Definition& d) {
  return DictionaryEntry{.name = ConstituentName::make(name).value(),
                         .definition = d,
                         .description = std::nullopt};
}

constexpr std::vector<DictionaryEntry> small_dictionary() {
  return {
      entry("MS4",
            compound({{.base = constituent_name("M2"), .coefficient = 1},
                      {.base = constituent_name("S2"), .coefficient = 1}})),
      entry("S2", s2_darwin), entry("K1", k1_darwin), entry("M2", m2_darwin),
      entry("M4",
            compound({{.base = constituent_name("M2"), .coefficient = 2}}))};
}

}  // namespace

TEST_CASE("a constituent set resolves and orders its entries",
          "[core][tide][constituent_set][constexpr]") {
  // Canonical order: species, then speed, then name.
  STATIC_REQUIRE([] {
    const auto set = ConstituentSet::make(small_dictionary());
    if (not set or set->size() != 5) {
      return false;
    }
    const std::array<std::string_view, 5> expected{"K1", "M2", "S2", "M4",
                                                   "MS4"};
    std::size_t k = 0;
    for (const auto i : set->indices()) {
      if (set->name(i).view() != expected[k] or i.value() != k) {
        return false;
      }
      ++k;
    }
    return k == expected.size();
  }());
  STATIC_REQUIRE([] {
    const auto set = ConstituentSet::make(small_dictionary());
    const auto m4 = set->find("M4");
    const auto m2 = set->find("M2");
    return m4 and m2 and set->argument(*m4) == scale(2, set->argument(*m2)) and
           set->speed(*m4) == 2.0 * set->speed(*m2) and
           std::holds_alternative<CompoundTerms>(set->definition(*m4)) and
           not set->description(*m4) and not set->find("m4") and
           not set->find("M6");
  }());
  // Equal whatever the input order.
  STATIC_REQUIRE([] {
    auto reversed = small_dictionary();
    std::ranges::reverse(reversed);
    return ConstituentSet::make(reversed) ==
           ConstituentSet::make(small_dictionary());
  }());
}

TEST_CASE("a constituent set refuses duplicate names and bad terms",
          "[core][tide][constituent_set][constexpr]") {
  STATIC_REQUIRE([] {
    auto d = small_dictionary();
    d.push_back(entry("ms4", s2_darwin));
    d.push_back(entry("K1", k1_darwin));
    return ConstituentSet::make(d).error() ==
           DictionaryError{DuplicateName{.entry = 5, .first = 0}};
  }());
  STATIC_REQUIRE([] {
    auto d = small_dictionary();
    d.insert(
        d.begin(),
        entry("MK3",
              compound({{.base = constituent_name("M2"), .coefficient = 1},
                        {.base = constituent_name("K2"), .coefficient = 1}})));
    return ConstituentSet::make(d).error() ==
           DictionaryError{tide::InvalidTerm{
               .entry = 0,
               .error = tide::UnknownTerm{.term = constituent_name("K2")}}};
  }());
  STATIC_REQUIRE([] {
    auto d = small_dictionary();
    d.push_back(entry(
        "M8", compound({{.base = constituent_name("M4"), .coefficient = 2}})));
    return ConstituentSet::make(d).error() ==
           DictionaryError{tide::InvalidTerm{
               .entry = 5,
               .error = tide::TermNotDarwin{.term = constituent_name("M4")}}};
  }());
  // The earliest entry of a case-insensitive group is the one reported.
  STATIC_REQUIRE([] {
    const std::vector d{entry("Sa", s2_darwin), entry("SSA", s2_darwin),
                        entry("SA", s2_darwin), entry("sa", s2_darwin)};
    const auto first = mov::core::detail::same_name_ignoring_case(d);
    return not first[0] and not first[1] and first[2] == 0 and first[3] == 0;
  }());
}

TEST_CASE("every error and warning code has its format spelling",
          "[core][tide][harmonics][constexpr]") {
  using enum tide::HarmonicsErrc;
  STATIC_REQUIRE(tide::to_string(schema) == "E-SCHEMA");
  STATIC_REQUIRE(tide::to_string(date) == "E-DATE");
  STATIC_REQUIRE(tide::to_string(duplicate_source) == "E-DUPLICATE-SOURCE");
  STATIC_REQUIRE(tide::to_string(licence) == "E-LICENCE");
  STATIC_REQUIRE(tide::to_string(duplicate_constituent) ==
                 "E-DUPLICATE-CONSTITUENT");
  STATIC_REQUIRE(tide::to_string(compound_term) == "E-COMPOUND-TERM");
  STATIC_REQUIRE(tide::to_string(speed_mismatch) == "E-SPEED-MISMATCH");
  STATIC_REQUIRE(tide::to_string(unknown_source) == "E-UNKNOWN-SOURCE");
  STATIC_REQUIRE(tide::to_string(unknown_constituent) ==
                 "E-UNKNOWN-CONSTITUENT");
  STATIC_REQUIRE(tide::to_string(duplicate_row) == "E-DUPLICATE-ROW");
  STATIC_REQUIRE(tide::to_string(datum_conflict) == "E-DATUM-CONFLICT");
  STATIC_REQUIRE(tide::to_string(duplicate_station) == "E-DUPLICATE-STATION");
  STATIC_REQUIRE(tide::to_string(reference) == "E-REFERENCE");
  STATIC_REQUIRE(tide::to_string(offset_datum) == "E-OFFSET-DATUM");
  using W = tide::HarmonicsWarningCode;
  STATIC_REQUIRE(tide::to_string(W::constituent_redefined) ==
                 "W-CONSTITUENT-REDEFINED");
  STATIC_REQUIRE(tide::to_string(W::unused_constituent) ==
                 "W-UNUSED-CONSTITUENT");
  STATIC_REQUIRE(tide::to_string(W::unused_source) == "W-UNUSED-SOURCE");
  STATIC_REQUIRE(tide::code(tide::HarmonicsError{
                     tide::ShortDatumPeriod{.station = 0}}) == datum_conflict);
  STATIC_REQUIRE(tide::code(tide::HarmonicsError{tide::InvalidNumber{
                     .station = 0, .number = tide::StationNumber::z0}}) ==
                 schema);
  STATIC_REQUIRE(tide::code(tide::HarmonicsWarning{
                     tide::UnusedSource{.source = 0}}) == W::unused_source);
}
