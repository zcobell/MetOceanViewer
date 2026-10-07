// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// format_imeds / write_imeds (docs/core-design.md section 5.2, D15; N18).

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/meta.hpp"
#include "mov/core/units.hpp"
#include "mov/io/imeds.hpp"
#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"
#include "table_helpers.hpp"

namespace {

namespace core = mov::core;
namespace io = mov::io;
namespace test = mov::test;
using io::Warning;
using io::WarningCode;

core::SeriesMeta meta_with(std::optional<core::Unit> unit,
                           std::optional<core::VerticalDatum> datum) {
  core::SeriesMeta meta = core::SeriesMeta::make({.unit = std::move(unit)});
  if (datum) {
    const auto with = meta.assume_datum(*datum);
    REQUIRE(with.has_value());
    meta = *with;
  }
  return meta;
}

std::string formatted(const core::StationTable& t,
                      std::string_view source = "MetOceanViewer") {
  const auto text = io::format_imeds(t, source);
  REQUIRE(text.has_value());
  return text->value;
}

core::StationTable synthetic() {
  return test::one_column_table(
      meta_with(core::Unit{core::LengthUnit::foot}, core::VerticalDatum::mllw),
      {{.id = "A1",
        .name = "Bar Harbor, ME",
        .where = test::location(44.3917, -68.205),
        .times = {test::utc(2015, 7, 1, 0, 0), test::utc(2015, 7, 1, 0, 6),
                  test::utc(2015, 7, 1, 0, 12)},
        .samples = {test::value(2.605), core::Missing{}, test::value(-0.5)}},
       {.id = "B2",
        .name = "",
        .where = test::location(40.4669, -74.0094),
        .times = {test::utc(1969, 12, 31, 23, 59, 59),
                  test::utc(2000, 2, 29, 12, 34, 56, 789)},
        .samples = {test::value(1.0), test::value(1234.5678)}}});
}

}  // namespace

TEST_CASE("format_imeds writes the pinned layout (D15)",
          "[io][imeds][format]") {
  const auto text = io::format_imeds(synthetic());
  REQUIRE(text.has_value());
  CHECK(text->value ==
        "% IMEDS generic format\n"
        "% year month day hour min sec value\n"
        "MetOceanViewer    UTC    MLLW    ft\n"
        "Bar_Harbor_ME    44.391700    -68.205000\n"
        "2015 07 01 00 00 00       2.605000\n"
        "2015 07 01 00 12 00      -0.500000\n"
        "station_1    40.466900    -74.009400\n"
        "1969 12 31 23 59 59       1.000000\n"
        "2000 02 29 12 34 56    1234.567800\n");
  // One time was cut to whole seconds.
  CHECK(text->warnings ==
        std::vector{Warning{.code = WarningCode::time_precision_dropped,
                            .subject = "",
                            .count = 1}});
}

TEST_CASE("the datum and unit are 'none' and 'unknown' when unset",
          "[io][imeds][format]") {
  const auto table =
      test::one_column_table(meta_with(std::nullopt, std::nullopt),
                             {{.id = "S",
                               .name = "S",
                               .where = test::location(1.0, 2.0),
                               .times = {test::utc(2020, 1, 1)},
                               .samples = {test::value(1.0)}}});
  const std::string text = formatted(table);
  CHECK(
      text.starts_with("% IMEDS generic format\n% year month day hour min "
                       "sec value\nMetOceanViewer    UTC    none    "
                       "unknown\n"));
}

TEST_CASE("a unit that is not in a family is written by its symbol",
          "[io][imeds][format]") {
  const auto unit = core::parse_unit("S m-1");
  REQUIRE(unit.has_value());
  const auto table = test::one_column_table(meta_with(*unit, std::nullopt),
                                            {{.id = "S",
                                              .name = "S",
                                              .where = test::location(1.0, 2.0),
                                              .times = {},
                                              .samples = {}}});
  CHECK(formatted(table).contains("    UTC    none    S m-1\n"));
  // And it reads back as the same unit (the unit is the rest of the line).
  const auto back = io::parse_imeds(formatted(table));
  REQUIRE(back.has_value());
  CHECK(back->value.header.unit == unit);
}

TEST_CASE("coordinates keep six decimals (v4 kept six significant digits, N18)",
          "[io][imeds][format][regression][N18]") {
  const auto table =
      test::one_column_table(meta_with(std::nullopt, std::nullopt),
                             {{.id = "S",
                               .name = "S",
                               .where = test::location(29.987793, -90.0127),
                               .times = {},
                               .samples = {}}});
  CHECK(formatted(table).contains("S    29.987793    -90.012700\n"));
}

TEST_CASE(
    "Missing and Dry samples are omitted, never printed as a number "
    "(N18)",
    "[io][imeds][format][regression][N18]") {
  const auto table = test::one_column_table(
      meta_with(std::nullopt, std::nullopt),
      {{.id = "S",
        .name = "S",
        .where = test::location(1.0, 2.0),
        .times = {test::utc(2020, 1, 1, 0), test::utc(2020, 1, 1, 1),
                  test::utc(2020, 1, 1, 2)},
        .samples = {core::Missing{}, core::Dry{}, test::value(7.0)}},
       {.id = "T",
        .name = "T",
        .where = test::location(3.0, 4.0),
        .times = {test::utc(2020, 1, 1, 0)},
        .samples = {core::Dry{}}}});
  const std::string text = formatted(table);
  CHECK(
      text.ends_with("S    1.000000    2.000000\n"
                     "2020 01 01 02 00 00       7.000000\n"
                     "T    3.000000    4.000000\n"));
  CHECK(not text.contains("e+"));
  CHECK(not text.contains("99999"));
}

TEST_CASE("times are floored to whole seconds, before and after the epoch",
          "[io][imeds][format]") {
  const auto table = test::one_column_table(
      meta_with(std::nullopt, std::nullopt),
      {{.id = "S",
        .name = "S",
        .where = test::location(1.0, 2.0),
        .times = {test::utc(1969, 12, 31, 23, 59, 59, 500),
                  test::utc(2020, 1, 1, 0, 0, 0, 999)},
        .samples = {test::value(1.0), test::value(2.0)}}});
  const auto text = io::format_imeds(table);
  REQUIRE(text.has_value());
  CHECK(
      text->value.ends_with("1969 12 31 23 59 59       1.000000\n"
                            "2020 01 01 00 00 00       2.000000\n"));
  CHECK(text->warnings.size() == 1);
  CHECK(text->warnings.front().count == 2);
}

TEST_CASE("two rows in one second keep the first, with warnings",
          "[io][imeds][format]") {
  const auto table = test::one_column_table(
      meta_with(std::nullopt, std::nullopt),
      {{.id = "S",
        .name = "S",
        .where = test::location(1.0, 2.0),
        .times = {test::utc(2020, 1, 1, 0, 0, 0, 100),
                  test::utc(2020, 1, 1, 0, 0, 0, 900),
                  test::utc(2020, 1, 1, 0, 0, 1, 0)},
        .samples = {test::value(1.0), test::value(2.0), test::value(3.0)}}});
  const auto text = io::format_imeds(table);
  REQUIRE(text.has_value());
  CHECK(
      text->value.ends_with("2020 01 01 00 00 00       1.000000\n"
                            "2020 01 01 00 00 01       3.000000\n"));
  CHECK(text->warnings ==
        std::vector{Warning{.code = WarningCode::time_precision_dropped,
                            .subject = "",
                            .count = 2},
                    Warning{.code = WarningCode::duplicate_times_dropped,
                            .subject = "",
                            .count = 1}});
}

TEST_CASE("a non-value in the same second is not a collision",
          "[io][imeds][format]") {
  const auto table = test::one_column_table(
      meta_with(std::nullopt, std::nullopt),
      {{.id = "S",
        .name = "S",
        .where = test::location(1.0, 2.0),
        .times = {test::utc(2020, 1, 1, 0, 0, 0, 100),
                  test::utc(2020, 1, 1, 0, 0, 0, 900)},
        .samples = {core::Missing{}, test::value(2.0)}}});
  const auto text = io::format_imeds(table);
  REQUIRE(text.has_value());
  CHECK(text->value.ends_with("2020 01 01 00 00 00       2.000000\n"));
  CHECK(text->warnings ==
        std::vector{Warning{.code = WarningCode::time_precision_dropped,
                            .subject = "",
                            .count = 1}});
}

TEST_CASE("a time outside the years 0000-9999 cannot be written",
          "[io][imeds][format]") {
  for (const core::Time t : {test::utc(10000, 1, 1), test::utc(-1, 12, 31)}) {
    const auto table =
        test::one_column_table(meta_with(std::nullopt, std::nullopt),
                               {{.id = "S",
                                 .name = "S",
                                 .where = test::location(1.0, 2.0),
                                 .times = {t},
                                 .samples = {test::value(1.0)}}});
    const auto text = io::format_imeds(table);
    REQUIRE(not text.has_value());
    CHECK(text.error() ==
          io::FormatError{.code = io::FormatErrc::time_out_of_range,
                          .subject = "S",
                          .station = 0,
                          .index = 0});
  }
  // The edges are fine.
  const auto edges = test::one_column_table(
      meta_with(std::nullopt, std::nullopt),
      {{.id = "S",
        .name = "S",
        .where = test::location(1.0, 2.0),
        .times = {test::utc(0, 1, 1), test::utc(9999, 12, 31, 23, 59, 59)},
        .samples = {test::value(1.0), test::value(2.0)}}});
  const std::string text = formatted(edges);
  CHECK(text.contains("0000 01 01 00 00 00 "));
  CHECK(text.contains("9999 12 31 23 59 59 "));
}

TEST_CASE("IMEDS holds one quantity: any other column count is an error",
          "[io][imeds][format]") {
  const auto none = io::format_imeds(core::StationTable{});
  REQUIRE(not none.has_value());
  CHECK(none.error().code == io::FormatErrc::wrong_column_count);
  CHECK(none.error().subject == "0");

  // Two columns: a station with u and v.
  std::vector<core::TimeAxis> axes{{test::utc(2020, 1, 1)}};
  std::vector<core::Variable> variables;
  variables.push_back(
      {.meta = core::SeriesMeta::make({.quantity = core::Quantity::current_u,
                                       .unit = core::LengthUnit::meter}),
       .per_station = {{test::value(1.0)}}});
  variables.push_back(
      {.meta = core::SeriesMeta::make({.quantity = core::Quantity::current_v,
                                       .unit = core::LengthUnit::meter}),
       .per_station = {{test::value(2.0)}}});
  const test::StationSpec spec{.id = "S",
                               .name = "S",
                               .where = test::location(1.0, 2.0),
                               .times = {},
                               .samples = {}};
  std::vector<core::StationRow> rows;
  rows.push_back({.station = test::file_station(spec), .axis = 0});
  auto two = core::StationTable::make(std::move(variables), std::move(axes),
                                      std::move(rows));
  REQUIRE(two.has_value());
  const auto text = io::format_imeds(*two);
  REQUIRE(not text.has_value());
  CHECK(text.error().code == io::FormatErrc::wrong_column_count);
  CHECK(text.error().subject == "2");
}

TEST_CASE("a table without stations is the three header lines",
          "[io][imeds][format]") {
  const auto table = test::one_column_table(
      meta_with(core::Unit{core::LengthUnit::meter}, core::VerticalDatum::msl),
      {});
  CHECK(formatted(table) ==
        "% IMEDS generic format\n"
        "% year month day hour min sec value\n"
        "MetOceanViewer    UTC    MSL    m\n");
}

TEST_CASE("the source is cleaned like a name and defaults when empty",
          "[io][imeds][format]") {
  const auto table =
      test::one_column_table(meta_with(std::nullopt, std::nullopt), {});
  CHECK(formatted(table, "My Tool, v1").contains("\nMy_Tool_v1    UTC    "));
  CHECK(formatted(table, "").contains("\nMetOceanViewer    UTC    "));
  CHECK(formatted(table, " , ").contains("\n_    UTC    "));
}

TEST_CASE("station names: runs of white space or commas become one _",
          "[io][imeds][format][regression]") {
  using io::detail::imeds_name;
  CHECK(imeds_name("Bar Harbor", 0) == "Bar_Harbor");
  // v4: "a, b" -> "a__b" -> "a_b" by a single replacement; a run is the rule.
  CHECK(imeds_name("a, b", 0) == "a_b");
  CHECK(imeds_name("a ,  , b", 0) == "a_b");
  CHECK(imeds_name("a\tb\r\nc", 0) == "a_b_c");
  CHECK(imeds_name(" lead", 0) == "_lead");
  CHECK(imeds_name("trail ", 0) == "trail_");
  CHECK(imeds_name(",,,", 0) == "_");
  CHECK(imeds_name("Ba\xC3\xAD"
                   "a",
                   0) ==
        "Ba\xC3\xAD"
        "a");
  CHECK(imeds_name("a#2", 0) == "a#2");
  CHECK(imeds_name("", 0) == "station_0");
  CHECK(imeds_name("", 12) == "station_12");
}

// ---- round trips
// -------------------------------------------------------------------

namespace {

void check_round_trip(std::string_view fixture_name) {
  const auto text =
      test::read_bytes(test::fixture(std::format("io/imeds/{}", fixture_name)));
  const auto first = io::parse_imeds(text);
  REQUIRE(first.has_value());
  const auto written = io::format_imeds(first->value.table);
  REQUIRE(written.has_value());
  const auto second = io::parse_imeds(written->value);
  REQUIRE(second.has_value());
  // The names are still equal in the file, so the ids are renamed again.
  const bool renames = fixture_name == "duplicate_stations.imeds";
  CHECK(second->warnings.empty() != renames);
  CHECK(second->value.table == first->value.table);
  CHECK(second->value.header.datum == first->value.header.datum);
  CHECK(second->value.header.unit == first->value.header.unit);
  CHECK(second->value.header.time_zone == "UTC");
}

}  // namespace

TEST_CASE("read -> write -> read is the identity on the fixtures",
          "[io][imeds][format][roundtrip]") {
  for (const std::string_view name :
       {"mllw_small.imeds", "msl_small.imeds", "six_field.imeds",
        "mixed_fields.imeds", "unsorted.imeds", "empty_station.imeds",
        "blank_line.imeds", "header_tokens.imeds", "header_unit_ft.imeds",
        "tabs.imeds", "crlf.imeds", "bom.imeds", "utf8_name.imeds",
        "longitude_360.imeds", "duplicate_stations.imeds",
        "header_only.imeds"}) {
    CAPTURE(name);
    check_round_trip(name);
  }
}

TEST_CASE("masked sentinels are not written, and the rest round-trips",
          "[io][imeds][format][roundtrip]") {
  const auto first = io::parse_imeds(
      test::read_bytes(test::fixture("io/imeds/sentinels.imeds")));
  REQUIRE(first.has_value());
  const auto written = io::format_imeds(first->value.table);
  REQUIRE(written.has_value());
  const auto second = io::parse_imeds(written->value);
  REQUIRE(second.has_value());
  CHECK(second->warnings.empty());  // no sentinel left to mask
  const std::vector<std::optional<double>> kept{-99998.9, -999.0, -9999.5,
                                                1.25};
  CHECK(test::numbers(second->value.table, 0) == kept);
}

TEST_CASE("values survive to the fixed precision, large ones exactly",
          "[io][imeds][format][roundtrip]") {
  const auto table = test::one_column_table(
      meta_with(std::nullopt, std::nullopt),
      {{.id = "S",
        .name = "S",
        .where = test::location(1.0, 2.0),
        .times = {test::utc(2020, 1, 1, 0), test::utc(2020, 1, 1, 1),
                  test::utc(2020, 1, 1, 2)},
        .samples = {test::value(1.0e300), test::value(0.1234564),
                    test::value(-1.0e-7)}}});
  const auto back = io::parse_imeds(formatted(table));
  REQUIRE(back.has_value());
  const auto values = test::numbers(back->value.table, 0);
  REQUIRE(values.size() == 3);
  CHECK(values[0] == 1.0e300);   // printed in full by {:.6f}
  CHECK(values[1] == 0.123456);  // rounded to six decimals
  CHECK(values[2] == 0.0);       // below the precision
}

TEST_CASE("a longitude of 190 is -170 in the table and in the file",
          "[io][imeds][format][roundtrip]") {
  const auto table =
      test::one_column_table(meta_with(std::nullopt, std::nullopt),
                             {{.id = "S",
                               .name = "S",
                               .where = test::location(10.0, 190.0),
                               .times = {},
                               .samples = {}}});
  CHECK(formatted(table).contains("S    10.000000    -170.000000\n"));
}

// ---- write_imeds
// ---------------------------------------------------------------------

TEST_CASE("write_imeds writes format_imeds's bytes and replaces a file",
          "[io][imeds][format]") {
  const test::ScratchDir dir;
  const auto path = dir / "out.imeds";
  test::write_bytes(path, "old content");
  const auto table = synthetic();
  const auto written = io::write_imeds(path, table);
  REQUIRE(written.has_value());
  CHECK(test::read_bytes(path) == formatted(table));
  CHECK(*written ==
        std::vector{Warning{.code = WarningCode::time_precision_dropped,
                            .subject = "",
                            .count = 1}});
  CHECK(test::entry_names(dir.path()) == std::vector<std::string>{"out.imeds"});

  // A source other than the default.
  const auto custom = io::write_imeds(path, table, "NOAA");
  REQUIRE(custom.has_value());
  CHECK(test::read_bytes(path).contains("\nNOAA    UTC    MLLW    ft\n"));
}

TEST_CASE("write_imeds reports a format error and leaves the file alone",
          "[io][imeds][format]") {
  const test::ScratchDir dir;
  const auto path = dir / "keep.imeds";
  test::write_bytes(path, "keep me");
  const auto written = io::write_imeds(path, core::StationTable{});
  REQUIRE(not written.has_value());
  const auto* format = std::get_if<io::FormatError>(&written.error());
  REQUIRE(format != nullptr);
  CHECK(format->code == io::FormatErrc::wrong_column_count);
  CHECK(test::read_bytes(path) == "keep me");
  CHECK(test::entry_names(dir.path()) ==
        std::vector<std::string>{"keep.imeds"});
}

TEST_CASE("write_imeds reports a directory that does not exist",
          "[io][imeds][format]") {
  const test::ScratchDir dir;
  const auto written =
      io::write_imeds(dir / "no_such_dir" / "out.imeds", synthetic());
  REQUIRE(not written.has_value());
  CHECK(std::holds_alternative<io::FileError>(written.error()));
}

TEST_CASE("write_imeds -> read_imeds round-trips through a file",
          "[io][imeds][format][roundtrip]") {
  const test::ScratchDir dir;
  const auto path = dir / "rt.imeds";
  const auto first = io::read_imeds(test::fixture("io/imeds/msl_small.imeds"),
                                    io::ReadContext{});
  REQUIRE(first.has_value());
  REQUIRE(io::write_imeds(path, first->value.table, "NOAA").has_value());
  const auto second = io::read_imeds(path, io::ReadContext{});
  REQUIRE(second.has_value());
  CHECK(second->value == first->value);  // header and table
}
