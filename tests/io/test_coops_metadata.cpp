// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The CO-OPS metadata parsers: station lists, the tide-prediction list and
// datums.json (docs/providers-design.md sections 3.3 and 4.3).

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/station.hpp"
#include "mov/core/stop_token.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/coops.hpp"
#include "wire_test_support.hpp"

namespace {

namespace cc = mov::core::coops;
namespace wc = mov::io::wire::coops;
using mov::core::DatumTable;
using mov::core::Length;
using mov::core::LengthUnit;
using mov::core::VerticalDatum;
using mov::io::FormatErrc;
using mov::io::ParseErrc;
using mov::io::ReadContext;
using mov::io::Warning;
using mov::io::WarningCode;
using mov::test::coops_fixture;
using mov::test::format_error_in;
using mov::test::parse_error_in;
using Station = mov::core::StationId<mov::core::provider::Coops>;

Station id(std::string_view raw) {
  const auto made = Station::make(raw);
  REQUIRE(made.has_value());
  return *made;
}

std::vector<wc::StationRecord> stations(std::string_view body) {
  auto r = wc::parse_stations(body, ReadContext{});
  REQUIRE(r.has_value());
  return std::move(r->value);
}

const wc::StationRecord& record(const std::vector<wc::StationRecord>& all,
                                std::string_view raw) {
  for (const wc::StationRecord& r : all) {
    if (r.station.id.value() == raw) {
      return r;
    }
  }
  FAIL("no station " << raw);
  return all.front();
}

std::optional<std::chrono::sys_days> on_day(int y, unsigned m, unsigned d) {
  using namespace std::chrono;
  return sys_days{year{y} / month{m} / day{d}};
}

// A list of one station built from `members`.
std::string list_of(std::string_view members) {
  return R"({"count": 1, "stations": [{)" + std::string{members} + "}]}";
}

constexpr std::string_view good_members =
    R"("id": "8729840", "name": "Pensacola", "lat": 30.4, "lng": -87.2,)"
    R"( "greatlakes": false)";

std::size_t kept(std::string_view members) {
  auto r = wc::parse_stations(list_of(members), ReadContext{});
  REQUIRE(r.has_value());
  return r->value.size();
}

double height(const DatumTable& t, VerticalDatum d) {
  const auto h = t.height_above_msl(d);
  REQUIRE(h.has_value());
  return h ? h->as(LengthUnit::meter) : 0.0;
}

}  // namespace

// ---- station lists
// ----------------------------------------------------------------

TEST_CASE("the water-level list: stations, validity and sensors",
          "[io][wire][coops]") {
  auto r = wc::parse_stations(coops_fixture("stations_waterlevels.json"),
                              ReadContext{});
  REQUIRE(r.has_value());
  CHECK(r->warnings.empty());
  const auto& all = r->value;
  REQUIRE(all.size() == 4);

  const wc::StationRecord& pensacola = record(all, "8729840");
  CHECK(pensacola.station.name.view() == "Pensacola");
  CHECK(pensacola.station.location.lat() == 30.4044);
  CHECK(pensacola.station.location.lon() == -87.2112);
  CHECK(pensacola.station.validity.first() == on_day(1923, 4, 30));
  CHECK(pensacola.observed == cc::ObservedSet{cc::Observed::six_minute,
                                              cc::Observed::hourly,
                                              cc::Observed::one_minute});
  CHECK(pensacola.met == cc::MetSet{cc::Met::wind, cc::Met::air_temperature,
                                    cc::Met::water_temperature,
                                    cc::Met::air_pressure});
  CHECK(pensacola.datums.empty());
  CHECK(not pensacola.air_gap);

  // An inactive Tsunami WL sensor offers no 1-minute series.
  CHECK(record(all, "8771972").observed ==
        cc::ObservedSet{cc::Observed::six_minute, cc::Observed::hourly});
  const wc::StationRecord& buffalo = record(all, "9063020");
  // Great Lakes: the list says IGLD is served, as datums.json does not.
  CHECK(buffalo.datums == cc::DatumSet{cc::Datum::igld85});
  CHECK(buffalo.station.validity.first() == on_day(1860, 4, 1));
  CHECK(buffalo.met.contains(cc::Met::humidity));
}

TEST_CASE("an ongoing station has no last day, not 2050",
          "[io][wire][coops][regression][L9]") {
  // v4's station list said "present" and stored 2050.
  for (const wc::StationRecord& s :
       stations(coops_fixture("stations_waterlevels.json"))) {
    CHECK(not s.station.validity.last().has_value());
  }
  const auto ended =
      stations(list_of(std::string{good_members} +
                       R"(, "details": {"established": "1990-01-02 00:00:00",)"
                       R"( "removed": "2001-03-04 12:00:00"})"));
  REQUIRE(ended.size() == 1);
  CHECK(ended[0].station.validity.first() == on_day(1990, 1, 2));
  CHECK(ended[0].station.validity.last() == on_day(2001, 3, 4));
  const auto unknown =
      stations(list_of(std::string{good_members} +
                       R"(, "details": {"established": "", "removed": null})"));
  REQUIRE(unknown.size() == 1);
  CHECK(unknown[0].station.validity == mov::core::ValidRange{});
}

TEST_CASE("the met and air-gap lists", "[io][wire][coops]") {
  auto met =
      wc::parse_stations(coops_fixture("stations_met.json"), ReadContext{});
  REQUIRE(met.has_value());
  REQUIRE(met->value.size() == 5);
  // Conductivity serves salinity too.
  const wc::StationRecord& seavey = record(met->value, "8419870");
  CHECK(seavey.met.contains(cc::Met::conductivity));
  CHECK(seavey.met.contains(cc::Met::salinity));
  CHECK(seavey.met.contains(cc::Met::humidity));
  CHECK(record(met->value, "8575437").met.contains(cc::Met::visibility));
  CHECK(record(met->value, "8575437").observed.empty());
  // "details": null: dates unknown, station kept.
  CHECK(record(met->value, "8775302").station.validity ==
        mov::core::ValidRange{});
  // "sensors": {"sensors": null}: no products, station kept.
  const wc::StationRecord& mclean = record(met->value, "9435373");
  CHECK(mclean.met.empty());
  CHECK(mclean.observed.empty());
  CHECK(met->warnings.empty());

  auto gap =
      wc::parse_stations(coops_fixture("stations_airgap.json"), ReadContext{});
  REQUIRE(gap.has_value());
  CHECK(record(gap->value, "8517986").air_gap);
  CHECK(record(gap->value, "8517986").met.empty());
  CHECK(not record(gap->value, "8767931").air_gap);  // its sensor is off
}

TEST_CASE("unknown sensor names are counted, by name", "[io][wire][coops]") {
  const std::string body = R"({"stations": [)"
                           R"({"id": "1000001", "lat": 1, "lng": 2, "sensors":)"
                           R"( {"sensors": [{"name": "Laser WL", "status": 1},)"
                           R"( {"name": "Wind", "status": 1}]}},)"
                           R"({"id": "1000002", "lat": 1, "lng": 2, "sensors":)"
                           R"( {"sensors": [{"name": "Laser WL", "status": 0},)"
                           R"( {"name": "Aardvark", "status": 1}]}}]})";
  auto r = wc::parse_stations(body, ReadContext{});
  REQUIRE(r.has_value());
  CHECK(r->value.size() == 2);
  CHECK(r->value[0].met == cc::MetSet{cc::Met::wind});
  CHECK(r->warnings ==
        std::vector<Warning>{
            {.code = WarningCode::unknown_sensor, .subject = "Aardvark"},
            {.code = WarningCode::unknown_sensor,
             .subject = "Laser WL",
             .count = 2}});
  // A station left out does not report its sensors.
  const auto skipped = wc::parse_stations(
      R"({"stations": [{"id": "1000001", "lat": 1, "lng": 2, "sensors":)"
      R"( {"sensors": [{"name": "Laser WL", "status": 1}, {"name": 1}]}}]})",
      ReadContext{});
  REQUIRE(skipped.has_value());
  CHECK(skipped->value.empty());
  CHECK(skipped->warnings ==
        std::vector<Warning>{
            {.code = WarningCode::station_skipped, .subject = "sensors"}});
}

TEST_CASE("a bad station is left out and counted, by reason",
          "[io][wire][coops]") {
  const std::string body =
      R"({"stations": [{"id": "TEC1603", "lat": 1, "lng": 2},)"
      R"({"id": "8729840", "lat": 91, "lng": 2},)"
      R"({"lat": 1, "lng": 2},)"
      R"(7,)"
      R"({"id": "8729842", "lat": 1, "lng": 2, "greatlakes": 1},)"
      R"({"id": "8729843", "lat": 1, "lng": 2, "details": {"removed": "x"}},)"
      R"({"id": "8729844", "name": 5, "lat": 1, "lng": 2},)"
      R"({"id": "8729841", "lat": 1, "lng": 2}]})";
  auto r = wc::parse_stations(body, ReadContext{});
  REQUIRE(r.has_value());
  REQUIRE(r->value.size() == 1);
  CHECK(r->value[0].station.id == id("8729841"));
  CHECK(r->value[0].station.name.view().empty());
  CHECK(r->warnings ==
        std::vector<Warning>{
            {.code = WarningCode::station_skipped, .subject = "id", .count = 2},
            {.code = WarningCode::station_skipped, .subject = "name"},
            {.code = WarningCode::station_skipped, .subject = "position"},
            {.code = WarningCode::station_skipped, .subject = "dates"},
            {.code = WarningCode::station_skipped,
             .subject = "member",
             .count = 2}});
}

TEST_CASE("each way a station can be bad", "[io][wire][coops]") {
  const std::string m{good_members};
  CHECK(kept(m) == 1);
  CHECK(kept(R"("id": "872984", "lat": 1, "lng": 2)") == 0);
  CHECK(kept(R"("id": 8729840, "lat": 1, "lng": 2)") == 0);
  CHECK(kept(R"("id": "8729840", "name": 5, "lat": 1, "lng": 2)") == 0);
  CHECK(kept(R"("id": "8729840", "name": "a\u0000b", "lat": 1, "lng": 2)") ==
        0);
  CHECK(kept(R"("id": "8729840", "lat": "30.4", "lng": 2)") == 0);
  CHECK(kept(R"("id": "8729840", "lng": 2)") == 0);
  CHECK(kept(R"("id": "8729840", "lat": 1, "lng": 400)") == 0);
  CHECK(kept(R"("id": "8729840", "lat": 1, "lng": 2, "greatlakes": "no")") ==
        0);
  CHECK(kept(m + R"(, "details": 5)") == 0);
  CHECK(kept(m + R"(, "details": {"established": 1900})") == 0);
  CHECK(kept(m + R"(, "details": {"established": "1900-13-01 00:00:00"})") ==
        0);
  CHECK(kept(m + R"(, "details": {"established": "2001-01-02 00:00:00",)"
                 R"( "removed": "2001-01-01 00:00:00"})") == 0);
  CHECK(kept(m + R"(, "sensors": 5)") == 0);
  CHECK(kept(m + R"(, "sensors": {"sensors": {}})") == 0);
  CHECK(kept(m + R"(, "sensors": {"sensors": [5]})") == 0);
  CHECK(kept(m + R"(, "sensors": {"sensors": [{"name": "Wind"}]})") == 0);
  CHECK(kept(m + R"(, "sensors": {"sensors": [{"name": "Wind",)"
                 R"( "status": "1"}]})") == 0);
  CHECK(kept(m + R"(, "sensors": {"sensors": [{"name": 1, "status": 1}]})") ==
        0);
  // Status 1 only; a negative or large status is inactive, not an error.
  CHECK(
      kept(
          m +
          R"(, "sensors": {"sensors": [{"name": "Wind",)"
          R"( "status": -1}, {"name": "Wind", "status": 18446744073709551615}]})") ==
      1);
}

TEST_CASE("a list without a stations array", "[io][wire][coops]") {
  CHECK(parse_error_in(wc::parse_stations(R"({"count": 0})", ReadContext{}))
            .code() == ParseErrc::bad_member);
  CHECK(
      parse_error_in(wc::parse_stations(R"({"stations": null})", ReadContext{}))
          .code() == ParseErrc::bad_member);
  CHECK(
      parse_error_in(wc::parse_stations(coops_fixture("mdapi_error_type.json"),
                                        ReadContext{}))
          .code() == ParseErrc::bad_member);
  CHECK(parse_error_in(wc::parse_stations("", ReadContext{})).code() ==
        ParseErrc::empty_input);
  CHECK(parse_error_in(wc::parse_stations(R"({"stations": [)", ReadContext{}))
            .code() == ParseErrc::not_json);
  // A repeated member inside a station refuses the list: the source is
  // broken, not the station.
  CHECK(parse_error_in(wc::parse_stations(list_of(std::string{good_members} +
                                                  R"(, "id": "8729841")"),
                                          ReadContext{}))
            .code() == ParseErrc::bad_member);
  auto empty = wc::parse_stations(R"({"stations": []})", ReadContext{});
  REQUIRE(empty.has_value());
  CHECK(empty->value.empty());
}

TEST_CASE("station lists count sixteen elements a station",
          "[io][wire][coops]") {
  ReadContext ctx;
  ctx.limits.max_elements = 63;
  const auto r =
      wc::parse_stations(coops_fixture("stations_waterlevels.json"), ctx);
  CHECK(parse_error_in(r).code() == ParseErrc::too_large);
  CHECK(parse_error_in(r).context() == "64 elements, limit 63");
  ctx.limits.max_elements = 64;
  CHECK(wc::parse_stations(coops_fixture("stations_waterlevels.json"), ctx)
            .has_value());
  ReadContext stop;
  stop.stop = mov::core::StopToken{[] { return true; }};
  const auto cancelled = wc::parse_prediction_stations(
      coops_fixture("stations_tidepredictions.json"), stop);
  REQUIRE(not cancelled.has_value());
  CHECK(std::holds_alternative<mov::core::Cancelled>(cancelled.error()));
}

// ---- the tide-prediction list
// -----------------------------------------------------------

TEST_CASE("the prediction type of each station", "[io][wire][coops]") {
  auto r = wc::parse_prediction_stations(
      coops_fixture("stations_tidepredictions.json"), ReadContext{});
  REQUIRE(r.has_value());
  CHECK(r->value ==
        std::vector<wc::PredictionStation>{
            {.id = id("1610367"), .type = wc::PredictionType::subordinate},
            {.id = id("1612340"), .type = wc::PredictionType::harmonic},
            {.id = id("8218617"), .type = wc::PredictionType::subordinate},
            {.id = id("8729840"), .type = wc::PredictionType::harmonic}});
  CHECK(r->warnings ==
        std::vector<Warning>{
            {.code = WarningCode::station_skipped, .subject = "id"}});
  auto odd = wc::parse_prediction_stations(
      R"({"stations": [{"id": "1000001", "type": "X"},)"
      R"( {"id": "1000002"}, {"id": "1000003", "type": 1},)"
      R"( {"id": "1000004", "type": "S"}]})",
      ReadContext{});
  REQUIRE(odd.has_value());
  CHECK(odd->value.size() == 1);
  CHECK(odd->warnings ==
        std::vector<Warning>{{.code = WarningCode::station_skipped,
                              .subject = "type",
                              .count = 3}});
  CHECK(parse_error_in(
            wc::parse_prediction_stations(R"({"x": []})", ReadContext{}))
            .code() == ParseErrc::bad_member);
  // An element that is not an object is a member of the wrong type.
  auto scalar =
      wc::parse_prediction_stations(R"({"stations": [7]})", ReadContext{});
  REQUIRE(scalar.has_value());
  CHECK(scalar->warnings ==
        std::vector<Warning>{
            {.code = WarningCode::station_skipped, .subject = "member"}});
  ReadContext small;
  small.limits.max_elements = 16;
  CHECK(
      parse_error_in(wc::parse_prediction_stations(
                         coops_fixture("stations_tidepredictions.json"), small))
          .code() == ParseErrc::too_large);
}

// ---- datums.json
// -------------------------------------------------------------------------

TEST_CASE("datums.json: the table relative to STND and the datums offered",
          "[io][wire][coops]") {
  auto r = wc::parse_datums(coops_fixture("datums.json"), ReadContext{});
  REQUIRE(r.has_value());
  CHECK(r->warnings.empty());
  CHECK(r->value.offered == cc::DatumSet{cc::Datum::stnd, cc::Datum::mhhw,
                                         cc::Datum::mhw, cc::Datum::mtl,
                                         cc::Datum::msl, cc::Datum::mlw,
                                         cc::Datum::mllw, cc::Datum::navd88});
  REQUIRE(r->value.table.has_value());
  if (r->value.table) {
    const DatumTable& t = *r->value.table;
    // Pensacola: MSL 2.757 above STND, MLLW 2.569, NAVD88 2.667, MHHW 2.952.
    CHECK(height(t, VerticalDatum::stnd) == -2.757);
    CHECK(std::abs(height(t, VerticalDatum::mllw) - (2.569 - 2.757)) < 1e-12);
    CHECK(std::abs(height(t, VerticalDatum::navd88) - (2.667 - 2.757)) < 1e-12);
    CHECK(std::abs(height(t, VerticalDatum::mhhw) - (2.952 - 2.757)) < 1e-12);
    CHECK(not t.height_above_msl(VerticalDatum::igld85).has_value());
  }
}

TEST_CASE("a station without MSL has no table, and that is not an error",
          "[io][wire][coops]") {
  // Buffalo lists only its low water datum.
  auto lakes =
      wc::parse_datums(coops_fixture("datums_great_lakes.json"), ReadContext{});
  REQUIRE(lakes.has_value());
  CHECK(lakes->value ==
        wc::StationDatums{.offered = {}, .table = std::nullopt});
  CHECK(lakes->warnings.empty());
  // An air-gap station: "datums": null.
  auto none =
      wc::parse_datums(coops_fixture("datums_null.json"), ReadContext{});
  REQUIRE(none.has_value());
  CHECK(none->value == wc::StationDatums{.offered = {}, .table = std::nullopt});
}

TEST_CASE("datums.json names outside the table", "[io][wire][coops]") {
  auto r = wc::parse_datums(
      R"({"units": "meters", "datums": [)"
      R"({"name": "MSL", "value": 1.0}, {"name": "NGVD29", "value": 0.5},)"
      R"( {"name": "LAT", "value": 0.1}, {"name": "IGLD", "value": 174.0},)"
      R"( {"name": "MHW", "value": null}]})",
      ReadContext{});
  REQUIRE(r.has_value());
  // NGVD29 is in the table but never offered (decision 32); a null is no
  // height and no datum.
  CHECK(r->value.offered == cc::DatumSet{cc::Datum::msl, cc::Datum::igld85});
  REQUIRE(r->value.table.has_value());
  if (r->value.table) {
    CHECK(height(*r->value.table, VerticalDatum::ngvd29) == -0.5);
    CHECK(not r->value.table->height_above_msl(VerticalDatum::mhw).has_value());
  }
  CHECK(r->warnings == std::vector<Warning>{{.code = WarningCode::datum_unknown,
                                             .subject = "LAT"}});
}

TEST_CASE("hostile datums.json", "[io][wire][coops]") {
  const auto code = [](std::string_view body) {
    return parse_error_in(wc::parse_datums(body, ReadContext{})).code();
  };
  CHECK(code(R"({"units": "meters"})") == ParseErrc::bad_member);
  CHECK(code(R"({"units": "meters", "datums": {}})") == ParseErrc::bad_member);
  CHECK(code(R"({"units": "meters", "datums": [5]})") == ParseErrc::bad_member);
  CHECK(code(R"({"units": "meters", "datums": [{"value": 1}]})") ==
        ParseErrc::bad_member);
  CHECK(code(R"({"units": "meters", "datums": [{"name": "MSL"}]})") ==
        ParseErrc::bad_member);
  CHECK(
      code(
          R"({"units": "meters", "datums": [{"name": "MSL", "value": "1"}]})") ==
      ParseErrc::bad_member);
  CHECK(code(R"({"datums": []})") == ParseErrc::bad_member);
  CHECK(
      code(
          R"({"units": "meters", "datums": [{"name": "MSL", "value": 1e999}]})") ==
      ParseErrc::out_of_range);
  CHECK(code(R"({"units": "meters", "datums": [], "datums": []})") ==
        ParseErrc::bad_member);
  // English units would mislabel every height.
  const auto feet =
      wc::parse_datums(R"({"units": "feet", "datums": []})", ReadContext{});
  CHECK(format_error_in(feet).code == FormatErrc::noncanonical_unit);
  CHECK(format_error_in(feet).subject == "units: feet");
  // Two heights for one datum.
  const auto conflict = wc::parse_datums(
      R"({"units": "meters", "datums": [{"name": "MSL", "value": 1},)"
      R"( {"name": "IGLD", "value": 2}, {"name": "IGLD85", "value": 3}]})",
      ReadContext{});
  CHECK(format_error_in(conflict).code == FormatErrc::inconsistent_metadata);
  CHECK(format_error_in(conflict).subject == "IGLD85");
  // An equal repeat is fine.
  CHECK(
      wc::parse_datums(
          R"({"units": "meters", "datums": [{"name": "MSL", "value": 1},)"
          R"( {"name": "IGLD", "value": 2}, {"name": "IGLD85", "value": 2}]})",
          ReadContext{})
          .has_value());
  ReadContext small;
  small.limits.max_elements = 3;
  CHECK(parse_error_in(wc::parse_datums(coops_fixture("datums.json"), small))
            .code() == ParseErrc::too_large);
}
