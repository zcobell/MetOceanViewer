// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The USGS requests byte for byte, the next-link check (Verbatim) and the
// unit spellings (docs/providers-design.md sections 4.1 and 10.1).

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/provider/usgs.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/windows.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/usgs.hpp"
#include "usgs_test_support.hpp"
#include "wire_test_support.hpp"

namespace {

namespace cu = mov::core::usgs;
namespace wu = mov::io::wire::usgs;
using mov::io::WarningCode;
using mov::io::wire::Composed;
using mov::io::wire::LinkError;
using mov::io::wire::Origin;
using mov::io::wire::QueryParam;
using mov::io::wire::Target;
using mov::io::wire::Verbatim;
using mov::test::day;
using mov::test::must;
using mov::test::usgs_station;
using mov::test::utc;

using Query = std::vector<QueryParam>;

constexpr std::string_view series_properties =
    "id,parameter_code,parameter_name,statistic_id,primary,unit_of_measure,"
    "begin,end,sublocation_identifier,web_description";

cu::TimeSeriesId series_id(std::string_view text) {
  return must(cu::TimeSeriesId::make(text));
}

cu::ParameterCode parameter(std::string_view text) {
  return must(cu::ParameterCode::make(text));
}

mov::core::TimeRange range(mov::core::Time b, mov::core::Time e) {
  return must(mov::core::TimeRange::make(b, e));
}

mov::core::DayRange days(std::chrono::local_days first,
                         std::chrono::local_days end) {
  return must(mov::core::DayRange::make(first, end));
}

// The datetime parameter of a data target.
std::string datetime_of(const Composed& c) {
  for (const QueryParam& p : c.query) {
    if (p.name == "datetime") {
      return p.value;
    }
  }
  FAIL("no datetime parameter");
  return {};
}

constexpr std::string_view discharge = "66a4e3fb8abe42fcb0942b5c0fe98f68";
constexpr std::string_view continuous_items =
    "https://api.waterdata.usgs.gov/ogcapi/v1/collections/continuous/items";

}  // namespace

// ---- resolution and catalog
// -------------------------------------------------

TEST_CASE("resolution asks combined-metadata for the site's series",
          "[io][usgs][targets]") {
  const cu::Continuous by_parameter{
      .station = usgs_station(),
      .series = cu::ByParameter{.parameter = parameter("00060")},
      .range = range(utc(2026, 10, 1), utc(2026, 10, 2))};
  CHECK(wu::resolve_target(by_parameter) ==
        Composed{.origin = Origin::usgs,
                 .path = "/ogcapi/v1/collections/combined-metadata/items",
                 .query = Query{{"monitoring_location_id", "USGS-01646500"},
                                {"parameter_code", "00060"},
                                {"statistic_id", "00011"},
                                {"properties", std::string{series_properties}},
                                {"skipGeometry", "true"},
                                {"limit", "50000"},
                                {"f", "json"}}});

  const cu::Continuous by_id{
      .station = usgs_station(),
      .series = cu::ByTimeSeries{.id = series_id(discharge)},
      .range = by_parameter.range};
  CHECK(wu::resolve_target(by_id).query ==
        Query{{"monitoring_location_id", "USGS-01646500"},
              {"id", std::string{discharge}},
              {"properties", std::string{series_properties}},
              {"skipGeometry", "true"},
              {"limit", "50000"},
              {"f", "json"}});

  const cu::Daily daily_mean{
      .station = usgs_station("USGS-02492000"),
      .series = cu::DailyByParameter{.parameter = parameter("00065"),
                                     .statistic = cu::StatisticId::mean()},
      .days = days(day(2026, 9, 1), day(2026, 10, 1))};
  CHECK(wu::resolve_target(daily_mean).query ==
        Query{{"monitoring_location_id", "USGS-02492000"},
              {"parameter_code", "00065"},
              {"statistic_id", "00003"},
              {"properties", std::string{series_properties}},
              {"skipGeometry", "true"},
              {"limit", "50000"},
              {"f", "json"}});
  const cu::Daily daily_id{
      .station = usgs_station(),
      .series = cu::ByTimeSeries{.id = series_id(discharge)},
      .days = daily_mean.days};
  CHECK(wu::resolve_target(daily_id).query.at(1) ==
        QueryParam{"id", std::string{discharge}});
}

TEST_CASE("the site's series leave out field measurements and peaks",
          "[io][usgs][targets]") {
  const Composed c = wu::site_series_target(usgs_station());
  CHECK(c.path == "/ogcapi/v1/collections/combined-metadata/items");
  CHECK(c.query == Query{{"monitoring_location_id", "USGS-01646500"},
                         {"data_type", "Continuous values,Daily values"},
                         {"properties", std::string{series_properties}},
                         {"skipGeometry", "true"},
                         {"limit", "50000"},
                         {"f", "json"}});
}

TEST_CASE("the catalog asks for the map parameters with their locations",
          "[io][usgs][targets]") {
  const Composed c = wu::catalog_target();
  CHECK(c.origin == Origin::usgs);
  CHECK(c.path == "/ogcapi/v1/collections/combined-metadata/items");
  // Geometry kept: no skipGeometry.
  CHECK(c.query == Query{{"parameter_code", "00060,00065,62620,62619"},
                         {"statistic_id", "00011"},
                         {"properties",
                          "monitoring_location_id,monitoring_location_name,id,"
                          "parameter_code,parameter_name,statistic_id,primary,"
                          "unit_of_measure,begin,end,sublocation_identifier,"
                          "web_description"},
                         {"limit", "50000"},
                         {"f", "json"}});
}

// ---- data
// --------------------------------------------------------------------------

TEST_CASE("a continuous window is sent as the whole seconds it holds",
          "[io][usgs][targets][regression][L6]") {
  const auto series = mov::test::continuous_epoch();
  const auto target =
      must(wu::data_target(series, range(utc(2026, 10, 1), utc(2026, 10, 2))));
  CHECK(target ==
        Composed{.origin = Origin::usgs,
                 .path = "/ogcapi/v1/collections/continuous/items",
                 .query = Query{
                     {"time_series_id", std::string{discharge}},
                     {"datetime", "2026-10-01T00:00:00Z/2026-10-01T23:59:59Z"},
                     {"properties", "time,value,approval_status,qualifier"},
                     {"skipGeometry", "true"},
                     {"limit", "50000"},
                     {"f", "json"}}});
  // The range as asked: no day added (v4 bug L6), no `sortby` (the API then
  // serves the first page only).
  for (const QueryParam& p : target.query) {
    CHECK(p.name != "sortby");
  }
}

TEST_CASE("adjacent continuous windows share no second",
          "[io][usgs][targets][regression][L6]") {
  const auto series = mov::test::continuous_epoch();
  const mov::core::Time b = utc(2023, 1, 1);
  const auto windows = mov::core::split(
      range(b, utc(2026, 1, 1)),
      cu::max_window(
          cu::Continuous{.station = usgs_station(),
                         .series = cu::ByTimeSeries{.id = series_id(discharge)},
                         .range = range(b, utc(2026, 1, 1))}));
  REQUIRE(windows.size() == 2);
  CHECK(datetime_of(must(wu::data_target(series, windows[0]))) ==
        "2023-01-01T00:00:00Z/2025-09-26T23:59:59Z");
  CHECK(datetime_of(must(wu::data_target(series, windows[1]))) ==
        "2025-09-27T00:00:00Z/2025-12-31T23:59:59Z");
}

TEST_CASE("a window off the second grid is cut to its whole seconds",
          "[io][usgs][targets]") {
  const auto series = mov::test::continuous_epoch();
  using std::chrono::milliseconds;
  const mov::core::Time b = utc(2026, 10, 1, 0, 0, 0) + milliseconds{250};
  const mov::core::Time e = utc(2026, 10, 1, 0, 0, 2) + milliseconds{500};
  CHECK(datetime_of(must(wu::data_target(series, range(b, e)))) ==
        "2026-10-01T00:00:01Z/2026-10-01T00:00:02Z");
  // A window inside one second holds none.
  // A window inside one second holds none; the error names the window.
  const auto inside = range(b, b + milliseconds{500});
  CHECK(wu::data_target(series, inside) ==
        std::unexpected{wu::TargetError{
            .reason = wu::TargetErrc::no_whole_second, .window = inside}});
  // datetime has four-digit years.
  const auto far = range(utc(9999, 12, 31), utc(10000, 1, 2));
  CHECK(
      wu::data_target(series, far) ==
      std::unexpected{wu::TargetError{
          .reason = wu::TargetErrc::outside_four_digit_years, .window = far}});
  CHECK(datetime_of(must(wu::data_target(
            series, range(utc(9999, 12, 31, 23, 59, 59), utc(10000, 1, 1))))) ==
        "9999-12-31T23:59:59Z/9999-12-31T23:59:59Z");
}

TEST_CASE("daily values are asked for by their first and last day",
          "[io][usgs][targets][regression][L6]") {
  const auto series = mov::test::daily_epoch();
  const auto target =
      must(wu::data_target(series, days(day(2026, 9, 28), day(2026, 10, 1))));
  CHECK(target ==
        Composed{.origin = Origin::usgs,
                 .path = "/ogcapi/v1/collections/daily/items",
                 .query = Query{
                     {"time_series_id", "2cf409b0d9694c01828f5d4ff14586a5"},
                     {"datetime", "2026-09-28/2026-09-30"},
                     {"properties", "time,value,approval_status,qualifier"},
                     {"skipGeometry", "true"},
                     {"limit", "50000"},
                     {"f", "json"}}});
  // One day is that day.
  CHECK(datetime_of(must(wu::data_target(
            series, days(day(2024, 2, 29), day(2024, 3, 1))))) ==
        "2024-02-29/2024-02-29");
  CHECK(datetime_of(must(
            wu::data_target(series, days(day(0, 1, 1), day(10000, 1, 1))))) ==
        "0000-01-01/9999-12-31");
  const auto before = days(day(-1, 12, 31), day(2026, 1, 1));
  CHECK(wu::data_target(series, before) ==
        std::unexpected{
            wu::TargetError{.reason = wu::TargetErrc::outside_four_digit_years,
                            .window = before}});
  const auto after = days(day(2026, 1, 1), day(10000, 1, 2));
  CHECK(wu::data_target(series, after) ==
        std::unexpected{
            wu::TargetError{.reason = wu::TargetErrc::outside_four_digit_years,
                            .window = after}});
}

// ---- next links
// ----------------------------------------------------------------

TEST_CASE("a next link is followed only on its origin and path",
          "[io][usgs][verbatim]") {
  const std::string next = std::string{continuous_items} +
                           "?cursor=NjZh&time_series_id=66a4&limit=2&f=json";
  const auto pinned = wu::items_path(wu::Endpoint::continuous);
  const Verbatim link = must(Verbatim::make(Origin::usgs, pinned, next));
  CHECK(link.url() == next);
  CHECK(link.origin() == Origin::usgs);
  CHECK(mov::io::wire::origin_of(Target{link}) == Origin::usgs);
  CHECK(mov::io::wire::origin_of(Target{wu::catalog_target()}) == Origin::usgs);
  // The path alone, and percent escapes in the query.
  CHECK(Verbatim::make(Origin::usgs, pinned, continuous_items).has_value());
  CHECK(Verbatim::make(Origin::usgs, pinned,
                       std::string{continuous_items} +
                           "?datetime=2026-10-01T00%3A00%3A00Z%2F")
            .has_value());

  const auto refused = [&](std::string_view url) {
    const auto r = Verbatim::make(Origin::usgs, pinned, url);
    REQUIRE(not r.has_value());
    return r.error();
  };
  // Another scheme, host, port or user: not the origin (N B1).
  CHECK(refused("http://api.waterdata.usgs.gov/ogcapi/v1/collections/"
                "continuous/items?cursor=x") == LinkError::other_origin);
  CHECK(refused("https://api.waterdata.usgs.gov.evil.example/ogcapi/v1/"
                "collections/continuous/items") == LinkError::other_origin);
  CHECK(refused("https://api.waterdata.usgs.gov:8443/ogcapi/v1/collections/"
                "continuous/items") == LinkError::other_origin);
  CHECK(refused("https://api.waterdata.usgs.gov@evil.example/ogcapi/v1/"
                "collections/continuous/items") == LinkError::other_origin);
  CHECK(refused("https://api.waterdata.usgs.gov") == LinkError::other_origin);
  CHECK(refused("https://api.tidesandcurrents.noaa.gov/ogcapi/v1/collections/"
                "continuous/items") == LinkError::other_origin);
  // Another collection or path.
  CHECK(refused("https://api.waterdata.usgs.gov/ogcapi/v1/collections/daily/"
                "items?cursor=x") == LinkError::other_path);
  CHECK(refused("https://api.waterdata.usgs.gov/ogcapi/v1/collections/"
                "continuous/items/") == LinkError::other_path);
  CHECK(refused("https://api.waterdata.usgs.gov/ogcapi/v1/collections/"
                "continuous/../daily/items") == LinkError::other_path);
  CHECK(refused("https://api.waterdata.usgs.gov/ogcapi/v1/collections/"
                "continuous%2Fitems") == LinkError::other_path);
  // Bytes no URI has, a fragment, a broken escape.
  CHECK(refused(std::string{continuous_items} + "?a=b c") ==
        LinkError::bad_character);
  CHECK(refused(std::string{continuous_items} + "#frag") ==
        LinkError::bad_character);
  CHECK(refused(std::string{continuous_items} + "?a=[1]") ==
        LinkError::bad_character);
  CHECK(refused(std::string{continuous_items} + "?a=%zz") ==
        LinkError::bad_character);
  CHECK(refused(std::string{continuous_items} + "?a=%2") ==
        LinkError::bad_character);
  CHECK(refused("https:\\\\api.waterdata.usgs.gov/") ==
        LinkError::bad_character);
  CHECK(refused(std::string{continuous_items} + "?a=\xC3\xA9") ==
        LinkError::bad_character);
  CHECK(refused(std::string(Verbatim::max_bytes + 1, 'a')) ==
        LinkError::too_long);
}

// ---- units
// -------------------------------------------------------------------------

TEST_CASE("USGS unit spellings", "[io][usgs][units]") {
  const auto unit_of = [](std::string_view text) {
    const auto r = wu::unit_of_measure(text);
    CHECK(r.warnings.empty());
    return r.value;
  };
  using mov::core::parse_unit;
  CHECK(unit_of("ft^3/s") == parse_unit("cfs"));
  CHECK(unit_of("ft3/s") == parse_unit("cfs"));
  CHECK(unit_of("m^3/s") == parse_unit("m3/s"));
  CHECK(unit_of("m3/sec") == parse_unit("m3/s"));
  CHECK(unit_of("ft") == parse_unit("feet"));
  CHECK(unit_of("degC") == parse_unit("deg C"));
  CHECK(unit_of("deg C") == parse_unit("degC"));
  CHECK(unit_of("degF") == parse_unit("deg F"));
  CHECK(unit_of("ft/sec") == parse_unit("ft/s"));
  CHECK(unit_of("ft/s") == parse_unit("ft/s"));
  CHECK(unit_of("mph") == parse_unit("mph"));
  CHECK(unit_of("Deg") == mov::core::degree());
  CHECK(unit_of("deg") == mov::core::degree());
  CHECK(unit_of("in") == parse_unit("in"));
  CHECK(unit_of("uS/cm") == parse_unit("uS/cm"));
  CHECK(unit_of("%") == parse_unit("percent"));
  CHECK(not wu::unit_of_measure(std::nullopt).value.has_value());
  CHECK(not wu::unit_of_measure("  ").value.has_value());
  // Kept as text, and said.
  const auto turbidity = wu::unit_of_measure("_FNU");
  REQUIRE(turbidity.value.has_value());
  CHECK(turbidity.warnings ==
        std::vector<mov::io::Warning>{
            {.code = WarningCode::unrecognized_unit, .subject = "_FNU"}});
}
