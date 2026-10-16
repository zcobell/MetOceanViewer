// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The CO-OPS catalog from the station lists (docs/providers-design.md
// sections 4.8 and 7): the cross-list merge policy and datums.json.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "catalog_test_support.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/units.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/station_catalog.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/coops.hpp"
#include "mov/io/wire/coops_catalog.hpp"
#include "wire_test_support.hpp"

namespace {

namespace cc = mov::core::coops;
namespace wc = mov::io::wire::coops;
namespace cat = mov::test::catalog;
using mov::core::DatumTable;
using mov::core::Length;
using mov::core::LengthUnit;
using mov::core::VerticalDatum;
using mov::io::ReadContext;
using mov::io::Warning;
using mov::io::WarningCode;
using mov::test::coops_fixture;
using mov::test::utc;
using Coops = mov::core::provider::Coops;

std::vector<wc::StationRecord> list(std::string_view name) {
  auto r = wc::parse_stations(coops_fixture(name), ReadContext{});
  REQUIRE(r.has_value());
  return std::move(r->value);
}

wc::StationDatums datums(std::string_view name) {
  const auto r = wc::parse_datums(coops_fixture(name), ReadContext{});
  REQUIRE(r.has_value());
  return r->value;
}

// A station with an active 6-minute water-level sensor.
wc::StationRecord record(std::string_view id, std::string_view name, double lat,
                         double lon) {
  const auto e = cat::entry<Coops>(id, name, lat, lon);
  return wc::StationRecord{.station = e.station,
                           .observed = {cc::Observed::six_minute},
                           .met = {},
                           .air_gap = false,
                           .datums = {}};
}

const wc::CatalogEntry& find(const std::vector<wc::CatalogEntry>& entries,
                             std::string_view id) {
  for (const wc::CatalogEntry& e : entries) {
    if (e.station.id.value() == id) {
      return e;
    }
  }
  FAIL("no station " << id);
  return entries.front();
}

// The catalog of the recorded lists (tests/fixtures/io/coops/), with the
// three recorded datums.json bodies at their stations.
mov::io::CoopsCatalog recorded_catalog() {
  auto merged = wc::merge_station_lists(
      {.water_levels = list("stations_waterlevels.json"),
       .met = list("stations_met.json"),
       .air_gap = list("stations_airgap.json"),
       .tide_predictions = [] {
         auto r = wc::parse_prediction_stations(
             coops_fixture("stations_tidepredictions.json"), ReadContext{});
         REQUIRE(r.has_value());
         return std::move(r->value);
       }()});
  // 8767931 (an air-gap bridge whose sensor is inactive) and 9435373 (a met
  // station without sensors) offer nothing.
  CHECK(merged.warnings ==
        std::vector<Warning>{{.code = WarningCode::station_skipped,
                              .subject = "no_active_sensor",
                              .count = 2}});
  std::vector<wc::CatalogEntry> entries;
  for (wc::CatalogEntry& e : merged.value) {
    const std::string_view id = e.station.id.value();
    if (id == "8729840") {
      e = wc::with_datums(std::move(e), datums("datums.json"));
    } else if (id == "9063020") {
      e = wc::with_datums(std::move(e), datums("datums_great_lakes.json"));
    } else if (id == "8517986") {
      e = wc::with_datums(std::move(e), datums("datums_null.json"));
    }
    entries.push_back(std::move(e));
  }
  const mov::core::Time generated = utc(2026, 10, 15, 16, 50, 0);
  const auto source = [](std::string_view query) {
    return mov::io::CatalogSource{
        .url =
            cat::text("https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/"
                      "stations.json?" +
                      std::string{query}),
        .retrieved = utc(2026, 10, 15, 16, 49, 0)};
  };
  return cat::catalog<Coops>(
      std::move(entries),
      {.generated = generated,
       .sources = {source("type=waterlevels&expand=details,sensors"),
                   source("type=met&expand=details,sensors"),
                   source("type=airgap&expand=details,sensors"),
                   source("type=tidepredictions")}});
}

}  // namespace

TEST_CASE("the recorded station lists make the golden CO-OPS catalog",
          "[io][catalog][coops]") {
  const mov::io::CoopsCatalog catalog = recorded_catalog();
  // 4 water level, 5 met, 2 air gap, less 2 that offer nothing.
  REQUIRE(catalog.size() == 9);
  CHECK(catalog.find(cat::id_of<Coops>("8767931")) == nullptr);
  CHECK(catalog.find(cat::id_of<Coops>("9435373")) == nullptr);

  const auto& pensacola = *catalog.find(cat::id_of<Coops>("8729840"));
  CHECK(pensacola.caps.predictions == cc::Predictions::any_interval);
  CHECK(pensacola.caps.datums_offered.contains(cc::Datum::navd88));
  REQUIRE(pensacola.caps.datums.has_value());
  // datums.json is relative to STND: MLLW 2.569 - MSL 2.757.
  const auto mllw = pensacola.caps.datums.value_or(DatumTable{})
                        .height_above_msl(VerticalDatum::mllw);
  REQUIRE(mllw.has_value());
  CHECK(std::abs(mllw.value_or(Length{}).as(LengthUnit::meter) - -0.188) <
        1e-12);

  const auto& buffalo = *catalog.find(cat::id_of<Coops>("9063020"));
  CHECK(buffalo.caps.datums_offered == cc::DatumSet{cc::Datum::igld85});
  CHECK(not buffalo.caps.datums.has_value());
  CHECK(buffalo.caps.predictions == cc::Predictions::none);

  const auto& bridge = *catalog.find(cat::id_of<Coops>("8517986"));
  CHECK(bridge.caps.air_gap);
  CHECK(not bridge.caps.datums.has_value());

  const std::string text = cat::format(catalog);
  cat::check_golden(text, "coops_lists.json");
  // And it reads back as the catalog, rounded as the file holds it.
  auto read = cat::parse(text);
  REQUIRE(read.has_value());
  CHECK(read->warnings.empty());
  CHECK(cat::format(std::get<mov::io::CoopsCatalog>(read->value)) == text);
}

TEST_CASE("each station of the worked example is a line of the golden catalog",
          "[io][catalog][coops]") {
  const std::string golden = mov::test::read_bytes(
      mov::test::fixture("io/station_catalog/coops_lists.json"));
  const std::string example = mov::test::read_bytes(
      mov::test::fixture("io/station_catalog/worked_example.json"));
  std::size_t stations = 0;
  for (std::size_t at = 0; at < example.size();) {
    const std::size_t end = example.find('\n', at);
    const std::string line = example.substr(at, end - at);
    at = end == std::string::npos ? example.size() : end + 1;
    if (not line.starts_with("{\"id\":")) {
      continue;
    }
    ++stations;
    const std::string bare =
        line.ends_with(',') ? line.substr(0, line.size() - 1) : line;
    CHECK(golden.find(bare) != std::string::npos);
  }
  CHECK(stations == 3);
}

TEST_CASE("a station in several lists is one entry with the union of them",
          "[io][catalog][coops]") {
  wc::StationRecord level = record("8729840", "Pensacola", 30.4044, -87.2112);
  level.observed = {cc::Observed::six_minute, cc::Observed::hourly};
  level.station.validity = cat::valid(cat::day(1923, 4, 30));
  wc::StationRecord met = level;
  met.station.validity =
      cat::valid(cat::day(1950, 1, 1), cat::day(2000, 12, 31));
  met.observed = {};
  met.met = {cc::Met::wind, cc::Met::air_pressure};
  wc::StationRecord gap = level;
  gap.observed = {cc::Observed::one_minute};
  gap.air_gap = true;
  gap.datums = {cc::Datum::igld85};

  const auto merged = wc::merge_station_lists(
      {.water_levels = {level}, .met = {met}, .air_gap = {gap}});
  CHECK(merged.warnings.empty());  // the same station in each list
  REQUIRE(merged.value.size() == 1);
  const auto& caps = merged.value.front().caps;
  CHECK(caps.observed == cc::ObservedSet{cc::Observed::six_minute,
                                         cc::Observed::hourly,
                                         cc::Observed::one_minute});
  CHECK(caps.met == cc::MetSet{cc::Met::wind, cc::Met::air_pressure});
  CHECK(caps.air_gap);
  CHECK(caps.datums_offered == cc::DatumSet{cc::Datum::igld85});
  // Ongoing in one list: ongoing; first reported in 1923 in another.
  CHECK(merged.value.front().station.validity ==
        cat::valid(cat::day(1923, 4, 30)));
}

TEST_CASE("validity across lists is the join of the records' ranges",
          "[io][catalog][coops]") {
  wc::StationRecord a = record("8729840", "Pensacola", 30.4044, -87.2112);
  a.station.validity = cat::valid(cat::day(1950, 1, 1), cat::day(1990, 1, 1));
  wc::StationRecord b = a;
  b.station.validity = cat::valid(cat::day(1923, 4, 30), cat::day(1960, 1, 1));
  wc::StationRecord c = a;
  c.station.validity = cat::valid(std::nullopt, cat::day(2001, 2, 3));
  const auto merged = wc::merge_station_lists(
      {.water_levels = {a}, .met = {b}, .air_gap = {c}});
  CHECK(merged.warnings.empty());
  REQUIRE(merged.value.size() == 1);
  CHECK(merged.value.front().station.validity ==
        cat::valid(cat::day(1923, 4, 30), cat::day(2001, 2, 3)));
}

TEST_CASE("positions are compared as a catalog holds them",
          "[io][catalog][coops]") {
  const auto a = record("8729840", "Pensacola", 30.4044, -87.2112);
  // Digits past 1e-6 degrees: the same position in a catalog.
  const auto b = record("8729840", "Pensacola", 30.40440004, -87.21120003);
  const auto merged =
      wc::merge_station_lists({.water_levels = {a}, .met = {b}});
  CHECK(merged.warnings.empty());
  REQUIRE(merged.value.size() == 1);
  CHECK(merged.value.front().station == a.station);
}

TEST_CASE("a station that offers nothing is left out and counted",
          "[io][catalog][coops]") {
  wc::StationRecord idle = record("8767931", "Lake Charles", 30.2, -93.28);
  idle.observed = {};
  wc::StationRecord predicted = record("1612340", "Honolulu", 21.3, -157.86);
  predicted.observed = {};
  wc::StationRecord idle_met = record("9435373", "McLean Point", 44.6, -124.0);
  idle_met.observed = {};
  const auto merged = wc::merge_station_lists(
      {.water_levels = {predicted},
       .met = {idle_met},
       .air_gap = {idle},
       .tide_predictions = {{.id = cat::id_of<Coops>("1612340"),
                             .type = wc::PredictionType::harmonic}}});
  // Predictions alone are something to offer.
  REQUIRE(merged.value.size() == 1);
  CHECK(merged.value.front().station.id.value() == "1612340");
  CHECK(merged.warnings ==
        std::vector<Warning>{{.code = WarningCode::station_skipped,
                              .subject = "no_active_sensor",
                              .count = 2}});
}

TEST_CASE("the recorded overlapping lists merge without a disagreement",
          "[io][catalog][coops]") {
  const auto levels = list("stations_waterlevels_overlap.json");
  const auto met = list("stations_met_overlap.json");
  const auto merged =
      wc::merge_station_lists({.water_levels = levels, .met = met});
  CHECK(merged.warnings.empty());
  REQUIRE(merged.value.size() == 5);  // 3 in both, 1 in each alone
  const auto& battery = find(merged.value, "8518750");
  const auto& level = [&]() -> const wc::StationRecord& {
    for (const wc::StationRecord& r : levels) {
      if (r.station.id.value() == "8518750") {
        return r;
      }
    }
    FAIL("8518750 is not in the water-level list");
    return levels.front();
  }();
  CHECK(battery.station.name == level.station.name);
  CHECK(not battery.caps.observed.empty());
  CHECK(not battery.caps.met.empty());
  CHECK(not find(merged.value, "8447435").caps.observed.empty());
  CHECK(not find(merged.value, "8444069").caps.met.empty());
}

TEST_CASE("the first list's station is kept and a later difference is warned",
          "[io][catalog][coops]") {
  const wc::StationRecord level =
      record("8729840", "Pensacola", 30.4044, -87.2112);
  wc::StationRecord met = record("8729840", "Pensacola, FL", 30.4044, -87.2112);
  met.met = {cc::Met::wind};
  wc::StationRecord moved = record("8729840", "Pensacola", 30.5, -87.2112);
  moved.air_gap = true;
  const wc::StationRecord battery =
      record("8518750", "The Battery", 40.7, -74.0);
  const wc::StationRecord battery_met =
      record("8518750", "The Battery, NY", 40.7, -74.0);
  const wc::StationRecord other =
      record("9414290", "San Francisco", 37.8, -122.5);

  const auto merged =
      wc::merge_station_lists({.water_levels = {level, battery, other},
                               .met = {met, battery_met},
                               .air_gap = {moved}});
  REQUIRE(merged.value.size() == 3);
  // Sorted by id.
  CHECK(merged.value.at(0).station.id.value() == "8518750");
  const auto& kept = merged.value.at(1);
  CHECK(kept.station == level.station);
  CHECK(kept.caps.met == cc::MetSet{cc::Met::wind});
  CHECK(kept.caps.air_gap);
  // One warning: the first station that disagrees, and how many do.
  CHECK(merged.warnings ==
        std::vector<Warning>{{.code = WarningCode::station_lists_disagree,
                              .subject = "8518750",
                              .count = 2}});
}

TEST_CASE("predictions come from the tide-prediction list's type",
          "[io][catalog][coops]") {
  const auto a = record("1612340", "Honolulu", 21.3, -157.86);
  const auto b = record("1610367", "Nonopapa", 21.87, -160.23);
  const auto c = record("8729840", "Pensacola", 30.4044, -87.2112);
  const auto merged = wc::merge_station_lists(
      {.water_levels = {a, b, c},
       .tide_predictions = {{.id = cat::id_of<Coops>("1612340"),
                             .type = wc::PredictionType::harmonic},
                            {.id = cat::id_of<Coops>("1610367"),
                             .type = wc::PredictionType::subordinate},
                            // Twice, with another type: the first is kept.
                            {.id = cat::id_of<Coops>("1610367"),
                             .type = wc::PredictionType::harmonic},
                            // Not in any station list: not a catalog station.
                            {.id = cat::id_of<Coops>("8218617"),
                             .type = wc::PredictionType::subordinate}}});
  REQUIRE(merged.value.size() == 3);
  CHECK(find(merged.value, "1612340").caps.predictions ==
        cc::Predictions::any_interval);
  CHECK(find(merged.value, "1610367").caps.predictions ==
        cc::Predictions::hilo_only);
  CHECK(find(merged.value, "8729840").caps.predictions ==
        cc::Predictions::none);
  CHECK(merged.warnings ==
        std::vector<Warning>{{.code = WarningCode::station_lists_disagree,
                              .subject = "1610367",
                              .count = 1}});
}

TEST_CASE("with_datums adds the offered datums and the table",
          "[io][catalog][coops]") {
  wc::CatalogEntry e = cat::entry<Coops>("9063020", "Buffalo", 42.877, -78.89);
  e.caps.datums_offered = {cc::Datum::igld85};
  const auto table = DatumTable::from_heights(
      VerticalDatum::msl, std::vector<mov::core::DatumHeight>{
                              {.datum = VerticalDatum::mllw,
                               .height = Length::in(-0.2, LengthUnit::meter)}});
  REQUIRE(table.has_value());
  const auto with = wc::with_datums(
      e, {.offered = {cc::Datum::mllw, cc::Datum::msl}, .table = *table});
  CHECK(with.caps.datums_offered ==
        cc::DatumSet{cc::Datum::igld85, cc::Datum::mllw, cc::Datum::msl});
  CHECK(with.caps.datums == table);
  // A datums.json without MSL: no table, offered names still added.
  const auto without =
      wc::with_datums(e, {.offered = {cc::Datum::stnd}, .table = std::nullopt});
  CHECK(not without.caps.datums.has_value());
  CHECK(without.caps.datums_offered ==
        cc::DatumSet{cc::Datum::igld85, cc::Datum::stnd});
}
