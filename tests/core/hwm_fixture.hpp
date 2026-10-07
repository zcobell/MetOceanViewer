// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Test-only readers for the F8 fixtures (tests/fixtures/core/hwm). The real
// HWM file parser is io's (WP8); this one reads only the five or six numeric
// columns of the fixtures and aborts the test on anything else.

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <cerrno>
#include <charconv>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include <version>

#include "mov/core/geo.hpp"
#include "mov/core/hwm.hpp"
#include "mov/core/units.hpp"
#include "mov/test/fixture.hpp"

namespace mov::test {

[[nodiscard]] inline double parse_number(const std::string& text) {
  double value = 0.0;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
  const auto [end, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  REQUIRE(ec == std::errc{});
  REQUIRE(end == text.data() + text.size());
#else
  // No floating-point from_chars (libc++ defines no __cpp_lib_to_chars). The
  // tests run in the "C" locale, which strtod then reads.
  char* end = nullptr;
  errno = 0;
  value = std::strtod(text.c_str(), &end);
  REQUIRE(errno == 0);
  REQUIRE(end == text.c_str() + text.size());
#endif
  return value;
}

[[nodiscard]] inline std::vector<std::string> split(const std::string& line,
                                                    char separator) {
  std::vector<std::string> fields;
  std::istringstream in{line};
  for (std::string field; std::getline(in, field, separator);) {
    fields.push_back(field);
  }
  return fields;
}

/// Columns: lon, lat, ground, observed, modeled[, difference (ignored)].
[[nodiscard]] inline std::vector<mov::core::HighWaterMark> load_hwm_csv(
    const std::string& name, mov::core::LengthUnit unit) {
  std::ifstream file{fixture("core/hwm/" + name)};
  REQUIRE(file.is_open());
  std::vector<mov::core::HighWaterMark> marks;
  for (std::string line; std::getline(file, line);) {
    const auto columns = split(line, ',');
    REQUIRE((columns.size() == 5 or columns.size() == 6));
    const auto where = mov::core::Location::make(
        {.lat = parse_number(columns[1]), .lon = parse_number(columns[0])});
    REQUIRE(where.has_value());
    // The same boundary a real reader uses.
    const auto ground =
        mov::core::checked_elevation(parse_number(columns[2]), unit);
    const auto observed =
        mov::core::checked_elevation(parse_number(columns[3]), unit);
    const auto modeled = mov::core::model_value(parse_number(columns[4]), unit);
    REQUIRE(ground.has_value());
    REQUIRE(observed.has_value());
    REQUIRE(modeled.has_value());
    marks.push_back(mov::core::HighWaterMark{.location = *where,
                                             .ground = *ground,
                                             .observed = *observed,
                                             .modeled = *modeled});
  }
  return marks;
}

/// The expected values golden.py wrote for one fixture file (golden.txt).
class Golden {
 public:
  explicit Golden(const std::string& fixture_name) {
    std::ifstream file{fixture("core/hwm/golden.txt")};
    REQUIRE(file.is_open());
    for (std::string line; std::getline(file, line);) {
      auto fields = split(line, ' ');
      REQUIRE(fields.size() >= 3);
      if (fields[0] != fixture_name) {
        continue;
      }
      const std::string key = fields[1];
      fields.erase(fields.begin(), fields.begin() + 2);
      values_[key] = std::move(fields);
    }
    REQUIRE(not values_.empty());
  }

  [[nodiscard]] bool has(const std::string& key) const {
    return values_.contains(key);
  }
  [[nodiscard]] const std::string& word(const std::string& key) const {
    return values_.at(key).at(0);
  }
  [[nodiscard]] double number(const std::string& key) const {
    return parse_number(word(key));
  }
  [[nodiscard]] std::size_t count(const std::string& key) const {
    return static_cast<std::size_t>(number(key));
  }
  /// nullopt for the word `none`.
  [[nodiscard]] std::optional<double> maybe_number(
      const std::string& key) const {
    return word(key) == "none" ? std::nullopt
                               : std::optional<double>{number(key)};
  }
  [[nodiscard]] const std::vector<std::string>& words(
      const std::string& key) const {
    return values_.at(key);
  }

 private:
  std::map<std::string, std::vector<std::string>> values_;
};

}  // namespace mov::test
