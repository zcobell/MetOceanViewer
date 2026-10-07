// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <array>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "adcirc_test_support.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/vector_series.hpp"
#include "mov/io/adcirc_ascii.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"

namespace {

using mov::core::ColumnIndex;
using mov::core::Dry;
using mov::core::Epsg;
using mov::core::FileStation;
using mov::core::Missing;
using mov::core::Sample;
using mov::core::StationIndex;
using mov::core::StationSelection;
using mov::core::StationTable;
using mov::io::AdcircAsciiRequest;
using mov::io::AdcircKind;
using mov::io::Cancelled;
using mov::io::FormatErrc;
using mov::io::parse_adcirc_ascii;
using mov::io::parse_adcirc_ascii_header;
using mov::io::ParseErrc;
using mov::io::ReadContext;
using mov::io::StopToken;
using mov::io::WarningCode;
using mov::test::at_seconds;
using mov::test::cold_start;
using mov::test::fixture_text;
using mov::test::format_error_of;
using mov::test::parse_error_of;
using mov::test::sample;
using mov::test::warning_count;
using namespace std::string_literals;

// The three stations of the legacy station file.
std::vector<FileStation> three_stations() {
  auto read = mov::io::parse_adcirc_station_file(
      fixture_text("io/adcirc/legacy/stations.csv"), Epsg::wgs84(),
      ReadContext{});
  REQUIRE(read.has_value());
  return std::move(read->value);
}

StationSelection select(std::vector<std::size_t> indices, std::size_t of = 3) {
  auto made = StationSelection::make(std::move(indices), of);
  REQUIRE(made.has_value());
  return *std::move(made);
}

AdcircAsciiRequest request(AdcircKind kind, StationSelection stations) {
  return {.kind = kind,
          .cold_start = cold_start(),
          .stations = std::move(stations)};
}

using TableResult = std::expected<mov::io::Read<StationTable>, mov::io::Error>;

TableResult parse(std::string_view text, AdcircKind kind,
                  StationSelection stations,
                  const ReadContext& ctx = ReadContext{}) {
  return parse_adcirc_ascii(text, three_stations(),
                            request(kind, std::move(stations)), ctx);
}

TableResult parse_all(std::string_view text, AdcircKind kind) {
  return parse(text, kind, StationSelection::all(3));
}

StationTable table_of(std::string_view text, AdcircKind kind,
                      StationSelection stations) {
  auto result = parse(text, kind, std::move(stations));
  REQUIRE(result.has_value());
  CHECK(result->warnings.empty());
  return std::move(result->value);
}

std::vector<Sample> column_of(const StationTable& t, std::size_t station,
                              std::size_t column = 0) {
  const auto cells = t.column(StationIndex{station}, ColumnIndex{column});
  return {cells.begin(), cells.end()};
}

std::string_view quantity_token(const mov::core::SeriesMeta& meta) {
  return mov::core::token(meta.quantity());
}

}  // namespace

// ---- the header ----

TEST_CASE("the header is the second line", "[io][adcirc][header]") {
  const auto elevation =
      parse_adcirc_ascii_header(fixture_text("io/adcirc/legacy/fort.61"));
  REQUIRE(elevation.has_value());
  CHECK(*elevation == mov::io::AdcircAsciiHeader{
                          .snapshots = 144, .stations = 3, .columns = 1});
  const auto velocity =
      parse_adcirc_ascii_header(fixture_text("io/adcirc/legacy/fort.62"));
  REQUIRE(velocity.has_value());
  CHECK(*velocity == mov::io::AdcircAsciiHeader{
                         .snapshots = 48, .stations = 3, .columns = 2});
}

TEST_CASE("the run description may be blank and the version text is optional",
          "[io][adcirc][header]") {
  const auto header = parse_adcirc_ascii_header("\n 5 7 0.1E+01 3 2\n");
  REQUIRE(header.has_value());
  CHECK(*header == mov::io::AdcircAsciiHeader{
                       .snapshots = 5, .stations = 7, .columns = 2});
}

TEST_CASE("a BOM before the description is skipped", "[io][adcirc][header]") {
  const auto header = parse_adcirc_ascii_header(
      "\xEF\xBB\xBF"
      "run\n 5 7 0.1E+01 3 2\n");
  CHECK(header.has_value());
}

TEST_CASE("header errors", "[io][adcirc][header]") {
  using mov::io::ParseError;
  const auto code = [](std::string_view text) {
    const auto header = parse_adcirc_ascii_header(text);
    REQUIRE(not(header.has_value()));
    return header.error().code();
  };
  CHECK(code("") == ParseErrc::empty_input);
  CHECK(code("only a description\n") == ParseErrc::missing_header);
  CHECK(code("run\n") == ParseErrc::missing_header);
  CHECK(code("run\n 5 7 0.1E+01 3\n") == ParseErrc::wrong_field_count);
  CHECK(code("run\n five 7 0.1E+01 3 1\n") == ParseErrc::bad_integer);
  CHECK(code("run\n 5 seven 0.1E+01 3 1\n") == ParseErrc::bad_integer);
  CHECK(code("run\n 5 7 0.1E+01 3 one\n") == ParseErrc::bad_integer);
  CHECK(code("run\n -5 7 0.1E+01 3 1\n") == ParseErrc::out_of_range);
  CHECK(code("run\n 5 7.5 0.1E+01 3 1\n") == ParseErrc::bad_integer);
  const auto error = parse_adcirc_ascii_header("run\n 5 x 0.1E+01 3 1\n");
  REQUIRE(not(error.has_value()));
  CHECK(error.error().line() == 2);
  CHECK(error.error().column() == std::optional<std::size_t>{3});
}

// ---- the legacy fixtures ----

// tests/fixtures/io/adcirc/legacy/fort.6x are byte-exact copies of
// MetOceanViewer/function_tests/ReadADCIRC/ASCII/fort.6x.

TEST_CASE("legacy fort.61: 144 snapshots of three stations",
          "[io][adcirc][legacy]") {
  const auto result = parse_all(fixture_text("io/adcirc/legacy/fort.61"),
                                AdcircKind::elevation);
  REQUIRE(result.has_value());
  CHECK(result->warnings.empty());
  const StationTable& table = result->value;
  REQUIRE(table.size() == 3);
  REQUIRE(table.schema().size() == 1);
  CHECK(table.single_axis());
  CHECK(table.total_samples() == 144 * 3);
  CHECK(quantity_token(table.schema()[0]) == "water_level");
  CHECK(table.schema()[0].unit() ==
        std::optional<mov::core::Unit>{mov::core::LengthUnit::meter});
  CHECK(not(table.schema()[0].datum().has_value()));

  // Seconds after the cold start: 600, 1200, ... 86400.
  const auto times = table.times(StationIndex{0});
  REQUIRE(times.size() == 144);
  CHECK(times.front() == at_seconds(600));
  CHECK(times[1] == at_seconds(1200));
  CHECK(times.back() == at_seconds(86400));
  CHECK(times.back() - times.front() == std::chrono::seconds{85800});

  // Station 1 of snapshot 1 is the -99999 fill: Dry, not a value.
  const auto first = column_of(table, 0);
  CHECK(first[0] == Sample{Dry{}});
  CHECK(first[1] == Sample{Dry{}});
  CHECK(column_of(table, 1)[0] == sample(9.1211551723e-1));
  CHECK(column_of(table, 2)[0] == sample(1.8084168160));
  CHECK(table.station(StationIndex{2}).location ==
        *mov::core::Location::make({.lat = 25.0, .lon = -91.0}));
}

TEST_CASE("legacy fort.62: B1, the vector magnitude is a square root",
          "[io][adcirc][legacy][regression][B1]") {
  const auto result =
      parse_all(fixture_text("io/adcirc/legacy/fort.62"), AdcircKind::velocity);
  REQUIRE(result.has_value());
  CHECK(result->warnings.empty());
  const StationTable& table = result->value;
  REQUIRE(table.schema().size() == 2);
  CHECK(quantity_token(table.schema()[0]) == "current_u");
  CHECK(quantity_token(table.schema()[1]) == "current_v");
  CHECK(table.schema()[0].unit() ==
        std::optional<mov::core::Unit>{mov::core::SpeedUnit::meter_per_second});
  CHECK(table.total_samples() == 48 * 3 * 2);

  const auto u = column_of(table, 0, 0);
  const auto v = column_of(table, 0, 1);
  CHECK(u[0] == sample(4.9298094833e-2));
  CHECK(v[0] == sample(1.2227184139e-2));

  const auto vector = mov::core::vector_series(table, StationIndex{0},
                                               ColumnIndex{0}, ColumnIndex{1});
  REQUIRE(vector.has_value());
  const auto magnitude = vector->magnitude();
  REQUIRE(magnitude.samples()[0].is_value());
  const double first = magnitude.samples()[0].value().value_or(-1.0);
  CHECK_THAT(first, Catch::Matchers::WithinAbs(0.0507918, 1e-7));
  const double u0 = 4.9298094833e-2;
  const double v0 = 1.2227184139e-2;
  CHECK(std::abs(first - std::sqrt((u0 * u0) + (v0 * v0))) < 1e-15);
  // v4 computed pow(pow(u, 2) + pow(v, 2), 2) = (u^2 + v^2)^2, 6.7e-6 here.
  const double legacy = std::pow(std::pow(u0, 2) + std::pow(v0, 2), 2);
  CHECK(std::abs(legacy - 6.6552e-6) < 1e-9);
  CHECK(std::abs(first - legacy) > 0.05);
}

TEST_CASE("legacy fort.71: pressure in metres of water",
          "[io][adcirc][legacy]") {
  const auto result =
      parse_all(fixture_text("io/adcirc/legacy/fort.71"), AdcircKind::pressure);
  REQUIRE(result.has_value());
  CHECK(result->warnings.empty());
  const StationTable& table = result->value;
  REQUIRE(table.schema().size() == 1);
  CHECK(quantity_token(table.schema()[0]) == "air_pressure");
  CHECK(
      table.schema()[0].unit() ==
      std::optional<mov::core::Unit>{mov::core::PressureUnit::meter_of_water});
  CHECK(table.total_samples() == 48 * 3);
  CHECK(column_of(table, 0)[0] == sample(1.0322769043e1));
  CHECK(table.times(StationIndex{0}).front() == at_seconds(1800));
}

TEST_CASE("legacy fort.72: wind components", "[io][adcirc][legacy]") {
  const auto result =
      parse_all(fixture_text("io/adcirc/legacy/fort.72"), AdcircKind::wind);
  REQUIRE(result.has_value());
  CHECK(result->warnings.empty());
  const StationTable& table = result->value;
  REQUIRE(table.schema().size() == 2);
  CHECK(quantity_token(table.schema()[0]) == "wind_u");
  CHECK(quantity_token(table.schema()[1]) == "wind_v");
  CHECK(column_of(table, 0, 0)[0] == sample(-8.4382305552));
  CHECK(column_of(table, 0, 1)[0] == sample(-2.1457132325));
  const auto vector = mov::core::vector_series(table, StationIndex{0},
                                               ColumnIndex{0}, ColumnIndex{1});
  REQUIRE(vector.has_value());
}

TEST_CASE("the kind is the caller's: fort.61 read as velocity is an error",
          "[io][adcirc]") {
  const auto as_velocity =
      parse_all(fixture_text("io/adcirc/legacy/fort.61"), AdcircKind::velocity);
  CHECK(format_error_of(as_velocity).code == FormatErrc::wrong_column_count);
  const auto as_elevation = parse_all(fixture_text("io/adcirc/legacy/fort.62"),
                                      AdcircKind::elevation);
  CHECK(format_error_of(as_elevation).code == FormatErrc::wrong_column_count);
  const auto three = parse_all(
      fixture_text("io/adcirc/header_three_columns.txt"), AdcircKind::wind);
  CHECK(format_error_of(three).code == FormatErrc::wrong_column_count);
}

// ---- values ----

TEST_CASE("elevation: -999 and below is Dry, -998.99 and -950 are values",
          "[io][adcirc][values][regression]") {
  // v4 treated anything below -900 as null; the rule is now "at or below
  // -999" (D16), so -950 is a water level.
  const StationTable table =
      table_of(fixture_text("io/adcirc/elevation_small.txt"),
               AdcircKind::elevation, StationSelection::all(3));
  CHECK(column_of(table, 0) ==
        std::vector<Sample>{Dry{}, Dry{}, Dry{}, sample(0.125), sample(0.25)});
  CHECK(column_of(table, 1) ==
        std::vector<Sample>{sample(0.5), sample(0.75), sample(-998.99),
                            sample(-0.25), sample(-0.5)});
  CHECK(column_of(table, 2) == std::vector<Sample>{sample(1.25), sample(1.5),
                                                   sample(-950.0), Dry{},
                                                   sample(2.0)});
  CHECK(table.times(StationIndex{0}).size() == 5);
}

TEST_CASE("other outputs: the fill is Missing, never Dry",
          "[io][adcirc][values]") {
  const StationTable table =
      table_of(fixture_text("io/adcirc/pressure_small.txt"),
               AdcircKind::pressure, StationSelection::all(3));
  CHECK(column_of(table, 2) == std::vector<Sample>{Missing{}, sample(10.29)});
  CHECK(column_of(table, 0) ==
        std::vector<Sample>{sample(10.32), sample(10.31)});
}

TEST_CASE("vectors: a fill in either component masks both (N7)",
          "[io][adcirc][values][regression][N7]") {
  const StationTable table =
      table_of(fixture_text("io/adcirc/velocity_small.txt"),
               AdcircKind::velocity, StationSelection::all(3));
  // Station 3 is fill in both at the first record, and in v only at the
  // second: u is valid there but is masked too.
  CHECK(column_of(table, 2, 0) ==
        std::vector<Sample>{Missing{}, Missing{}, sample(0.0)});
  CHECK(column_of(table, 2, 1) ==
        std::vector<Sample>{Missing{}, Missing{}, sample(0.0)});
  // Station 2: u is fill at the second record and v is valid there.
  CHECK(column_of(table, 1, 0) ==
        std::vector<Sample>{sample(-3.12058356), Missing{}, sample(-1.0)});
  CHECK(column_of(table, 1, 1) ==
        std::vector<Sample>{sample(-1.4904421703), Missing{}, sample(-0.0)});
  // The vector magnitude is Missing wherever a component is.
  const auto vector = mov::core::vector_series(table, StationIndex{2},
                                               ColumnIndex{0}, ColumnIndex{1});
  REQUIRE(vector.has_value());
  const auto speed = vector->magnitude();
  CHECK(speed.samples()[0].is_missing());
}

TEST_CASE("a vector keeps its components when nothing is fill",
          "[io][adcirc][values]") {
  const StationTable table =
      table_of(fixture_text("io/adcirc/velocity_small.txt"),
               AdcircKind::velocity, StationSelection::all(3));
  const auto vector = mov::core::vector_series(table, StationIndex{0},
                                               ColumnIndex{0}, ColumnIndex{1});
  REQUIRE(vector.has_value());
  // A 3-4-5 triangle at the third record.
  const auto speed = vector->magnitude();
  CHECK(speed.samples()[2] == sample(5.0));
}

TEST_CASE("NaN, Inf and **** are Missing and counted; Fortran exponents work",
          "[io][adcirc][values]") {
  const auto result = parse_all(
      fixture_text("io/adcirc/elevation_nonfinite.txt"), AdcircKind::elevation);
  REQUIRE(result.has_value());
  const StationTable& table = result->value;
  CHECK(column_of(table, 0) == std::vector<Sample>{Missing{}, Missing{}});
  CHECK(column_of(table, 1) == std::vector<Sample>{Missing{}, Missing{}});
  CHECK(column_of(table, 2) ==
        std::vector<Sample>{sample(1.25e-100), sample(0.5)});
  REQUIRE(result->warnings.size() == 1);
  CHECK(result->warnings[0].code == WarningCode::nonfinite_masked);
  CHECK(result->warnings[0].count == 4);
  // The first masked token is on line 4 (the first station of record 1).
  CHECK(result->warnings[0].subject == "first at line 4");
}

TEST_CASE("a number that is not a number at all is corrupt, not Missing",
          "[io][adcirc][values]") {
  const std::string text =
      "run\n 1 3 0.6E+03 1 1\n 6.0E+02 1\n 1 0.5\n 2 abc\n 3 0.5\n"s;
  const auto result = parse_all(text, AdcircKind::elevation);
  // The bad line is not the last: the file is damaged.
  const auto& error = parse_error_of(result);
  CHECK(error.code() == ParseErrc::corrupt_record);
  CHECK(error.line() == 5);
  CHECK(error.column() == std::optional<std::size_t>{3});
}

// ---- selection ----

TEST_CASE("a selection reads its stations in its order, and only them",
          "[io][adcirc][selection]") {
  const std::string text = fixture_text("io/adcirc/elevation_small.txt");
  const StationTable everything =
      table_of(text, AdcircKind::elevation, StationSelection::all(3));
  const StationTable picked =
      table_of(text, AdcircKind::elevation, select({2, 0}));
  REQUIRE(picked.size() == 2);
  CHECK(picked.station(StationIndex{0}).id.view() == "2");
  CHECK(picked.station(StationIndex{1}).id.view() == "0");
  CHECK(column_of(picked, 0) == column_of(everything, 2));
  CHECK(column_of(picked, 1) == column_of(everything, 0));
  CHECK(std::ranges::equal(picked.times(StationIndex{0}),
                           everything.times(StationIndex{0})));
}

TEST_CASE("every selection of three stations matches the full read",
          "[io][adcirc][selection]") {
  const std::string text = fixture_text("io/adcirc/legacy/fort.62");
  const StationTable everything =
      table_of(text, AdcircKind::velocity, StationSelection::all(3));
  const std::vector<std::vector<std::size_t>> choices{
      {}, {0}, {1}, {2}, {0, 1}, {1, 0}, {0, 2}, {2, 1}, {1, 2, 0}, {2, 0, 1}};
  for (const auto& choice : choices) {
    const StationTable picked =
        table_of(text, AdcircKind::velocity, select(choice));
    REQUIRE(picked.size() == choice.size());
    for (std::size_t p = 0; p < choice.size(); ++p) {
      for (std::size_t k = 0; k < 2; ++k) {
        CHECK(column_of(picked, p, k) == column_of(everything, choice[p], k));
      }
    }
  }
}

TEST_CASE("an empty selection is a table with the time axis and no stations",
          "[io][adcirc][selection]") {
  const auto result = parse(fixture_text("io/adcirc/elevation_small.txt"),
                            AdcircKind::elevation, select({}));
  REQUIRE(result.has_value());
  CHECK(result->value.size() == 0);
  CHECK(result->value.schema().size() == 1);
  CHECK(result->warnings.empty());
}

TEST_CASE("the lines of unselected stations are skipped, not parsed",
          "[io][adcirc][selection]") {
  // Station 2's lines are garbage in both records. Station 1 and 3 are fine.
  const std::string text =
      "run\n 2 3 0.6E+03 1 1\n 6.0E+02 1\n 1 0.5\n 2 garbage\n 3 0.75\n"
      " 1.2E+03 2\n 1 0.25\n whatever else\n 3 1.0\n"s;
  const auto picked = parse(text, AdcircKind::elevation, select({0, 2}));
  REQUIRE(picked.has_value());
  CHECK(picked->warnings.empty());
  CHECK(column_of(picked->value, 0) ==
        std::vector<Sample>{sample(0.5), sample(0.25)});
  CHECK(column_of(picked->value, 1) ==
        std::vector<Sample>{sample(0.75), sample(1.0)});
  // Selecting the damaged station reports it.
  const auto all = parse_all(text, AdcircKind::elevation);
  CHECK(parse_error_of(all).code() == ParseErrc::corrupt_record);
}

TEST_CASE("the station list and the selection must be for the file's count",
          "[io][adcirc][selection]") {
  const std::string text = fixture_text("io/adcirc/elevation_small.txt");
  // A selection made for four stations.
  const auto other_selection =
      parse(text, AdcircKind::elevation, StationSelection::all(4));
  const auto& selection_error = format_error_of(other_selection);
  CHECK(selection_error.code == FormatErrc::station_count_mismatch);
  CHECK(selection_error.subject == "NStations is 3, the selection is for 4");

  // A station list of two stations.
  auto two = three_stations();
  two.pop_back();
  const auto short_list = parse_adcirc_ascii(
      text, two, request(AdcircKind::elevation, StationSelection::all(2)),
      ReadContext{});
  const auto& list_error = format_error_of(short_list);
  CHECK(list_error.code == FormatErrc::station_count_mismatch);
  CHECK(list_error.subject == "NStations is 3, the station list has 2");
}

TEST_CASE("the cold start is added to the record times", "[io][adcirc][time]") {
  using namespace std::chrono;
  const auto start = *mov::core::parse_utc_datetime("2005-08-28 12:30:00");
  const auto result = parse_adcirc_ascii(
      fixture_text("io/adcirc/elevation_small.txt"), three_stations(),
      {.kind = AdcircKind::elevation,
       .cold_start = start,
       .stations = StationSelection::all(3)},
      ReadContext{});
  REQUIRE(result.has_value());
  const auto times = result->value.times(StationIndex{0});
  CHECK(times[0] == start + seconds{600});
  CHECK(times[4] == start + seconds{3000});
}

// ---- incomplete runs ----

TEST_CASE("fewer records than the header says: kept, with a warning",
          "[io][adcirc][partial]") {
  const auto result =
      parse_all(fixture_text("io/adcirc/elevation_fewer_snapshots.txt"),
                AdcircKind::elevation);
  REQUIRE(result.has_value());
  CHECK(result->value.times(StationIndex{0}).size() == 3);
  REQUIRE(result->warnings.size() == 1);
  CHECK(result->warnings[0].code == WarningCode::fewer_snapshots_than_header);
  CHECK(result->warnings[0].count == 2);
}

TEST_CASE("a record cut off after some station lines is dropped",
          "[io][adcirc][partial]") {
  const auto result =
      parse_all(fixture_text("io/adcirc/elevation_partial_record.txt"),
                AdcircKind::elevation);
  REQUIRE(result.has_value());
  CHECK(result->value.times(StationIndex{0}).size() == 3);
  for (std::size_t s = 0; s < 3; ++s) {
    CHECK(column_of(result->value, s).size() == 3);
  }
  CHECK(warning_count(result->warnings, WarningCode::partial_record_dropped) ==
        1);
  CHECK(warning_count(result->warnings,
                      WarningCode::fewer_snapshots_than_header) == 2);
  CHECK(result->warnings.size() == 2);
  // The complete records are exactly the first three of the full file.
  const StationTable full =
      table_of(fixture_text("io/adcirc/elevation_small.txt"),
               AdcircKind::elevation, StationSelection::all(3));
  for (std::size_t s = 0; s < 3; ++s) {
    const auto head = column_of(full, s);
    CHECK(column_of(result->value, s) ==
          std::vector<Sample>(head.begin(), head.begin() + 3));
  }
}

TEST_CASE("a record cut off right after its header is dropped",
          "[io][adcirc][partial]") {
  const auto result =
      parse_all(fixture_text("io/adcirc/elevation_partial_header.txt"),
                AdcircKind::elevation);
  REQUIRE(result.has_value());
  CHECK(result->value.times(StationIndex{0}).size() == 3);
  CHECK(warning_count(result->warnings, WarningCode::partial_record_dropped) ==
        1);
}

TEST_CASE("a last line cut in the middle of a number is not a value",
          "[io][adcirc][partial]") {
  // The fixture ends "-9.999900" with no newline: it parses as a number,
  // -9.9999, which is not what was written. The run is unfinished, so the
  // line is taken as cut and its record dropped.
  const auto result =
      parse_all(fixture_text("io/adcirc/elevation_cut_mid_line.txt"),
                AdcircKind::elevation);
  REQUIRE(result.has_value());
  CHECK(result->value.times(StationIndex{0}).size() == 3);
  CHECK(warning_count(result->warnings, WarningCode::partial_record_dropped) ==
        1);
  for (std::size_t s = 0; s < 3; ++s) {
    for (const Sample cell : column_of(result->value, s)) {
      CHECK(cell != sample(-9.9999));
    }
  }
}

TEST_CASE("a last line without its newline is cut off, even when complete",
          "[io][adcirc][partial]") {
  // A finished ADCIRC file ends with a newline. One that does not was cut by
  // a crash or a copy, and its last number may be a truncated one that still
  // parses, so the record it ends is dropped.
  std::string text = fixture_text("io/adcirc/elevation_small.txt");
  REQUIRE(text.ends_with('\n'));
  text.pop_back();
  const auto result = parse_all(text, AdcircKind::elevation);
  REQUIRE(result.has_value());
  CHECK(result->value.times(StationIndex{0}).size() == 4);
  CHECK(warning_count(result->warnings, WarningCode::partial_record_dropped) ==
        1);
  CHECK(warning_count(result->warnings,
                      WarningCode::fewer_snapshots_than_header) == 1);
  CHECK(column_of(result->value, 2).back() == Sample{Dry{}});
}

TEST_CASE("a record header with no newline is cut off too",
          "[io][adcirc][partial]") {
  const std::string text =
      "run\n 0 3 0.6E+03 1 1\n 6.0E+02 1"s;  // no stations, no newline
  const auto result = parse_all(text, AdcircKind::elevation);
  REQUIRE(result.has_value());
  CHECK(result->value.times(StationIndex{0}).empty());
  CHECK(warning_count(result->warnings, WarningCode::partial_record_dropped) ==
        1);
}

TEST_CASE("what a dropped record held is not counted or reported",
          "[io][adcirc][partial]") {
  // The second record is cut off after a NaN: only the first record's NaN
  // is in the count, and the first masked line is the first record's.
  const std::string text =
      "run\n 2 3 0.6E+03 1 1\n 6.0E+02 1\n 1 NaN\n 2 0.5\n 3 0.75\n"
      " 1.2E+03 2\n 1 NaN\n 2 NaN\n"s;
  const auto result = parse_all(text, AdcircKind::elevation);
  REQUIRE(result.has_value());
  CHECK(result->value.times(StationIndex{0}).size() == 1);
  CHECK(result->warnings.front().code == WarningCode::nonfinite_masked);
  CHECK(result->warnings.front().count == 1);
  CHECK(result->warnings.front().subject == "first at line 4");
}

TEST_CASE("mid-file corruption is an error, not a partial record",
          "[io][adcirc][partial]") {
  const auto result =
      parse_all(fixture_text("io/adcirc/elevation_corrupt_mid.txt"),
                AdcircKind::elevation);
  const auto& error = parse_error_of(result);
  CHECK(error.code() == ParseErrc::corrupt_record);
  CHECK(error.line() == 9);
  CHECK(error.context().find("not-a-number") != std::string::npos);
}

TEST_CASE("each way a line can be malformed, mid-file, is corrupt_record",
          "[io][adcirc][partial]") {
  const auto corrupt = [](std::string_view middle) {
    // Two records; `middle` replaces station 2's line of the first record.
    const std::string text = "run\n 2 3 0.6E+03 1 1\n 6.0E+02 1\n 1 0.5\n" +
                             std::string{middle} +
                             "\n 3 0.75\n 1.2E+03 2\n 1 0.25\n 2 0.5\n 3 1.0\n";
    const auto result = parse_all(text, AdcircKind::elevation);
    return parse_error_of(result).code();
  };
  CHECK(corrupt(" 2") == ParseErrc::corrupt_record);
  CHECK(corrupt(" 2 0.5 0.6") == ParseErrc::corrupt_record);
  CHECK(corrupt(" 2 1e5000x") == ParseErrc::corrupt_record);
  CHECK(corrupt("") == ParseErrc::corrupt_record);
  CHECK(corrupt("   ") == ParseErrc::corrupt_record);
}

TEST_CASE("a bad record header is corrupt_record unless it ends the file",
          "[io][adcirc][partial]") {
  const auto head = [](std::string_view header_line, bool more) {
    const std::string text =
        "run\n 2 3 0.6E+03 1 1\n 6.0E+02 1\n 1 0.5\n 2 0.5\n"
        " 3 0.75\n" +
        std::string{header_line} + "\n" +
        (more ? " 1 0.25\n 2 0.5\n 3 1.0\n" : "");
    return parse_all(text, AdcircKind::elevation);
  };
  // A station line where a record header should be: its second field is not
  // an integer.
  for (const std::string_view bad : {" 1.2E+03 2.5", " 1.2E+03", " abc 2",
                                     " 1.2E+03 2 extra", " 1.2E+03 two"}) {
    INFO(bad);
    CHECK(parse_error_of(head(bad, true)).code() == ParseErrc::corrupt_record);
    const auto last = head(bad, false);
    REQUIRE(last.has_value());
    CHECK(warning_count(last->warnings, WarningCode::partial_record_dropped) ==
          1);
  }
}

TEST_CASE("records past NSnaps are read, with a warning",
          "[io][adcirc][partial]") {
  const std::string text = fixture_text("io/adcirc/elevation_small.txt") +
                           " 3.6E+03 6\n 1 0.5\n 2 0.5\n 3 0.5\n"s;
  const auto result = parse_all(text, AdcircKind::elevation);
  REQUIRE(result.has_value());
  CHECK(result->value.times(StationIndex{0}).size() == 6);
  REQUIRE(result->warnings.size() == 1);
  CHECK(result->warnings[0].code == WarningCode::more_snapshots_than_header);
  CHECK(result->warnings[0].count == 1);
  CHECK(result->warnings[0].subject == "NSnaps 5, read 6");
}

TEST_CASE("a restart that writes past NSnaps and overlaps the run is merged",
          "[io][adcirc][time]") {
  // 144 records planned, 100 written, restarted from record 90: the file
  // holds 100 + 55 records, 11 of them for times already written, and the
  // restart disagrees with the first run at record 100.
  const auto result =
      parse_all(fixture_text("io/adcirc/elevation_restart_past_header.txt"),
                AdcircKind::elevation);
  REQUIRE(result.has_value());
  const StationTable& table = result->value;
  const auto times = table.times(StationIndex{0});
  REQUIRE(times.size() == 144);
  CHECK(times.front() == at_seconds(600));
  CHECK(times.back() == at_seconds(86400));
  CHECK(std::ranges::adjacent_find(times, std::greater_equal<>{}) ==
        times.end());
  // The first run's record 100 is kept (1.0), not the restart's (2.0).
  CHECK(column_of(table, 0)[99] == sample(1.0));
  CHECK(column_of(table, 0)[100] == sample(1.01));
  CHECK(column_of(table, 1)[143] == sample(1.54));
  REQUIRE(result->warnings.size() == 4);
  CHECK(warning_count(result->warnings,
                      WarningCode::more_snapshots_than_header) == 11);
  CHECK(warning_count(result->warnings, WarningCode::times_reordered) == 1);
  CHECK(warning_count(result->warnings, WarningCode::duplicate_times_dropped) ==
        11);
  CHECK(warning_count(result->warnings,
                      WarningCode::conflicting_duplicate_times) == 1);
}

TEST_CASE("garbage after the last record is corrupt or a cut-off record",
          "[io][adcirc][partial]") {
  const std::string text = fixture_text("io/adcirc/elevation_small.txt");
  const auto last = parse_all(text + "garbage\n", AdcircKind::elevation);
  REQUIRE(last.has_value());
  CHECK(warning_count(last->warnings, WarningCode::partial_record_dropped) ==
        1);
  CHECK(last->value.times(StationIndex{0}).size() == 5);
  const auto more = parse_all(text + "garbage\n 1 2\n", AdcircKind::elevation);
  CHECK(parse_error_of(more).code() == ParseErrc::corrupt_record);
}

TEST_CASE("a blank line is never a station, selected or not",
          "[io][adcirc][selection]") {
  // Station 2's line is blank in the first of two records; only station 1 is
  // selected, and the file is damaged all the same.
  const std::string text =
      "run\n 2 3 0.6E+03 1 1\n 6.0E+02 1\n 1 0.5\n\n 3 0.75\n"
      " 1.2E+03 2\n 1 0.25\n 2 0.5\n 3 1.0\n"s;
  CHECK(
      parse_error_of(parse(text, AdcircKind::elevation, select({0}))).code() ==
      ParseErrc::corrupt_record);
}

TEST_CASE("a station line must carry its own number", "[io][adcirc]") {
  // Station 3's line says 4: a line was lost or doubled.
  const std::string text =
      "run\n 1 3 0.6E+03 1 1\n 6.0E+02 1\n 1 0.5\n 2 0.5\n 4 0.75\n"s;
  const auto result = parse_all(text, AdcircKind::elevation);
  // Last line of the text: a cut-off record.
  REQUIRE(result.has_value());
  CHECK(warning_count(result->warnings, WarningCode::partial_record_dropped) ==
        1);
  const auto more = parse_all(text + " 1.2E+03 2\n 1 0.5\n 2 0.5\n 3 0.5\n",
                              AdcircKind::elevation);
  const auto& error = parse_error_of(more);
  CHECK(error.code() == ParseErrc::corrupt_record);
  CHECK(error.line() == 6);
  CHECK(error.column() == std::optional<std::size_t>{1});
  // Unselected stations' numbers are not read.
  CHECK(parse(text + " 1.2E+03 2\n 1 0.5\n 2 0.5\n 3 0.5\n",
              AdcircKind::elevation, select({0, 1}))
            .has_value());
}

TEST_CASE("blank lines after the last record are fine, inside are not",
          "[io][adcirc][partial]") {
  const std::string text = fixture_text("io/adcirc/elevation_small.txt");
  const auto trailing = parse_all(text + "\n  \n\n", AdcircKind::elevation);
  REQUIRE(trailing.has_value());
  CHECK(trailing->warnings.empty());

  // Fewer records than the header says, then blank lines: no cut-off record.
  std::string short_run =
      fixture_text("io/adcirc/elevation_fewer_snapshots.txt");
  short_run += "\n\n";
  const auto few = parse_all(short_run, AdcircKind::elevation);
  REQUIRE(few.has_value());
  CHECK(warning_count(few->warnings, WarningCode::partial_record_dropped) == 0);

  std::string inside = text;
  inside.insert(inside.find(" 1.2000000000E+003"), "\n");
  CHECK(parse_error_of(parse_all(inside, AdcircKind::elevation)).code() ==
        ParseErrc::corrupt_record);
}

TEST_CASE("a header and nothing else is a table with no records",
          "[io][adcirc][partial]") {
  const auto result =
      parse_all("run\n 0 3 0.6E+03 1 1\n", AdcircKind::elevation);
  REQUIRE(result.has_value());
  CHECK(result->warnings.empty());
  CHECK(result->value.size() == 3);
  CHECK(result->value.times(StationIndex{0}).empty());
  CHECK(result->value.total_samples() == 0);

  const auto none = parse_all("run\n 2 3 0.6E+03 1 1\n", AdcircKind::elevation);
  REQUIRE(none.has_value());
  CHECK(none->value.times(StationIndex{0}).empty());
  CHECK(warning_count(none->warnings,
                      WarningCode::fewer_snapshots_than_header) == 2);
  CHECK(warning_count(none->warnings, WarningCode::partial_record_dropped) ==
        0);
}

TEST_CASE(
    "a header that claims an absurd number of records allocates for "
    "the text only",
    "[io][adcirc][partial]") {
  const auto result = parse_all(
      "run\n 1152921504606846976 3 0.6E+03 1 1\n 6.0E+02 1\n 1 0.5\n 2 0.5\n"
      " 3 0.75\n",
      AdcircKind::elevation);
  REQUIRE(result.has_value());
  CHECK(result->value.times(StationIndex{0}).size() == 1);
  CHECK(warning_count(result->warnings,
                      WarningCode::fewer_snapshots_than_header) ==
        1152921504606846975ULL);
}

// ---- the time axis ----

TEST_CASE("a hot start that overlaps its run is sorted and deduplicated",
          "[io][adcirc][time]") {
  const auto result =
      parse_all(fixture_text("io/adcirc/elevation_hot_start_overlap.txt"),
                AdcircKind::elevation);
  REQUIRE(result.has_value());
  const StationTable& table = result->value;
  // File order 600, 1200, 900, 1200, 1500.
  const auto times = table.times(StationIndex{0});
  REQUIRE(times.size() == 4);
  CHECK(times[0] == at_seconds(600));
  CHECK(times[1] == at_seconds(900));
  CHECK(times[2] == at_seconds(1200));
  CHECK(times[3] == at_seconds(1500));
  // The first record at 1200 is kept, not the restart's.
  CHECK(column_of(table, 0) == std::vector<Sample>{sample(0.1), sample(0.7),
                                                   sample(0.4), sample(1.0)});
  CHECK(column_of(table, 1) == std::vector<Sample>{sample(0.2), sample(0.8),
                                                   sample(0.5), sample(1.1)});
  CHECK(warning_count(result->warnings, WarningCode::times_reordered) == 1);
  CHECK(warning_count(result->warnings, WarningCode::duplicate_times_dropped) ==
        1);
  CHECK(warning_count(result->warnings,
                      WarningCode::conflicting_duplicate_times) == 1);
}

TEST_CASE("a duplicated record with the same data is not a conflict",
          "[io][adcirc][time]") {
  const std::string text =
      "run\n 2 3 0.6E+03 1 1\n 6.0E+02 1\n 1 0.5\n 2 0.5\n 3 0.5\n"
      " 6.0E+02 2\n 1 0.5\n 2 0.5\n 3 0.5\n"s;
  const auto result = parse_all(text, AdcircKind::elevation);
  REQUIRE(result.has_value());
  CHECK(result->value.times(StationIndex{0}).size() == 1);
  CHECK(warning_count(result->warnings, WarningCode::duplicate_times_dropped) ==
        1);
  CHECK(warning_count(result->warnings,
                      WarningCode::conflicting_duplicate_times) == 0);
  CHECK(warning_count(result->warnings, WarningCode::times_reordered) == 0);
}

TEST_CASE("a time beyond 2^53 ms is time_out_of_range", "[io][adcirc][time]") {
  const auto result =
      parse_all(fixture_text("io/adcirc/elevation_time_overflow.txt"),
                AdcircKind::elevation);
  const auto& error = parse_error_of(result);
  CHECK(error.code() == ParseErrc::time_out_of_range);
  CHECK(error.line() == 3);
  CHECK(error.column() == std::optional<std::size_t>{4});
}

TEST_CASE("fractional seconds round to the millisecond", "[io][adcirc][time]") {
  const std::string text =
      "run\n 1 3 0.6E+03 1 1\n 6.0004E+02 1\n 1 0.5\n 2 0.5\n 3 0.5\n"s;
  const auto result = parse_all(text, AdcircKind::elevation);
  REQUIRE(result.has_value());
  CHECK(result->value.times(StationIndex{0})[0] ==
        cold_start() + std::chrono::milliseconds{600040});
}

// ---- text forms ----

TEST_CASE("CRLF and a BOM do not change the result", "[io][adcirc][text]") {
  const std::string text = fixture_text("io/adcirc/legacy/fort.62");
  std::string crlf;
  for (const char c : text) {
    if (c == '\n') {
      crlf += '\r';
    }
    crlf += c;
  }
  const StationTable plain =
      table_of(text, AdcircKind::velocity, StationSelection::all(3));
  const StationTable windows =
      table_of(crlf, AdcircKind::velocity, StationSelection::all(3));
  const StationTable bom = table_of("\xEF\xBB\xBF" + text, AdcircKind::velocity,
                                    StationSelection::all(3));
  CHECK(plain == windows);
  CHECK(plain == bom);
}

TEST_CASE("D exponents and wide numbers are read", "[io][adcirc][text]") {
  const std::string text =
      "run\n 1 3 0.6D+03 1 1\n 6.0D+02 1\n 1 5.0D-01\n 2 -0.25\n 3 +2\n"s;
  const auto result = parse_all(text, AdcircKind::elevation);
  REQUIRE(result.has_value());
  CHECK(column_of(result->value, 0) == std::vector<Sample>{sample(0.5)});
  CHECK(column_of(result->value, 2) == std::vector<Sample>{sample(2.0)});
}

// ---- limits and cancellation ----

TEST_CASE("more samples than max_elements is too_large",
          "[io][adcirc][limits]") {
  ReadContext ctx;
  ctx.limits.max_elements = 8;  // 3 stations x 5 records = 15
  const auto result =
      parse(fixture_text("io/adcirc/elevation_small.txt"),
            AdcircKind::elevation, StationSelection::all(3), ctx);
  CHECK(parse_error_of(result).code() == ParseErrc::too_large);

  // Only the selected stations count.
  ctx.limits.max_elements = 5;
  const auto one = parse(fixture_text("io/adcirc/elevation_small.txt"),
                         AdcircKind::elevation, select({1}), ctx);
  CHECK(one.has_value());
}

TEST_CASE("a stop request is honoured between records", "[io][adcirc][stop]") {
  ReadContext stopped;
  stopped.stop = StopToken{[] { return true; }};
  const auto result =
      parse(fixture_text("io/adcirc/legacy/fort.61"), AdcircKind::elevation,
            StationSelection::all(3), stopped);
  REQUIRE(not(result.has_value()));
  CHECK(std::holds_alternative<Cancelled>(result.error()));

  // Cancelled after ten polls: partway through, so no partial table either.
  std::size_t polls = 0;
  ReadContext later;
  later.stop = StopToken{[&polls] { return ++polls > 10; }};
  const auto partway =
      parse(fixture_text("io/adcirc/legacy/fort.61"), AdcircKind::elevation,
            StationSelection::all(3), later);
  REQUIRE(not(partway.has_value()));
  CHECK(std::holds_alternative<Cancelled>(partway.error()));
  CHECK(polls == 11);
}

// ---- a large file ----

TEST_CASE("reading three stations of a wide file", "[io][adcirc][large]") {
  constexpr std::size_t stations = 3000;
  constexpr std::size_t records = 40;
  std::string text =
      std::format("run\n {} {} 0.6E+03 1 1\n", records, stations);
  for (std::size_t r = 0; r < records; ++r) {
    text += std::format(" {}.0E+02 {}\n", r + 1, r + 1);
    for (std::size_t s = 0; s < stations; ++s) {
      text += std::format(" {} {}.5\n", s + 1, (r + s) % 7);
    }
  }
  std::vector<FileStation> all;
  all.reserve(stations);
  const auto location = *mov::core::Location::make({.lat = 29.0, .lon = -90.0});
  for (std::size_t s = 0; s < stations; ++s) {
    all.push_back(
        FileStation{.id = *mov::core::StationKey::make(std::to_string(s)),
                    .name = {},
                    .location = location,
                    .native = std::nullopt,
                    .source = std::nullopt});
  }
  auto pick = StationSelection::make({2999, 0, 1500}, stations);
  REQUIRE(pick.has_value());
  const auto result = parse_adcirc_ascii(text, all,
                                         {.kind = AdcircKind::elevation,
                                          .cold_start = cold_start(),
                                          .stations = *std::move(pick)},
                                         ReadContext{});
  REQUIRE(result.has_value());
  CHECK(result->warnings.empty());
  const StationTable& table = result->value;
  REQUIRE(table.size() == 3);
  CHECK(table.times(StationIndex{0}).size() == records);
  for (std::size_t r = 0; r < records; ++r) {
    CHECK(column_of(table, 0)[r] ==
          sample(static_cast<double>((r + 2999) % 7) + 0.5));
    CHECK(column_of(table, 1)[r] == sample(static_cast<double>(r % 7) + 0.5));
    CHECK(column_of(table, 2)[r] ==
          sample(static_cast<double>((r + 1500) % 7) + 0.5));
  }
}

// ---- files ----

TEST_CASE("read_adcirc_ascii pairs an output with its station file",
          "[io][adcirc][files]") {
  const auto result = mov::io::read_adcirc_ascii(
      mov::test::fixture("io/adcirc/legacy/fort.61"),
      mov::test::fixture("io/adcirc/legacy/stations.csv"), Epsg::wgs84(),
      request(AdcircKind::elevation, StationSelection::all(3)), ReadContext{});
  REQUIRE(result.has_value());
  CHECK(result->value.size() == 3);
  CHECK(result->value.station(StationIndex{1}).location ==
        *mov::core::Location::make({.lat = 28.0, .lon = -90.5}));
  CHECK(result->warnings.empty());
}

TEST_CASE("read_adcirc_ascii: the station file's warnings come first",
          "[io][adcirc][files]") {
  const mov::test::ScratchDir dir;
  // Three stations in EPSG:26915, whose conversion to WGS 84 is approximate.
  mov::test::write_bytes(dir / "stations.csv",
                         "3\n788502.4,3320332.9\n402597.4,3208397.7\n"
                         "500000.0,4982950.4\n");
  const std::string text =
      fixture_text("io/adcirc/elevation_fewer_snapshots.txt");
  mov::test::write_bytes(dir / "fort.61", text);
  const auto result = mov::io::read_adcirc_ascii(
      dir / "fort.61", dir / "stations.csv", mov::test::epsg(26915),
      request(AdcircKind::elevation, StationSelection::all(3)), ReadContext{});
  REQUIRE(result.has_value());
  REQUIRE(result->warnings.size() == 2);
  CHECK(result->warnings[0].code == WarningCode::crs_approximate);
  CHECK(result->warnings[1].code == WarningCode::fewer_snapshots_than_header);
}

TEST_CASE("read_adcirc_ascii: missing files are FileErrors",
          "[io][adcirc][files]") {
  const auto no_output = mov::io::read_adcirc_ascii(
      mov::test::fixture("io/adcirc/legacy/no_such_file"),
      mov::test::fixture("io/adcirc/legacy/stations.csv"), Epsg::wgs84(),
      request(AdcircKind::elevation, StationSelection::all(3)), ReadContext{});
  REQUIRE(not(no_output.has_value()));
  CHECK(std::holds_alternative<mov::io::FileError>(no_output.error()));

  const auto no_stations = mov::io::read_adcirc_ascii(
      mov::test::fixture("io/adcirc/legacy/fort.61"),
      mov::test::fixture("io/adcirc/legacy/no_such_file.csv"), Epsg::wgs84(),
      request(AdcircKind::elevation, StationSelection::all(3)), ReadContext{});
  REQUIRE(not(no_stations.has_value()));
  CHECK(std::holds_alternative<mov::io::FileError>(no_stations.error()));
}

TEST_CASE("read_adcirc_ascii: a station file of another count",
          "[io][adcirc][files]") {
  const auto result = mov::io::read_adcirc_ascii(
      mov::test::fixture("io/adcirc/legacy/fort.61"),
      mov::test::fixture("io/adcirc/stations_crlf.csv"), Epsg::wgs84(),
      request(AdcircKind::elevation, StationSelection::all(3)), ReadContext{});
  // CRLF fixture has three stations too: this one reads.
  CHECK(result.has_value());
  const auto bad = mov::io::read_adcirc_ascii(
      mov::test::fixture("io/adcirc/legacy/fort.61"),
      mov::test::fixture("io/adcirc/stations_names.csv"), Epsg::wgs84(),
      request(AdcircKind::elevation, StationSelection::all(4)), ReadContext{});
  CHECK(format_error_of(bad).code == FormatErrc::station_count_mismatch);
}
