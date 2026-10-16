// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// usgs::read_series and read_catalog: combined-metadata pages, recorded and
// hostile (docs/providers-design.md sections 3.4, 4.3, 4.9 and 10.1).

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/usgs_json.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/service.hpp"
#include "mov/io/wire/usgs.hpp"
#include "usgs_test_support.hpp"
#include "wire_test_support.hpp"

namespace {

namespace cu = mov::core::usgs;
namespace wu = mov::io::wire::usgs;
using mov::io::ParseErrc;
using mov::io::Read;
using mov::io::ReadContext;
using mov::io::Warning;
using mov::io::WarningCode;
using mov::test::must;
using mov::test::next_url;
using mov::test::parse_error_in;
using mov::test::usgs_fixture;

Read<wu::SeriesPage> series_page(std::string_view body) {
  auto r = mov::io::detail::usgs::parse_series(body, {});
  REQUIRE(r.has_value());
  return *std::move(r);
}

Read<wu::CatalogPage> catalog_page(std::string_view body) {
  auto r = mov::io::detail::usgs::parse_catalog(body, {});
  REQUIRE(r.has_value());
  return *std::move(r);
}

std::chrono::sys_days ymd(int y, unsigned m, unsigned d) {
  using namespace std::chrono;
  return sys_days{year{y} / month{m} / day{d}};
}

const cu::SeriesInfo* with_id(const std::vector<cu::SeriesInfo>& rows,
                              std::string_view id) {
  for (const cu::SeriesInfo& s : rows) {
    if (s.id.view() == id) {
      return &s;
    }
  }
  return nullptr;
}

std::string row_page(std::string_view properties,
                     std::string_view geometry = "null") {
  return R"({"type":"FeatureCollection","features":[{"type":"Feature",)"
         R"("properties":{)" +
         std::string{properties} + R"(},"geometry":)" + std::string{geometry} +
         "}]}";
}

constexpr std::string_view good_row =
    R"("id":"66a4e3fb8abe42fcb0942b5c0fe98f68","parameter_code":"00060",)"
    R"("parameter_name":"Discharge","statistic_id":"00011",)"
    R"("primary":"Primary","unit_of_measure":"ft^3/s",)"
    R"("begin":"1972-06-09T05:00:00+00:00","end":null)";

constexpr std::string_view site_fields =
    R"("monitoring_location_id":"USGS-01646500",)"
    R"("monitoring_location_name":"POTOMAC RIVER",)";

constexpr std::string_view point =
    R"({"type":"Point","coordinates":[-77.1,38.9]})";

cu::ParameterCode parameter(std::string_view code) {
  return must(cu::ParameterCode::make(code));
}

// A continuous request at USGS-01646500 over 2026-10-01.
cu::Continuous continuous_request(cu::ParameterCode code) {
  return {.station = mov::test::usgs_station(),
          .series = cu::ByParameter{.parameter = code},
          .range = must(mov::core::TimeRange::make(
              mov::test::utc(2026, 10, 1), mov::test::utc(2026, 10, 2)))};
}

// The series_skipped warning of `subject`, or a count of 0.
std::size_t skipped(const std::vector<Warning>& warnings,
                    std::string_view subject) {
  for (const Warning& w : warnings) {
    if (w.code == WarningCode::series_skipped and w.subject == subject) {
      return w.count;
    }
  }
  return 0;
}

}  // namespace

// ---- a site's series
// --------------------------------------------------------

TEST_CASE("resolution by id: one series, its unit and days",
          "[io][usgs][series]") {
  const auto r = series_page(usgs_fixture("series_by_id.json"));
  REQUIRE(r.value.series.size() == 1);
  const cu::SeriesInfo& s = r.value.series.front();
  CHECK(s.id.view() == "66a4e3fb8abe42fcb0942b5c0fe98f68");
  CHECK(s.parameter.view() == "00060");
  CHECK(s.statistic == cu::StatisticId::instantaneous());
  CHECK(s.parameter_name == "Discharge");
  CHECK(s.primary);
  CHECK(s.unit == mov::core::parse_unit("cfs"));
  // begin and end are instants (the site's midnight in UTC): their UTC days.
  CHECK(s.validity.first() == ymd(1972, 6, 9));
  CHECK(s.validity.last() == ymd(2026, 10, 15));
  CHECK(r.value.paging == wu::Paging{.features = 1, .next = wu::NoNext{}});
  CHECK(r.warnings.empty());
  // By parameter, the same row; a series of another site, none.
  CHECK(series_page(usgs_fixture("series_by_parameter.json")).value.series ==
        r.value.series);
  const auto none = series_page(usgs_fixture("series_none.json"));
  CHECK(none.value.series.empty());
  CHECK(none.value.paging.features == 0);
}

TEST_CASE("resolution: the rows make the site's series set",
          "[io][usgs][series]") {
  const auto rows = series_page(usgs_fixture("series_by_parameter.json"));
  const auto set = must(cu::SeriesSet::make(rows.value.series));
  const auto resolved =
      must(cu::resolve(continuous_request(parameter("00060")), set));
  REQUIRE(resolved.epochs().size() == 1);
  CHECK(resolved.epochs()[0].series() == rows.value.series.front());
  // Two Primary gage heights without a sublocation at one site
  // (USGS-12175000, web_description "Primary" since 2007 and "Secondary"
  // since 2023): one logical series of two epochs, the later-begun first.
  const auto several = series_page(usgs_fixture("series_several_primary.json"));
  REQUIRE(several.value.series.size() == 2);
  CHECK(several.value.series[0].description == "Primary");
  CHECK(several.value.series[1].description == "Secondary");
  const auto stitched =
      must(cu::resolve(continuous_request(parameter("00065")),
                       must(cu::SeriesSet::make(several.value.series))));
  REQUIRE(stitched.epochs().size() == 2);
  CHECK(stitched.epochs()[0].series().description == "Secondary");
  CHECK(stitched.epochs()[1].series().description == "Primary");
}

TEST_CASE("sublocations are groups to choose from", "[io][usgs][series]") {
  // USGS-02431011: gage heights at Headwater, Tailwater and Lock Chamber.
  const auto lock = series_page(usgs_fixture("series_sublocations.json"));
  REQUIRE(lock.value.series.size() == 3);
  CHECK(lock.value.series[2].sublocation.has_value());
  CHECK(lock.value.series[2].description == "Lock Level");
  const auto set = must(cu::SeriesSet::make(lock.value.series));
  CHECK(set.keys().size() == 3);
  const auto ambiguous =
      cu::resolve(continuous_request(parameter("00065")), set);
  REQUIRE_FALSE(ambiguous.has_value());
  const auto* candidates = std::get_if<cu::AmbiguousSeries>(&ambiguous.error());
  REQUIRE(candidates != nullptr);
  CHECK(candidates->candidates.size() == 3);
  // The map shows the group whose data reach latest: Headwater and
  // Tailwater both end on 2026-10-15 and began together; Headwater sorts
  // first. The lock chamber ended in 2025.
  const auto shown = cu::map_default(set, parameter("00065"),
                                     cu::StatisticId::instantaneous());
  CHECK(shown.transform([](const cu::SeriesKey& k) { return k.sublocation; }) ==
        std::optional{std::optional{must(cu::Sublocation::make("Headwater"))}});
  // USGS-01646500's water temperature: four sensors (a sonde and three
  // depths), the sonde in two epochs, 2013-2019 and 2019 on.
  const auto temperature =
      series_page(usgs_fixture("series_water_temperature.json"));
  const auto temperature_set =
      must(cu::SeriesSet::make(temperature.value.series));
  CHECK(temperature_set.keys().size() == 4);
  const auto sonde = must(cu::resolve(
      cu::Continuous{
          .station = mov::test::usgs_station(),
          .series = cu::ByParameter{.parameter = parameter("00010"),
                                    .sublocation = must(cu::Sublocation::make(
                                        "From multiparameter sonde"))},
          .range = must(mov::core::TimeRange::make(
              mov::test::utc(2019, 1, 1), mov::test::utc(2020, 1, 1)))},
      temperature_set));
  REQUIRE(sonde.epochs().size() == 2);
  CHECK(sonde.epochs()[0].series().id.view() ==
        "d1875d1d73e447d2a1203105ba654e7d");
}

TEST_CASE("a site's every series, units said once per spelling",
          "[io][usgs][series]") {
  const auto r = series_page(usgs_fixture("series_site.json"));
  CHECK(r.value.series.size() == 68);
  CHECK(r.value.paging.features == 68);
  const cu::SeriesInfo* temperature =
      with_id(r.value.series, "089a50eca92a4e268b8816a1bc22c000");
  REQUIRE(temperature != nullptr);
  CHECK(temperature->parameter.view() == "00010");
  CHECK(temperature->unit == mov::core::parse_unit("degC"));
  CHECK(temperature->description == "Discontinued");
  CHECK(r.warnings ==
        std::vector<Warning>{{.code = WarningCode::unrecognized_unit,
                              .subject = "_FNU",
                              .count = 8},
                             {.code = WarningCode::unrecognized_unit,
                              .subject = "mg/l",
                              .count = 16},
                             {.code = WarningCode::unrecognized_unit,
                              .subject = "pH Units",
                              .count = 8}});
  // Unique ids: a set, whatever the epochs and sublocations.
  CHECK(cu::SeriesSet::make(r.value.series).has_value());
}

TEST_CASE("rows that are not series are left out and counted",
          "[io][usgs][series]") {
  // Without the data_type filter: three field-measurement rows (UUID ids) and
  // two peak rows (no statistic).
  const auto r = series_page(usgs_fixture("series_site_unfiltered.json"));
  CHECK(r.value.paging.features == 73);
  CHECK(r.value.series.size() == 68);
  CHECK(skipped(r.warnings, "id") == 3);
  CHECK(skipped(r.warnings, "statistic") == 2);
  CHECK(r.value.series ==
        series_page(usgs_fixture("series_site.json")).value.series);
}

TEST_CASE("each field of a row", "[io][usgs][series]") {
  const auto one = [](std::string_view properties) {
    return series_page(row_page(properties));
  };
  const auto row = one(good_row);
  REQUIRE(row.value.series.size() == 1);
  CHECK(row.value.series[0].validity.first() == ymd(1972, 6, 9));
  CHECK(not row.value.series[0].validity.last().has_value());
  CHECK(not row.value.series[0].sublocation.has_value());
  CHECK(row.value.series[0].description.empty());

  // primary: "Primary" only.
  const auto not_primary =
      one(R"("id":"66a4e3fb8abe42fcb0942b5c0fe98f68","parameter_code":"00060",)"
          R"("statistic_id":"00011","primary":"Secondary")");
  REQUIRE(not_primary.value.series.size() == 1);
  CHECK(not not_primary.value.series[0].primary);
  CHECK(not_primary.value.series[0].parameter_name.empty());
  CHECK(not not_primary.value.series[0].unit.has_value());
  CHECK(not_primary.value.series[0].validity == mov::core::ValidRange{});

  // A sublocation and a description.
  const auto at =
      one(R"("id":"66a4e3fb8abe42fcb0942b5c0fe98f68","parameter_code":"00065",)"
          R"("statistic_id":"00011","sublocation_identifier":"Tailwater",)"
          R"("web_description":"Lock Level")");
  REQUIRE(at.value.series.size() == 1);
  CHECK(at.value.series[0].sublocation == cu::Sublocation::make("Tailwater"));
  CHECK(at.value.series[0].description == "Lock Level");

  const auto skip = [&](std::string_view properties, std::string_view why) {
    const auto r = one(properties);
    CHECK(r.value.series.empty());
    CHECK(skipped(r.warnings, why) == 1);
  };
  skip(
      R"("id":"66A4E3FB8ABE42FCB0942B5C0FE98F68","parameter_code":"00060","statistic_id":"00011")",
      "id");
  skip(R"("parameter_code":"00060","statistic_id":"00011")", "id");
  skip(
      R"("id":"66a4e3fb8abe42fcb0942b5c0fe98f68","parameter_code":"60","statistic_id":"00011")",
      "parameter");
  skip(
      R"("id":"66a4e3fb8abe42fcb0942b5c0fe98f68","parameter_code":"00060","statistic_id":null)",
      "statistic");
  skip(
      R"("id":"66a4e3fb8abe42fcb0942b5c0fe98f68","parameter_code":"00060","statistic_id":"00011","sublocation_identifier":" Tailwater")",
      "sublocation");
  skip(
      R"("id":"66a4e3fb8abe42fcb0942b5c0fe98f68","parameter_code":"00060","statistic_id":"00011","sublocation_identifier":3)",
      "member");
  skip(R"("id":5,"parameter_code":"00060","statistic_id":"00011")", "member");
  skip(
      R"("id":"66a4e3fb8abe42fcb0942b5c0fe98f68","parameter_code":"00060","statistic_id":"00011","primary":true)",
      "member");
  skip(
      R"("id":"66a4e3fb8abe42fcb0942b5c0fe98f68","parameter_code":"00060","statistic_id":"00011","unit_of_measure":3)",
      "member");
  skip(
      R"("id":"66a4e3fb8abe42fcb0942b5c0fe98f68","parameter_code":"00060","statistic_id":"00011","web_description":[])",
      "member");
  // One day, a begin equal to the end, is a range.
  CHECK(
      one(R"("id":"66a4e3fb8abe42fcb0942b5c0fe98f68","parameter_code":"00060","statistic_id":"00011","begin":"2026-01-01T05:00:00Z","end":"2026-01-01T23:00:00Z")")
          .value.series.size() == 1);
}

TEST_CASE("validity is advisory: bad dates keep the row",
          "[io][usgs][series]") {
  const auto kept = [](std::string_view dates) {
    const auto r = series_page(row_page(
        R"("id":"66a4e3fb8abe42fcb0942b5c0fe98f68","parameter_code":"00060",)"
        R"("statistic_id":"00011","primary":"Primary",)" +
        std::string{dates}));
    REQUIRE(r.value.series.size() == 1);
    CHECK(r.value.series[0].validity == mov::core::ValidRange{});
    CHECK(r.warnings ==
          std::vector<Warning>{{.code = WarningCode::validity_ignored,
                                .subject = "dates",
                                .count = 1}});
    return r.value.series;
  };
  static_cast<void>(kept(R"("begin":"2026-01-02","end":null)"));
  static_cast<void>(kept(R"("begin":5,"end":null)"));
  static_cast<void>(kept(R"("begin":null,"end":"yesterday")"));
  // An end before the begin: kept, and it still resolves.
  const auto inverted =
      kept(R"("begin":"2026-01-02T00:00:00Z","end":"2026-01-01T00:00:00Z")");
  const auto resolved = cu::resolve(continuous_request(parameter("00060")),
                                    must(cu::SeriesSet::make(inverted)));
  REQUIRE(resolved.has_value());
  CHECK(resolved->epochs()[0].series().id == inverted[0].id);
}

TEST_CASE("read_series classifies before it parses", "[io][usgs][series]") {
  const auto page = wu::read_series(usgs_fixture("series_by_id.json"),
                                    "application/json", {});
  REQUIRE(page.has_value());
  CHECK(std::holds_alternative<Read<wu::SeriesPage>>(*page));
  const auto refusal = wu::read_series(
      usgs_fixture("error_unknown_property.json"), "application/json", {});
  REQUIRE(refusal.has_value());
  CHECK(std::get<mov::io::wire::ServiceError>(*refusal).code() ==
        mov::io::wire::ServiceErrc::invalid_parameter);
  const auto bad = mov::io::detail::usgs::parse_series(R"({"features":5})", {});
  CHECK(parse_error_in(bad).code() == ParseErrc::bad_member);
}

// ---- the catalog
// --------------------------------------------------------------

TEST_CASE("catalog rows: the series and their monitoring locations",
          "[io][usgs][catalog]") {
  const auto r = catalog_page(usgs_fixture("catalog_bbox.json"));
  REQUIRE(r.value.rows.size() == 14);
  CHECK(r.warnings.empty());
  const wu::SiteSeries& first = r.value.rows.front();
  CHECK(first.station.value() == "USGS-0204289402");
  CHECK(first.name.view() ==
        "STORM DRAIN AT CRANEYBROOK LANE AT EDGEFIELD, VA");
  CHECK(first.location ==
        must(mov::core::Location::make(
            {.lat = 36.8835444444444, .lon = -76.3806361111111})));
  CHECK(first.series.parameter.view() == "00065");
  CHECK(first.series.unit == mov::core::parse_unit("ft"));
  CHECK(first.series.validity.first() == ymd(2015, 11, 18));
  // An estuary series without dates: unknown and ongoing.
  const wu::SiteSeries& third = r.value.rows[2];
  CHECK(third.station.value() == "USGS-365017076174700");
  CHECK(third.series.parameter.view() == "62620");
  CHECK(third.series.validity == mov::core::ValidRange{});
  // Ended series are kept (decision 33).
  CHECK(r.value.rows[10].series.validity.last() == ymd(2022, 10, 5));
}

TEST_CASE("a catalog in pages is the catalog", "[io][usgs][catalog][paging]") {
  std::vector<wu::SiteSeries> rows;
  for (const std::string_view name :
       {"catalog_page1.json", "catalog_page2.json", "catalog_page3.json"}) {
    const auto page = catalog_page(usgs_fixture(name));
    rows.insert(rows.end(), page.value.rows.begin(), page.value.rows.end());
    if (name == "catalog_page3.json") {
      CHECK(mov::test::ends(page.value.paging));
    } else {
      CHECK(next_url(page.value.paging)
                .starts_with(
                    "https://api.waterdata.usgs.gov/ogcapi/v1/collections/"
                    "combined-metadata/items?cursor="));
    }
  }
  CHECK(rows == catalog_page(usgs_fixture("catalog_bbox.json")).value.rows);
}

TEST_CASE("catalog rows without a location are left out",
          "[io][usgs][catalog]") {
  // The sixteen rows of the 2026-10-15 catalog with a null geometry.
  const auto r = catalog_page(usgs_fixture("catalog_null_geometry.json"));
  CHECK(r.value.rows.empty());
  CHECK(r.value.paging.features == 16);
  CHECK(r.warnings == std::vector<Warning>{{.code = WarningCode::series_skipped,
                                            .subject = "position",
                                            .count = 16}});
}

TEST_CASE("each location field of a catalog row", "[io][usgs][catalog]") {
  const auto with = [](std::string_view site, std::string_view geometry) {
    return catalog_page(
        row_page(std::string{site} + std::string{good_row}, geometry));
  };
  CHECK(with(site_fields, point).value.rows.size() == 1);
  // A height after lon and lat is ignored.
  // A row with four coordinates is read by its first two, and the page's
  // other rows are untouched; a row array past the cap fails the page.
  const std::string feature_at =
      std::string{R"({"type":"Feature","properties":{)"} +
      std::string{site_fields} + std::string{good_row} + R"(},"geometry":)";
  const auto two_rows = [&](std::string_view coordinates) {
    return R"({"type":"FeatureCollection","features":[)" + feature_at +
           R"({"type":"Point","coordinates":)" + std::string{coordinates} +
           "}}," + feature_at + std::string{point} + "}]}";
  };
  const auto four = catalog_page(two_rows("[-77.1,38.9,12,7]"));
  REQUIRE(four.value.rows.size() == 2);
  CHECK(four.value.rows[0].location == four.value.rows[1].location);
  CHECK(four.warnings.empty());
  std::string seventeen = "[-77.1,38.9";
  for (int i = 0; i < 15; ++i) {
    seventeen += ",0";
  }
  CHECK(parse_error_in(
            mov::io::detail::usgs::parse_catalog(two_rows(seventeen + "]"), {}))
            .code() == ParseErrc::too_large);
  CHECK(with(site_fields, R"({"type":"Point","coordinates":[-77.1,38.9,12]})")
            .value.rows.size() == 1);
  const auto skip = [&](std::string_view site, std::string_view geometry,
                        std::string_view why) {
    const auto r = with(site, geometry);
    CHECK(r.value.rows.empty());
    CHECK(skipped(r.warnings, why) == 1);
  };
  skip(site_fields, R"({"type":"LineString","coordinates":[-77.1,38.9]})",
       "position");
  skip(site_fields, R"({"type":"Point","coordinates":[-77.1]})", "position");
  skip(site_fields, R"({"type":"Point","coordinates":[-77.1,98.9]})",
       "position");
  skip(site_fields, R"({"type":"Point","coordinates":["a","b"]})", "position");
  skip(site_fields, R"({"type":"Point"})", "position");
  skip(site_fields, "[1,2]", "position");
  skip(R"("monitoring_location_id":"01646500","monitoring_location_name":"x",)",
       point, "station");
  skip(R"("monitoring_location_name":"x",)", point, "station");
  skip(R"("monitoring_location_id":7,"monitoring_location_name":"x",)", point,
       "member");
  skip(
      R"("monitoring_location_id":"USGS-1","monitoring_location_name":"a\u0000b",)",
      point, "name");
  // The agency prefix is canonical upper case; a missing name is empty.
  const auto lower =
      with(R"("monitoring_location_id":"usgs-01646500",)", point);
  REQUIRE(lower.value.rows.size() == 1);
  CHECK(lower.value.rows[0].station.value() == "USGS-01646500");
  CHECK(lower.value.rows[0].name.view().empty());
}

TEST_CASE("catalog pages are bounded before they are built",
          "[io][usgs][catalog]") {
  ReadContext small;
  small.limits.max_elements = 1;
  const std::string two =
      R"({"features":[{"properties":{}},{"properties":{}}]})";
  const auto r = mov::io::detail::usgs::parse_catalog(two, small);
  CHECK(parse_error_in(r).code() == ParseErrc::too_large);
  const auto cut = mov::io::detail::usgs::parse_catalog(
      R"({"features":[{"properties":{}}],"numberReturned":2})", {});
  CHECK(parse_error_in(cut).code() == ParseErrc::count_mismatch);
  const auto page = wu::read_catalog(usgs_fixture("catalog_bbox.json"),
                                     "application/json", {});
  REQUIRE(page.has_value());
  CHECK(std::get<Read<wu::CatalogPage>>(*page).value.rows.size() == 14);
}
