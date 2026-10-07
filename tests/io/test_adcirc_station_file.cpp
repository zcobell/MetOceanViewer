// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "adcirc_test_support.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/station.hpp"
#include "mov/io/adcirc_ascii.hpp"
#include "mov/io/error.hpp"
#include "mov/io/projection.hpp"
#include "mov/io/warning.hpp"

namespace {

using mov::core::DataSource;
using mov::core::Epsg;
using mov::core::FileStation;
using mov::core::Location;
using mov::io::FileError;
using mov::io::FormatErrc;
using mov::io::ParseErrc;
using mov::io::ReadContext;
using mov::io::WarningCode;
using mov::test::epsg;
using mov::test::find_warning;
using mov::test::fixture_text;
using mov::test::format_error_of;
using mov::test::parse_error_of;
using namespace std::string_literals;

void configure_test_database() {
#if defined(MOV_TEST_PROJ_DATA_DIR)
  mov::io::set_projection_data_dir(MOV_TEST_PROJ_DATA_DIR);
#endif
}

const bool database_configured = (configure_test_database(), true);

Location where(double lat, double lon) {
  const auto made = Location::make({.lat = lat, .lon = lon});
  REQUIRE(made.has_value());
  return *made;
}

// The station file with no limits of its own.
auto parse_adcirc_station_file(std::string_view text, Epsg crs) {
  return mov::io::parse_adcirc_station_file(text, crs, ReadContext{});
}

std::vector<FileStation> parse_ok(std::string_view text,
                                  Epsg crs = Epsg::wgs84()) {
  auto parsed = parse_adcirc_station_file(text, crs);
  REQUIRE(parsed.has_value());
  return std::move(parsed->value);
}

}  // namespace

// ---- the legacy fixture -----------------------------------------------------

// tests/fixtures/io/adcirc/legacy/stations.csv is a byte-exact copy of
// MetOceanViewer/function_tests/ReadADCIRC/ASCII/stations.csv: a count, then
// "lon,lat " with trailing spaces, no names, no final newline.
TEST_CASE("the legacy station file has three unnamed stations",
          "[io][adcirc][stations][legacy]") {
  const auto parsed = parse_adcirc_station_file(
      fixture_text("io/adcirc/legacy/stations.csv"), Epsg::wgs84());
  REQUIRE(parsed.has_value());
  CHECK(parsed->warnings.empty());
  const auto& stations = parsed->value;
  REQUIRE(stations.size() == 3);
  // Longitude first (opposite of IMEDS).
  CHECK(stations[0].location == where(29.987793, -90.0127));
  CHECK(stations[1].location == where(28.0, -90.5));
  CHECK(stations[2].location == where(25.0, -91.0));
  // N6: ids are the 0-based index, the default name is "Station <id>" (v4
  // said "Station_0" here and "Station 0" for netCDF).
  for (std::size_t i = 0; i < stations.size(); ++i) {
    CHECK(stations[i].id.view() == std::to_string(i));
    CHECK(stations[i].name.view() == "Station " + std::to_string(i));
    CHECK(stations[i].source == std::optional<DataSource>{DataSource::adcirc});
    CHECK(not(stations[i].native.has_value()));
  }
}

// ---- names and separators ----

TEST_CASE("names are the remaining words joined by single spaces (N6)",
          "[io][adcirc][stations][regression][N6]") {
  const auto stations = parse_ok(fixture_text("io/adcirc/stations_names.csv"));
  REQUIRE(stations.size() == 4);
  // v4 gave " Lake Pontchartrain": a leading space on every name.
  CHECK(stations[0].name.view() == "Lake Pontchartrain");
  CHECK(stations[0].location == where(29.987793, -90.0127));
  // Space separators, and a doubled space inside the name.
  CHECK(stations[1].name.view() == "Gulf Buoy");
  CHECK(stations[1].location == where(28.0, -90.5));
  // "lon , lat" with surrounding blanks, and no name.
  CHECK(stations[2].name.view() == "Station 2");
  CHECK(stations[2].location == where(25.0, -91.0));
  // Tabs separate too.
  CHECK(stations[3].name.view() == "Tabbed Name");
  CHECK(stations[3].location == where(30.25, -89.5));
}

TEST_CASE("a name may be valid UTF-8 beyond ASCII", "[io][adcirc][stations]") {
  const auto stations = parse_ok(
      "1\n-90.0,29.0,Mar\xC3\xA9"
      "e \xE2\x82\xAC\n");
  REQUIRE(stations.size() == 1);
  CHECK(stations[0].name.view() ==
        "Mar\xC3\xA9"
        "e \xE2\x82\xAC");
}

TEST_CASE("bytes that are not UTF-8 become U+FFFD with a warning",
          "[io][adcirc][stations]") {
  const auto parsed = parse_adcirc_station_file(
      "2\n-90.0,29.0,Bad\xFF"
      "name\n-91.0,28.0,\xC3\n"s,
      Epsg::wgs84());
  REQUIRE(parsed.has_value());
  CHECK(parsed->value[0].name.view() ==
        "Bad\xEF\xBF\xBD"
        "name");
  CHECK(parsed->value[1].name.view() == "\xEF\xBF\xBD");
  const auto* warning =
      find_warning(parsed->warnings, WarningCode::invalid_utf8_replaced);
  REQUIRE(warning != nullptr);
  CHECK(warning->count == 2);
}

TEST_CASE("a NUL ends the name (C14)", "[io][adcirc][stations]") {
  const auto stations = parse_ok("1\n-90.0,29.0,Name\0junk after\n"s);
  REQUIRE(stations.size() == 1);
  CHECK(stations[0].name.view() == "Name");
}

TEST_CASE("a name that is only a NUL and junk is the default name",
          "[io][adcirc][stations]") {
  const auto stations = parse_ok("1\n-90.0,29.0,\0junk\n"s);
  REQUIRE(stations.size() == 1);
  CHECK(stations[0].name.view() == "Station 0");
}

// ---- line endings, BOM, blank lines ----

TEST_CASE("CRLF line endings are accepted", "[io][adcirc][stations]") {
  const auto stations = parse_ok(fixture_text("io/adcirc/stations_crlf.csv"));
  REQUIRE(stations.size() == 3);
  CHECK(stations[0].name.view() == "A");
  CHECK(stations[1].name.view() == "Station 1");
  CHECK(stations[2].location == where(25.0, -91.0));
}

TEST_CASE("a BOM and blank lines are skipped", "[io][adcirc][stations]") {
  const auto stations =
      parse_ok(fixture_text("io/adcirc/stations_bom_blank.csv"));
  REQUIRE(stations.size() == 2);
  CHECK(stations[0].name.view() == "One");
  CHECK(stations[1].name.view() == "Two");
  CHECK(stations[1].id.view() == "1");
}

TEST_CASE("no final newline is fine", "[io][adcirc][stations]") {
  CHECK(parse_ok("1\n-90.0,29.0").size() == 1);
}

TEST_CASE("a count of zero is an empty list", "[io][adcirc][stations]") {
  CHECK(parse_ok("0\n").empty());
  CHECK(parse_ok("0").empty());
}

// ---- the count ----

TEST_CASE("fewer lines than the count is count_mismatch",
          "[io][adcirc][stations]") {
  const auto parsed = parse_adcirc_station_file(
      fixture_text("io/adcirc/stations_count_mismatch.csv"), Epsg::wgs84());
  const auto& error = parse_error_of(parsed);
  CHECK(error.code() == ParseErrc::count_mismatch);
  // The file ends at line 4.
  CHECK(error.line() == 4);
}

TEST_CASE("more lines than the count is count_mismatch at the first extra",
          "[io][adcirc][stations]") {
  const auto parsed = parse_adcirc_station_file(
      "1\n-90.0,29.0\n-91.0,28.0\n-92.0,27.0\n", Epsg::wgs84());
  const auto& error = parse_error_of(parsed);
  CHECK(error.code() == ParseErrc::count_mismatch);
  CHECK(error.line() == 3);
}

TEST_CASE("trailing blank lines are not extra stations",
          "[io][adcirc][stations]") {
  CHECK(parse_ok("1\n-90.0,29.0\n\n  \n\n").size() == 1);
}

TEST_CASE("the count must be a non-negative integer",
          "[io][adcirc][stations]") {
  CHECK(
      parse_error_of(parse_adcirc_station_file("x\n", Epsg::wgs84())).code() ==
      ParseErrc::bad_integer);
  CHECK(parse_error_of(parse_adcirc_station_file("3.5\n", Epsg::wgs84()))
            .code() == ParseErrc::bad_integer);
  CHECK(
      parse_error_of(parse_adcirc_station_file("-1\n", Epsg::wgs84())).code() ==
      ParseErrc::out_of_range);
  CHECK(parse_error_of(parse_adcirc_station_file("99999999999999999999999\n",
                                                 Epsg::wgs84()))
            .code() == ParseErrc::out_of_range);
}

TEST_CASE("a count over max_elements is too_large, before any station",
          "[io][adcirc][stations]") {
  const auto parsed =
      parse_adcirc_station_file("4000000000000\n-90.0,29.0\n", Epsg::wgs84());
  const auto& error = parse_error_of(parsed);
  CHECK(error.code() == ParseErrc::too_large);
  CHECK(error.line() == 1);
  CHECK(error.column() == std::optional<std::size_t>{0});

  ReadContext small;
  small.limits.max_elements = 2;
  CHECK(mov::io::parse_adcirc_station_file("2\n-90,29\n-91,28\n", Epsg::wgs84(),
                                           small)
            .has_value());
  CHECK(parse_error_of(mov::io::parse_adcirc_station_file(
                           "3\n-90,29\n-91,28\n-92,27\n", Epsg::wgs84(), small))
            .code() == ParseErrc::too_large);
}

TEST_CASE("a count within the limit but past the text is count_mismatch",
          "[io][adcirc][stations]") {
  const auto parsed =
      parse_adcirc_station_file("100000\n-90.0,29.0\n", Epsg::wgs84());
  CHECK(parse_error_of(parsed).code() == ParseErrc::count_mismatch);
}

TEST_CASE("the count line splits like the others", "[io][adcirc][stations]") {
  // Commas separate here too: "2,stations" is a count of two.
  CHECK(parse_ok("2,stations\n-90,29\n-91,28\n").size() == 2);
  CHECK(parse_ok("  2 \n-90,29\n-91,28\n").size() == 2);
  CHECK(parse_error_of(parse_adcirc_station_file(",,,\n", Epsg::wgs84()))
            .code() == ParseErrc::bad_integer);
}

TEST_CASE("a stop request is honoured while stations are read",
          "[io][adcirc][stations]") {
  ReadContext stopped;
  stopped.stop = mov::io::StopToken{[] { return true; }};
  const auto result = mov::io::parse_adcirc_station_file(
      "2\n-90,29\n-91,28\n", Epsg::wgs84(), stopped);
  REQUIRE(not(result.has_value()));
  CHECK(std::holds_alternative<mov::io::Cancelled>(result.error()));

  std::string many = "3000\n";
  for (int i = 0; i < 3000; ++i) {
    many += "-90.0,29.0\n";
  }
  std::size_t polls = 0;
  ReadContext later;
  later.stop = mov::io::StopToken{[&polls] { return ++polls >= 2; }};
  const auto partway =
      mov::io::parse_adcirc_station_file(many, Epsg::wgs84(), later);
  REQUIRE(not(partway.has_value()));
  CHECK(std::holds_alternative<mov::io::Cancelled>(partway.error()));
  // One poll per 1024 stations: the second is at station 1024.
  CHECK(polls == 2);
}

TEST_CASE("empty text is empty_input", "[io][adcirc][stations]") {
  for (const std::string_view text : {"", "   \n\n \t\n", "\xEF\xBB\xBF"}) {
    INFO(text);
    CHECK(
        parse_error_of(parse_adcirc_station_file(text, Epsg::wgs84())).code() ==
        ParseErrc::empty_input);
  }
}

// ---- the lines ----

TEST_CASE("a station line needs a longitude and a latitude",
          "[io][adcirc][stations]") {
  const auto parsed =
      parse_adcirc_station_file("2\n-90.0,29.0\n-91.0\n", Epsg::wgs84());
  const auto& error = parse_error_of(parsed);
  CHECK(error.code() == ParseErrc::wrong_field_count);
  CHECK(error.line() == 3);
  CHECK(error.context() == "-91.0");
}

TEST_CASE("a bad coordinate names its line and column",
          "[io][adcirc][stations]") {
  const auto parsed = parse_adcirc_station_file(
      "2\n-90.0,29.0\n  -91.0 ,twenty,Name\n", Epsg::wgs84());
  const auto& error = parse_error_of(parsed);
  CHECK(error.code() == ParseErrc::bad_number);
  CHECK(error.line() == 3);
  CHECK(error.column() == std::optional<std::size_t>{9});
}

TEST_CASE("a position that is not a Location is out_of_range at its token",
          "[io][adcirc][stations]") {
  const auto latitude = parse_adcirc_station_file(
      fixture_text("io/adcirc/stations_bad_latitude.csv"), Epsg::wgs84());
  const auto& lat_error = parse_error_of(latitude);
  CHECK(lat_error.code() == ParseErrc::out_of_range);
  CHECK(lat_error.line() == 3);
  CHECK(lat_error.column() == std::optional<std::size_t>{6});

  const auto longitude =
      parse_adcirc_station_file("1\n400.0,29.0\n", Epsg::wgs84());
  const auto& lon_error = parse_error_of(longitude);
  CHECK(lon_error.code() == ParseErrc::out_of_range);
  CHECK(lon_error.column() == std::optional<std::size_t>{0});

  const auto overflow =
      parse_adcirc_station_file("1\n1e999,29.0\n", Epsg::wgs84());
  CHECK(parse_error_of(overflow).code() == ParseErrc::out_of_range);
}

TEST_CASE("longitudes in 0..360 are normalized like any Location",
          "[io][adcirc][stations]") {
  const auto stations = parse_ok("1\n270.0,29.0\n");
  CHECK(stations[0].location == where(29.0, -90.0));
}

// ---- projection ----

TEST_CASE("a projected station file becomes WGS84 Locations",
          "[io][adcirc][stations][projection]") {
  // EPSG:32615 is WGS 84 / UTM zone 15N: exact, so no approximation warning.
  // The easting and northing are the reference values of tests/io/
  // utm_reference.py for (29.98, -90.01) and (29.0, -94.0).
  const auto parsed = parse_adcirc_station_file(
      fixture_text("io/adcirc/stations_utm15n.csv"), epsg(32615));
  REQUIRE(parsed.has_value());
  const auto& stations = parsed->value;
  REQUIRE(stations.size() == 2);
  CHECK(std::abs(stations[0].location.lat() - 29.98) < 1e-7);
  CHECK(std::abs(stations[0].location.lon() - -90.01) < 1e-7);
  CHECK(std::abs(stations[1].location.lat() - 29.0) < 1e-7);
  CHECK(std::abs(stations[1].location.lon() - -94.0) < 1e-7);
  CHECK(stations[0].name.view() == "Barataria");
  // The file's own point is kept.
  if (const auto& native = stations[0].native) {
    CHECK(native->crs() == epsg(32615));
    CHECK(native->x() == 788502.4135602423);
    CHECK(native->y() == 3320332.9763316396);
  } else {
    FAIL("the native point was not kept");
  }
  CHECK(find_warning(parsed->warnings, WarningCode::crs_approximate) ==
        nullptr);
}

TEST_CASE("an inexact projection carries the crs_approximate warning",
          "[io][adcirc][stations][projection]") {
  // NAD83 to WGS 84 is a datum shift of a stated 4 m without grids.
  const auto parsed = parse_adcirc_station_file(
      fixture_text("io/adcirc/stations_utm15n.csv"), epsg(26915));
  REQUIRE(parsed.has_value());
  const auto* warning =
      find_warning(parsed->warnings, WarningCode::crs_approximate);
  REQUIRE(warning != nullptr);
  CHECK(warning->subject == "EPSG:26915");
  CHECK(warning->count == 2);
  // Within 10 m (1e-4 degrees) of the GRS80 reference.
  CHECK(std::abs(parsed->value[0].location.lat() - 29.98) < 1e-4);
}

TEST_CASE("WGS84 stations keep no native point", "[io][adcirc][stations]") {
  const auto stations = parse_ok("1\n-90.0,29.0\n");
  CHECK(not(stations[0].native.has_value()));
}

TEST_CASE("a CRS that is not geographic or projected is unsupported_crs",
          "[io][adcirc][stations][projection]") {
  // 5703: NAVD88 height, a vertical CRS.
  const auto parsed = parse_adcirc_station_file("1\n-90.0,29.0\n", epsg(5703));
  const auto& error = format_error_of(parsed);
  CHECK(error.code == FormatErrc::unsupported_crs);
  CHECK(error.subject == "EPSG:5703");
}

TEST_CASE("a point PROJ cannot transform is bad_coordinates of its station",
          "[io][adcirc][stations][projection]") {
  const auto parsed = parse_adcirc_station_file(
      "2\n788502.0,3320332.0\n1e15,0.0\n", epsg(32615));
  const auto& error = format_error_of(parsed);
  CHECK(error.code == FormatErrc::bad_coordinates);
  CHECK(error.subject == "EPSG:32615");
  CHECK(error.station == std::optional<std::size_t>{1});
}

TEST_CASE("a projection database that cannot be opened is its own error",
          "[io][adcirc][stations][projection]") {
  // A proj.db that is not a database, named by the override variable.
  const mov::test::ScratchDir broken;
  mov::test::write_bytes(broken / "proj.db", "this is not a database");
  const mov::test::ScopedEnv env{"MOV_PROJ_DATA", broken.path().string()};
  const auto parsed =
      parse_adcirc_station_file("1\n788502.0,3320332.0\n", epsg(32615));
  const auto& error = format_error_of(parsed);
  CHECK(error.code == FormatErrc::projection_unavailable);
  CHECK(error.subject == "EPSG:32615");
  // WGS84 needs no database.
  CHECK(parse_adcirc_station_file("1\n-90,29\n", Epsg::wgs84()).has_value());
}

// ---- files ----

TEST_CASE("read_adcirc_station_file reads a file", "[io][adcirc][stations]") {
  const auto read = mov::io::read_adcirc_station_file(
      mov::test::fixture("io/adcirc/legacy/stations.csv"), Epsg::wgs84(),
      mov::io::ReadContext{});
  REQUIRE(read.has_value());
  CHECK(read->value.size() == 3);
}

TEST_CASE("read_adcirc_station_file: a missing file is a FileError",
          "[io][adcirc][stations]") {
  const auto read = mov::io::read_adcirc_station_file(
      mov::test::fixture("io/adcirc/no_such_file.csv"), Epsg::wgs84(),
      mov::io::ReadContext{});
  REQUIRE(not(read.has_value()));
  CHECK(std::holds_alternative<FileError>(read.error()));
}

TEST_CASE("read_adcirc_station_file honours the size limit",
          "[io][adcirc][stations]") {
  mov::io::ReadContext ctx;
  ctx.limits.max_text_bytes = 10;
  const auto read = mov::io::read_adcirc_station_file(
      mov::test::fixture("io/adcirc/legacy/stations.csv"), Epsg::wgs84(), ctx);
  REQUIRE(not(read.has_value()));
  const auto* error = std::get_if<FileError>(&read.error());
  REQUIRE(error != nullptr);
  CHECK(error->ec == std::errc::file_too_large);
}
