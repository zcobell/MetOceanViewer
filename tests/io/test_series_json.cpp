// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The series JSON writer (docs/series-json.md) against its golden files.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <ios>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/daily_series.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/units.hpp"
#include "mov/io/series_json.hpp"
#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"
#include "table_helpers.hpp"

namespace {

namespace core = mov::core;
namespace io = mov::io;
using mov::test::StationSpec;
using mov::test::utc;
using mov::test::value;

void check_golden(const std::string& actual, std::string_view name) {
  const std::string expected = mov::test::read_bytes(
      mov::test::fixture("io/series_json/" + std::string{name}));
  if (actual != expected) {
    std::ofstream out{std::string{name} + ".actual", std::ios::binary};
    out << actual;
  }
  CHECK(actual == expected);
}

std::string json_of(const core::StationTable& table) {
  auto text = io::format_series_json(table);
  REQUIRE(text.has_value());
  return *std::move(text);
}

std::string json_of(std::span<const io::DailyAtStation> stations) {
  auto text = io::format_series_json(stations);
  REQUIRE(text.has_value());
  return *std::move(text);
}

core::SeriesMeta water_level() {
  const auto meta =
      core::SeriesMeta::make({.quantity = core::Quantity::water_level,
                              .label = "Water level",
                              .unit = core::LengthUnit::meter})
          .assume_datum(core::VerticalDatum::mllw);
  REQUIRE(meta.has_value());
  return *meta;
}

core::FileStation station(std::string id, std::string name, double lat,
                          double lon, std::optional<core::DataSource> source) {
  auto s = mov::test::file_station({.id = std::move(id),
                                    .name = std::move(name),
                                    .where = mov::test::location(lat, lon),
                                    .times = {},
                                    .samples = {}});
  s.source = source;
  return s;
}

core::StationTable table() {
  std::vector<core::Variable> variables;
  variables.push_back(
      {.meta = water_level(),
       .per_station = {{value(0.123), core::Missing{}, value(-1e-7)},
                       {core::Dry{}, value(2.5)}}});
  variables.push_back(
      {.meta =
           core::SeriesMeta::make({.quantity = core::Quantity::wind_speed,
                                   .label = "",
                                   .unit = core::SpeedUnit::meter_per_second}),
       .per_station = {{value(3.6), value(4.0), core::Missing{}},
                       {core::Missing{}, core::Missing{}}}});
  auto t = core::StationTable::make(
      std::move(variables),
      {{utc(2026, 10, 1), utc(2026, 10, 1, 0, 6), utc(2026, 10, 1, 0, 12)},
       {utc(1969, 12, 31, 23, 59, 59, 5), utc(2026, 10, 1)}},
      {{.station = station("8729840", "Pensacola", 30.4044, -87.2112,
                           core::DataSource::noaa_coops),
        .axis = 0},
       {.station =
            station("model 1", "Grid \"point\"\n", 29.0, 270.5, std::nullopt),
        .axis = 1}});
  REQUIRE(t.has_value());
  return *std::move(t);
}

std::vector<io::DailyAtStation> daily() {
  using std::chrono::local_days;
  const auto days = [](int y, unsigned m, unsigned d) {
    return local_days{std::chrono::year{y} / std::chrono::month{m} /
                      std::chrono::day{d}};
  };
  auto series = core::DailySeries::make(
      core::SeriesMeta::make(
          {.quantity = core::Quantity::discharge,
           .label = "Discharge, cubic feet per second",
           .unit = core::DischargeUnit::cubic_foot_per_second}),
      {days(2026, 9, 30), days(2026, 10, 1), days(2026, 10, 2)},
      {value(512000), core::Missing{}, value(498000.5)},
      core::usgs::StatisticId::mean());
  REQUIRE(series.has_value());
  return {
      {.station = station("USGS-07010000", "MISSISSIPPI RIVER AT ST. LOUIS, MO",
                          38.629, -90.1796, core::DataSource::usgs),
       .data = *std::move(series)}};
}

}  // namespace

TEST_CASE("a station table is written as its golden series JSON",
          "[io][series_json]") {
  check_golden(json_of(table()), "table.json");
}

TEST_CASE("daily values are written with dates and their statistic",
          "[io][series_json]") {
  const auto stations = daily();
  check_golden(json_of(stations), "daily.json");
}

TEST_CASE("an empty table or list is a document without stations",
          "[io][series_json]") {
  const std::string empty =
      "{\"format\":\"metoceanviewer-series\",\"version\":\"1.0\",\n"
      " \"stations\":[]}\n";
  CHECK(json_of(core::StationTable{}) == empty);
  CHECK(json_of(std::vector<io::DailyAtStation>{}) == empty);
}

TEST_CASE("write_series_json writes what format_series_json makes",
          "[io][series_json]") {
  const mov::test::ScratchDir dir;
  const auto t = table();
  REQUIRE(io::write_series_json(dir / "t.json", t).has_value());
  CHECK(mov::test::read_bytes(dir / "t.json") == json_of(t));
  const auto d = daily();
  REQUIRE(io::write_series_json(dir / "d.json", d).has_value());
  CHECK(mov::test::read_bytes(dir / "d.json") == json_of(d));
}

TEST_CASE("a long series is streamed in chunks and written whole",
          "[io][series_json]") {
  std::vector<core::Time> times;
  std::vector<core::Sample> samples;
  for (int i = 0; i < 20000; ++i) {
    times.push_back(utc(2026, 1, 1) + std::chrono::minutes{6 * i});
    samples.push_back(value(i * 0.001));
  }
  const auto t = mov::test::one_column_table(
      water_level(), {StationSpec{.id = "A",
                                  .name = "a",
                                  .where = mov::test::location(1, 2),
                                  .times = times,
                                  .samples = samples}});
  const mov::test::ScratchDir dir;
  REQUIRE(io::write_series_json(dir / "long.json", t).has_value());
  const std::string text = mov::test::read_bytes(dir / "long.json");
  CHECK(text.size() > (std::size_t{1} << 16) * 4);
  CHECK(text == json_of(t));
}

TEST_CASE("a label or unit text the format cannot hold is refused up front",
          "[io][series_json]") {
  for (const std::string& label :
       {std::string{"Water \xE9level"}, std::string{"a\0b", 3}}) {
    CAPTURE(label);
    const auto meta = core::SeriesMeta::make(
        {.quantity = core::Quantity::water_level, .label = label, .unit = {}});
    const auto t = mov::test::one_column_table(
        meta, {StationSpec{.id = "A",
                           .name = "a",
                           .where = mov::test::location(1, 2),
                           .times = {utc(2026, 1, 1)},
                           .samples = {value(1.0)}}});
    const auto refused = io::format_series_json(t);
    REQUIRE(not refused.has_value());
    CHECK(refused.error().code == io::FormatErrc::bad_encoding);
    CHECK(refused.error().subject == "water_level");

    const mov::test::ScratchDir dir;
    const auto written = io::write_series_json(dir / "t.json", t);
    REQUIRE(not written.has_value());
    CHECK(std::holds_alternative<io::FormatError>(written.error()));
    CHECK(mov::test::entry_names(dir.path()).empty());

    auto daily_series = core::DailySeries::make(
        meta, {std::chrono::local_days{std::chrono::days{0}}}, {value(1.0)},
        core::usgs::StatisticId::mean());
    REQUIRE(daily_series.has_value());
    const std::vector<io::DailyAtStation> stations{
        {.station = mov::test::file_station({.id = "B",
                                             .name = "b",
                                             .where = mov::test::location(1, 2),
                                             .times = {},
                                             .samples = {}}),
         .data = *std::move(daily_series)}};
    const auto daily_refused = io::format_series_json(stations);
    REQUIRE(not daily_refused.has_value());
    CHECK(daily_refused.error().code == io::FormatErrc::bad_encoding);
    CHECK(not io::write_series_json(dir / "d.json", stations).has_value());
  }
}

TEST_CASE("a unit text that is not UTF-8 is refused", "[io][series_json]") {
  const auto unit = core::parse_unit("\xE9");
  REQUIRE(unit.has_value());
  const auto meta = core::SeriesMeta::make(
      {.quantity = core::Quantity::water_level, .label = "", .unit = unit});
  const auto t = mov::test::one_column_table(
      meta, {StationSpec{.id = "A",
                         .name = "a",
                         .where = mov::test::location(1, 2),
                         .times = {utc(2026, 1, 1)},
                         .samples = {value(1.0)}}});
  const auto refused = io::format_series_json(t);
  REQUIRE(not refused.has_value());
  CHECK(refused.error().code == io::FormatErrc::bad_encoding);
}

TEST_CASE("a series' sublocation is written after its datum",
          "[io][series_json]") {
  const auto headwater = core::usgs::Sublocation::make("Headwater");
  REQUIRE(headwater.has_value());
  const auto t = table();
  const std::vector<io::ColumnSublocation> sublocations{
      {.station = core::StationIndex{1},
       .column = core::ColumnIndex{0},
       .sublocation = *headwater}};
  const auto text = io::format_series_json(t, sublocations);
  REQUIRE(text.has_value());
  CHECK(text->find(
            R"("datum":"MLLW","sublocation":"Headwater","value":[null,2.5])") !=
        std::string::npos);
  // Only that column has one.
  CHECK(text->find("sublocation") == text->rfind("sublocation"));
  const mov::test::ScratchDir dir;
  REQUIRE(io::write_series_json(dir / "t.json", t, sublocations).has_value());
  CHECK(mov::test::read_bytes(dir / "t.json") == *text);

  auto d = daily();
  d.front().sublocation = *headwater;
  CHECK(
      json_of(d).find(
          R"("unit":"ft3 s-1","sublocation":"Headwater","statistic":"00003")") !=
      std::string::npos);
}

TEST_CASE("a sublocation the table cannot place is refused",
          "[io][series_json]") {
  const auto lock = core::usgs::Sublocation::make("Lock Chamber");
  REQUIRE(lock.has_value());
  const auto t = table();  // 2 stations, 2 columns
  const auto at = [&](std::size_t station, std::size_t column) {
    return io::ColumnSublocation{.station = core::StationIndex{station},
                                 .column = core::ColumnIndex{column},
                                 .sublocation = *lock};
  };
  for (const std::vector<io::ColumnSublocation>& bad :
       {std::vector{at(2, 0)}, std::vector{at(0, 2)},
        std::vector{at(1, 1), at(1, 1)}}) {
    const auto refused = io::format_series_json(t, bad);
    REQUIRE(not refused.has_value());
    CHECK(refused.error() == io::FormatError{.code = io::FormatErrc::bad_option,
                                             .subject = "sublocation"});
    const mov::test::ScratchDir dir;
    CHECK(not io::write_series_json(dir / "t.json", t, bad).has_value());
  }
}
