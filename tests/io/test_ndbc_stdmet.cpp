// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// ndbc::parse_stdmet and read_stdmet: the recorded files of every era
// (goldens), the v4 bugs they pin (L12-L15), and hostile text
// (docs/providers-design.md sections 4.4 and 10.1).

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "gzip_test_support.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/provider/ndbc.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/stop_token.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "mov/io/error.hpp"
#include "mov/io/part.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/ndbc.hpp"
#include "mov/io/wire/service.hpp"
#include "wire_test_support.hpp"

namespace {

namespace wn = mov::io::wire::ndbc;
using mov::core::LengthUnit;
using mov::core::Quantity;
using mov::core::TimeSeries;
using mov::core::ndbc::FileKind;
using mov::io::Error;
using mov::io::ParseErrc;
using mov::io::Part;
using mov::io::Read;
using mov::io::ReadContext;
using mov::io::WarningCode;
using mov::test::bytes_of;
using mov::test::parse_error_in;
using mov::test::utc;
using mov::test::values_of;
using mov::test::warning_of;
using Opt = std::optional<double>;
using Values = std::vector<Opt>;

constexpr Opt missing = std::nullopt;

std::string ndbc_fixture(std::string_view name) {
  std::string bytes =
      mov::test::read_bytes(mov::test::fixture("io/ndbc/" + std::string{name}));
  REQUIRE(not bytes.empty());
  return bytes;
}

std::expected<Read<Part>, Error> parse(std::string_view text,
                                       FileKind kind = FileKind::historical,
                                       const ReadContext& ctx = {}) {
  return wn::parse_stdmet(text, kind, ctx);
}

Read<Part> parsed(std::string_view text, FileKind kind = FileKind::historical) {
  auto r = parse(text, kind);
  if (not r) {
    const auto* p = std::get_if<mov::io::ParseError>(&r.error());
    INFO((p != nullptr ? p->context() : std::string{"not a ParseError"}));
    REQUIRE(r.has_value());
  }
  return *std::move(r);
}

// The body as received, read: its part.
Read<Part> read(std::string_view body, FileKind kind) {
  auto r = wn::read_stdmet(bytes_of(body), "text/plain", kind, {});
  REQUIRE(r.has_value());
  auto* part = std::get_if<Read<Part>>(&*r);
  REQUIRE(part != nullptr);
  return std::move(*part);
}

mov::io::ColumnKey key(Quantity q) {
  return mov::io::ColumnKey{.quantity = mov::core::QuantityId{q}};
}

const TimeSeries& column(const Part& part, Quantity q) {
  const auto found = part.columns().find(key(q));
  REQUIRE(found != part.columns().end());
  return found->second;
}

const TimeSeries& generic_column(const Part& part, std::string_view token) {
  const auto found = std::ranges::find_if(part.columns(), [&](const auto& c) {
    return mov::core::token(c.first.quantity) == token;
  });
  REQUIRE(found != part.columns().end());
  return found->second;
}

std::size_t masked(const Read<Part>& r, std::string_view column_token) {
  const std::string subject = "ndbc:" + std::string{column_token};
  for (const auto& w : r.warnings) {
    if (w.code == WarningCode::legacy_sentinel_masked and
        w.subject == subject) {
      return w.count;
    }
  }
  return 0;
}

std::optional<LengthUnit> length_unit(const TimeSeries& s) {
  const auto& unit = s.meta().unit();
  REQUIRE(unit.has_value());
  const auto* u = unit ? std::get_if<LengthUnit>(&*unit) : nullptr;
  return u != nullptr ? std::optional<LengthUnit>{*u} : std::nullopt;
}

constexpr std::size_t columns_before_2001 = 12;  // WD ... VIS
constexpr std::size_t columns_with_tide = 13;    // ... TIDE
constexpr std::size_t realtime_columns = 14;     // ... PTDY, TIDE

}  // namespace

// ---- goldens, oldest first
// ---------------------------------------------------

TEST_CASE("1973: two-digit years, no minute, old names, read from gzip",
          "[io][wire][ndbc][regression][L13]") {
  const std::string gz = ndbc_fixture("41002h1973.txt.gz");
  const auto r = wn::read_stdmet(bytes_of(gz), "application/x-gzip",
                                 FileKind::historical, {});
  REQUIRE(r.has_value());
  const auto& part = std::get<Read<Part>>(*r);
  CHECK(part.value.columns().size() == columns_before_2001);
  const TimeSeries& wind = column(part.value, Quantity::wind_direction);
  REQUIRE(wind.size() == 461);
  CHECK(wind.times().front() == utc(1973, 12, 1, 1));
  CHECK(wind.samples().front().value() == 149.0);
  CHECK(column(part.value, Quantity::air_pressure).samples().front().value() ==
        1031.0);
  CHECK(column(part.value, Quantity::wind_speed).samples().front().value() ==
        1.5);  // written 01.5
  // Every documented sentinel, counted per column as written (WD, BAR).
  CHECK(masked(part, "WD") == 445);
  CHECK(masked(part, "WSPD") == 445);
  CHECK(masked(part, "GST") == 461);
  CHECK(masked(part, "WVHT") == 461);
  CHECK(masked(part, "MWD") == 461);
  CHECK(masked(part, "BAR") == 445);
  CHECK(masked(part, "WTMP") == 461);
  CHECK(masked(part, "DEWP") == 445);
  CHECK(masked(part, "VIS") == 461);
  CHECK(length_unit(column(part.value, Quantity::visibility)) ==
        LengthUnit::statute_mile);
}

TEST_CASE("1996: two-digit years of the 1990s and a value written .7",
          "[io][wire][ndbc]") {
  const auto r = parsed(ndbc_fixture("41002h1996.txt"));
  const TimeSeries& speed = column(r.value, Quantity::wind_speed);
  REQUIRE(speed.size() == 12);
  CHECK(speed.times().front() == utc(1996, 1, 1, 0));
  CHECK(speed.times().back() == utc(1996, 1, 1, 11));
  CHECK(values_of(speed)[4] == Opt{0.7});
  CHECK(values_of(column(r.value, Quantity::dew_point)) == Values(12, missing));
  const auto& unit = column(r.value, Quantity::wind_direction).meta().unit();
  CHECK(unit == mov::core::degree());
}

TEST_CASE("1999: four-digit years, CRLF rows, a row missing everywhere",
          "[io][wire][ndbc][regression][L12]") {
  const auto r = parsed(ndbc_fixture("41002h1999.txt"));
  CHECK(r.value.columns().size() == columns_before_2001);
  for (const auto& [k, series] : r.value.columns()) {
    INFO(mov::io::format(k));
    REQUIRE(series.size() == 12);
    CHECK(series.times().front() == utc(1999, 1, 1, 0));
    CHECK(series.samples().front().is_missing());  // never 0.0 (L12)
  }
  CHECK(masked(r, "BAR") == 1);  // 9999.0
  CHECK(masked(r, "WD") == 1);
  CHECK(masked(r, "MWD") == 12);
  CHECK(values_of(column(r.value, Quantity::wave_height))[1] == Opt{1.84});
}

TEST_CASE("2001: TIDE appears, feet above MLLW", "[io][wire][ndbc]") {
  const auto r = parsed(ndbc_fixture("41002h2001.txt"));
  CHECK(r.value.columns().size() == columns_with_tide);
  const TimeSeries& tide = column(r.value, Quantity::water_level);
  CHECK(tide.meta().datum() == mov::core::VerticalDatum::mllw);
  CHECK(length_unit(tide) == LengthUnit::foot);
  CHECK(values_of(tide) == Values(6, missing));
  CHECK(masked(r, "TIDE") == 6);
}

TEST_CASE("2005: the minute column, found by its token",
          "[io][wire][ndbc][regression][L13]") {
  const auto r = parsed(ndbc_fixture("41002h2005.txt"));
  const TimeSeries& gust = column(r.value, Quantity::wind_gust);
  REQUIRE(gust.size() == 6);
  CHECK(gust.times()[1] == utc(2005, 1, 1, 1, 0));
  CHECK(values_of(gust)[0] == Opt{4.0});
  CHECK(values_of(column(r.value, Quantity::wind_direction))[0] == Opt{177.0});
}

TEST_CASE("2007: the units row says nmi, in a historical file",
          "[io][wire][ndbc]") {
  const auto r = parsed(ndbc_fixture("41002h2007.txt"));
  CHECK(r.value.columns().size() == columns_with_tide);
  CHECK(length_unit(column(r.value, Quantity::visibility)) ==
        LengthUnit::nautical_mile);
  CHECK(values_of(column(r.value, Quantity::wave_period_dominant))[0] ==
        Opt{8.33});
  // "sec" in the units row is the registry's "s".
  CHECK(column(r.value, Quantity::wave_period_dominant).meta().unit() ==
        mov::core::parse_unit("s"));
  CHECK(r.warnings.size() == 3);  // MWD, VIS, TIDE: all 9-filled, nothing else
}

TEST_CASE("2019: ten-minute rows ending in a space; VIS in mi",
          "[io][wire][ndbc]") {
  const auto r = parsed(ndbc_fixture("41002h2019.txt"));
  const TimeSeries& pressure = column(r.value, Quantity::air_pressure);
  REQUIRE(pressure.size() == 6);
  CHECK(pressure.times()[5] == utc(2019, 1, 1, 0, 50));
  CHECK(values_of(pressure)[5] == Opt{1023.1});
  CHECK(length_unit(column(r.value, Quantity::visibility)) ==
        LengthUnit::statute_mile);
  CHECK(values_of(column(r.value, Quantity::wave_direction))[4] == Opt{137.0});
  CHECK(masked(r, "WDIR") == 6);
}

TEST_CASE("a finalized month, as served (gzip)", "[io][wire][ndbc]") {
  const std::string gz = ndbc_fixture("4100212026.txt.gz");
  const auto r = read(gz, FileKind::monthly);
  const TimeSeries& wind = column(r.value, Quantity::wind_speed);
  REQUIRE(wind.size() == 4464);
  CHECK(wind.times().front() == utc(2026, 1, 1, 0, 0));
  CHECK(wind.times().back() < utc(2026, 2, 1));
  CHECK(column(r.value, Quantity::wave_direction).meta().unit() ==
        mov::core::degree());  // the units row says "deg"
  CHECK(length_unit(column(r.value, Quantity::visibility)) ==
        LengthUnit::nautical_mile);
  CHECK_FALSE(warning_of(r.warnings, WarningCode::times_reordered));
}

TEST_CASE("the latest month: text, nmi, 9-filled", "[io][wire][ndbc]") {
  const auto r =
      parsed(ndbc_fixture("Sep_41002.txt"), FileKind::monthly_latest);
  const TimeSeries& temperature = column(r.value, Quantity::water_temperature);
  REQUIRE(temperature.size() == 12);
  CHECK(temperature.times().front() == utc(2026, 9, 1));
  CHECK(values_of(temperature)[0] == Opt{29.5});
  CHECK(masked(r, "MWD") == 8);
}

TEST_CASE("realtime2: newest first is reversed, MM is Missing, PTDY signed",
          "[io][wire][ndbc][regression][L12]") {
  const auto r =
      parsed(ndbc_fixture("41002_realtime2.txt"), FileKind::realtime);
  CHECK(r.value.columns().size() == realtime_columns);
  const TimeSeries& direction = column(r.value, Quantity::wind_direction);
  REQUIRE(direction.size() == 14);
  CHECK(direction.times().front() == utc(2026, 8, 19, 0, 0));
  CHECK(direction.times().back() == utc(2026, 10, 3, 16, 50));
  const Values dir = values_of(direction);
  CHECK(dir[0] == Opt{210.0});
  CHECK(dir[2] == missing);  // MM, not 0.0 (v4, L12)
  const TimeSeries& tendency = generic_column(r.value, "pressure_tendency");
  CHECK(tendency.meta().label() == "Pressure tendency");
  CHECK(tendency.meta().unit() ==
        std::optional<mov::core::Unit>{mov::core::PressureUnit::hectopascal});
  const Values ptdy = values_of(tendency);
  CHECK(ptdy[0] == Opt{-0.0});
  CHECK(ptdy[2] == Opt{1.3});
  CHECK(ptdy[1] == missing);
  CHECK(values_of(column(r.value, Quantity::wave_period_dominant))[13] ==
        Opt{11.0});
  // The reversal is the file's order, not a fault; and MM is not a sentinel.
  CHECK(r.warnings.empty());
}

TEST_CASE("999.0 hPa is a pressure; 9999.0 is missing",
          "[io][wire][ndbc][regression][L12]") {
  const auto r = parsed(ndbc_fixture("42040h2005_arlene.txt"));
  const Values pressure = values_of(column(r.value, Quantity::air_pressure));
  CHECK(pressure == Values{1009.4, 1009.4, missing, 1009.6, 1001.0, 1000.0,
                           999.2, 999.0, 998.6, 999.0, 999.3});
  CHECK(masked(r, "BAR") == 1);
}

TEST_CASE("each file by its own header: two eras of one station",
          "[io][wire][ndbc][regression][L14]") {
  const auto older = parsed(ndbc_fixture("41002h1999.txt"));
  const auto newer = parsed(ndbc_fixture("41002h2007.txt"));
  CHECK_FALSE(older.value.columns().contains(key(Quantity::water_level)));
  CHECK(newer.value.columns().contains(key(Quantity::water_level)));
  CHECK(length_unit(column(older.value, Quantity::visibility)) ==
        LengthUnit::statute_mile);
  CHECK(length_unit(column(newer.value, Quantity::visibility)) ==
        LengthUnit::nautical_mile);
  CHECK(column(newer.value, Quantity::wind_speed).times().front() ==
        utc(2007, 1, 1));
}

TEST_CASE("the CGI wrapper's error body is not data",
          "[io][wire][ndbc][regression][L15]") {
  const std::string body = ndbc_fixture("view_text_file_missing.txt");
  const auto r = parse(body);
  CHECK(parse_error_in(r).code() == ParseErrc::missing_header);
  CHECK(parse_error_in(r).line() == 1);
}

// ---- the rules, on small texts
// -------------------------------------------------

TEST_CASE("the minute column is a token, not a position",
          "[io][wire][ndbc][regression][L13]") {
  // v4 took the fifth word for minutes when it read "mm"; here WD is a value.
  const auto hourly = parsed("YYYY MM DD hh WD\n2004 01 01 05 00\n");
  CHECK(column(hourly.value, Quantity::wind_direction).times()[0] ==
        utc(2004, 1, 1, 5));
  CHECK(values_of(column(hourly.value, Quantity::wind_direction))[0] ==
        Opt{0.0});
  const auto minutes = parsed("YYYY MM DD hh mm WD\n2005 01 01 05 30 10\n");
  CHECK(column(minutes.value, Quantity::wind_direction).times()[0] ==
        utc(2005, 1, 1, 5, 30));
}

TEST_CASE("missing values are exact per column",
          "[io][wire][ndbc][regression][L12]") {
  const auto r = parsed(
      "#YY  MM DD hh mm WDIR WSPD  ATMP  PRES  TIDE  PTDY   MWD\n"
      "2010 01 01 00 00  999 99.0  99.0 999.0 99.00  99.0 999.0\n"
      "2010 01 01 01 00   99  9.9 999.0 9999 99.0  -1.0    99\n");
  CHECK(values_of(column(r.value, Quantity::wind_direction)) ==
        Values{missing, 99.0});
  CHECK(values_of(column(r.value, Quantity::wind_speed)) ==
        Values{missing, 9.9});
  // 99.0 is a (hot) air temperature: only 999 is missing there.
  CHECK(values_of(column(r.value, Quantity::air_temperature)) ==
        Values{99.0, missing});
  CHECK(values_of(column(r.value, Quantity::air_pressure)) ==
        Values{999.0, missing});
  CHECK(values_of(column(r.value, Quantity::water_level)) ==
        Values{missing, missing});
  CHECK(values_of(generic_column(r.value, "pressure_tendency")) ==
        Values{99.0, -1.0});  // PTDY has no sentinel
  CHECK(values_of(column(r.value, Quantity::wave_direction)) ==
        Values{missing, 99.0});
  CHECK(masked(r, "TIDE") == 2);
  CHECK(masked(r, "PTDY") == 0);
}

TEST_CASE("units without a units row follow the file kind",
          "[io][wire][ndbc]") {
  const std::string_view text = "YYYY MM DD hh VIS\n2005 01 01 00 1.5\n";
  CHECK(length_unit(column(parsed(text, FileKind::historical).value,
                           Quantity::visibility)) == LengthUnit::statute_mile);
  for (const FileKind k :
       {FileKind::monthly, FileKind::monthly_latest, FileKind::realtime}) {
    CHECK(length_unit(column(parsed(text, k).value, Quantity::visibility)) ==
          LengthUnit::nautical_mile);
  }
}

TEST_CASE("a units row of the wrong kind is refused", "[io][wire][ndbc]") {
  for (const std::string_view row :
       {"#yr mo dy hr hPa", "#yr mo dy hr degT", "#yr mo dy hr sec"}) {
    const std::string text =
        "#YY MM DD hh WSPD\n" + std::string{row} + "\n2010 01 01 00 1.0\n";
    const auto e = mov::test::format_error_in(parse(text));
    CHECK(e.code == mov::io::FormatErrc::noncanonical_unit);
    CHECK(e.subject.starts_with("WSPD "));
  }
  // Another unit of the right kind is the unit: knots stay knots.
  const auto knots =
      parsed("#YY MM DD hh WSPD\n#yr mo dy hr kt\n2010 01 01 00 1.0\n");
  CHECK(column(knots.value, Quantity::wind_speed).meta().unit() ==
        std::optional<mov::core::Unit>{mov::core::SpeedUnit::knot});
}

TEST_CASE("the units row has the header's shape", "[io][wire][ndbc]") {
  CHECK(parse_error_in(parse("#YY MM DD hh WSPD\n#yr mo dy hr\n")).code() ==
        ParseErrc::wrong_field_count);
  const auto shifted = parse("#YY MM DD hh WSPD\n#yr mo dy mn m/s\n");
  CHECK(parse_error_in(shifted).code() == ParseErrc::missing_header);
  CHECK(parse_error_in(shifted).line() == 2);
  CHECK(parse_error_in(shifted).column() == 10);
}

TEST_CASE("headers that are not standard met", "[io][wire][ndbc]") {
  CHECK(parse_error_in(parse("")).code() == ParseErrc::empty_input);
  CHECK(parse_error_in(parse("  \n\n")).code() == ParseErrc::empty_input);
  for (const std::string_view header :
       {"WDIR WSPD GST", "YY MM DD", "YY MM DD HH WDIR", "YYY MM DD hh",
        "#YY MM DD hhmm LAT LON WDIR", "<html><body>Not Found</body></html>"}) {
    INFO(header);
    CHECK(parse_error_in(parse(std::string{header} + "\n")).code() ==
          ParseErrc::missing_header);
  }
  // The drifting-buoy layout: hhmm is where hh must be.
  CHECK(parse_error_in(parse("#YY MM DD hhmm WDIR\n")).column() == 10);
}

TEST_CASE("columns the parser does not know are skipped and named",
          "[io][wire][ndbc]") {
  const auto r =
      parsed("#YY MM DD hh mm LAT WSPD OTHER\n2022 07 27 12 00 25.99 9.0 x\n");
  CHECK(r.value.columns().size() == 1);
  CHECK(values_of(column(r.value, Quantity::wind_speed)) == Values{9.0});
  REQUIRE(r.warnings.size() == 2);
  CHECK(r.warnings[0].code == WarningCode::skipped_variable);
  CHECK(r.warnings[0].subject == "LAT");
  CHECK(r.warnings[1].subject == "OTHER");
}

TEST_CASE("a column named twice is refused", "[io][wire][ndbc]") {
  for (const std::string_view header :
       {"YYYY MM DD hh WD WDIR", "YYYY MM DD hh BAR PRES",
        "YYYY MM DD hh WSPD WSPD"}) {
    const auto e =
        mov::test::format_error_in(parse(std::string{header} + "\n"));
    CHECK(e.code == mov::io::FormatErrc::duplicate_quantity);
  }
}

TEST_CASE("rows with another number of words", "[io][wire][ndbc]") {
  const auto r = parse(
      "YYYY MM DD hh WD WSPD\n2005 01 01 00 10 1.0\n"
      "2005 01 01 01 10\n");
  CHECK(parse_error_in(r).code() == ParseErrc::wrong_field_count);
  CHECK(parse_error_in(r).line() == 3);
  CHECK(
      parse_error_in(parse("YYYY MM DD hh WD\n2005 01 01 00 10 1\n")).code() ==
      ParseErrc::wrong_field_count);
}

TEST_CASE("bad dates, at the field", "[io][wire][ndbc]") {
  struct Case {
    std::string_view row;
    std::size_t column;
  };
  for (const Case c : {Case{.row = "123 01 01 00 00 1", .column = 0},
                       Case{.row = "20O5 01 01 00 00 1", .column = 0},
                       Case{.row = "2005 13 01 00 00 1", .column = 5},
                       Case{.row = "2005 1x 01 00 00 1", .column = 5},
                       Case{.row = "2005 02 29 00 00 1", .column = 8},
                       Case{.row = "00 02 29 00 00 1", .column = 6},
                       Case{.row = "2005 01 00 00 00 1", .column = 8},
                       Case{.row = "2005 01 01 24 00 1", .column = 11},
                       Case{.row = "2005 01 01 23 60 1", .column = 14},
                       Case{.row = "2005 01 01 023 00 1", .column = 11},
                       Case{.row = "2005 01 01 -1 00 1", .column = 11}}) {
    INFO(c.row);
    const auto r = parse("YYYY MM DD hh mm WD\n" + std::string{c.row} + "\n");
    CHECK(parse_error_in(r).code() == ParseErrc::bad_date);
    CHECK(parse_error_in(r).line() == 2);
    CHECK(parse_error_in(r).column() == c.column);
  }
  // Leap days of leap years are dates; 1900 (two digits "00") was not one.
  CHECK(parse("YYYY MM DD hh mm WD\n2004 02 29 00 00 1\n").has_value());
  CHECK(parse("YYYY MM DD hh mm WD\n2000 02 29 00 00 1\n").has_value());
}

TEST_CASE("values that are not numbers", "[io][wire][ndbc]") {
  for (const std::string_view value :
       {"abc", "1,5", "nan", "inf", "0x10", "--1", "M", "mm", "1.2.3"}) {
    INFO(value);
    const auto r =
        parse("YYYY MM DD hh WSPD\n2005 01 01 00 " + std::string{value} + "\n");
    CHECK(parse_error_in(r).code() == ParseErrc::bad_number);
    CHECK(parse_error_in(r).column() == 14);
  }
  CHECK(parse_error_in(parse("YYYY MM DD hh WSPD\n2005 01 01 00 1e999\n"))
            .code() == ParseErrc::out_of_range);
}

TEST_CASE("a byte order mark, comments and blank lines", "[io][wire][ndbc]") {
  const auto r = parsed(
      "\xEF\xBB\xBF#YY MM DD hh WSPD\n#yr mo dy hr m/s\n\n"
      "2010 01 01 00 1.0\n# a comment\n  \n2010 01 01 01 2.0\n#another\n");
  CHECK(values_of(column(r.value, Quantity::wind_speed)) == Values{1.0, 2.0});
  const auto skipped = warning_of(r.warnings, WarningCode::header_line_skipped);
  CHECK(skipped == mov::io::Warning{.code = WarningCode::header_line_skipped,
                                    .subject = "# a comment",
                                    .count = 2});
}

TEST_CASE("a header without rows gives empty columns", "[io][wire][ndbc]") {
  const auto r =
      parsed("#YY MM DD hh mm WDIR WSPD\n#yr mo dy hr mn degT m/s\n");
  CHECK(r.value.columns().size() == 2);
  CHECK(column(r.value, Quantity::wind_speed).empty());
}

TEST_CASE("rows out of order go through normalize, about the file kind",
          "[io][wire][ndbc]") {
  const auto r = parsed(
      "YYYY MM DD hh WSPD\n2005 01 01 02 3.0\n2005 01 01 00 1.0\n"
      "2005 01 01 01 2.0\n2005 01 01 01 9.0\n",
      FileKind::monthly);
  CHECK(values_of(column(r.value, Quantity::wind_speed)) ==
        Values{1.0, 2.0, 3.0});
  const auto reordered = warning_of(r.warnings, WarningCode::times_reordered);
  REQUIRE(reordered.has_value());
  CHECK(reordered.value_or(mov::io::Warning{}).subject == "ndbc:monthly");
  CHECK(warning_of(r.warnings, WarningCode::duplicate_times_dropped));
  CHECK(warning_of(r.warnings, WarningCode::conflicting_duplicate_times));
}

TEST_CASE("a strictly decreasing file of any kind is reversed silently",
          "[io][wire][ndbc]") {
  const auto r = parsed(
      "YYYY MM DD hh WSPD\n2005 01 01 02 3.0\n"
      "2005 01 01 01 2.0\n2005 01 01 00 1.0\n");
  CHECK(values_of(column(r.value, Quantity::wind_speed)) ==
        Values{1.0, 2.0, 3.0});
  CHECK(r.warnings.empty());
  // Two equal times are not strictly decreasing: normalize keeps the first.
  const auto tied = parsed(
      "YYYY MM DD hh WSPD\n2005 01 01 02 3.0\n"
      "2005 01 01 02 2.0\n2005 01 01 00 1.0\n");
  CHECK(values_of(column(tied.value, Quantity::wind_speed)) ==
        Values{1.0, 3.0});
}

TEST_CASE("limits come before the samples are kept", "[io][wire][ndbc]") {
  const std::string text =
      "YYYY MM DD hh WSPD GST\n2005 01 01 00 1 2\n2005 01 01 01 1 2\n";
  ReadContext ctx;
  ctx.limits.max_elements = 3;  // one row of two columns
  const auto r = parse(text, FileKind::historical, ctx);
  CHECK(parse_error_in(r).code() == ParseErrc::too_large);
  CHECK(parse_error_in(r).line() == 3);
  ctx.limits.max_elements = 4;
  CHECK(parse(text, FileKind::historical, ctx).has_value());
  ReadContext small;
  small.limits.max_text_bytes = text.size() - 1;
  CHECK(parse_error_in(parse(text, FileKind::historical, small)).code() ==
        ParseErrc::too_large);
}

TEST_CASE("a stop request cancels the parse", "[io][wire][ndbc]") {
  ReadContext ctx;
  ctx.stop = mov::core::StopToken{[] { return true; }};
  const auto r =
      parse("YYYY MM DD hh WSPD\n2005 01 01 00 1\n", FileKind::historical, ctx);
  REQUIRE(not r.has_value());
  CHECK(std::holds_alternative<mov::io::Cancelled>(r.error()));
  // Polled between rows too, not only at the start.
  int polls = 0;
  ReadContext later;
  later.stop = mov::core::StopToken{[&polls] { return ++polls > 1; }};
  const auto stopped = parse("YYYY MM DD hh WSPD\n2005 01 01 00 1\n",
                             FileKind::historical, later);
  REQUIRE(not stopped.has_value());
  CHECK(std::holds_alternative<mov::io::Cancelled>(stopped.error()));
}

// ---- read_stdmet: the body as received
// --------------------------------------------

TEST_CASE("gzip by magic: the same columns as the text", "[io][wire][ndbc]") {
  const std::string text = ndbc_fixture("41002h2019.txt");
  const std::string gz = mov::test::gzip_of(text);
  const auto from_gzip = read(gz, FileKind::historical);
  const auto from_text = read(text, FileKind::historical);
  CHECK(from_gzip == from_text);
  CHECK(from_text == parsed(text));
}

TEST_CASE("an HTML page is the service's, not a parse error",
          "[io][wire][ndbc]") {
  const std::string_view page = "\n  <!DOCTYPE html><html>maintenance</html>";
  const auto r =
      wn::read_stdmet(bytes_of(page), "text/html", FileKind::realtime, {});
  REQUIRE(r.has_value());
  const auto* refusal = std::get_if<mov::io::wire::ServiceError>(&*r);
  REQUIRE(refusal != nullptr);
  CHECK(refusal->code() == mov::io::wire::ServiceErrc::unexpected_content);
  CHECK(refusal->origin() == mov::io::wire::Origin::ndbc);
  CHECK(refusal->text() == "text/html");
}

TEST_CASE("a broken gzip body is a parse error at its byte",
          "[io][wire][ndbc]") {
  const std::string gz = mov::test::gzip_of(ndbc_fixture("41002h2005.txt"));
  const std::string truncated = gz.substr(0, gz.size() / 2);
  const auto r = wn::read_stdmet(bytes_of(truncated), "application/x-gzip",
                                 FileKind::historical, {});
  CHECK(parse_error_in(r).code() == ParseErrc::bad_gzip);
  CHECK(parse_error_in(r).line() == 1);
  CHECK(parse_error_in(r).context() == "truncated gzip stream");
  const std::string two = gz + gz;
  const auto extra =
      wn::read_stdmet(bytes_of(two), "", FileKind::historical, {});
  CHECK(parse_error_in(extra).column() == gz.size());
  CHECK(parse_error_in(extra).context() == "a second gzip member");
  const std::string trailing = gz + "x";
  CHECK(parse_error_in(
            wn::read_stdmet(bytes_of(trailing), "", FileKind::historical, {}))
            .context() == "bytes after the gzip member");
  std::string bad_crc = gz;
  bad_crc[bad_crc.size() - 8] =
      static_cast<char>(bad_crc[bad_crc.size() - 8] ^ 0x01);
  CHECK(parse_error_in(
            wn::read_stdmet(bytes_of(bad_crc), "", FileKind::historical, {}))
            .context() == "corrupt gzip stream");
}

TEST_CASE("a body larger than the text limit, compressed or not",
          "[io][wire][ndbc]") {
  const std::string text = ndbc_fixture("41002h2005.txt");
  ReadContext ctx;
  ctx.limits.max_text_bytes = text.size() - 1;
  const auto plain =
      wn::read_stdmet(bytes_of(text), "", FileKind::historical, ctx);
  CHECK(parse_error_in(plain).code() == ParseErrc::too_large);
  const std::string gz = mov::test::gzip_of(text);
  const auto inflated =
      wn::read_stdmet(bytes_of(gz), "", FileKind::historical, ctx);
  CHECK(parse_error_in(inflated).code() == ParseErrc::too_large);
  ReadContext stop;
  stop.stop = mov::core::StopToken{[] { return true; }};
  const auto cancelled =
      wn::read_stdmet(bytes_of(gz), "", FileKind::historical, stop);
  REQUIRE(not cancelled.has_value());
  CHECK(std::holds_alternative<mov::io::Cancelled>(cancelled.error()));
}
