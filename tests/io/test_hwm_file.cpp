// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "adcirc_test_support.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/hwm.hpp"
#include "mov/core/units.hpp"
#include "mov/io/error.hpp"
#include "mov/io/hwm_file.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"

namespace {

using mov::core::Dry;
using mov::core::HighWaterMark;
using mov::core::Length;
using mov::core::LengthUnit;
using mov::core::Location;
using mov::core::Wet;
using mov::io::Cancelled;
using mov::io::FileError;
using mov::io::parse_hwm_csv;
using mov::io::ParseErrc;
using mov::io::ReadContext;
using mov::io::StopToken;
using mov::io::WarningCode;
using mov::test::fixture_text;
using mov::test::parse_error_of;
using namespace std::string_literals;

using Result =
    std::expected<mov::io::Read<std::vector<HighWaterMark>>, mov::io::Error>;

Result parse(std::string_view text, LengthUnit unit = LengthUnit::meter) {
  return parse_hwm_csv(text, unit, ReadContext{});
}

std::vector<HighWaterMark> marks_of(std::string_view text,
                                    LengthUnit unit = LengthUnit::meter) {
  auto parsed = parse(text, unit);
  REQUIRE(parsed.has_value());
  CHECK(parsed->warnings.empty());
  return std::move(parsed->value);
}

Length m(double v) { return Length::in(v, LengthUnit::meter); }

std::optional<double> wet_metres(const HighWaterMark& mark) {
  if (const auto* wet = std::get_if<Wet>(&mark.modeled)) {
    return wet->elevation.as(LengthUnit::meter);
  }
  return std::nullopt;
}

}  // namespace

// ---- rows ----

TEST_CASE("a row is lon, lat, ground, observed, modeled and a difference",
          "[io][hwm]") {
  const auto marks = marks_of("-90.100,29.900,0.50,1.00,1.50,0.50\n");
  REQUIRE(marks.size() == 1);
  CHECK(marks[0].location == *Location::make({.lat = 29.9, .lon = -90.1}));
  CHECK(marks[0].ground == m(0.5));
  CHECK(marks[0].observed == m(1.0));
  CHECK(marks[0].modeled == mov::core::WetDry{Wet{.elevation = m(1.5)}});
}

TEST_CASE("the difference column is optional and ignored", "[io][hwm]") {
  const auto five = marks_of("-90.1,29.9,0.5,1.0,1.5\n");
  const auto six = marks_of("-90.1,29.9,0.5,1.0,1.5,999\n");
  const auto junk = marks_of("-90.1,29.9,0.5,1.0,1.5,not a number\n");
  const auto empty = marks_of("-90.1,29.9,0.5,1.0,1.5,\n");
  REQUIRE(five.size() == 1);
  CHECK(five == six);
  CHECK(five == junk);
  CHECK(five == empty);
}

TEST_CASE("blanks around fields are fine", "[io][hwm]") {
  // v4 simplified the line first, so "a, b" worked.
  const auto marks = marks_of("  -90.1, 29.9 ,\t0.5,  1.0 ,1.5  \n");
  REQUIRE(marks.size() == 1);
  CHECK(marks[0].observed == m(1.0));
}

TEST_CASE("the unit is the file's for all three elevations", "[io][hwm]") {
  const auto marks = marks_of("-90.1,29.9,3.0,10.0,4.0\n", LengthUnit::foot);
  REQUIRE(marks.size() == 1);
  CHECK(marks[0].ground == Length::in(3.0, LengthUnit::foot));
  CHECK(marks[0].observed == Length::in(10.0, LengthUnit::foot));
  CHECK(wet_metres(marks[0]) ==
        Length::in(4.0, LengthUnit::foot).as(LengthUnit::meter));
}

TEST_CASE("no final newline, CRLF and a BOM", "[io][hwm]") {
  const auto plain =
      marks_of("-90.1,29.9,0.5,1.0,1.5\n-90.2,29.9,0.6,2.0,1.75");
  CHECK(plain.size() == 2);
  const auto crlf =
      marks_of("-90.1,29.9,0.5,1.0,1.5\r\n-90.2,29.9,0.6,2.0,1.75\r\n");
  CHECK(crlf == plain);
  const auto bom = marks_of(
      "\xEF\xBB\xBF"
      "-90.1,29.9,0.5,1.0,1.5\n-90.2,29.9,0.6,2.0,1.75\n");
  CHECK(bom == plain);
}

TEST_CASE("fixtures: the BOM and CRLF file reads like the plain one",
          "[io][hwm]") {
  const auto plain = marks_of(fixture_text("core/hwm/hwm_basic.csv"));
  const auto bom = marks_of(fixture_text("io/hwm/hwm_bom_crlf.csv"));
  REQUIRE(plain.size() == 8);
  CHECK(bom == plain);
}

// ---- blank lines and the header ----

TEST_CASE("blank lines are skipped, not zero marks (N20)",
          "[io][hwm][regression][N20]") {
  // v4 turned each blank line into a mark at (0, 0) with zero elevations.
  const auto plain = marks_of(fixture_text("core/hwm/hwm_basic.csv"));
  const auto blank = marks_of(fixture_text("io/hwm/hwm_blank_line.csv"));
  CHECK(blank.size() == 8);
  CHECK(blank == plain);
}

TEST_CASE("a header line of words is skipped with a warning", "[io][hwm]") {
  const auto parsed = parse(fixture_text("io/hwm/hwm_header.csv"));
  REQUIRE(parsed.has_value());
  CHECK(parsed->value == marks_of(fixture_text("core/hwm/hwm_basic.csv")));
  REQUIRE(parsed->warnings.size() == 1);
  CHECK(parsed->warnings[0].code == WarningCode::header_line_skipped);
  CHECK(parsed->warnings[0].subject == "lon,lat,ground,observed,modeled,diff");
}

TEST_CASE("a header after blank lines, and a header with other words",
          "[io][hwm]") {
  const auto parsed =
      parse("\n\n# lon,lat,ground,observed,modeled\n-90.1,29.9,0.5,1.0,1.5\n");
  REQUIRE(parsed.has_value());
  CHECK(parsed->value.size() == 1);
  CHECK(parsed->warnings.size() == 1);
  const auto spaced = parse(
      "Longitude, Latitude, Ground, Measured, Modeled\n"
      "-90.1,29.9,0.5,1.0,1.5\n");
  REQUIRE(spaced.has_value());
  CHECK(spaced->value.size() == 1);
}

TEST_CASE("a header has the shape of a row: five or six fields", "[io][hwm]") {
  // Words with another number of fields are not a header; they are a bad row.
  for (const std::string_view first :
       {"# lon lat", "lon,lat,ground,observed", "a,b,c,d,e,f,g", "x"}) {
    INFO(first);
    const auto result =
        parse(std::string{first} + "\n-90.1,29.9,0.5,1.0,1.5\n");
    const auto& error = parse_error_of(result);
    CHECK(error.code() == ParseErrc::wrong_field_count);
    CHECK(error.line() == 1);
  }
  // Five or six fields of words: a header, with six the usual.
  CHECK(parse("a,b,c,d,e\n-90.1,29.9,0.5,1.0,1.5\n").has_value());
  CHECK(parse("a,b,c,d,e,f\n-90.1,29.9,0.5,1.0,1.5\n").has_value());
}

TEST_CASE("a first line with a bad number is an error, not a header",
          "[io][hwm]") {
  const auto result = parse(fixture_text("io/hwm/hwm_bad_first_line.csv"));
  const auto& error = parse_error_of(result);
  CHECK(error.code() == ParseErrc::bad_number);
  CHECK(error.line() == 1);
  CHECK(error.column() == std::optional<std::size_t>{8});
  CHECK(error.context() == "1.0,2.0,abc,4.0,5.0");
}

TEST_CASE("a line of NaN is data, so an error, not a header", "[io][hwm]") {
  const auto result = parse("nan,nan,nan,nan,nan\n-90.1,29.9,0.5,1.0,1.5\n");
  CHECK(parse_error_of(result).code() == ParseErrc::bad_number);
  const auto infinities = parse("inf,-inf,Infinity,NaN,****\n");
  CHECK(parse_error_of(infinities).code() == ParseErrc::bad_number);
}

TEST_CASE("only the first line can be a header", "[io][hwm]") {
  const auto result =
      parse("-90.1,29.9,0.5,1.0,1.5\nlon,lat,ground,obs,model\n");
  const auto& error = parse_error_of(result);
  CHECK(error.code() == ParseErrc::bad_number);
  CHECK(error.line() == 2);
}

TEST_CASE("a first line of numbers is data even when it looks like a header",
          "[io][hwm]") {
  const auto marks = marks_of("1,2,3,4,5\n");
  CHECK(marks.size() == 1);
}

TEST_CASE("a first line of separators is not a header", "[io][hwm]") {
  const auto result = parse(",,,,,\n");
  const auto& error = parse_error_of(result);
  CHECK(error.code() == ParseErrc::bad_number);
  CHECK(not(error.column().has_value()));
}

TEST_CASE("no marks is empty_input", "[io][hwm]") {
  for (const std::string_view text : {"", "\n", "  \n\t\n", "\xEF\xBB\xBF"}) {
    INFO(text);
    CHECK(parse_error_of(parse(text)).code() == ParseErrc::empty_input);
  }
  // A header alone is no marks either; its warning is lost with the result.
  CHECK(parse_error_of(parse(fixture_text("io/hwm/hwm_header_only.csv")))
            .code() == ParseErrc::empty_input);
}

// ---- field counts and numbers ----

TEST_CASE("a row needs five or six fields (v4 filled in zeros)", "[io][hwm]") {
  const auto four = parse(fixture_text("io/hwm/hwm_four_columns.csv"));
  const auto& error = parse_error_of(four);
  CHECK(error.code() == ParseErrc::wrong_field_count);
  CHECK(error.line() == 1);
  CHECK(not error.column().has_value());
  CHECK(parse_error_of(parse("-90.1,29.9,0.5,1.0,1.5,0.5,7\n")).code() ==
        ParseErrc::wrong_field_count);
  CHECK(parse_error_of(parse("-90.1\n")).code() ==
        ParseErrc::wrong_field_count);
  CHECK(parse_error_of(parse("-90.1,29.9,0.5,1.0,1.5\n,\n")).code() ==
        ParseErrc::wrong_field_count);
}

TEST_CASE("a bad number names its line and column", "[io][hwm]") {
  const auto result =
      parse("-90.1,29.9,0.5,1.0,1.5\n-90.2,29.9,0.6,1.x0,1.75\n");
  const auto& error = parse_error_of(result);
  CHECK(error.code() == ParseErrc::bad_number);
  CHECK(error.line() == 2);
  CHECK(error.column() == std::optional<std::size_t>{15});
}

TEST_CASE("an empty field is a bad number", "[io][hwm]") {
  const auto result = parse("-90.1,29.9,,1.0,1.5\n");
  const auto& error = parse_error_of(result);
  CHECK(error.code() == ParseErrc::bad_number);
  // An empty field has no column.
  CHECK(not(error.column().has_value()));
}

TEST_CASE("NaN and infinity are never accepted, in any column", "[io][hwm]") {
  for (const std::string_view row :
       {"nan,29.9,0.5,1.0,1.5", "-90.1,nan,0.5,1.0,1.5",
        "-90.1,29.9,nan,1.0,1.5", "-90.1,29.9,0.5,nan,1.5",
        "-90.1,29.9,0.5,1.0,nan", "-90.1,29.9,0.5,1.0,inf",
        "-90.1,29.9,0.5,1.0,-Infinity", "-90.1,29.9,0.5,inf,1.5"}) {
    INFO(row);
    const auto result = parse(std::string{row} + "\n-90.2,29.9,0.6,2.0,1.75\n");
    // The first line is data (it has a number-like field), not a header.
    CHECK(parse_error_of(result).code() == ParseErrc::bad_number);
  }
}

TEST_CASE("a number too big for a double is out_of_range", "[io][hwm]") {
  const auto result = parse("-90.1,29.9,0.5,1e999,1.5\n");
  const auto& error = parse_error_of(result);
  CHECK(error.code() == ParseErrc::out_of_range);
  CHECK(error.column() == std::optional<std::size_t>{15});
}

TEST_CASE("a position that is not a Location is out_of_range at its field",
          "[io][hwm]") {
  const auto latitude = parse("-90.1,91.0,0.5,1.0,1.5\n");
  CHECK(parse_error_of(latitude).code() == ParseErrc::out_of_range);
  CHECK(parse_error_of(latitude).column() == std::optional<std::size_t>{6});
  const auto longitude = parse("-190.1,29.0,0.5,1.0,1.5\n");
  CHECK(parse_error_of(longitude).code() == ParseErrc::out_of_range);
  CHECK(parse_error_of(longitude).column() == std::optional<std::size_t>{0});
  // 0..360 longitudes are normalized.
  const auto east = marks_of("270.0,29.0,0.5,1.0,1.5\n");
  CHECK(east[0].location == *Location::make({.lat = 29.0, .lon = -90.0}));
}

// ---- elevations and the dry rule ----

TEST_CASE("modeled values at or below -999 are Dry, anything above is wet",
          "[io][hwm][regression][N2]") {
  // One rule (D16) where v4 had -999, -9999 and -900.
  const auto marks = marks_of(fixture_text("io/hwm/hwm_dry_marks.csv"));
  REQUIRE(marks.size() == 5);
  CHECK(wet_metres(marks[0]) == 1.5);
  CHECK(marks[1].modeled == mov::core::WetDry{Dry{}});  // -99999
  CHECK(marks[2].modeled == mov::core::WetDry{Dry{}});  // -999
  CHECK(wet_metres(marks[3]) == -998.5);                // above the threshold
  CHECK(marks[4].modeled == mov::core::WetDry{Dry{}});  // -1.797e308
}

TEST_CASE("the dry rule is in the file's unit, before any conversion",
          "[io][hwm]") {
  // -998.5 ft is wet; -999 ft is dry, though -999 ft is -304 m.
  const auto marks = marks_of(
      "-90.1,29.9,0.5,1.0,-998.5\n-90.1,29.9,0.5,1.0,-999\n", LengthUnit::foot);
  REQUIRE(marks.size() == 2);
  CHECK(wet_metres(marks[0]) ==
        Length::in(-998.5, LengthUnit::foot).as(LengthUnit::meter));
  CHECK(marks[1].modeled == mov::core::WetDry{Dry{}});
}

TEST_CASE("an elevation beyond 1e4 m is out_of_range", "[io][hwm]") {
  // The statistics take |elevation| <= 1e4 m (max_elevation_m), so no sum
  // of squares overflows.
  const auto observed = parse(fixture_text("io/hwm/hwm_out_of_range.csv"));
  const auto& error = parse_error_of(observed);
  CHECK(error.code() == ParseErrc::out_of_range);
  CHECK(error.line() == 2);
  CHECK(error.column() == std::optional<std::size_t>{15});

  CHECK(parse_error_of(parse("-90.1,29.9,20000,1.0,1.5\n")).code() ==
        ParseErrc::out_of_range);
  CHECK(parse_error_of(parse("-90.1,29.9,0.5,1.0,10001\n")).code() ==
        ParseErrc::out_of_range);
  CHECK(parse_error_of(parse("-90.1,29.9,0.5,-10001,1.5\n")).code() ==
        ParseErrc::out_of_range);
  // The bound is in metres, whatever the unit: 1e4 m is 32808 ft.
  CHECK(parse("-90.1,29.9,0.5,32000,1.5\n", LengthUnit::foot).has_value());
  CHECK(parse_error_of(parse("-90.1,29.9,0.5,33000,1.5\n", LengthUnit::foot))
            .code() == ParseErrc::out_of_range);
  // And the edge itself is allowed.
  CHECK(parse("-90.1,29.9,0.5,10000,-10000\n").has_value());
}

TEST_CASE("a modeled value is not exempt from the bound when it is wet",
          "[io][hwm]") {
  const auto result = parse("-90.1,29.9,0.5,1.0,5e5\n");
  const auto& error = parse_error_of(result);
  CHECK(error.code() == ParseErrc::out_of_range);
  CHECK(error.column() == std::optional<std::size_t>{19});
}

// ---- the other fixtures ----

TEST_CASE("the core fixtures all parse", "[io][hwm]") {
  for (const auto& [name, count] :
       {std::pair{"hwm_basic.csv", 8}, std::pair{"hwm_ft.csv", 7},
        std::pair{"hwm_allDry.csv", 3}, std::pair{"hwm_one_wet.csv", 3},
        std::pair{"hwm_ties.csv", 7}, std::pair{"hwm_ties_ft.csv", 10}}) {
    INFO(name);
    const auto parsed = parse(fixture_text("core/hwm/"s + name));
    REQUIRE(parsed.has_value());
    CHECK(parsed->value.size() == static_cast<std::size_t>(count));
    CHECK(parsed->warnings.empty());
  }
}

// ---- limits, cancellation, files ----

TEST_CASE("more marks than max_elements allows is too_large", "[io][hwm]") {
  // The limit counts numbers: a mark is five.
  ReadContext ctx;
  ctx.limits.max_elements = 10;
  const std::string text =
      "-90.1,29.9,0.5,1.0,1.5\n-90.2,29.9,0.5,1.0,1.5\n-90.3,29.9,0.5,1.0,1."
      "5\n";
  const auto result = parse_hwm_csv(text, LengthUnit::meter, ctx);
  const auto& error = parse_error_of(result);
  CHECK(error.code() == ParseErrc::too_large);
  CHECK(error.line() == 3);
  ctx.limits.max_elements = 14;  // two marks and some
  CHECK(parse_error_of(parse_hwm_csv(text, LengthUnit::meter, ctx)).code() ==
        ParseErrc::too_large);
  ctx.limits.max_elements = 15;
  CHECK(parse_hwm_csv(text, LengthUnit::meter, ctx).has_value());
}

TEST_CASE("a stop request is honoured as rows are read", "[io][hwm]") {
  std::string text;
  for (int i = 0; i < 3000; ++i) {
    text += "-90.1,29.9,0.5,1.0,1.5\n";
  }
  ReadContext now;
  now.stop = StopToken{[] { return true; }};
  const auto stopped = parse_hwm_csv(text, LengthUnit::meter, now);
  REQUIRE(not(stopped.has_value()));
  CHECK(std::holds_alternative<Cancelled>(stopped.error()));

  std::size_t polls = 0;
  ReadContext later;
  later.stop = StopToken{[&polls] { return ++polls >= 2; }};
  const auto partway = parse_hwm_csv(text, LengthUnit::meter, later);
  REQUIRE(not(partway.has_value()));
  CHECK(std::holds_alternative<Cancelled>(partway.error()));
  // One poll per 1024 rows: the second poll is at row 1024.
  CHECK(polls == 2);
}

TEST_CASE("a hostile line cannot put more than the context limit in an error",
          "[io][hwm]") {
  const std::string text = std::string(1'000'000, 'z') + "\n";
  const auto result = parse(text);
  // One field: not a row, so not a header either.
  CHECK(parse_error_of(result).code() == ParseErrc::wrong_field_count);
  const std::string numeric = "1," + std::string(1'000'000, 'z') + "\n";
  const auto too_long = parse(numeric);
  const auto& error = parse_error_of(too_long);
  CHECK(error.context().size() <= mov::io::ParseError::max_context_bytes);
}

TEST_CASE("read_hwm_csv reads a file", "[io][hwm][files]") {
  const auto read =
      mov::io::read_hwm_csv(mov::test::fixture("core/hwm/hwm_ft.csv"),
                            LengthUnit::foot, ReadContext{});
  REQUIRE(read.has_value());
  CHECK(read->value.size() == 7);
}

TEST_CASE("read_hwm_csv: a missing file is a FileError", "[io][hwm][files]") {
  const auto read =
      mov::io::read_hwm_csv(mov::test::fixture("core/hwm/no_such.csv"),
                            LengthUnit::foot, ReadContext{});
  REQUIRE(not(read.has_value()));
  CHECK(std::holds_alternative<FileError>(read.error()));
}

TEST_CASE("read_hwm_csv honours the size limit", "[io][hwm][files]") {
  ReadContext ctx;
  ctx.limits.max_text_bytes = 20;
  const auto read = mov::io::read_hwm_csv(
      mov::test::fixture("core/hwm/hwm_ft.csv"), LengthUnit::foot, ctx);
  REQUIRE(not(read.has_value()));
  const auto* error = std::get_if<FileError>(&read.error());
  REQUIRE(error != nullptr);
  CHECK(error->ec == std::errc::file_too_large);
}

TEST_CASE("read_hwm_csv passes a ParseError through", "[io][hwm][files]") {
  const auto read =
      mov::io::read_hwm_csv(mov::test::fixture("io/hwm/hwm_four_columns.csv"),
                            LengthUnit::meter, ReadContext{});
  CHECK(parse_error_of(read).code() == ParseErrc::wrong_field_count);
}
