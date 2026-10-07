// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <map>
#include <random>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/test/fixture.hpp"

using mov::core::DatumHeight;
using mov::core::DatumTable;
using mov::core::DatumTableError;
using mov::core::Length;
using mov::core::LengthUnit;
using mov::core::MissingMsl;
using mov::core::NonFiniteHeight;
using mov::core::parse_vertical_datum;
using mov::core::to_string;
using mov::core::VerticalDatum;

namespace {

constexpr std::array all_datums{VerticalDatum::mhhw,   VerticalDatum::mhw,
                                VerticalDatum::mtl,    VerticalDatum::msl,
                                VerticalDatum::mlw,    VerticalDatum::mllw,
                                VerticalDatum::navd88, VerticalDatum::ngvd29,
                                VerticalDatum::igld85, VerticalDatum::stnd};

Length metres(double v) { return Length::in(v, LengthUnit::meter); }
double in_metres(Length v) { return v.as(LengthUnit::meter); }

// ---- the F12 fixture --------------------------------------------------------

struct Expectation {
  VerticalDatum from;
  VerticalDatum to;
  double offset;
};

struct Station {
  VerticalDatum reference{VerticalDatum::msl};
  std::vector<DatumHeight> heights;
  std::vector<Expectation> expectations;
  bool bare{false};  // no usable offsets
};

std::vector<std::string> split_commas(const std::string& line) {
  std::vector<std::string> fields;
  std::stringstream stream{line};
  for (std::string field; std::getline(stream, field, ',');) {
    fields.push_back(field);
  }
  return fields;
}

VerticalDatum datum_of(const std::string& token) {
  const auto datum = parse_vertical_datum(token);
  REQUIRE(datum.has_value());
  return datum ? datum->value_or(VerticalDatum::msl) : VerticalDatum::msl;
}

std::map<std::string, Station> read_offsets_fixture() {
  std::ifstream file{mov::test::fixture("core/datum/offsets.csv")};
  REQUIRE(file.is_open());
  std::map<std::string, Station> stations;
  for (std::string line; std::getline(file, line);) {
    if (line.empty() or line.front() == '#') {
      continue;
    }
    const auto f = split_commas(line);
    REQUIRE(f.size() >= 3);
    Station& station = stations[f[1]];
    if (f[0] == "bare") {
      station.reference = datum_of(f[2]);
      station.bare = true;
    } else if (f[0] == "row") {
      REQUIRE(f.size() == 5);
      station.reference = datum_of(f[2]);
      station.heights.push_back(
          {.datum = datum_of(f[3]), .height = metres(std::stod(f[4]))});
    } else {
      REQUIRE(f[0] == "expect");
      REQUIRE(f.size() == 5);
      station.expectations.push_back({.from = datum_of(f[2]),
                                      .to = datum_of(f[3]),
                                      .offset = std::stod(f[4])});
    }
  }
  return stations;
}

}  // namespace

TEST_CASE("F12: NOAA and XTide rows give the legacy offsets",
          "[core][datum][fixture]") {
  const auto stations = read_offsets_fixture();
  REQUIRE(stations.size() == 3);
  for (const auto& [name, station] : stations) {
    if (station.bare) {
      continue;
    }
    INFO("station " << name);
    const auto table =
        DatumTable::from_heights(station.reference, station.heights);
    REQUIRE(table.has_value());
    REQUIRE_FALSE(station.expectations.empty());
    for (const Expectation& e : station.expectations) {
      INFO(to_string(e.from) << " -> " << to_string(e.to));
      const auto offset = table->offset(e.from, e.to);
      REQUIRE(offset.has_value());
      CHECK(std::abs(in_metres(*offset) - e.offset) < 1e-9);
    }
  }
}

TEST_CASE("F12: a station with no usable offsets has no table",
          "[core][datum][fixture]") {
  const auto stations = read_offsets_fixture();
  const Station& bare = stations.at("xtide:0002");
  REQUIRE(bare.bare);
  const auto table = DatumTable::from_heights(bare.reference, bare.heights);
  REQUIRE_FALSE(table.has_value());
  CHECK(std::holds_alternative<MissingMsl>(table.error()));
}

TEST_CASE("F12: NOAA legacy offsets are the negated heights",
          "[core][datum][fixture]") {
  // noaa_stations.csv row 8518750, MLLW column: MSL data + 0.77527 = MLLW data.
  const auto stations = read_offsets_fixture();
  const Station& battery = stations.at("noaa:8518750");
  const auto table =
      DatumTable::from_heights(battery.reference, battery.heights);
  REQUIRE(table.has_value());
  const auto offset = table->offset(VerticalDatum::msl, VerticalDatum::mllw);
  REQUIRE(offset.has_value());
  CHECK(std::abs(in_metres(*offset) - 0.77527) < 1e-12);
  // xtide_stations.csv row xTide_0001, MSL column: MLLW data - 2.199 = MSL
  // data.
  const Station& narrows = stations.at("xtide:0001");
  const auto xtide =
      DatumTable::from_heights(narrows.reference, narrows.heights);
  REQUIRE(xtide.has_value());
  const auto to_msl = xtide->offset(VerticalDatum::mllw, VerticalDatum::msl);
  REQUIRE(to_msl.has_value());
  CHECK(std::abs(in_metres(*to_msl) + 2.199) < 1e-12);
}

TEST_CASE("from_heights rejects NaN heights", "[core][datum]") {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const std::array bad{
      DatumHeight{.datum = VerticalDatum::mhhw, .height = metres(nan)}};
  const auto table = DatumTable::from_heights(VerticalDatum::msl, bad);
  REQUIRE_FALSE(table.has_value());
  CHECK(table.error() == DatumTableError{NonFiniteHeight{VerticalDatum::mhhw}});

  // Before a conflict that comes later, and in the reference's own row.
  const std::array first{
      DatumHeight{.datum = VerticalDatum::mhw, .height = metres(1.0)},
      DatumHeight{.datum = VerticalDatum::mlw, .height = metres(nan)},
      DatumHeight{.datum = VerticalDatum::mhw, .height = metres(2.0)}};
  CHECK(DatumTable::from_heights(VerticalDatum::msl, first).error() ==
        DatumTableError{NonFiniteHeight{VerticalDatum::mlw}});
  const std::array reference_row{
      DatumHeight{.datum = VerticalDatum::mllw, .height = metres(nan)}};
  CHECK(DatumTable::from_heights(VerticalDatum::mllw, reference_row).error() ==
        DatumTableError{NonFiniteHeight{VerticalDatum::mllw}});
}

TEST_CASE("from_heights rejects a rebasing that overflows", "[core][datum]") {
  // 1.7e308 above a reference that MSL sits 1.7e308 below is out of range.
  const std::array rows{
      DatumHeight{.datum = VerticalDatum::msl, .height = metres(-1.7e308)},
      DatumHeight{.datum = VerticalDatum::mhhw, .height = metres(1.7e308)}};
  const auto table = DatumTable::from_heights(VerticalDatum::mllw, rows);
  REQUIRE_FALSE(table.has_value());
  CHECK(table.error() == DatumTableError{NonFiniteHeight{VerticalDatum::mhhw}});
}

TEST_CASE("offsets are transitive on arbitrary heights (1e-12)",
          "[core][datum]") {
  std::seed_seq seed{2026, 10, 7};  // fixed: a failure must reproduce
  std::mt19937 rng{seed};
  std::uniform_real_distribution<double> height_range{-5.0, 5.0};
  for (int trial = 0; trial < 200; ++trial) {
    std::vector<DatumHeight> heights;
    for (const VerticalDatum d : all_datums) {
      if (d != VerticalDatum::msl) {
        heights.push_back({.datum = d, .height = metres(height_range(rng))});
      }
    }
    const auto table = DatumTable::from_heights(VerticalDatum::msl, heights);
    REQUIRE(table.has_value());
    for (const VerticalDatum a : all_datums) {
      for (const VerticalDatum b : all_datums) {
        for (const VerticalDatum c : all_datums) {
          const double ab = in_metres(*table->offset(a, b));
          const double bc = in_metres(*table->offset(b, c));
          const double ac = in_metres(*table->offset(a, c));
          REQUIRE(std::abs((ab + bc) - ac) < 1e-12);
          REQUIRE(in_metres(*table->offset(b, a)) == -ab);
        }
      }
    }
  }
}

TEST_CASE("datum tokens are distinct and upper case", "[core][datum]") {
  std::vector<std::string> seen;
  for (const VerticalDatum d : all_datums) {
    const std::string token{to_string(d)};
    CHECK(not token.empty());
    for (const char c : token) {
      CHECK(((c >= 'A' and c <= 'Z') or (c >= '0' and c <= '9')));
    }
    for (const std::string& other : seen) {
      CHECK(token != other);
    }
    seen.push_back(token);
  }
}

namespace {

std::string upper(std::string text) {
  for (char& c : text) {
    if (c >= 'a' and c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  return text;
}

std::string lower(std::string text) {
  for (char& c : text) {
    if (c >= 'A' and c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  return text;
}

}  // namespace

// The datum or the no-datum answer must not depend on the case or on the
// whitespace around the text; an unknown text stays unknown.
TEST_CASE("datum parsing ignores case and surrounding whitespace",
          "[core][datum]") {
  std::vector<std::string> texts{"navd", "ngvd", "igld",  "none",
                                 "",     "LWI",  "mllwx", "Odd Case"};
  for (const VerticalDatum d : all_datums) {
    texts.emplace_back(to_string(d));
  }
  for (const std::string& text : texts) {
    const auto base = parse_vertical_datum(text);
    for (const std::string& variant :
         {upper(text), lower(text), " " + text + "\t",
          "\n" + upper(text) + " "}) {
      INFO("text '" << text << "', variant '" << variant << "'");
      const auto other = parse_vertical_datum(variant);
      REQUIRE(base.has_value() == other.has_value());
      if (base) {
        CHECK(*base == *other);
      }
    }
  }
}
