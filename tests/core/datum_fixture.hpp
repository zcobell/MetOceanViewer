// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Test-only reader for the datum offsets fixture
// (tests/fixtures/core/datum/offsets.csv), shared by the DatumTable and
// datum-shift tests. It aborts the test on any line it does not understand.

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/units.hpp"
#include "mov/test/fixture.hpp"

namespace mov::test {

inline mov::core::Length metres(double v) {
  return mov::core::Length::in(v, mov::core::LengthUnit::meter);
}
inline double in_metres(mov::core::Length v) {
  return v.as(mov::core::LengthUnit::meter);
}

struct Expectation {
  mov::core::VerticalDatum from;
  mov::core::VerticalDatum to;
  double offset;
};

struct Station {
  mov::core::VerticalDatum reference{mov::core::VerticalDatum::msl};
  std::vector<mov::core::DatumHeight> heights;
  std::vector<Expectation> expectations;
  bool bare{false};  // no usable offsets
};

inline std::vector<std::string> split_commas(const std::string& line) {
  std::vector<std::string> fields;
  std::stringstream stream{line};
  for (std::string field; std::getline(stream, field, ',');) {
    fields.push_back(field);
  }
  return fields;
}

inline mov::core::VerticalDatum datum_of(const std::string& token) {
  const auto datum = mov::core::parse_vertical_datum(token);
  REQUIRE(datum.has_value());
  return datum ? datum->value_or(mov::core::VerticalDatum::msl)
               : mov::core::VerticalDatum::msl;
}

inline std::map<std::string, Station> read_offsets_fixture() {
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

}  // namespace mov::test
