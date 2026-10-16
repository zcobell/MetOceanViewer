// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The station catalog file format (docs/station-catalog.md): the canonical
// writer against golden files, the round trip, the reader's rules, its
// versioning, and hostile input.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <limits>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "catalog_test_support.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/stop_token.hpp"
#include "mov/core/units.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/station_catalog.hpp"
#include "mov/io/warning.hpp"
#include "mov/test/scratch_dir.hpp"
#include "wire_test_support.hpp"

namespace {

namespace cat = mov::test::catalog;
namespace cc = mov::core::coops;
namespace cu = mov::core::usgs;
namespace io = mov::io;
using cat::Coops;
using cat::Ndbc;
using cat::Usgs;
using io::FormatErrc;
using io::ParseErrc;
using io::WarningCode;
using mov::core::Length;
using mov::core::LengthUnit;
using mov::core::VerticalDatum;
using mov::test::format_error_in;
using mov::test::parse_error_in;
using mov::test::utc;

template <class Code>
Code code(std::string_view text) {
  const auto c = Code::make(text);
  REQUIRE(c.has_value());
  return *c;
}

cu::SeriesInfo series(std::string_view id, std::string_view parameter,
                      std::string name, bool primary,
                      std::optional<mov::core::Unit> unit,
                      mov::core::ValidRange validity) {
  return {.id = code<cu::TimeSeriesId>(id),
          .parameter = code<cu::ParameterCode>(parameter),
          .statistic = cu::StatisticId::instantaneous(),
          .parameter_name = std::move(name),
          .primary = primary,
          .unit = std::move(unit),
          .validity = validity};
}

mov::core::Unit unit(std::string_view text) {
  const auto u = mov::core::parse_unit(text);
  REQUIRE(u.has_value());
  return u.value_or(mov::core::Unit{});
}

// The USGS golden catalog: two sites, one with an ended series.
io::UsgsCatalog usgs_catalog() {
  auto site = cat::entry<Usgs>(
      "USGS-07010000", "MISSISSIPPI RIVER AT ST. LOUIS, MO", 38.629, -90.1796);
  site.station.validity = cat::valid(cat::day(2007, 10, 1));
  auto set = cu::SeriesSet::make(
      {series("0a8e2a0f6ad94e2a9e0e3a3b3c3d3e3f", "00065", "Gage height, feet",
              true, unit("ft"), cat::valid(cat::day(2007, 10, 1))),
       series("1b2c3d4e5f60718293a4b5c6d7e8f901", "00060",
              "Discharge, cubic feet per second", true, unit("ft3 s-1"),
              cat::valid(cat::day(2007, 10, 1), cat::day(2025, 9, 30)))});
  REQUIRE(set.has_value());
  site.caps.series = *std::move(set);
  // A lock and dam: two sublocations, the headwater's gage restarted as a
  // new series (overlapping epochs, decision 37), ids out of file order.
  auto lock = cat::entry<Usgs>(
      "USGS-05587450", "MISSISSIPPI RIVER AT GRAFTON, IL", 38.968, -90.429);
  const auto at = [](std::string_view where) {
    auto s = cu::Sublocation::make(std::string{where});
    REQUIRE(s.has_value());
    return *std::move(s);
  };
  auto tail =
      series("00000000000000000000000000000003", "00065", "Gage height, feet",
             true, unit("ft"), cat::valid(cat::day(2001, 1, 1)));
  tail.sublocation = at("Tailwater");
  tail.description = "Tailwater";
  auto head_new =
      series("00000000000000000000000000000001", "00065", "Gage height, feet",
             true, unit("ft"), cat::valid(cat::day(2020, 6, 1)));
  head_new.sublocation = at("Headwater");
  head_new.description = "Headwater (new gage)";
  auto head_old = series(
      "00000000000000000000000000000002", "00065", "Gage height, feet", true,
      unit("ft"), cat::valid(cat::day(1990, 1, 1), cat::day(2020, 9, 30)));
  head_old.sublocation = at("Headwater");
  head_old.description = "Headwater";
  auto main =
      series("00000000000000000000000000000004", "00065", "Gage height, feet",
             true, unit("ft"), cat::valid(cat::day(2010, 1, 1)));
  auto epochs = cu::SeriesSet::make({tail, head_new, head_old, main});
  REQUIRE(epochs.has_value());
  lock.caps.series = *std::move(epochs);
  auto quiet =
      cat::entry<Usgs>("USGS-295501090190400", "", 29.916944, -90.317778);
  auto bare =
      cu::SeriesSet::make({series("ffffffffffffffffffffffffffffffff", "62620",
                                  "", false, std::nullopt, {})});
  REQUIRE(bare.has_value());
  quiet.caps.series = *std::move(bare);
  return cat::catalog<Usgs>(
      {site, lock, quiet},
      {.generated = utc(2026, 10, 15, 12, 0, 0),
       .sources = {{.url = cat::text("https://api.waterdata.usgs.gov/ogcapi/"
                                     "v0/collections/combined-metadata/items"),
                    .retrieved = utc(2026, 10, 15, 11, 58, 3)}}});
}

// The NDBC golden catalog: a buoy with every fact, and one with none.
io::NdbcCatalog ndbc_catalog() {
  auto buoy = cat::entry<Ndbc>("41001",
                               "EAST HATTERAS - 150 NM East of "
                               "Cape Hatteras",
                               34.724, -72.317);
  buoy.caps = {.owner = cat::text("NDBC"),
               .program = cat::text("NDBC Meteorological/Ocean"),
               .platform_type = cat::text("buoy"),
               .elevation = Length::in(0.0, LengthUnit::meter)};
  auto pier = cat::entry<Ndbc>("BURL1", "Southwest Pass, LA", 28.905, -89.428);
  pier.caps.elevation = Length::in(37.8, LengthUnit::meter);
  auto other = cat::entry<Ndbc>("TPLM2", "Thomas Point, MD \"Lighthouse\"",
                                38.899, -76.436);
  return cat::catalog<Ndbc>(
      {buoy, pier, other},
      {.generated = utc(2026, 10, 15, 12, 0, 0),
       .sources = {
           {.url = cat::text("https://www.ndbc.noaa.gov/activestations.xml"),
            .retrieved = utc(2026, 10, 15, 11, 59, 0)}}});
}

// A minimal file of the provider with these station lines.
std::string document(std::string_view provider, std::string_view stations,
                     std::string_view version = "1.0") {
  return std::format(
      R"({{"format":"metoceanviewer-stations","version":"{}","provider":"{}",)"
      "\n"
      R"( "generated":"2026-10-15T17:00:00Z","sources":[],)"
      "\n"
      R"( "stations":[{}]}})",
      version, provider, stations);
}

constexpr std::string_view pensacola =
    R"({"id":"8729840","name":"Pensacola","lon":-87.2112,"lat":30.4044,"coops":{}})";

std::string coops_with(std::string_view block) {
  return document(
      "noaa_coops",
      std::format(R"({{"id":"8729840","name":"Pensacola",)"
                  R"("lon":-87.2112,"lat":30.4044,"coops":{{{}}}}})",
                  block));
}

std::string usgs_with(std::string_view series_row) {
  return document(
      "usgs",
      std::format(R"({{"id":"USGS-07010000","name":"x","lon":-91,"lat":30,)"
                  R"("usgs":{{"series":[{}]}}}})",
                  series_row));
}

// The line and column of a ParseError of reading `text`.
struct At {
  ParseErrc code;
  std::size_t line;
  std::size_t column;
};

At error_at(const std::string& text) {
  const auto r = cat::parse(text);
  const auto& e = parse_error_in(r);
  return {.code = e.code(), .line = e.line(), .column = e.column().value_or(0)};
}

// Where `needle` starts in `text`, as a line and column.
At position_of(const std::string& text, std::string_view needle,
               ParseErrc code) {
  const std::size_t at = text.find(needle);
  REQUIRE(at != std::string::npos);
  const std::size_t line_start = text.rfind('\n', at);
  const std::size_t line =
      static_cast<std::size_t>(std::count(
          text.begin(), text.begin() + static_cast<std::ptrdiff_t>(at), '\n')) +
      1;
  const std::size_t column =
      line_start == std::string::npos ? at : at - line_start - 1;
  return {.code = code, .line = line, .column = column};
}

// The codes cases.txt names.
FormatErrc format_code(std::string_view name) {
  struct Row {
    std::string_view name;
    FormatErrc code;
  };
  constexpr std::array rows{
      Row{.name = "not_this_format", .code = FormatErrc::not_this_format},
      Row{.name = "bad_version", .code = FormatErrc::bad_version},
      Row{.name = "unsupported_version",
          .code = FormatErrc::unsupported_version},
      Row{.name = "duplicate_station_id",
          .code = FormatErrc::duplicate_station_id},
      Row{.name = "future_dated", .code = FormatErrc::future_dated},
      Row{.name = "inconsistent_metadata",
          .code = FormatErrc::inconsistent_metadata}};
  const auto row = std::ranges::find(rows, name, &Row::name);
  REQUIRE(row != rows.end());
  return row->code;
}

ParseErrc parse_code(std::string_view name) {
  struct Row {
    std::string_view name;
    ParseErrc code;
  };
  constexpr std::array rows{
      Row{.name = "bad_member", .code = ParseErrc::bad_member},
      Row{.name = "bad_date", .code = ParseErrc::bad_date},
      Row{.name = "out_of_range", .code = ParseErrc::out_of_range},
      Row{.name = "not_json", .code = ParseErrc::not_json},
      Row{.name = "too_large", .code = ParseErrc::too_large}};
  const auto row = std::ranges::find(rows, name, &Row::name);
  REQUIRE(row != rows.end());
  return row->code;
}

void check_at(const std::string& text, std::string_view needle,
              ParseErrc code) {
  CAPTURE(text);
  const At got = error_at(text);
  const At want = position_of(text, needle, code);
  CHECK(got.code == want.code);
  CHECK(got.line == want.line);
  CHECK(got.column == want.column);
}

}  // namespace

// ---- golden files and the round trip
// -------------------------------------------

TEST_CASE("the USGS and NDBC catalogs are written as their golden files",
          "[io][catalog][golden]") {
  const auto usgs = usgs_catalog();
  const std::string usgs_text = cat::format(usgs);
  cat::check_golden(usgs_text, "usgs.json");
  const auto ndbc = ndbc_catalog();
  const std::string ndbc_text = cat::format(ndbc);
  cat::check_golden(ndbc_text, "ndbc.json");

  // Their values are on the file's grid already: they read back equal.
  const auto u = cat::parse(usgs_text);
  REQUIRE(u.has_value());
  CHECK(std::get<io::UsgsCatalog>(u->value) == usgs);
  const auto n = cat::parse(ndbc_text);
  REQUIRE(n.has_value());
  CHECK(std::get<io::NdbcCatalog>(n->value) == ndbc);
}

TEST_CASE("the worked example of the spec reads and writes back unchanged",
          "[io][catalog][golden]") {
  const std::string text = mov::test::read_bytes(
      mov::test::fixture("io/station_catalog/worked_example.json"));
  const auto read = cat::parse(text);
  REQUIRE(read.has_value());
  CHECK(read->warnings.empty());
  const auto& c = std::get<io::CoopsCatalog>(read->value);
  CHECK(c.size() == 3);
  CHECK(cat::format(c) == text);

  const auto* pensacola_entry = c.find(cat::id_of<Coops>("8729840"));
  REQUIRE(pensacola_entry != nullptr);
  CHECK(pensacola_entry->station.name.view() == "Pensacola");
  const auto& caps = pensacola_entry->caps;
  CHECK(caps.observed.contains(cc::Observed::six_minute));
  CHECK(caps.predictions == cc::Predictions::any_interval);
  REQUIRE(caps.datums.has_value());
  CHECK(caps.datums.value_or(mov::core::DatumTable{})
            .height_above_msl(VerticalDatum::stnd) ==
        Length::in(-2.757, LengthUnit::meter));
}

TEST_CASE(
    "a catalog file is read through read_station_catalog and written "
    "atomically",
    "[io][catalog]") {
  const mov::test::ScratchDir dir;
  const auto path = dir / "ndbc.json";
  const io::StationCatalog catalog{ndbc_catalog()};
  REQUIRE(io::write_station_catalog(path, catalog).has_value());
  const auto read = io::read_station_catalog(path, cat::later(), {});
  REQUIRE(read.has_value());
  CHECK(read->value == catalog);
  CHECK(mov::test::entry_names(dir.path()) ==
        std::vector<std::string>{"ndbc.json"});

  const auto missing =
      io::read_station_catalog(dir / "absent.json", cat::later(), {});
  REQUIRE(not missing.has_value());
  CHECK(std::holds_alternative<io::FileError>(missing.error()));

  // A refused catalog leaves the target untouched.
  auto bad = ndbc_catalog();
  const auto refused = io::write_station_catalog(
      path, io::StationCatalog{
                cat::catalog<Ndbc>({cat::entry<Ndbc>("41001", "x", 30, -70)},
                                   cat::header(utc(10000, 1, 1)))});
  CHECK(format_error_in(refused).code == FormatErrc::time_out_of_range);
  CHECK(io::read_station_catalog(path, cat::later(), {})->value == catalog);
}

TEST_CASE("numbers are written on the file's grid and read back on it",
          "[io][catalog]") {
  auto e = cat::entry<Coops>("8729840", "x", 30.40441234567, -179.99999996);
  const auto table = mov::core::DatumTable::from_heights(
      VerticalDatum::stnd,
      std::vector<mov::core::DatumHeight>{
          {.datum = VerticalDatum::mhhw,
           .height = Length::in(2.952, LengthUnit::meter)},
          {.datum = VerticalDatum::msl,
           .height = Length::in(2.757, LengthUnit::meter)}});
  REQUIRE(table.has_value());
  e.caps.datums = *table;
  const std::string text = cat::format(cat::catalog<Coops>({e}));
  // -179.99999996 rounds to -180, which is 180; 2.952 - 2.757 is
  // 0.19500000000000028 in binary, written 0.195.
  CHECK(text.find(R"("lon":180,"lat":30.404412,)") != std::string::npos);
  CHECK(text.find(R"("datums":{"MHHW":0.195,"STND":-2.757})") !=
        std::string::npos);

  const auto read = cat::parse(text);
  REQUIRE(read.has_value());
  const auto& again = std::get<io::CoopsCatalog>(read->value).entries()[0];
  CHECK(again.station.location.lon() == 180.0);
  CHECK(again.station.location.lat() == 30.404412);
  CHECK(again.caps.datums.value_or(mov::core::DatumTable{})
            .height_above_msl(VerticalDatum::mhhw) ==
        Length::in(0.195, LengthUnit::meter));
  CHECK(cat::format(std::get<io::CoopsCatalog>(read->value)) == text);
}

TEST_CASE("random catalogs survive the round trip", "[io][catalog][property]") {
  // A fixed seed: a failure must reproduce.
  std::seed_seq seed{2026U, 10U, 15U, 5U};
  std::mt19937_64 rng{seed};
  std::uniform_real_distribution<double> lat{-90.0, 90.0};
  std::uniform_real_distribution<double> lon{-180.0, 360.0};
  std::uniform_real_distribution<double> metres{-50.0, 50.0};
  std::uniform_int_distribution<int> bit{0, 1};
  for (int round = 0; round < 50; ++round) {
    std::vector<io::CatalogEntry<Coops>> entries;
    for (int i = 0; i < 20; ++i) {
      auto e = cat::entry<Coops>(std::format("{:07}", (round * 20) + i),
                                 std::format("station \"{}\"\n", i), lat(rng),
                                 lon(rng));
      for (const cc::Observed o : cc::every_observed) {
        if (bit(rng) == 1) {
          e.caps.observed.insert(o);
        }
      }
      for (const cc::Met m : cc::every_met) {
        if (bit(rng) == 1) {
          e.caps.met.insert(m);
        }
      }
      for (const cc::Datum d : cc::every_datum) {
        if (bit(rng) == 1) {
          e.caps.datums_offered.insert(d);
        }
      }
      e.caps.air_gap = bit(rng) == 1;
      e.caps.predictions = static_cast<cc::Predictions>(i % 3);
      if (bit(rng) == 1) {
        std::vector<mov::core::DatumHeight> rows;
        for (const VerticalDatum d :
             mov::core::Enumerators<VerticalDatum>::values) {
          if (bit(rng) == 1) {
            rows.push_back({.datum = d,
                            .height = Length::in(
                                d == VerticalDatum::msl ? 0.0 : metres(rng),
                                LengthUnit::meter)});
          }
        }
        e.caps.datums =
            mov::core::DatumTable::from_heights(VerticalDatum::msl, rows)
                .value();
      }
      e.station.validity =
          cat::valid(cat::day(1900 + i, 1, 1),
                     bit(rng) == 1 ? std::optional{cat::day(2000 + i, 12, 31)}
                                   : std::nullopt);
      entries.push_back(std::move(e));
    }
    const auto c = cat::catalog<Coops>(std::move(entries));
    const std::string once = cat::format(c);
    const auto read = cat::parse(once);
    REQUIRE(read.has_value());
    const auto& value = std::get<io::CoopsCatalog>(read->value);
    // The file's grid: format(parse(format(c))) == format(c), and what the
    // reader returns survives the round trip exactly.
    const std::string twice = cat::format(value);
    CHECK(twice == once);
    const auto again = cat::parse(twice);
    REQUIRE(again.has_value());
    CHECK(again->value == read->value);
  }
}

// ---- what the writer refuses
// ---------------------------------------------------

TEST_CASE("the writer refuses what the format cannot hold", "[io][catalog]") {
  SECTION("a generated time or retrieval outside the four-digit years") {
    const auto e = format_error_in(io::format_station_catalog(
        cat::catalog<Ndbc>({}, cat::header(utc(-1, 12, 31)))));
    CHECK(e.code == FormatErrc::time_out_of_range);
    CHECK(e.subject == "generated");
    auto h = cat::header();
    h.sources.at(0).retrieved = utc(10000, 1, 1);
    const auto s =
        format_error_in(io::format_station_catalog(cat::catalog<Ndbc>({}, h)));
    CHECK(s.subject == "sources");
  }
  SECTION("a station date outside the four-digit years") {
    auto e = cat::entry<Ndbc>("41001", "x", 30, -70);
    e.station.validity =
        cat::valid(cat::day(1900, 1, 1), cat::day(10000, 1, 1));
    const auto error =
        format_error_in(io::format_station_catalog(cat::catalog<Ndbc>({e})));
    CHECK(error.code == FormatErrc::time_out_of_range);
    CHECK(error.subject == "41001");
  }
  SECTION("an elevation that is not finite") {
    for (const double bad : {std::numeric_limits<double>::infinity(),
                             -std::numeric_limits<double>::infinity(),
                             std::numeric_limits<double>::quiet_NaN()}) {
      auto e = cat::entry<Ndbc>("41001", "x", 30, -70);
      e.caps.elevation = Length::in(bad, LengthUnit::meter);
      const auto error =
          format_error_in(io::format_station_catalog(cat::catalog<Ndbc>({e})));
      CHECK(error.code == FormatErrc::bad_coordinates);
      CHECK(error.subject == "41001");
    }
  }
  SECTION("a series name that is not UTF-8, a series date out of range") {
    auto site = cat::entry<Usgs>("USGS-1", "x", 30, -90);
    const auto with = [&](cu::SeriesInfo s) {
      auto e = site;
      e.caps.series = cu::SeriesSet::make({std::move(s)}).value();
      return io::format_station_catalog(cat::catalog<Usgs>({e}));
    };
    const std::string_view id = "00000000000000000000000000000000";
    CHECK(format_error_in(
              with(series(id, "00060", "\xff", true, std::nullopt, {})))
              .code == FormatErrc::bad_encoding);
    CHECK(format_error_in(with(series(id, "00060", std::string{"a\0b", 3}, true,
                                      std::nullopt, {})))
              .code == FormatErrc::bad_encoding);
    auto described = series(id, "00060", "", true, std::nullopt, {});
    described.description = "\xff";
    CHECK(format_error_in(with(described)).code == FormatErrc::bad_encoding);
    const auto late =
        with(series(id, "00060", "", true, std::nullopt,
                    cat::valid(std::nullopt, cat::day(-5, 1, 1))));
    CHECK(format_error_in(late).code == FormatErrc::time_out_of_range);
    CHECK(format_error_in(late).subject == "USGS-1");
  }
}

TEST_CASE("units are written as udunits text and read back as the same unit",
          "[io][catalog]") {
  for (const std::string_view text :
       {"m", "ft", "ft3 s-1", "m3 s-1", "degC", "percent", "1", "furlong"}) {
    CAPTURE(text);
    auto site = cat::entry<Usgs>("USGS-1", "x", 30, -90);
    site.caps.series =
        cu::SeriesSet::make({series("00000000000000000000000000000000", "00060",
                                    "", true, unit(text), {})})
            .value();
    const auto c = cat::catalog<Usgs>({site});
    const auto read = cat::parse(cat::format(c));
    REQUIRE(read.has_value());
    CHECK(std::get<io::UsgsCatalog>(read->value) == c);
  }
}

// ---- what the reader refuses
// ---------------------------------------------------

TEST_CASE("a file that is not a catalog is not_this_format", "[io][catalog]") {
  for (const std::string& text :
       {std::string{R"({"stations":[]})"}, std::string{"[1,2]"},
        std::string{"7"},
        std::string{R"({"format":"metoceanviewer-harmonics","version":"1.0"})"},
        std::string{R"({"format":7})"}}) {
    CAPTURE(text);
    CHECK(format_error_in(cat::parse(text)).code ==
          FormatErrc::not_this_format);
  }
  CHECK(parse_error_in(cat::parse("")).code() == ParseErrc::empty_input);
  CHECK(parse_error_in(cat::parse("{\"format\":")).code() ==
        ParseErrc::not_json);
}

TEST_CASE("the version gate: 1.x reads, other majors and bad text do not",
          "[io][catalog][version]") {
  for (const std::string_view bad :
       {"1", "1.", ".0", "1.0.0", "01.0", "1.00", "1.x", "v1.0", "", "1.0 ",
        "1234567890.0"}) {
    CAPTURE(bad);
    const auto e = format_error_in(cat::parse(document("ndbc", "", bad)));
    CHECK(e.code == FormatErrc::bad_version);
  }
  CHECK(format_error_in(cat::parse(R"({"format":"metoceanviewer-stations"})"))
            .code == FormatErrc::bad_version);
  for (const std::string_view other : {"0.9", "2.0", "10.0"}) {
    CAPTURE(other);
    const auto e = format_error_in(cat::parse(document("ndbc", "", other)));
    CHECK(e.code == FormatErrc::unsupported_version);
    CHECK(e.subject == other);
  }
  // A failure later in the file does not hide the version.
  const std::string late_version =
      R"({"stations":[{"id":"bad"}],"format":"metoceanviewer-stations",)"
      R"("version":"2.0"})";
  CHECK(format_error_in(cat::parse(late_version)).code ==
        FormatErrc::unsupported_version);
}

TEST_CASE("a newer minor is read with its unknown content ignored and counted",
          "[io][catalog][version]") {
  const std::string text =
      R"({"format":"metoceanviewer-stations","version":"1.3",)"
      R"("provider":"noaa_coops","generated":"2026-10-15T17:00:00Z",)"
      R"("sources":[{"url":"u","retrieved":"2026-10-15T17:00:00Z","hash":"x"}],)"
      R"("licence":"public domain","stations":[)"
      R"({"id":"8729840","name":"Pensacola","lon":-87.2112,"lat":30.4044,)"
      R"("tz":"America/Chicago","coops":{"met":["wind","rain_fall"],)"
      R"("currents":{"bins":3},"predictions":"currents_only"}},)"
      R"({"id":"8729108","name":"Panama City","lon":-85.6669,"lat":30.1525,)"
      R"("tz":"America/Chicago","coops":{"met":["rain_fall"]}}]})";
  const auto read = cat::parse(text);
  REQUIRE(read.has_value());
  CHECK(read->warnings ==
        std::vector<io::Warning>{
            {.code = WarningCode::minor_newer, .subject = "1.3"},
            {.code = WarningCode::newer_content_ignored, .subject = "licence"},
            {.code = WarningCode::newer_content_ignored,
             .subject = "sources[].hash"},
            {.code = WarningCode::newer_content_ignored,
             .subject = "stations[].coops.currents"},
            {.code = WarningCode::newer_content_ignored,
             .subject = "stations[].coops.met[]=rain_fall",
             .count = 2},
            {.code = WarningCode::newer_content_ignored,
             .subject = "stations[].coops.predictions=currents_only"},
            {.code = WarningCode::newer_content_ignored,
             .subject = "stations[].tz",
             .count = 2}});
  const auto& c = std::get<io::CoopsCatalog>(read->value);
  REQUIRE(c.size() == 2);
  CHECK(c.find(cat::id_of<Coops>("8729840"))->caps.met ==
        cc::MetSet{cc::Met::wind});
  CHECK(c.find(cat::id_of<Coops>("8729840"))->caps.predictions ==
        cc::Predictions::none);

  // The same content in a 1.0 file: the first unknown is an error there.
  std::string strict = text;
  strict.replace(strict.find("1.3"), 3, "1.0");
  check_at(strict, "\"hash\"", ParseErrc::bad_member);
}

TEST_CASE("a newer minor counts at most 32 distinct unknown paths",
          "[io][catalog][version]") {
  std::string members;
  for (int i = 0; i < 40; ++i) {
    members += std::format(R"("x{:02}":1,)", i);
  }
  const std::string text =
      document("ndbc",
               std::format(R"({{{}"id":"41001","name":"","lon":0,"lat":0,)"
                           R"("ndbc":{{}}}})",
                           members),
               "1.1");
  const auto read = cat::parse(text);
  REQUIRE(read.has_value());
  REQUIRE(read->warnings.size() == 1 + 32 + 1);
  CHECK(read->warnings.back() ==
        io::Warning{.code = WarningCode::newer_content_ignored,
                    .subject = "other",
                    .count = 8});
}

TEST_CASE("unknown members and tokens of a 1.0 file are refused where they are",
          "[io][catalog]") {
  check_at(document("noaa_coops", pensacola).insert(1, R"("extra":1,)"),
           "\"extra\"", ParseErrc::bad_member);
  check_at(coops_with(R"("met":["wind","rain_fall"])"), "\"rain_fall\"",
           ParseErrc::bad_member);
  check_at(coops_with(R"("predictions":"sometimes")"), "\"sometimes\"",
           ParseErrc::bad_member);
  check_at(coops_with(R"("datums_offered":["NGVD29"])"), "\"NGVD29\"",
           ParseErrc::bad_member);
  check_at(coops_with(R"("datums":{"MHHW":1,"XYZ":2})"), "\"XYZ\"",
           ParseErrc::bad_member);
}

TEST_CASE("each station value the format refuses is a ParseError at it",
          "[io][catalog]") {
  struct Case {
    std::string text;
    std::string_view at;
    ParseErrc code;
  };
  const auto refused = [](std::string text, std::string_view at,
                          ParseErrc code) {
    return Case{.text = std::move(text), .at = at, .code = code};
  };
  const std::vector<Case> cases{
      // ids: the provider's grammar, exactly in canonical form
      refused(
          document("noaa_coops",
                   R"({"id":"872984","name":"P","lon":0,"lat":0,"coops":{}})"),
          "\"872984\"", ParseErrc::bad_member),
      refused(
          document(
              "usgs",
              R"({"id":"usgs-07010000","name":"x","lon":0,"lat":0,"usgs":{}})"),
          "\"usgs-", ParseErrc::bad_member),
      refused(
          document("ndbc",
                   R"({"id":" 41001","name":"x","lon":0,"lat":0,"ndbc":{}})"),
          "\" 41001\"", ParseErrc::bad_member),
      refused(document("ndbc",
                       R"({"id":41001,"name":"x","lon":0,"lat":0,"ndbc":{}})"),
              "41001", ParseErrc::bad_member),
      // missing members: at the station
      refused(document("ndbc", R"({"name":"x","lon":0,"lat":0,"ndbc":{}})"),
              "{\"name\"", ParseErrc::bad_member),
      refused(document("ndbc", R"({"id":"41001","lon":0,"lat":0,"ndbc":{}})"),
              "{\"id\"", ParseErrc::bad_member),
      refused(
          document("ndbc", R"({"id":"41001","name":"x","lat":0,"ndbc":{}})"),
          "{\"id\"", ParseErrc::bad_member),
      // types and texts
      refused(
          document(
              "ndbc",
              R"({"id":"41001","name":"a\u0000b","lon":0,"lat":0,"ndbc":{}})"),
          R"("a\u0000b")", ParseErrc::bad_member),
      refused(
          document("ndbc",
                   R"({"id":"41001","name":"x","lon":"0","lat":0,"ndbc":{}})"),
          "\"0\"", ParseErrc::bad_member),
      refused(
          document("ndbc",
                   R"({"id":"41001","name":"x","lon":0,"lat":0,"ndbc":[]})"),
          "[]}]", ParseErrc::bad_member),
      refused(
          document(
              "ndbc",
              R"({"id":"41001","name":"x","lon":0,"lat":0,"ndbc":{"elevation_m":null}})"),
          "null", ParseErrc::bad_member),
      // positions
      refused(
          document("ndbc",
                   R"({"id":"41001","name":"x","lon":0,"lat":90.5,"ndbc":{}})"),
          "90.5", ParseErrc::out_of_range),
      refused(
          document("ndbc",
                   R"({"id":"41001","name":"x","lon":-181,"lat":0,"ndbc":{}})"),
          "-181", ParseErrc::out_of_range),
      // dates
      refused(
          document(
              "ndbc",
              R"({"id":"41001","name":"x","lon":0,"lat":0,"first":"2026-02-29","ndbc":{}})"),
          "\"2026-02-29\"", ParseErrc::bad_date),
      refused(
          document(
              "ndbc",
              R"({"id":"41001","name":"x","lon":0,"lat":0,"first":"2020-01-02","last":"2020-01-01","ndbc":{}})"),
          "\"2020-01-01\"", ParseErrc::out_of_range),
      // provider blocks
      refused(document("ndbc", R"({"id":"41001","name":"x","lon":0,"lat":0})"),
              "{\"id\"", ParseErrc::bad_member),
      refused(
          document(
              "ndbc",
              R"({"id":"41001","name":"x","lon":0,"lat":0,"ndbc":{},"usgs":{}})"),
          "{}}]", ParseErrc::bad_member),
      refused(
          document("noaa_coops",
                   R"({"id":"41001","name":"x","lon":0,"lat":0,"ndbc":{}})"),
          "{\"id\"", ParseErrc::bad_member),
      // CO-OPS block
      refused(coops_with(R"("observed":["hourly","hourly"])"), "\"hourly\"]",
              ParseErrc::bad_member),
      refused(coops_with(R"("observed":"hourly")"), "\"hourly\"",
              ParseErrc::bad_member),
      refused(coops_with(R"("air_gap":1)"), "1}", ParseErrc::bad_member),
      refused(coops_with(R"("datums":{"MSL":0.5})"), "0.5",
              ParseErrc::out_of_range),
      // MSL is exactly 0 as written, not once rounded to 1e-4 m.
      refused(coops_with(R"("datums":{"MSL":0.00001})"), "0.00001",
              ParseErrc::out_of_range),
      refused(coops_with(R"("datums":{"MHHW":"1"})"), "\"1\"",
              ParseErrc::bad_member),
      // USGS series
      refused(
          usgs_with(R"({"id":"ABC","parameter":"00060","statistic":"00011"})"),
          "\"ABC\"", ParseErrc::bad_member),
      refused(
          usgs_with(
              R"({"id":"00000000000000000000000000000000","parameter":"0006","statistic":"00011"})"),
          "\"0006\"", ParseErrc::bad_member),
      refused(
          usgs_with(
              R"({"id":"00000000000000000000000000000000","parameter":"00060"})"),
          R"({"id":"0000)", ParseErrc::bad_member),
      refused(
          usgs_with(
              R"({"id":"00000000000000000000000000000000","parameter":"00060","statistic":"00011","unit":" "})"),
          "\" \"", ParseErrc::bad_member),
      refused(
          usgs_with(
              R"({"id":"00000000000000000000000000000000","parameter":"00060","statistic":"00011","primary":"yes"})"),
          "\"yes\"", ParseErrc::bad_member),
      // A sublocation: non-empty text without surrounding space.
      refused(
          usgs_with(
              R"({"id":"00000000000000000000000000000000","parameter":"00060","statistic":"00011","sublocation":""})"),
          R"(""})", ParseErrc::bad_member),
      refused(
          usgs_with(
              R"({"id":"00000000000000000000000000000000","parameter":"00060","statistic":"00011","sublocation":"Headwater "})"),
          R"("Headwater ")", ParseErrc::bad_member),
      refused(
          usgs_with(
              R"({"id":"00000000000000000000000000000000","parameter":"00060","statistic":"00011","description":7})"),
          "7}", ParseErrc::bad_member),
      // A parameter name is station text: no NUL.
      refused(
          usgs_with(
              R"({"id":"00000000000000000000000000000000","parameter":"00060","statistic":"00011","parameter_name":"a\u0000"})"),
          R"("a\u0000")", ParseErrc::bad_member),
      // header values
      refused(document("harmonics", ""), "\"harmonics\"",
              ParseErrc::bad_member),
      refused(document("noaa", ""), "\"noaa\"", ParseErrc::bad_member),
      refused(
          R"({"format":"metoceanviewer-stations","version":"1.0","provider":"ndbc",)"
          R"("generated":"2026-10-15 17:00:00","sources":[],"stations":[]})",
          "\"2026-10-15 17:00:00\"", ParseErrc::bad_date),
      refused(
          R"({"format":"metoceanviewer-stations","version":"1.0","provider":"ndbc",)"
          R"("generated":"2026-10-15T17:00:00Z","sources":[{"url":"u"}],"stations":[]})",
          "{\"url\"", ParseErrc::bad_member),
      refused(
          R"({"format":"metoceanviewer-stations","version":"1.0","provider":"ndbc",)"
          R"("generated":"2026-10-15T17:00:00Z","sources":[],"stations":{}})",
          "{}", ParseErrc::bad_member),
      refused(
          R"({"format":"metoceanviewer-stations","version":"1.0","provider":"ndbc",)"
          R"("generated":"2026-10-15T17:00:00Z","stations":[]})",
          "{\"format\"", ParseErrc::bad_member),
  };
  for (const Case& c : cases) {
    check_at(c.text, c.at, c.code);
  }
}

TEST_CASE("the first refused value in document order is the one reported",
          "[io][catalog]") {
  // A bad second station, then a bad generated time after the stations.
  const std::string text =
      R"({"format":"metoceanviewer-stations","version":"1.0","provider":"ndbc",)"
      R"("sources":[],"stations":[)"
      R"({"id":"41001","name":"x","lon":0,"lat":0,"ndbc":{}},)"
      R"({"id":"41002","name":"x","lon":0,"lat":95,"ndbc":{}}],)"
      R"("generated":"never"})";
  check_at(text, "95", ParseErrc::out_of_range);
  // The provider after a station of another provider's block.
  const std::string other =
      R"({"format":"metoceanviewer-stations","version":"1.0","sources":[],)"
      R"("generated":"2026-10-15T17:00:00Z","stations":[)"
      R"({"id":"41001","name":"x","lon":0,"lat":0,"ndbc":{}}],)"
      R"("provider":"usgs"})";
  check_at(other, "{\"id\"", ParseErrc::bad_member);
}

TEST_CASE("cross-station and header rules are FormatErrors", "[io][catalog]") {
  SECTION("a repeated station id") {
    const auto e = format_error_in(cat::parse(document(
        "ndbc", R"({"id":"41002","name":"a","lon":0,"lat":0,"ndbc":{}},)"
                R"({"id":"41001","name":"b","lon":0,"lat":0,"ndbc":{}},)"
                R"({"id":"41002","name":"c","lon":0,"lat":0,"ndbc":{}})")));
    CHECK(e.code == FormatErrc::duplicate_station_id);
    CHECK(e.subject == "41002");
  }
  SECTION("series of a station that SeriesSet refuses") {
    const std::string row =
        R"({"id":"00000000000000000000000000000000","parameter":"00060","statistic":"00011","primary":true})";
    const auto dup = format_error_in(cat::parse(usgs_with(row + "," + row)));
    CHECK(dup.code == FormatErrc::inconsistent_metadata);
    CHECK(dup.subject.starts_with("USGS-07010000: "));
    // Two Primary epochs of one parameter and statistic: legal (decision
    // 37: the later-begun one wins where they overlap).
    const std::string other =
        R"({"id":"10000000000000000000000000000000","parameter":"00060","statistic":"00011","primary":true})";
    CHECK(cat::parse(usgs_with(row + "," + other)).has_value());
  }
  SECTION("generated more than a day ahead of the clock") {
    const std::string text = document("ndbc", "");
    const auto generated = utc(2026, 10, 15, 17, 0, 0);
    const auto day = std::chrono::milliseconds{std::chrono::hours{24}};
    CHECK(io::parse_station_catalog(text, generated - day, {}).has_value());
    const auto ahead = io::parse_station_catalog(
        text, generated - day - std::chrono::milliseconds{1}, {});
    const auto e = format_error_in(ahead);
    CHECK(e.code == FormatErrc::future_dated);
    CHECK(e.subject == "2026-10-15T17:00:00Z");
  }
}

TEST_CASE("what a reader accepts beyond the canonical text", "[io][catalog]") {
  // Defaults written out, MSL listed as 0, members in any order, stations
  // unsorted, a byte order mark, white space.
  const std::string text =
      "\xEF\xBB\xBF  {\"stations\":[\n"
      R"({"coops":{"air_gap":false,"predictions":"none","observed":[],"met":[],)"
      R"("datums":{"MSL":0,"MLLW":-0.188}},"lat":30.4044,"lon":-87.2112,)"
      R"("name":"Pensacola","id":"8729840"},)"
      "\n"
      R"({"id":"1611400","name":"Nawiliwili","lon":-159.3561,"lat":21.9544,)"
      R"("coops":{}}],)"
      R"("sources":[],"generated":"2026-10-15T17:00:00Z","provider":"noaa_coops",)"
      R"("version":"1.0","format":"metoceanviewer-stations"}  )";
  const auto read = cat::parse(text);
  REQUIRE(read.has_value());
  const auto& c = std::get<io::CoopsCatalog>(read->value);
  REQUIRE(c.size() == 2);
  CHECK(c.entries()[0].station.id.value() == "1611400");
  const auto& p = c.entries()[1];
  CHECK(not p.caps.air_gap);
  CHECK(p.caps.datums.value_or(mov::core::DatumTable{})
            .height_above_msl(VerticalDatum::mllw) ==
        Length::in(-0.188, LengthUnit::meter));
}

// ---- hostile input
// ------------------------------------------------------------------

TEST_CASE("hostile catalogs are refused within the limits", "[io][catalog]") {
  SECTION("nesting") {
    const std::string deep = std::string(100, '[') + std::string(100, ']');
    CHECK(parse_error_in(cat::parse(deep)).code() == ParseErrc::too_large);
  }
  SECTION("a NUL byte") {
    const std::string text = document("ndbc", "") + std::string(1, '\0');
    CHECK(parse_error_in(cat::parse(text)).code() == ParseErrc::not_json);
  }
  SECTION("more stations than max_elements allows") {
    std::string stations;
    for (int i = 0; i < 10; ++i) {
      stations += std::format(
          R"({}{{"id":"{:05}","name":"","lon":0,"lat":0,"ndbc":{{}}}})",
          i == 0 ? "" : ",", i);
    }
    io::ReadContext ctx;
    ctx.limits.max_elements = 16 * 9;
    CHECK(parse_error_in(cat::parse(document("ndbc", stations), ctx)).code() ==
          ParseErrc::too_large);
    ctx.limits.max_elements = 16 * 10;
    CHECK(cat::parse(document("ndbc", stations), ctx).has_value());
  }
  SECTION("too many unknown members in one object, even in a newer minor") {
    // 100k unknown members: each new key is compared with those kept so
    // far, so an unbounded object would make the parse quadratic.
    std::string members;
    for (int i = 0; i < 100000; ++i) {
      members += std::format(R"("u{}":0,)", i);
    }
    const std::string text = document(
        "ndbc",
        std::format(
            R"({{{}"id":"41001","name":"","lon":0,"lat":0,"ndbc":{{}}}})",
            members),
        "1.9");
    CHECK(parse_error_in(cat::parse(text)).code() == ParseErrc::too_large);
    // 64 are kept, and counted.
    std::string few;
    for (int i = 0; i < 64; ++i) {
      few += std::format(R"("u{}":0,)", i);
    }
    const auto read = cat::parse(document(
        "ndbc",
        std::format(
            R"({{{}"id":"41001","name":"","lon":0,"lat":0,"ndbc":{{}}}})", few),
        "1.9"));
    REQUIRE(read.has_value());
  }
  SECTION("too many tokens in a set") {
    std::string tokens = "\"wind\"";
    for (int i = 0; i < 64; ++i) {
      tokens += ",\"wind\"";
    }
    CHECK(parse_error_in(cat::parse(coops_with("\"met\":[" + tokens + "]")))
              .code() == ParseErrc::too_large);
  }
  SECTION("a text over max_text_bytes") {
    io::ReadContext ctx;
    ctx.limits.max_text_bytes = 64;
    CHECK(parse_error_in(cat::parse(document("ndbc", ""), ctx)).code() ==
          ParseErrc::too_large);
  }
  SECTION("a cancelled read") {
    const io::ReadContext ctx{
        .limits = {}, .stop = mov::core::StopToken{[] { return true; }}};
    const auto r = cat::parse(document("ndbc", ""), ctx);
    REQUIRE(not r.has_value());
    CHECK(std::holds_alternative<io::Cancelled>(r.error()));
  }
}

// ---- the fixture files and their verdicts
// --------------------------------------

TEST_CASE("every fixture file reads as cases.txt says", "[io][catalog]") {
  std::istringstream cases{mov::test::read_bytes(
      mov::test::fixture("io/station_catalog/cases.txt"))};
  std::size_t files = 0;
  for (std::string line; std::getline(cases, line);) {
    if (line.empty() or line.starts_with('#')) {
      continue;
    }
    std::istringstream words{line};
    std::string file;
    std::string verdict;
    words >> file >> verdict;
    CAPTURE(file, verdict);
    ++files;
    const auto read = io::read_station_catalog(
        mov::test::fixture("io/station_catalog/" + file), cat::later(), {});
    if (verdict == "ok") {
      REQUIRE(read.has_value());
      CHECK(read->warnings.empty());
    } else if (verdict == "newer") {
      REQUIRE(read.has_value());
      REQUIRE(not read->warnings.empty());
      CHECK(read->warnings.front().code == WarningCode::minor_newer);
    } else if (verdict.starts_with("format:")) {
      CHECK(format_error_in(read).code == format_code(verdict.substr(7)));
    } else {
      REQUIRE(verdict.starts_with("parse:"));
      CHECK(parse_error_in(read).code() == parse_code(verdict.substr(6)));
    }
  }
  CHECK(files >= 10);
}
