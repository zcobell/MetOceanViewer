// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The series JSON writer (docs/series-json.md) against its golden files.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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
  check_golden(io::format_series_json(table()), "table.json");
}

TEST_CASE("daily values are written with dates and their statistic",
          "[io][series_json]") {
  const auto stations = daily();
  check_golden(io::format_series_json(stations), "daily.json");
}

TEST_CASE("an empty table or list is a document without stations",
          "[io][series_json]") {
  const std::string empty =
      "{\"format\":\"metoceanviewer-series\",\"version\":\"1.0\",\n"
      " \"stations\":[]}\n";
  CHECK(io::format_series_json(core::StationTable{}) == empty);
  CHECK(io::format_series_json(std::vector<io::DailyAtStation>{}) == empty);
}

TEST_CASE("write_series_json writes what format_series_json makes",
          "[io][series_json]") {
  const mov::test::ScratchDir dir;
  const auto t = table();
  REQUIRE(io::write_series_json(dir / "t.json", t).has_value());
  CHECK(mov::test::read_bytes(dir / "t.json") == io::format_series_json(t));
  const auto d = daily();
  REQUIRE(io::write_series_json(dir / "d.json", d).has_value());
  CHECK(mov::test::read_bytes(dir / "d.json") == io::format_series_json(d));
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
  CHECK(text == io::format_series_json(t));
}
