// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for offered() over CO-OPS capabilities
// (mov/core/provider/capabilities.hpp) and the NDBC vocabulary.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <concepts>
#include <expected>
#include <optional>
#include <type_traits>

#include "mov/core/datum.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/provider/ndbc.hpp"
#include "mov/core/station.hpp"

namespace coops = mov::core::coops;

using coops::Datum;
using coops::Met;
using coops::Meteorological;
using coops::MetInterval;
using coops::Observed;
using coops::ObservedLevel;
using coops::ObservedVsPredicted;
using coops::PairedGrid;
using coops::PredictedLevel;
using coops::PredictionInterval;
using coops::Product;
using coops::WaterLevel;
using mov::core::NotOffered;
using mov::core::offered;
using CoopsCaps = mov::core::Capabilities<mov::core::provider::Coops>;

namespace {

/// A tidal station: 6-minute and hourly water level, predictions, two met
/// sensors, the tidal datums and STND.
constexpr CoopsCaps tidal() {
  return {
      .observed = {Observed::six_minute, Observed::hourly},
      .met = {Met::wind, Met::air_pressure},
      .predictions = true,
      .datums = mov::core::DatumTable{},
      .datums_offered = {Datum::mllw, Datum::msl, Datum::navd88, Datum::stnd}};
}

/// A Great Lakes station: water level against IGLD85, no predictions.
constexpr CoopsCaps great_lakes() {
  return {.observed = {Observed::six_minute, Observed::hourly,
                       Observed::one_minute},
          .met = {},
          .predictions = false,
          .datums = std::nullopt,
          .datums_offered = {Datum::igld85, Datum::stnd}};
}

/// A met-only station.
constexpr CoopsCaps met_only() {
  return {.observed = {},
          .met = {Met::air_temperature},
          .predictions = false,
          .datums = std::nullopt,
          .datums_offered = {Datum::stnd}};
}

constexpr Product level(coops::LevelKind kind, Datum d) {
  return WaterLevel{.kind = kind, .datum = d};
}
constexpr Product observed(Observed s, Datum d = Datum::mllw) {
  return level(ObservedLevel{.series = s}, d);
}
constexpr Product predicted(Datum d = Datum::mllw) {
  return level(PredictedLevel{.interval = PredictionInterval::six}, d);
}
constexpr Product paired(PairedGrid g, Datum d = Datum::mllw) {
  return level(ObservedVsPredicted{.grid = g}, d);
}
constexpr Product met(Met m) {
  return Meteorological{.product = m, .interval = MetInterval::hourly};
}

constexpr std::expected<void, NotOffered> refused(NotOffered why) {
  return std::unexpected{why};
}

constexpr bool is_offered(const CoopsCaps& caps, const Product& p) {
  return offered(caps, p).has_value();
}

}  // namespace

TEST_CASE("capabilities are values", "[core][capabilities][constexpr]") {
  STATIC_REQUIRE(std::regular<CoopsCaps>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<CoopsCaps>);
  using UsgsCaps = mov::core::Capabilities<mov::core::provider::Usgs>;
  using NdbcCaps = mov::core::Capabilities<mov::core::provider::Ndbc>;
  STATIC_REQUIRE(std::regular<UsgsCaps>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<UsgsCaps>);
  STATIC_REQUIRE(std::regular<NdbcCaps>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<NdbcCaps>);
  STATIC_REQUIRE(CoopsCaps{} == CoopsCaps{});
  STATIC_REQUIRE(tidal() != great_lakes());
}

TEST_CASE("NDBC vocabulary types are values", "[core][ndbc][constexpr]") {
  using mov::core::ndbc::FileKind;
  using mov::core::ndbc::StdMet;
  STATIC_REQUIRE(std::copyable<StdMet>);
  STATIC_REQUIRE(std::equality_comparable<StdMet>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<StdMet>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<StdMet>);
  // Precedence order: the quality-controlled file first.
  STATIC_REQUIRE(FileKind::historical < FileKind::monthly);
  STATIC_REQUIRE(FileKind::monthly < FileKind::monthly_latest);
  STATIC_REQUIRE(FileKind::monthly_latest < FileKind::realtime);
  STATIC_REQUIRE(mov::core::ndbc::grain == std::chrono::minutes{1});
}

TEST_CASE("observed water level needs the series and the datum",
          "[core][capabilities][constexpr]") {
  STATIC_REQUIRE(is_offered(tidal(), observed(Observed::six_minute)));
  STATIC_REQUIRE(is_offered(tidal(), observed(Observed::hourly)));
  // Water level, but not at one minute.
  STATIC_REQUIRE(offered(tidal(), observed(Observed::one_minute)) ==
                 refused(NotOffered::interval));
  // No water level at all.
  STATIC_REQUIRE(offered(met_only(), observed(Observed::six_minute)) ==
                 refused(NotOffered::product));
  // A datum the station does not serve.
  STATIC_REQUIRE(offered(tidal(), observed(Observed::hourly, Datum::igld85)) ==
                 refused(NotOffered::datum));
  STATIC_REQUIRE(
      is_offered(great_lakes(), observed(Observed::one_minute, Datum::igld85)));
  STATIC_REQUIRE(offered(great_lakes(), observed(Observed::one_minute)) ==
                 refused(NotOffered::datum));
  // The series is checked before the datum.
  STATIC_REQUIRE(offered(met_only(), observed(Observed::hourly, Datum::mhhw)) ==
                 refused(NotOffered::product));
}

TEST_CASE("predictions need the station's predictions and the datum",
          "[core][capabilities][constexpr]") {
  STATIC_REQUIRE(is_offered(tidal(), predicted()));
  STATIC_REQUIRE(is_offered(tidal(), predicted(Datum::navd88)));
  STATIC_REQUIRE(offered(tidal(), predicted(Datum::mhw)) ==
                 refused(NotOffered::datum));
  STATIC_REQUIRE(offered(great_lakes(), predicted(Datum::igld85)) ==
                 refused(NotOffered::predictions));
  // Predictions are checked before the datum.
  STATIC_REQUIRE(offered(great_lakes(), predicted(Datum::mllw)) ==
                 refused(NotOffered::predictions));
}

TEST_CASE("a paired product needs both halves",
          "[core][capabilities][constexpr]") {
  STATIC_REQUIRE(is_offered(tidal(), paired(PairedGrid::six_minute)));
  STATIC_REQUIRE(is_offered(tidal(), paired(PairedGrid::hourly, Datum::msl)));
  STATIC_REQUIRE(offered(met_only(), paired(PairedGrid::hourly)) ==
                 refused(NotOffered::product));
  STATIC_REQUIRE(offered(great_lakes(), paired(PairedGrid::hourly)) ==
                 refused(NotOffered::predictions));
  constexpr auto hourly_only_tidal = [] {
    CoopsCaps caps = tidal();
    caps.observed = {Observed::hourly};
    return caps;
  };
  STATIC_REQUIRE(offered(hourly_only_tidal(), paired(PairedGrid::six_minute)) ==
                 refused(NotOffered::interval));
  STATIC_REQUIRE(is_offered(hourly_only_tidal(), paired(PairedGrid::hourly)));
  STATIC_REQUIRE(offered(tidal(), paired(PairedGrid::hourly, Datum::mtl)) ==
                 refused(NotOffered::datum));
}

TEST_CASE("met products need their sensor", "[core][capabilities][constexpr]") {
  STATIC_REQUIRE(is_offered(tidal(), met(Met::wind)));
  STATIC_REQUIRE(is_offered(tidal(), met(Met::air_pressure)));
  STATIC_REQUIRE(offered(tidal(), met(Met::humidity)) ==
                 refused(NotOffered::product));
  STATIC_REQUIRE(offered(great_lakes(), met(Met::wind)) ==
                 refused(NotOffered::product));
  STATIC_REQUIRE(is_offered(met_only(), met(Met::air_temperature)));
}
