// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// parse_imeds / read_imeds (docs/core-design.md section 5.2,
// docs/legacy-formats.md section 2). Fixtures: tests/fixtures/io/imeds/. The
// mllw_small and msl_small files are excerpts of
// MetOceanViewer/function_tests/ReadIMEDS/{mllw,msl}.imeds (mllw has no final
// newline, as the original); the [legacy] tests read the originals when the
// legacy tree is present.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/station_table.hpp"
#include "mov/core/units.hpp"
#include "mov/io/imeds.hpp"
#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"
#include "table_helpers.hpp"

namespace {

namespace core = mov::core;
namespace io = mov::io;
namespace test = mov::test;
using io::ParseErrc;
using io::Warning;
using io::WarningCode;

std::string slurp(std::string_view name) {
  return test::read_bytes(test::fixture(std::format("io/imeds/{}", name)));
}

io::Read<io::ImedsFile> parse_fixture(std::string_view name) {
  auto parsed = io::parse_imeds(slurp(name));
  REQUIRE(parsed.has_value());
  return *std::move(parsed);
}

io::ParseError parse_error(std::string_view name) {
  const auto parsed = io::parse_imeds(slurp(name));
  REQUIRE(not parsed.has_value());
  return parsed.error();
}

std::string id_of(const core::StationTable& t, std::size_t i) {
  return std::string{t.station(core::StationIndex{i}).id.view()};
}

std::string name_of(const core::StationTable& t, std::size_t i) {
  return std::string{t.station(core::StationIndex{i}).name.view()};
}

std::vector<std::optional<double>> nums(const core::StationTable& t,
                                        std::size_t i) {
  return test::numbers(t, i);
}

std::size_t rows_of(const core::StationTable& t, std::size_t i) {
  return t.times(core::StationIndex{i}).size();
}

void check_error(const io::ParseError& e, ParseErrc code, std::size_t line,
                 std::optional<std::size_t> column) {
  CHECK(e.code() == code);
  CHECK(e.line() == line);
  CHECK(e.column() == column);
}

}  // namespace

// ---- the legacy fixtures ----------------------------------------------------

TEST_CASE("mllw_small: two stations, header tokens, no final newline",
          "[io][imeds]") {
  const auto read = parse_fixture("mllw_small.imeds");
  const io::ImedsFile& file = read.value;
  CHECK(read.warnings.empty());
  CHECK(file.header.source == "NOAA");
  CHECK(file.header.time_zone == "UTC");
  CHECK(file.header.datum == core::VerticalDatum::mllw);
  CHECK(file.header.unit == std::nullopt);

  const core::StationTable& t = file.table;
  REQUIRE(t.size() == 2);
  REQUIRE(t.schema().size() == 1);
  CHECK(t.schema()[0].quantity() ==
        core::QuantityId{core::GenericQuantity::value()});
  CHECK(t.schema()[0].datum() == core::VerticalDatum::mllw);
  CHECK(id_of(t, 0) == "NOAA_8413320");
  CHECK(id_of(t, 1) == "NOAA_8531680");
  CHECK(t.station(core::StationIndex{0}).location ==
        test::location(44.3917, -68.205));
  CHECK(t.station(core::StationIndex{1}).location ==
        test::location(40.4669, -74.0094));
  CHECK(t.station(core::StationIndex{0}).source == core::DataSource::user);
  CHECK(not t.station(core::StationIndex{0}).native.has_value());

  CHECK(rows_of(t, 0) == 8);
  CHECK(rows_of(t, 1) == 8);  // the last row has no newline after it
  CHECK(nums(t, 0).front() == 2.605);
  CHECK(nums(t, 0).back() == 3.113);
  CHECK(nums(t, 1).back() == 1.691);
  CHECK(t.times(core::StationIndex{0}).front() == test::utc(2015, 7, 1));
  CHECK(t.times(core::StationIndex{0})[1] == test::utc(2015, 7, 1, 0, 6));
  CHECK(t.times(core::StationIndex{1}).back() == test::utc(2015, 7, 1, 0, 42));
}

TEST_CASE("msl_small: another datum, station order differs, final newline",
          "[io][imeds]") {
  const auto read = parse_fixture("msl_small.imeds");
  const core::StationTable& t = read.value.table;
  CHECK(read.value.header.datum == core::VerticalDatum::msl);
  REQUIRE(t.size() == 2);
  CHECK(id_of(t, 0) == "NOAA_8534720");
  CHECK(id_of(t, 1) == "NOAA_8413320");
  CHECK(rows_of(t, 0) == 8);
  CHECK(rows_of(t, 1) == 8);
  CHECK(nums(t, 0).front() == 0.928);
}

namespace {

std::filesystem::path legacy_file(std::string_view name) {
  const std::filesystem::path repo =
      std::filesystem::path{mov::test::fixtures_dir}
          .parent_path()
          .parent_path();
  return repo / "MetOceanViewer" / "function_tests" / "ReadIMEDS" /
         std::string{name};
}

}  // namespace

TEST_CASE("the original legacy fixtures read with the counts v4 saw",
          "[io][imeds][legacy]") {
  const auto mllw_path = legacy_file("mllw.imeds");
  const auto msl_path = legacy_file("msl.imeds");
  if (not std::filesystem::exists(mllw_path) or
      not std::filesystem::exists(msl_path)) {
    SKIP("the legacy tree is not present");
  }
  const auto mllw = io::read_imeds(mllw_path, io::ReadContext{});
  REQUIRE(mllw.has_value());
  CHECK(mllw->warnings.empty());
  const core::StationTable& a = mllw->value.table;
  REQUIRE(a.size() == 2);
  CHECK(rows_of(a, 0) == 7681);
  CHECK(rows_of(a, 1) == 7681);
  CHECK(nums(a, 0).front() == 2.605);
  CHECK(nums(a, 0).back() == 0.735);
  CHECK(nums(a, 1).front() == 1.788);
  CHECK(nums(a, 1).back() == 1.705);  // the file's last line has no newline
  CHECK(a.times(core::StationIndex{0}).back() == test::utc(2015, 8, 2));

  const auto msl = io::read_imeds(msl_path, io::ReadContext{});
  REQUIRE(msl.has_value());
  CHECK(msl->warnings.empty());
  const core::StationTable& b = msl->value.table;
  REQUIRE(b.size() == 2);
  CHECK(id_of(b, 1) == "NOAA_8413320");
  CHECK(rows_of(b, 0) == 7681);
  CHECK(nums(b, 1).back() == -0.993);
}

// ---- the header
// ---------------------------------------------------------------

TEST_CASE("header tokens: source, zone, datum and a unit made of the rest",
          "[io][imeds][header]") {
  const auto read = parse_fixture("header_tokens.imeds");
  CHECK(read.warnings.empty());
  CHECK(read.value.header.source == "NOAA_CO-OPS");
  CHECK(read.value.header.time_zone == "UTC");
  CHECK(read.value.header.datum == core::VerticalDatum::navd88);
  CHECK(read.value.header.unit ==
        std::optional<core::Unit>{core::SpeedUnit::meter_per_second});
  CHECK(read.value.table.schema()[0].unit() == read.value.header.unit);
  CHECK(read.value.table.schema()[0].datum() == core::VerticalDatum::navd88);
}

TEST_CASE("a unit token becomes the column's unit", "[io][imeds][header]") {
  const auto read = parse_fixture("header_unit_ft.imeds");
  CHECK(read.warnings.empty());
  CHECK(read.value.header.unit ==
        std::optional<core::Unit>{core::LengthUnit::foot});
  CHECK(read.value.table.schema()[0].unit() == read.value.header.unit);
}

TEST_CASE("a time zone other than UTC is kept and reported",
          "[io][imeds][header]") {
  const auto read = parse_fixture("header_tz_cst.imeds");
  CHECK(read.value.header.time_zone == "CST");
  CHECK(read.warnings ==
        std::vector{
            Warning{.code = WarningCode::tz_assumed_utc, .subject = "CST"}});
  // Times are read as UTC all the same.
  CHECK(read.value.table.times(core::StationIndex{0}).front() ==
        test::utc(2020, 1, 1));
}

TEST_CASE("an unknown datum or an unrecognized unit is a warning",
          "[io][imeds][header]") {
  const auto datum = parse_fixture("header_datum_unknown.imeds");
  CHECK(datum.value.header.datum == std::nullopt);
  CHECK(datum.value.header.unit ==
        std::optional<core::Unit>{core::LengthUnit::foot});
  CHECK(datum.warnings ==
        std::vector{
            Warning{.code = WarningCode::datum_unknown, .subject = "LWI"}});

  const auto unit = parse_fixture("header_unit_odd.imeds");
  REQUIRE(unit.value.header.unit.has_value());
  CHECK(std::holds_alternative<core::OtherUnit>(*unit.value.header.unit));
  CHECK(unit.warnings ==
        std::vector{Warning{.code = WarningCode::unrecognized_unit,
                            .subject = "furlongs"}});
}

TEST_CASE("only the source is required in line 3", "[io][imeds][header]") {
  const auto read = parse_fixture("header_source_only.imeds");
  CHECK(read.value.header.source == "NOAA");
  CHECK(read.value.header.time_zone.empty());
  CHECK(read.value.header.datum == std::nullopt);
  CHECK(
      read.warnings ==
      std::vector{Warning{.code = WarningCode::tz_assumed_utc, .subject = ""}});
}

TEST_CASE("the datum 'none' and the unit 'unknown' are no datum and no unit",
          "[io][imeds][header]") {
  const auto read = parse_fixture("sentinels.imeds");  // none / unknown
  CHECK(read.value.header.source == "MetOceanViewer");
  CHECK(read.value.header.datum == std::nullopt);
  CHECK(read.value.header.unit == std::nullopt);
  CHECK(read.value.table.schema()[0].datum() == std::nullopt);
  CHECK(read.value.table.schema()[0].unit() == std::nullopt);
}

TEST_CASE("an empty input or too few header lines is an error",
          "[io][imeds][header]") {
  check_error(parse_error("empty.imeds"), ParseErrc::empty_input, 1,
              std::nullopt);
  check_error(parse_error("header_short.imeds"), ParseErrc::missing_header, 3,
              std::nullopt);
  check_error(parse_error("header_blank_source.imeds"),
              ParseErrc::missing_header, 3, std::nullopt);
  // The error names the first line that is missing.
  const auto one_line = io::parse_imeds("only one line\n");
  REQUIRE(not one_line.has_value());
  check_error(one_line.error(), ParseErrc::missing_header, 2, std::nullopt);
  const auto bom_only = io::parse_imeds("\xEF\xBB\xBF");
  REQUIRE(not bom_only.has_value());
  CHECK(bom_only.error().code() == ParseErrc::empty_input);
}

TEST_CASE("a file with a header and no station is an empty table",
          "[io][imeds][header]") {
  const auto read = parse_fixture("header_only.imeds");
  CHECK(read.value.table.size() == 0);
  CHECK(read.value.table.schema().size() == 1);
  CHECK(read.warnings.empty());
}

// ---- row shapes (N3)
// -----------------------------------------------------------

TEST_CASE("a 6-word row has no seconds: the last word is the value (N3)",
          "[io][imeds][regression][N3]") {
  const auto read = parse_fixture("six_field.imeds");
  const core::StationTable& t = read.value.table;
  CHECK(read.warnings.empty());
  // v4 read 2015 07 01 00 00 2.605 as second 2 and the value 0.605.
  CHECK(nums(t, 0) == std::vector<std::optional<double>>{2.605, 3.0, -0.5});
  const auto times = t.times(core::StationIndex{0});
  CHECK(times[0] == test::utc(2015, 7, 1, 0, 0, 0));
  CHECK(times[1] == test::utc(2015, 7, 1, 0, 6, 0));
  CHECK(times[2] == test::utc(2015, 7, 1, 0, 12, 0));
}

TEST_CASE("6- and 7-word rows may be mixed", "[io][imeds][regression][N3]") {
  const auto read = parse_fixture("mixed_fields.imeds");
  const core::StationTable& t = read.value.table;
  CHECK(nums(t, 0) == std::vector<std::optional<double>>{2.605, 3.5, 4.5});
  CHECK(t.times(core::StationIndex{0})[1] == test::utc(2015, 7, 1, 0, 6, 30));
}

TEST_CASE("fractional seconds and trailing text are errors, not a value (N3)",
          "[io][imeds][regression][N3]") {
  // The 7th word is read as an integer second.
  check_error(parse_error("fractional_seconds.imeds"), ParseErrc::bad_integer,
              5, 17);
  // v4 accepted "... 2.6 abc": 8 words are neither a row nor a station.
  check_error(parse_error("trailing_garbage.imeds"),
              ParseErrc::wrong_field_count, 5, 0);
  check_error(parse_error("bad_number.imeds"), ParseErrc::bad_number, 5, 20);
}

TEST_CASE("other word counts are wrong_field_count", "[io][imeds]") {
  check_error(parse_error("two_token_line.imeds"), ParseErrc::wrong_field_count,
              4, 0);
  // Line 5 is a valid 6-word row; line 6 has 5 words.
  check_error(parse_error("wrong_field_count.imeds"),
              ParseErrc::wrong_field_count, 6, 0);
  check_error(parse_error("data_before_station.imeds"),
              ParseErrc::missing_header, 4, 0);
}

TEST_CASE("white space: tabs, CRLF, a byte order mark", "[io][imeds]") {
  const auto tabs = parse_fixture("tabs.imeds");
  CHECK(tabs.value.header.datum == core::VerticalDatum::msl);
  CHECK(tabs.value.header.unit ==
        std::optional<core::Unit>{core::LengthUnit::meter});
  CHECK(nums(tabs.value.table, 0) ==
        std::vector<std::optional<double>>{1.5, 1.75});

  const auto crlf = parse_fixture("crlf.imeds");
  CHECK(crlf.warnings.empty());
  CHECK(id_of(crlf.value.table, 0) == "C1");  // no CR stuck to the name
  CHECK(nums(crlf.value.table, 0) ==
        std::vector<std::optional<double>>{1.5, 1.75});

  const auto last = parse_fixture("minimal_crlf_no_final_newline.imeds");
  CHECK(nums(last.value.table, 0) == std::vector<std::optional<double>>{1.0});

  const auto bom = parse_fixture("bom.imeds");
  CHECK(bom.value.header.source == "MetOceanViewer");
  CHECK(nums(bom.value.table, 0) == std::vector<std::optional<double>>{1.5});
}

TEST_CASE("blank lines after the header are skipped (N13)",
          "[io][imeds][regression][N13]") {
  const auto read = parse_fixture("blank_line.imeds");
  const core::StationTable& t = read.value.table;
  CHECK(read.warnings.empty());
  REQUIRE(t.size() == 2);
  CHECK(nums(t, 0) == std::vector<std::optional<double>>{1.0, 2.0});
  CHECK(nums(t, 1) == std::vector<std::optional<double>>{3.0});
}

TEST_CASE("a station without rows is allowed (N17)",
          "[io][imeds][regression][N17]") {
  const auto read = parse_fixture("empty_station.imeds");
  const core::StationTable& t = read.value.table;
  REQUIRE(t.size() == 3);
  CHECK(t.times(core::StationIndex{0}).empty());
  CHECK(nums(t, 1) == std::vector<std::optional<double>>{1.0});
  CHECK(t.times(core::StationIndex{2}).empty());
}

// ---- dates and coordinates
// -----------------------------------------------------

TEST_CASE("an impossible date or time is bad_date at its word", "[io][imeds]") {
  check_error(parse_error("bad_date.imeds"), ParseErrc::bad_date, 6, 5);
  check_error(parse_error("bad_day.imeds"), ParseErrc::bad_date, 5, 8);
  check_error(parse_error("hour_24.imeds"), ParseErrc::bad_date, 5, 11);
  check_error(parse_error("year_10000.imeds"), ParseErrc::bad_date, 5, 0);
}

TEST_CASE("February 29 exists in a leap year", "[io][imeds]") {
  const std::string text =
      "a\nb\nNOAA UTC MSL\nS 1.0 2.0\n2024 02 29 23 59 59 1.0\n";
  const auto read = io::parse_imeds(text);
  REQUIRE(read.has_value());
  CHECK(read->value.table.times(core::StationIndex{0}).front() ==
        test::utc(2024, 2, 29, 23, 59, 59));
}

TEST_CASE("coordinates are latitude then longitude, checked by Location",
          "[io][imeds]") {
  check_error(parse_error("bad_latitude.imeds"), ParseErrc::out_of_range, 4, 6);
  check_error(parse_error("bad_longitude.imeds"), ParseErrc::out_of_range, 4,
              14);
  const auto wrapped = parse_fixture("longitude_360.imeds");
  CHECK(wrapped.value.table.station(core::StationIndex{0}).location ==
        test::location(30.0, -90.0));
}

TEST_CASE("a value that is not a number or does not fit is an error",
          "[io][imeds]") {
  check_error(parse_error("nan_value.imeds"), ParseErrc::bad_number, 5, 20);
  check_error(parse_error("value_overflow.imeds"), ParseErrc::out_of_range, 5,
              20);
}

// ---- sentinels (C9)
// --------------------------------------------------------------

TEST_CASE("only the exact legacy sentinels are Missing, with a count",
          "[io][imeds][regression][C9][N18]") {
  const auto read = parse_fixture("sentinels.imeds");
  const std::vector<std::optional<double>> expected{
      std::nullopt,  // -99999
      std::nullopt,  // -9999
      std::nullopt,  // -DBL_MAX
      std::nullopt,  // -1.7977e+308 (what v4 printed for its null)
      -99998.9,      // a value, not a fill
      -999.0,        // a value: the D16 dry rule is not an IMEDS rule
      -9999.5,       // a value
      std::nullopt,  // -99999.0
      1.25};
  CHECK(nums(read.value.table, 0) == expected);
  CHECK(read.warnings ==
        std::vector{Warning{.code = WarningCode::legacy_sentinel_masked,
                            .subject = "",
                            .count = 5}});
  // The masked rows stay in the table, as Missing, not Dry.
  CHECK(read.value.table.total_samples() == 9);
  CHECK(read.value.table.column(core::StationIndex{0}, core::ColumnIndex{0})[0]
            .is_missing());
}

// ---- order, duplicates, names
// -----------------------------------------------------

TEST_CASE(
    "unsorted rows are sorted and duplicate times collapse, with warnings",
    "[io][imeds]") {
  const auto read = parse_fixture("unsorted.imeds");
  const core::StationTable& t = read.value.table;
  CHECK(nums(t, 0) == std::vector<std::optional<double>>{1.0, 2.0, 3.0, 4.0});
  CHECK(t.times(core::StationIndex{0})[0] == test::utc(2020, 1, 1, 0, 0));
  CHECK(t.times(core::StationIndex{0})[3] == test::utc(2020, 1, 1, 0, 18));
  CHECK(read.warnings ==
        std::vector{Warning{.code = WarningCode::times_reordered,
                            .subject = "U1",
                            .count = 1},
                    Warning{.code = WarningCode::duplicate_times_dropped,
                            .subject = "U1",
                            .count = 2},
                    Warning{.code = WarningCode::conflicting_duplicate_times,
                            .subject = "U1",
                            .count = 1}});
}

TEST_CASE("equal station names get #n suffixes in the id, with a warning",
          "[io][imeds]") {
  const auto read = parse_fixture("duplicate_stations.imeds");
  const core::StationTable& t = read.value.table;
  REQUIRE(t.size() == 5);
  CHECK(id_of(t, 0) == "A");
  CHECK(id_of(t, 1) == "B");
  CHECK(id_of(t, 2) == "A#2");
  CHECK(id_of(t, 3) == "A#3");
  CHECK(id_of(t, 4) == "A#2#2");  // a station really called A#2
  for (std::size_t i = 0; i < t.size(); ++i) {
    CHECK(name_of(t, i) == (i == 1 ? "B" : (i == 4 ? "A#2" : "A")));
  }
  CHECK(nums(t, 3) == std::vector<std::optional<double>>{4.0});
  CHECK(read.warnings ==
        std::vector{Warning{.code = WarningCode::duplicate_station_id_renamed,
                            .subject = "A",
                            .count = 2},
                    Warning{.code = WarningCode::duplicate_station_id_renamed,
                            .subject = "A#2",
                            .count = 1}});
}

TEST_CASE("names: UTF-8 is kept, bytes that are not UTF-8 and NUL are replaced",
          "[io][imeds]") {
  const auto utf8 = parse_fixture("utf8_name.imeds");
  CHECK(id_of(utf8.value.table, 0) ==
        "Ba\xC3\xAD"
        "a_Gal\xC3\xA1pagos");
  CHECK(utf8.warnings.empty());

  const auto bad = parse_fixture("bad_utf8_name.imeds");
  CHECK(id_of(bad.value.table, 0) == "Bad\xEF\xBF\xBD\xEF\xBF\xBDName");
  CHECK(bad.warnings ==
        std::vector{Warning{.code = WarningCode::invalid_utf8_replaced,
                            .subject = "",
                            .count = 1}});

  const auto nul = parse_fixture("nul_name.imeds");
  CHECK(id_of(nul.value.table, 0) == "Nul\xEF\xBF\xBDName");
  CHECK(nul.warnings.size() == 1);
}

// ---- read_imeds
// -----------------------------------------------------------------------

TEST_CASE("read_imeds reads a file like parse_imeds", "[io][imeds]") {
  const auto path = test::fixture("io/imeds/mllw_small.imeds");
  const auto file = io::read_imeds(path, io::ReadContext{});
  REQUIRE(file.has_value());
  const auto text = io::parse_imeds(slurp("mllw_small.imeds"));
  REQUIRE(text.has_value());
  CHECK(file->value == text->value);
  CHECK(file->warnings == text->warnings);
}

TEST_CASE(
    "read_imeds on a missing path or a directory is an error, not an "
    "empty table",
    "[io][imeds][regression][B3]") {
  const test::ScratchDir dir;
  const auto missing = io::read_imeds(dir / "nope.imeds", io::ReadContext{});
  REQUIRE(not missing.has_value());
  const auto* file_error = std::get_if<io::FileError>(&missing.error());
  REQUIRE(file_error != nullptr);
  CHECK(file_error->op == io::FileOp::open);
  CHECK(file_error->ec == std::errc::no_such_file_or_directory);

  const auto folder = io::read_imeds(dir.path(), io::ReadContext{});
  REQUIRE(not folder.has_value());
  CHECK(std::holds_alternative<io::FileError>(folder.error()));
}

TEST_CASE(
    "read_imeds reads a file the user cannot write (v4 returned no "
    "stations)",
    "[io][imeds][regression][B3]") {
  const test::ScratchDir dir;
  const auto copy = dir / "readonly.imeds";
  test::write_bytes(copy, slurp("mllw_small.imeds"));
  std::filesystem::permissions(
      copy,
      std::filesystem::perms::owner_read | std::filesystem::perms::group_read,
      std::filesystem::perm_options::replace);
  // The premise of the bug: opening for write must fail. A superuser can.
  if (std::ofstream{copy, std::ios::app | std::ios::binary}.is_open()) {
    SKIP("the process can write a 0444 file (running as root)");
  }
  const auto read = io::read_imeds(copy, io::ReadContext{});
  REQUIRE(read.has_value());
  CHECK(read->value.table.size() == 2);
}

TEST_CASE("read_imeds reports a parse error as io::Error", "[io][imeds]") {
  const auto read = io::read_imeds(test::fixture("io/imeds/bad_date.imeds"),
                                   io::ReadContext{});
  REQUIRE(not read.has_value());
  const auto* parse = std::get_if<io::ParseError>(&read.error());
  REQUIRE(parse != nullptr);
  CHECK(parse->code() == ParseErrc::bad_date);
}

TEST_CASE("read_imeds honours the limits", "[io][imeds]") {
  const auto path = test::fixture("io/imeds/mllw_small.imeds");
  io::ReadContext few_samples;
  few_samples.limits.max_elements = 3;
  const auto samples = io::read_imeds(path, few_samples);
  REQUIRE(not samples.has_value());
  const auto* too_many = std::get_if<io::FileError>(&samples.error());
  REQUIRE(too_many != nullptr);
  CHECK(too_many->op == io::FileOp::size);
  CHECK(too_many->ec == std::errc::file_too_large);

  io::ReadContext few_bytes;
  few_bytes.limits.max_text_bytes = 100;
  const auto bytes = io::read_imeds(path, few_bytes);
  REQUIRE(not bytes.has_value());
  const auto* too_big = std::get_if<io::FileError>(&bytes.error());
  REQUIRE(too_big != nullptr);
  CHECK(too_big->ec == std::errc::file_too_large);
}

TEST_CASE("read_imeds stops when asked", "[io][imeds]") {
  const auto path = test::fixture("io/imeds/mllw_small.imeds");
  const io::ReadContext stopped{.limits = {},
                                .stop = io::StopToken{[] { return true; }}};
  const auto at_once = io::read_imeds(path, stopped);
  REQUIRE(not at_once.has_value());
  CHECK(std::holds_alternative<io::Cancelled>(at_once.error()));

  // A long file is polled while it is parsed: the first question (before the
  // file is read) gets "no", the next one "yes".
  std::string text = "a\nb\nNOAA UTC MSL\nS 1.0 2.0\n";
  for (int i = 0; i < 5000; ++i) {
    text += std::format("2020 01 01 {:02} {:02} {:02} 1.0\n", i / 3600 % 24,
                        i / 60 % 60, i % 60);
  }
  const test::ScratchDir dir;
  test::write_bytes(dir / "long.imeds", text);
  int asked = 0;
  const io::ReadContext later{
      .limits = {}, .stop = io::StopToken{[&asked] { return ++asked > 1; }}};
  const auto mid = io::read_imeds(dir / "long.imeds", later);
  REQUIRE(not mid.has_value());
  CHECK(std::holds_alternative<io::Cancelled>(mid.error()));
  CHECK(asked == 2);

  const io::ReadContext never{};
  CHECK(io::read_imeds(dir / "long.imeds", never).has_value());
}
