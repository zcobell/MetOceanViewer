// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The CO-OPS request targets, byte for byte (docs/providers-design.md section
// 4.1).

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/provider/coops.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/windows.hpp"
#include "mov/io/wire/coops.hpp"
#include "mov/io/wire/origin.hpp"
#include "wire_test_support.hpp"

namespace {

namespace cc = mov::core::coops;
namespace wc = mov::io::wire::coops;
using mov::core::Time;
using mov::core::TimeRange;
using mov::io::wire::Composed;
using mov::io::wire::Origin;
using mov::io::wire::QueryParam;
using mov::test::utc;
using Station = mov::core::StationId<mov::core::provider::Coops>;

Station pensacola() {
  const auto id = Station::make("8729840");
  REQUIRE(id.has_value());
  return *id;
}

TimeRange range(Time a, Time b) {
  const auto r = TimeRange::make(a, b);
  REQUIRE(r.has_value());
  return *r;
}

// The query as "name=value&..." (unencoded), for compact comparisons.
std::string query_text(const Composed& c) {
  std::string out;
  for (const QueryParam& p : c.query) {
    out += (out.empty() ? "" : "&") + p.name + "=" + p.value;
  }
  return out;
}

wc::WindowTargets targets_of(const cc::Product& product, TimeRange window) {
  auto t = wc::targets(pensacola(), product, window);
  REQUIRE(t.has_value());
  return *std::move(t);
}

// The one exchange of a single product.
wc::SingleTarget single(const cc::Product& product, TimeRange window) {
  const wc::WindowTargets t = targets_of(product, window);
  const auto* one = std::get_if<wc::SingleTarget>(&t);
  REQUIRE(one != nullptr);
  return *one;
}

// The two exchanges of a pair.
wc::PairTargets pair(const cc::Product& product, TimeRange window) {
  const wc::WindowTargets t = targets_of(product, window);
  const auto* two = std::get_if<wc::PairTargets>(&t);
  REQUIRE(two != nullptr);
  return *two;
}

TimeRange january() { return range(utc(2026, 1, 1), utc(2026, 2, 1)); }

cc::Product water_level(cc::Observed s, cc::Datum d) {
  return cc::WaterLevel{.kind = cc::ObservedLevel{.series = s}, .datum = d};
}

const cc::Product humidity = cc::Meteorological{
    .product = cc::Met::humidity, .interval = cc::MetInterval::six_minute};

}  // namespace

TEST_CASE("an observed water level asks for its product and datum",
          "[io][wire][coops]") {
  const auto t =
      single(water_level(cc::Observed::six_minute, cc::Datum::mllw), january());
  CHECK(t.target.origin == Origin::coops_data);
  CHECK(t.target.path == "/api/prod/datagetter");
  CHECK(query_text(t.target) ==
        "product=water_level&application=MetOceanViewer&station=8729840"
        "&begin_date=20260101 00:00&end_date=20260131 23:59&datum=MLLW"
        "&time_zone=gmt&units=metric&format=json");
  CHECK(t.series ==
        wc::Series{wc::Observation{.series = cc::Observed::six_minute,
                                   .datum = cc::Datum::mllw}});
}

TEST_CASE("datums go out as the API spells them", "[io][wire][coops]") {
  const auto with = [](cc::Datum d) {
    return query_text(
        single(water_level(cc::Observed::hourly, d), january()).target);
  };
  CHECK(with(cc::Datum::navd88).find("&datum=NAVD&") != std::string::npos);
  CHECK(with(cc::Datum::igld85).find("&datum=IGLD&") != std::string::npos);
  CHECK(with(cc::Datum::stnd).find("&datum=STND&") != std::string::npos);
  CHECK(with(cc::Datum::stnd).starts_with("product=hourly_height&"));
}

TEST_CASE("predictions carry their own datum and interval",
          "[io][wire][coops][regression][L10]") {
  const auto t = single(
      cc::WaterLevel{
          .kind = cc::PredictedLevel{cc::PredictionInterval::fifteen},
          .datum = cc::Datum::navd88},
      january());
  CHECK(query_text(t.target) ==
        "product=predictions&application=MetOceanViewer&station=8729840"
        "&begin_date=20260101 00:00&end_date=20260131 23:59&datum=NAVD"
        "&interval=15&time_zone=gmt&units=metric&format=json");
}

TEST_CASE("observed versus predicted is two exchanges on one grid",
          "[io][wire][coops][regression][L10]") {
  const auto t = pair(
      cc::WaterLevel{.kind = cc::ObservedVsPredicted{cc::PairedGrid::hourly},
                     .datum = cc::Datum::msl},
      january());
  CHECK(t.pair ==
        wc::Pair{.grid = cc::PairedGrid::hourly, .datum = cc::Datum::msl});
  CHECK(query_text(t.observed) ==
        "product=hourly_height&application=MetOceanViewer&station=8729840"
        "&begin_date=20260101 00:00&end_date=20260131 23:59&datum=MSL"
        "&time_zone=gmt&units=metric&format=json");
  CHECK(query_text(t.predicted) ==
        "product=predictions&application=MetOceanViewer&station=8729840"
        "&begin_date=20260101 00:00&end_date=20260131 23:59&datum=MSL"
        "&interval=h&time_zone=gmt&units=metric&format=json");
  const auto six = pair(
      cc::WaterLevel{
          .kind = cc::ObservedVsPredicted{cc::PairedGrid::six_minute},
          .datum = cc::Datum::mllw},
      january());
  // The 6-minute default interval is not sent.
  CHECK(query_text(six.predicted).find("interval") == std::string::npos);
  CHECK(query_text(six.observed).starts_with("product=water_level&"));
}

TEST_CASE("met products send no datum, and h only when hourly",
          "[io][wire][coops]") {
  const auto six =
      single(cc::Meteorological{.product = cc::Met::wind,
                                .interval = cc::MetInterval::six_minute},
             january());
  CHECK(query_text(six.target) ==
        "product=wind&application=MetOceanViewer&station=8729840"
        "&begin_date=20260101 00:00&end_date=20260131 23:59"
        "&time_zone=gmt&units=metric&format=json");
  const auto hourly =
      single(cc::Meteorological{.product = cc::Met::air_temperature,
                                .interval = cc::MetInterval::hourly},
             january());
  CHECK(query_text(hourly.target) ==
        "product=air_temperature&application=MetOceanViewer&station=8729840"
        "&begin_date=20260101 00:00&end_date=20260131 23:59&interval=h"
        "&time_zone=gmt&units=metric&format=json");
}

TEST_CASE("air gap sends neither a datum nor an interval",
          "[io][wire][coops]") {
  const auto gap = single(cc::AirGap{}, january());
  CHECK(query_text(gap.target) ==
        "product=air_gap&application=MetOceanViewer&station=8729840"
        "&begin_date=20260101 00:00&end_date=20260131 23:59"
        "&time_zone=gmt&units=metric&format=json");
}

TEST_CASE("adjacent windows never share a boundary minute",
          "[io][wire][coops][regression][L1]") {
  // v4 sent [a, b] and then [b, c], so the sample at b came twice.
  const cc::Product product =
      water_level(cc::Observed::six_minute, cc::Datum::mllw);
  const auto r = range(utc(2026, 1, 1), utc(2026, 3, 15, 6, 30));
  const auto whole = mov::core::aligned(r, cc::grain).value_or(r);
  const auto windows = mov::core::split(whole, cc::max_window(product));
  REQUIRE(windows.size() == 3);
  std::vector<std::string> bounds;
  for (const TimeRange w : windows) {
    const auto t = single(product, w);
    bounds.push_back(t.target.query[3].value);
    bounds.push_back(t.target.query[4].value);
  }
  CHECK(bounds == std::vector<std::string>{"20260101 00:00", "20260131 23:59",
                                           "20260201 00:00", "20260303 23:59",
                                           "20260304 00:00", "20260315 06:29"});
}

TEST_CASE("a window is sent as the whole minutes it holds",
          "[io][wire][coops]") {
  using std::chrono::milliseconds;
  const auto t =
      single(humidity, range(utc(2026, 5, 4, 3, 2) + milliseconds{1},
                             utc(2026, 5, 4, 3, 9) + milliseconds{1}));
  CHECK(t.target.query[3].value == "20260504 03:03");
  CHECK(t.target.query[4].value == "20260504 03:09");
  // Before the epoch too: a whole minute is floored, never rounded toward 0.
  const auto old =
      single(humidity, range(utc(1960, 12, 31, 23, 58), utc(1961, 1, 1)));
  CHECK(old.target.query[3].value == "19601231 23:58");
  CHECK(old.target.query[4].value == "19601231 23:59");
}

TEST_CASE("a window without a whole minute, or past year 9999, is refused",
          "[io][wire][coops]") {
  using std::chrono::seconds;
  const auto refused = [](TimeRange w) {
    return wc::targets(pensacola(), humidity, w);
  };
  const TimeRange none =
      range(utc(2026, 5, 4, 3, 2, 1), utc(2026, 5, 4, 3, 2, 59));
  CHECK(refused(none) ==
        std::unexpected{wc::TargetError{
            .reason = wc::TargetErrc::no_whole_minute, .window = none}});
  const TimeRange late =
      range(utc(9999, 12, 31), utc(9999, 12, 31) + std::chrono::days{2});
  CHECK(refused(late) == std::unexpected{wc::TargetError{
                             .reason = wc::TargetErrc::outside_four_digit_years,
                             .window = late}});
  const TimeRange early = range(utc(0, 1, 1) - seconds{60}, utc(0, 1, 1, 1));
  CHECK(refused(early).error().reason ==
        wc::TargetErrc::outside_four_digit_years);
  // The last minute that has a four-digit year.
  const auto last =
      single(humidity, range(utc(9999, 12, 31, 23, 59),
                             utc(9999, 12, 31) + std::chrono::days{1}));
  CHECK(last.target.query[3].value == "99991231 23:59");
  CHECK(last.target.query[4].value == "99991231 23:59");
  const auto first = single(humidity, range(utc(0, 1, 1), utc(0, 1, 1, 0, 1)));
  CHECK(first.target.query[3].value == "00000101 00:00");
}

TEST_CASE("station lists and datums are metadata targets",
          "[io][wire][coops]") {
  CHECK(wc::station_list_target(wc::StationList::water_levels) ==
        Composed{.origin = Origin::coops_metadata,
                 .path = "/mdapi/prod/webapi/stations.json",
                 .query = {{.name = "type", .value = "waterlevels"},
                           {.name = "expand", .value = "details,sensors"}}});
  CHECK(query_text(wc::station_list_target(wc::StationList::met)) ==
        "type=met&expand=details,sensors");
  CHECK(query_text(wc::station_list_target(wc::StationList::air_gap)) ==
        "type=airgap&expand=details,sensors");
  CHECK(query_text(wc::station_list_target(
            wc::StationList::tide_predictions)) == "type=tidepredictions");
  CHECK(wc::datums_target(pensacola()) ==
        Composed{.origin = Origin::coops_metadata,
                 .path = "/mdapi/prod/webapi/stations/8729840/datums.json",
                 .query = {{.name = "units", .value = "metric"}}});
  CHECK(wc::every_station_list.size() == 4);
}
