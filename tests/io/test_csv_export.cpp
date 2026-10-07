// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// format_csv / write_csv (docs/core-design.md section 5.8, C18, D27, 9.1).

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/meta.hpp"
#include "mov/core/units.hpp"
#include "mov/io/csv_export.hpp"
#include "mov/test/scratch_dir.hpp"
#include "table_helpers.hpp"

namespace {

namespace core = mov::core;
namespace io = mov::io;
namespace test = mov::test;

constexpr std::string_view header =
    "station_id,station_name,time_utc,quantity,value,status,unit,datum\r\n";

core::SeriesMeta feet_mllw() {
  const core::SeriesMeta meta =
      core::SeriesMeta::make({.unit = core::LengthUnit::foot});
  const auto with = meta.assume_datum(core::VerticalDatum::mllw);
  REQUIRE(with.has_value());
  return *with;
}

core::SeriesMeta plain() { return core::SeriesMeta::make({}); }

// One station named `name` with one value, -1.5 at 2020-01-01.
core::StationTable named(std::string name, std::string id = "S1") {
  return test::one_column_table(plain(), {{.id = std::move(id),
                                           .name = std::move(name),
                                           .where = test::location(1.0, 2.0),
                                           .times = {test::utc(2020, 1, 1)},
                                           .samples = {test::value(-1.5)}}});
}

// The row after the header of a one-row table.
std::string row_of(const core::StationTable& t) {
  const std::string csv = io::format_csv(t);
  REQUIRE(csv.starts_with(header));
  REQUIRE(csv.ends_with("\r\n"));
  return csv.substr(header.size(), csv.size() - header.size() - 2);
}

}  // namespace

TEST_CASE("format_csv: long format with a status column (golden)",
          "[io][csv]") {
  const auto table = test::one_column_table(
      feet_mllw(),
      {{.id = "A1",
        .name = "Bar Harbor, ME",
        .where = test::location(44.3917, -68.205),
        .times = {test::utc(2015, 7, 1, 0, 0),
                  test::utc(2015, 7, 1, 0, 6, 0, 250),
                  test::utc(2015, 7, 1, 0, 12)},
        .samples = {test::value(2.605), core::Missing{}, core::Dry{}}},
       {.id = "B2",
        .name = "",
        .where = test::location(40.4669, -74.0094),
        .times = {test::utc(1969, 12, 31, 23, 59, 59, 999)},
        .samples = {test::value(-1.5)}}});
  CHECK(
      io::format_csv(table) ==
      "station_id,station_name,time_utc,quantity,value,status,unit,datum\r\n"
      "A1,\"Bar Harbor, ME\",2015-07-01T00:00:00.000Z,value,2.605000,value,ft,"
      "MLLW\r\n"
      "A1,\"Bar Harbor, "
      "ME\",2015-07-01T00:06:00.250Z,value,,missing,ft,MLLW\r\n"
      "A1,\"Bar Harbor, ME\",2015-07-01T00:12:00.000Z,value,,dry,ft,MLLW\r\n"
      "B2,,1969-12-31T23:59:59.999Z,value,-1.500000,value,ft,MLLW\r\n");
}

TEST_CASE("the value cell is empty unless the status is value (9.1)",
          "[io][csv]") {
  const auto table = test::one_column_table(
      plain(), {{.id = "S",
                 .name = "S",
                 .where = test::location(1.0, 2.0),
                 .times = {test::utc(2020, 1, 1, 0), test::utc(2020, 1, 1, 1),
                           test::utc(2020, 1, 1, 2)},
                 .samples = {core::Dry{}, core::Missing{}, test::value(0.0)}}});
  CHECK(io::format_csv(table) ==
        std::string{header} +
            "S,S,2020-01-01T00:00:00.000Z,value,,dry,,\r\n"
            "S,S,2020-01-01T01:00:00.000Z,value,,missing,,\r\n"
            "S,S,2020-01-01T02:00:00.000Z,value,0.000000,value,,\r\n");
}

TEST_CASE("a table without rows is the header alone", "[io][csv]") {
  CHECK(io::format_csv(core::StationTable{}) == header);
  CHECK(io::format_csv(test::one_column_table(plain(), {})) == header);
  const auto no_samples =
      test::one_column_table(plain(), {{.id = "S",
                                        .name = "S",
                                        .where = test::location(1.0, 2.0),
                                        .times = {},
                                        .samples = {}}});
  CHECK(io::format_csv(no_samples) == header);
}

TEST_CASE("times are ISO 8601 UTC with milliseconds, in any year",
          "[io][csv]") {
  const auto cell = [](core::Time t) {
    const auto table =
        test::one_column_table(plain(), {{.id = "S",
                                          .name = "S",
                                          .where = test::location(1.0, 2.0),
                                          .times = {t},
                                          .samples = {test::value(1.0)}}});
    const std::string row = row_of(table);
    return row.substr(4, row.find(",value,", 4) - 4);
  };
  CHECK(cell(test::utc(2024, 2, 29, 23, 59, 59, 7)) ==
        "2024-02-29T23:59:59.007Z");
  CHECK(cell(test::utc(1970, 1, 1)) == "1970-01-01T00:00:00.000Z");
  CHECK(cell(test::utc(0, 1, 1)) == "0000-01-01T00:00:00.000Z");
  CHECK(cell(test::utc(12345, 6, 7, 8, 9, 10, 11)) ==
        "12345-06-07T08:09:10.011Z");
  CHECK(cell(test::utc(-1, 12, 31, 23, 59, 59, 999)) ==
        "-0001-12-31T23:59:59.999Z");
}

TEST_CASE("the largest file time formats", "[io][csv]") {
  const core::Time latest{std::chrono::milliseconds{core::max_abs_time_ms}};
  const core::Time earliest{std::chrono::milliseconds{-core::max_abs_time_ms}};
  const auto table = test::one_column_table(
      plain(), {{.id = "S",
                 .name = "S",
                 .where = test::location(1.0, 2.0),
                 .times = {earliest, latest},
                 .samples = {test::value(1.0), test::value(2.0)}}});
  const std::string csv = io::format_csv(table);
  CHECK(csv.contains("S,S,-"));
  CHECK(csv.contains("Z,value"));
}

TEST_CASE("quoting follows RFC 4180", "[io][csv]") {
  CHECK(row_of(named("plain")).starts_with("S1,plain,"));
  CHECK(row_of(named("a,b")).starts_with("S1,\"a,b\","));
  CHECK(row_of(named("say \"hi\"")).starts_with("S1,\"say \"\"hi\"\"\","));
  CHECK(row_of(named("two\nlines")).starts_with("S1,\"two\nlines\","));
  CHECK(row_of(named("cr\rhere")).starts_with("S1,\"cr\rhere\","));
  // Spaces and non-ASCII text need no quotes.
  CHECK(row_of(named("Ba\xC3\xAD"
                     "a Blanca"))
            .starts_with("S1,Ba\xC3\xAD"
                         "a Blanca,"));
  // The id is a text cell too.
  CHECK(row_of(named("n", "id,1")).starts_with("\"id,1\",n,"));
}

TEST_CASE("text cells that start like a formula get a leading quote (C18)",
          "[io][csv][regression]") {
  for (const std::string_view risky :
       {"=SUM(A1)", "+1+1", "-2+3", "@cmd", "\t=x", "=1+1"}) {
    CAPTURE(risky);
    const std::string expected = "S1,'" + std::string{risky} + ",";
    CHECK(row_of(named(std::string{risky})).starts_with(expected));
  }
  // A CR start: guarded, and quoted because a CR is in the cell.
  CHECK(row_of(named("\r=x")).starts_with("S1,\"'\r=x\","));
  // Guard and quoting together.
  CHECK(row_of(named("=A1,B1")).starts_with("S1,\"'=A1,B1\","));
  CHECK(row_of(named("=\"x\"")).starts_with("S1,\"'=\"\"x\"\"\","));
  // The id too.
  CHECK(row_of(named("n", "=id")).starts_with("'=id,n,"));
  // Only the start matters; other cells are untouched.
  CHECK(row_of(named("a=b")).starts_with("S1,a=b,"));
  CHECK(row_of(named("a-b+c@d")).starts_with("S1,a-b+c@d,"));
  CHECK(row_of(named(" =x")).starts_with("S1, =x,"));
}

TEST_CASE("numbers and times are exempt: a negative value stays a number",
          "[io][csv][regression]") {
  const std::string row = row_of(named("n"));
  CHECK(row == "S1,n,2020-01-01T00:00:00.000Z,value,-1.500000,value,,");
  CHECK(not row.contains("'"));
}

TEST_CASE("the unit and datum cells are text cells", "[io][csv]") {
  const auto unit = core::parse_unit("-x");
  REQUIRE(unit.has_value());
  const auto table =
      test::one_column_table(core::SeriesMeta::make({.unit = *unit}),
                             {{.id = "S",
                               .name = "S",
                               .where = test::location(1.0, 2.0),
                               .times = {test::utc(2020, 1, 1)},
                               .samples = {test::value(1.0)}}});
  CHECK(row_of(table) ==
        "S,S,2020-01-01T00:00:00.000Z,value,1.000000,value,'-x,");
}

TEST_CASE("a registry quantity is written by its token", "[io][csv]") {
  const core::SeriesMeta meta =
      core::SeriesMeta::make({.quantity = core::Quantity::water_level,
                              .unit = core::LengthUnit::meter});
  const auto table =
      test::one_column_table(meta, {{.id = "S",
                                     .name = "S",
                                     .where = test::location(1.0, 2.0),
                                     .times = {test::utc(2020, 1, 1)},
                                     .samples = {test::value(1.0)}}});
  CHECK(row_of(table) ==
        "S,S,2020-01-01T00:00:00.000Z,water_level,1.000000,value,m,");
}

TEST_CASE("a multi-column table lists station, then column, then time",
          "[io][csv]") {
  std::vector<core::Variable> variables;
  variables.push_back({.meta = core::SeriesMeta::make(
                           {.quantity = core::Quantity::current_u,
                            .unit = core::SpeedUnit::meter_per_second}),
                       .per_station = {{test::value(1.0), test::value(2.0)},
                                       {test::value(3.0), test::value(4.0)}}});
  variables.push_back({.meta = core::SeriesMeta::make(
                           {.quantity = core::Quantity::current_v,
                            .unit = core::SpeedUnit::meter_per_second}),
                       .per_station = {{test::value(-1.0), core::Missing{}},
                                       {core::Dry{}, test::value(-4.0)}}});
  std::vector<core::TimeAxis> axes{
      {test::utc(2020, 1, 1), test::utc(2020, 1, 2)}};
  std::vector<core::StationRow> rows;
  for (const char* id : {"P", "Q"}) {
    rows.push_back(
        {.station = test::file_station({.id = id,
                                        .name = id,
                                        .where = test::location(1.0, 2.0),
                                        .times = {},
                                        .samples = {}}),
         .axis = 0});
  }
  auto table = core::StationTable::make(std::move(variables), std::move(axes),
                                        std::move(rows));
  REQUIRE(table.has_value());
  CHECK(io::format_csv(*table) ==
        std::string{header} +
            "P,P,2020-01-01T00:00:00.000Z,current_u,1.000000,value,m/s,\r\n"
            "P,P,2020-01-02T00:00:00.000Z,current_u,2.000000,value,m/s,\r\n"
            "P,P,2020-01-01T00:00:00.000Z,current_v,-1.000000,value,m/s,\r\n"
            "P,P,2020-01-02T00:00:00.000Z,current_v,,missing,m/s,\r\n"
            "Q,Q,2020-01-01T00:00:00.000Z,current_u,3.000000,value,m/s,\r\n"
            "Q,Q,2020-01-02T00:00:00.000Z,current_u,4.000000,value,m/s,\r\n"
            "Q,Q,2020-01-01T00:00:00.000Z,current_v,,dry,m/s,\r\n"
            "Q,Q,2020-01-02T00:00:00.000Z,current_v,-4.000000,value,m/s,\r\n");
}

TEST_CASE("a large table is written in chunks without losing a byte",
          "[io][csv]") {
  constexpr std::size_t n = 50000;
  std::vector<core::Time> times;
  std::vector<core::Sample> samples;
  for (std::size_t i = 0; i < n; ++i) {
    times.push_back(test::utc(2020, 1, 1) +
                    std::chrono::seconds{static_cast<std::int64_t>(i)});
    samples.push_back(i % 3 == 0 ? core::Sample{core::Missing{}}
                                 : test::value(static_cast<double>(i)));
  }
  const auto table =
      test::one_column_table(plain(), {{.id = "S",
                                        .name = "S",
                                        .where = test::location(1.0, 2.0),
                                        .times = std::move(times),
                                        .samples = std::move(samples)}});
  const std::string csv = io::format_csv(table);
  CHECK(static_cast<std::size_t>(std::ranges::count(csv, '\n')) == n + 1);
  CHECK(csv.ends_with(
      "S,S,2020-01-01T13:53:19.000Z,value,49999.000000,value,,\r\n"));
  // Every row is whole: this is longer than one chunk.
  CHECK(csv.size() > (std::size_t{1} << 16));

  const test::ScratchDir dir;
  REQUIRE(io::write_csv(dir / "big.csv", table).has_value());
  CHECK(test::read_bytes(dir / "big.csv") == csv);
}

TEST_CASE("write_csv replaces a file atomically and reports failures",
          "[io][csv]") {
  const test::ScratchDir dir;
  const auto path = dir / "out.csv";
  test::write_bytes(path, "old");
  const auto table = named("n");
  REQUIRE(io::write_csv(path, table).has_value());
  CHECK(test::read_bytes(path) == io::format_csv(table));
  CHECK(test::entry_names(dir.path()) == std::vector<std::string>{"out.csv"});

  const auto missing = io::write_csv(dir / "no_such_dir" / "out.csv", table);
  REQUIRE(not missing.has_value());
  CHECK(std::holds_alternative<io::FileError>(missing.error()));
}
