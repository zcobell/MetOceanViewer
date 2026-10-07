// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The HWM file parser feeding the statistics: the F8 fixtures through
// parse_hwm_csv, checked against the numbers golden.py wrote. tests/core does
// the same through a test-only loader; this is the real reader.

#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <expected>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "../core/hwm_fixture.hpp"
#include "../core/test_helpers.hpp"
#include "mov/core/hwm.hpp"
#include "mov/core/hwm_stats.hpp"
#include "mov/core/units.hpp"
#include "mov/io/hwm_file.hpp"
#include "mov/io/read_limits.hpp"

namespace {

using mov::core::DegenerateObserved;
using mov::core::ErrorClasses;
using mov::core::HighWaterMark;
using mov::core::HwmStats;
using mov::core::HwmStatsError;
using mov::core::Intercept;
using mov::core::LengthUnit;
using mov::core::NoWetMarks;
using mov::core::TooFewForFreeFit;
using mov::test::Golden;

// Relative 1e-12, or absolute 1e-12 for a golden that is exactly 0.
bool agrees(double a, double b) {
  return mov::test::near(a, b, 1e-12) or mov::test::near_abs(a, b, 1e-12);
}

double metres_of(mov::core::Length v) { return v.as(LengthUnit::meter); }

double slope_of(const HwmStats& s) {
  return std::visit([](const auto& fit) { return fit.slope; }, s.fit);
}

std::vector<HighWaterMark> read_marks(const std::string& name,
                                      LengthUnit unit) {
  const auto read = mov::io::read_hwm_csv(
      mov::test::fixture("core/hwm/" + name), unit, mov::io::ReadContext{});
  REQUIRE(read.has_value());
  CHECK(read->warnings.empty());
  return read->value;
}

void check_against_golden(const std::string& name, LengthUnit unit) {
  const Golden golden{name};
  const auto marks = read_marks(name, unit);
  // The parser and the test-only loader the core tests use agree.
  CHECK(marks == mov::test::load_hwm_csv(name, unit));
  CHECK(marks.size() == golden.count("total"));

  const auto expected_codes = golden.words("categories");
  REQUIRE(expected_codes.size() == marks.size());
  const ErrorClasses classes = unit == LengthUnit::foot
                                   ? ErrorClasses::feet_default()
                                   : ErrorClasses::meters_default();
  for (std::size_t i = 0; i < marks.size(); ++i) {
    CHECK(std::to_string(static_cast<int>(classify(marks[i], classes))) ==
          expected_codes[i]);
  }

  for (const auto& [mode, prefix] :
       {std::pair{Intercept::free, std::string{"free"}},
        std::pair{Intercept::through_origin, std::string{"origin"}}}) {
    const auto result = hwm_stats(marks, mode);
    const std::string status = golden.word(prefix + ".status");
    if (status != "ok") {
      REQUIRE(not(result.has_value()));
      if (status == "no_wet_marks") {
        CHECK(result.error() ==
              HwmStatsError{NoWetMarks{.total = golden.count("total")}});
      } else if (status == "too_few_for_free_fit") {
        CHECK(result.error() ==
              HwmStatsError{TooFewForFreeFit{.wet = golden.count("wet")}});
      } else {
        CHECK(result.error() == HwmStatsError{DegenerateObserved{}});
      }
      continue;
    }
    REQUIRE(result.has_value());
    CHECK(result->total == golden.count("total"));
    CHECK(result->wet == golden.count("wet"));
    CHECK(agrees(slope_of(*result), golden.number(prefix + ".slope")));
    const auto r2 = golden.maybe_number(prefix + ".r2");
    REQUIRE(result->r_squared.has_value() == r2.has_value());
    if (r2) {
      CHECK(agrees(result->r_squared.value_or(-1.0), *r2));
    }
    CHECK(agrees(metres_of(result->mean_error), golden.number("mean_error")));
    const auto sigma = golden.maybe_number("error_stddev");
    REQUIRE(result->error_stddev.has_value() == sigma.has_value());
    if (sigma) {
      CHECK(
          agrees(metres_of(result->error_stddev.value_or(mov::core::Length{})),
                 *sigma));
    }
    if (mode == Intercept::free) {
      CHECK(agrees(metres_of(std::get<mov::core::Free>(result->fit).intercept),
                   golden.number("free.intercept")));
    }
  }
}

}  // namespace

TEST_CASE(
    "golden through parse_hwm_csv: hwm_basic.csv (two dry, one on a break)",
    "[io][hwm][golden]") {
  check_against_golden("hwm_basic.csv", LengthUnit::meter);
}

TEST_CASE("golden through parse_hwm_csv: hwm_ft.csv (feet, one dry)",
          "[io][hwm][golden]") {
  check_against_golden("hwm_ft.csv", LengthUnit::foot);
}

TEST_CASE("golden through parse_hwm_csv: hwm_allDry.csv (no statistics)",
          "[io][hwm][golden]") {
  check_against_golden("hwm_allDry.csv", LengthUnit::meter);
}

TEST_CASE("golden through parse_hwm_csv: hwm_one_wet.csv (origin only)",
          "[io][hwm][golden]") {
  check_against_golden("hwm_one_wet.csv", LengthUnit::meter);
}

TEST_CASE("golden through parse_hwm_csv: hwm_ties.csv (a tie on every break)",
          "[io][hwm][golden]") {
  check_against_golden("hwm_ties.csv", LengthUnit::meter);
}

TEST_CASE("golden through parse_hwm_csv: hwm_ties_ft.csv (feet ties)",
          "[io][hwm][golden]") {
  check_against_golden("hwm_ties_ft.csv", LengthUnit::foot);
}

TEST_CASE("a header, blank lines, a BOM and CRLF do not change the statistics",
          "[io][hwm][golden]") {
  const auto basic =
      mov::io::read_hwm_csv(mov::test::fixture("core/hwm/hwm_basic.csv"),
                            LengthUnit::meter, mov::io::ReadContext{});
  REQUIRE(basic.has_value());
  const auto expected = hwm_stats(basic->value, Intercept::free);
  REQUIRE(expected.has_value());
  for (const char* name : {"io/hwm/hwm_header.csv", "io/hwm/hwm_blank_line.csv",
                           "io/hwm/hwm_bom_crlf.csv"}) {
    INFO(name);
    const auto read = mov::io::read_hwm_csv(
        mov::test::fixture(name), LengthUnit::meter, mov::io::ReadContext{});
    REQUIRE(read.has_value());
    CHECK(read->value == basic->value);
    CHECK(hwm_stats(read->value, Intercept::free) == expected);
  }
}

TEST_CASE("the dry-rule fixture feeds the statistics only its wet marks",
          "[io][hwm][golden]") {
  const auto read =
      mov::io::read_hwm_csv(mov::test::fixture("io/hwm/hwm_dry_marks.csv"),
                            LengthUnit::meter, mov::io::ReadContext{});
  REQUIRE(read.has_value());
  // -99999, -999 and -1.797e308 are dry; 1.5 and -998.5 are wet (N2: v4
  // counted a different set in each place).
  const auto stats = hwm_stats(read->value, Intercept::through_origin);
  REQUIRE(stats.has_value());
  CHECK(stats->total == 5);
  CHECK(stats->wet == 2);
}
