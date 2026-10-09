// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Run-time tests of the SP 98 astronomy, node factors and the yearly nodal
// scheme: the prototype's test vectors (docs/harmonics-json.md 13.2), the
// compound laws, the mid-year identity, and libcongen's golden table.

#include <algorithm>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/tide/astronomy.hpp"
#include "mov/core/tide/catalogue.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/node_factor.hpp"
#include "mov/core/tide/phase.hpp"
#include "mov/core/tide/year_arguments.hpp"
#include "mov/test/fixture.hpp"

using mov::core::Time;
using mov::core::tide::all_node_factors;
using mov::core::tide::Argument;
using mov::core::tide::catalogue_argument;
using mov::core::tide::CompoundErrc;
using mov::core::tide::CompoundTerms;
using mov::core::tide::ConstituentName;
using mov::core::tide::Equilibrium;
using mov::core::tide::NodalTerms;
using mov::core::tide::NodeFactor;
using mov::core::tide::Phase;
using mov::core::tide::YearAstronomy;
using mov::core::tide::YearOutOfRange;

namespace detail = mov::core::detail;
namespace tide = mov::core::tide;
namespace chr = std::chrono;

namespace {

Argument arg(std::string_view name) {
  return catalogue_argument(name).value_or(Argument{});
}

YearAstronomy year(int y) {
  auto a = YearAstronomy::make(chr::year{y});
  if (not a) {
    FAIL("year out of range: " << y);
  }
  return *std::move(a);
}

/// |a - b| on the circle, in [0, 180].
double angle_between(double a, double b) {
  const double d = Phase::wrap(a - b).degrees();
  return d > 180.0 ? 360.0 - d : d;
}

ConstituentName name(std::string_view s) {
  auto n = ConstituentName::make(s);
  if (not n) {
    FAIL("not a constituent name: " << s);
  }
  return *std::move(n);
}

}  // namespace

TEST_CASE("nodal terms at mid-2026 match the prototype",
          "[core][tide][astronomy]") {
  // proto_predict.py uterms(2026-07-02T12:00Z), %.10f (HJ 13.2 quotes them to
  // 4 decimals). Both evaluate the same formulas in double; the prototype's
  // N and p are unreduced, ours are reduced first, so they agree to rounding:
  // 1e-9 degrees, 1e-12 for the amplitude terms.
  const NodalTerms n = year(2026).at_mid_year();
  CHECK(std::abs(n.I - 28.1112191910) < 1e-9);
  CHECK(std::abs(n.nu - -5.0416591525) < 1e-9);
  CHECK(std::abs(n.xi - -4.5441040625) < 1e-9);
  CHECK(std::abs(n.nu1 - -3.5948982422) < 1e-9);
  CHECK(std::abs(n.two_nu2 - -7.6008845891) < 1e-9);
  CHECK(std::abs(n.P - 86.1673593785) < 1e-9);
  CHECK(std::abs(n.Q - 82.1034497056) < 1e-9);
  CHECK(std::abs(n.R - 2.0928378340) < 1e-9);
  CHECK(std::abs(n.Qu - 4.0639096730) < 1e-9);
  CHECK(std::abs(n.Qa - 1.061296758755) < 1e-12);
  CHECK(std::abs(n.Ra - 0.727995984002) < 1e-12);
  CHECK(std::abs(tide::elements(tide::mid_year(chr::year{2026})).N -
                 332.5023939316) < 1e-9);
}

TEST_CASE("yearly (V0+u) and f for 2026 match the prototype",
          "[core][tide][astronomy]") {
  // proto_predict.py const(name, 2026-01-01, 2026-07-02T12:00Z), %.10f and
  // %.12f; HJ 13.2 pins them to 1e-6.
  struct Vector {
    std::string_view name;
    double argument;
    double f;
  };
  const std::vector<Vector> vectors{
      {.name = "M2", .argument = 66.3564078510, .f = 0.967351515366},
      {.name = "S2", .argument = 0.0, .f = 1.0},
      {.name = "N2", .argument = 59.6632037610, .f = 0.967351515366},
      {.name = "K1", .argument = 14.2609017696, .f = 1.103115578544},
      {.name = "O1", .argument = 50.6487451736, .f = 1.166823335642},
      {.name = "M4", .argument = 132.7128157020, .f = 0.935768954282},
      {.name = "MS4", .argument = 66.3564078510, .f = 0.967351515366},
      {.name = "M1", .argument = 25.2816536315, .f = 1.099431733883},
      {.name = "L2", .argument = 250.9567741118, .f = 1.328786884302},
      {.name = "K2", .argument = 208.9328916445, .f = 1.281536831854},
      {.name = "MSF", .argument = 293.6435921490, .f = 0.967351515366},
      {.name = "2MK3", .argument = 118.4519139305, .f = 1.032261311386},
  };
  const YearAstronomy y2026 = year(2026);
  for (const Vector& v : vectors) {
    INFO(v.name);
    const Equilibrium e = tide::yearly_equilibrium(arg(v.name), y2026);
    CHECK(angle_between(e.argument.degrees(), v.argument) < 1e-8);
    CHECK(std::abs(e.node_factor - v.f) < 1e-11);
  }
}

TEST_CASE("node factors at mid-2026", "[core][tide][astronomy]") {
  const NodalTerms n = year(2026).at_mid_year();
  CHECK(tide::node_factor(NodeFactor::unity, n) == 1.0);
  for (const NodeFactor f : all_node_factors) {
    INFO(tide::to_token(f));
    const double value = tide::node_factor(f, n);
    // Every SP 98 factor is normalized to its mean: within (0.5, 2) here.
    CHECK(value > 0.5);
    CHECK(value < 2.0);
  }
  // f(M1) = f(O1) / Qa and f(L2) = f(M2) / Ra.
  CHECK(tide::node_factor(NodeFactor::f206, n) ==
        tide::node_factor(NodeFactor::f75, n) / n.Qa);
  CHECK(tide::node_factor(NodeFactor::f215, n) ==
        tide::node_factor(NodeFactor::f78, n) / n.Ra);
}

TEST_CASE("compound laws hold every year", "[core][tide][astronomy]") {
  for (int y = 1700; y <= 2200; y += 25) {
    INFO(y);
    const YearAstronomy a = year(y);
    const auto eq = [&](std::string_view n) {
      return tide::yearly_equilibrium(arg(n), a);
    };
    const NodalTerms mid = a.at_mid_year();
    CHECK(eq("MS4").node_factor == eq("M2").node_factor);
    CHECK(eq("M4").node_factor == eq("M2").node_factor * eq("M2").node_factor);
    // f is multiplied, never divided: f(MSF) = f(M2), not 1 / f(M2).
    CHECK(eq("MSF").node_factor == eq("M2").node_factor);
    CHECK(arg("MSF").u_deg(mid) == -arg("M2").u_deg(mid));
    CHECK(angle_between(eq("2MK3").argument.degrees(),
                        (2.0 * eq("M2").argument.degrees()) -
                            eq("K1").argument.degrees()) < 1e-9);
    CHECK(angle_between(eq("M4").argument.degrees(),
                        2.0 * eq("M2").argument.degrees()) < 1e-9);
  }
}

TEST_CASE("at mid-year the instantaneous scheme differs only in V",
          "[core][tide][astronomy]") {
  // docs/harmonics-engine.md section 1.3. At t_M both schemes evaluate u and
  // f at the same instant, so they agree exactly. V differs by
  // sum_k a_k (e_k(t_M) - e_k(t_Y) - rate_k(1900) dt) - b_Q p-rate dt, which
  // is computed here from the SP 98 polynomials. T contributes nothing: it
  // advances exactly 15 degrees per hour. The tolerance covers the rounding
  // of the reduced elements (about 1e-10 degrees each).
  for (const int y : {1800, 1900, 2026, 2040, 2150}) {
    const YearAstronomy a = year(y);
    const Time t_m = tide::mid_year(chr::year{y});
    const double hours =
        chr::duration<double, std::ratio<3600>>(t_m - a.start()).count();
    const double c_y = tide::julian_centuries(a.start());
    const double c_m = tide::julian_centuries(t_m);
    const auto drift = [&](const detail::Sp98Polynomial& p, double rate) {
      return p.value(c_m) - p.value(c_y) - (rate * hours);
    };
    const tide::Elements& r = tide::rates_1900;
    for (const std::string_view n : {"M2", "K1", "O1", "M1", "N2", "MF"}) {
      INFO(y << " " << n);
      const Argument x = arg(n);
      const Equilibrium yearly = tide::yearly_equilibrium(x, a);
      const Equilibrium now =
          tide::diagnostic::instantaneous_equilibrium(x, t_m);
      CHECK(now.node_factor == yearly.node_factor);
      const auto v = x.v();
      const double expected =
          (v[1] * drift(detail::moon_s, r.s)) +
          (v[2] * drift(detail::sun_h, r.h)) +
          (v[3] * drift(detail::lunar_perigee_p, r.p)) +
          (v[4] * drift(detail::solar_perigee_p1, r.p1)) -
          (x.u()[static_cast<std::size_t>(tide::UTerm::Q)] * r.p * hours);
      const double yearly_at_mid =
          yearly.argument.degrees() + (x.speed_deg_per_hour() * hours);
      CHECK(angle_between(now.argument.degrees() - yearly_at_mid, expected) <
            1e-8);
    }
  }
  // The sizes the design quotes: M1 about 20.3 degrees, others below 4.3e-4.
  const YearAstronomy a = year(2026);
  const Time t_m = tide::mid_year(chr::year{2026});
  const double hours = 4380.0;
  const auto gap = [&](std::string_view n) {
    const Argument x = arg(n);
    return angle_between(
        tide::diagnostic::instantaneous_equilibrium(x, t_m).argument.degrees(),
        tide::yearly_equilibrium(x, a).argument.degrees() +
            (x.speed_deg_per_hour() * hours));
  };
  CHECK(std::abs(gap("M1") - 20.33) < 0.01);
  CHECK(gap("M2") < 4.3e-4);
  CHECK(gap("K2") < 4.3e-4);
}

TEST_CASE("YearAstronomy covers 1700 to 2200", "[core][tide][astronomy]") {
  CHECK(YearAstronomy::make(chr::year{1699}).error() ==
        YearOutOfRange{.year = chr::year{1699}});
  CHECK(YearAstronomy::make(chr::year{2201}).error() ==
        YearOutOfRange{.year = chr::year{2201}});
  CHECK(YearAstronomy::make(chr::year{1700}).has_value());
  CHECK(YearAstronomy::make(chr::year{2200}).has_value());
  const YearAstronomy a = year(2026);
  CHECK(a.year() == chr::year{2026});
  CHECK(a.start() == Time{chr::sys_days{chr::year{2026} / 1 / 1}});
  CHECK(a.at_start() == tide::elements(a.start()));
  CHECK(a.at_mid_year() ==
        tide::nodal_terms(tide::elements(tide::mid_year(chr::year{2026}))));
  CHECK(a == year(2026));
  CHECK(a != year(2027));
}

TEST_CASE("Phase::wrap at run time equals the constant-expression path",
          "[core][tide][astronomy]") {
  // Run time uses std::fmod, constant evaluation detail::exact_fmod; both are
  // exact, so they agree bit for bit over every magnitude.
  std::seed_seq seed{2026, 10, 9};  // fixed: a failure must reproduce
  std::mt19937_64 rng{seed};
  std::uniform_real_distribution<double> mantissa{-1.0, 1.0};
  std::uniform_int_distribution<int> exponent{-10, 1000};
  for (int i = 0; i < 20'000; ++i) {
    const double x = std::ldexp(mantissa(rng), exponent(rng));
    const double expected = detail::exact_fmod(x, 360.0);
    const double actual = std::fmod(x, 360.0);
    REQUIRE(std::bit_cast<std::uint64_t>(expected) ==
            std::bit_cast<std::uint64_t>(actual));
    const double wrapped = Phase::wrap(x).degrees();
    REQUIRE(wrapped >= 0.0);
    REQUIRE(wrapped < 360.0);
  }
  CHECK(std::bit_cast<std::uint64_t>(detail::exact_fmod(-0.0, 360.0)) ==
        std::bit_cast<std::uint64_t>(std::fmod(-0.0, 360.0)));
  CHECK(std::bit_cast<std::uint64_t>(detail::exact_fmod(-720.0, 360.0)) ==
        std::bit_cast<std::uint64_t>(std::fmod(-720.0, 360.0)));
  CHECK(Phase::wrap(-1e-14).degrees() == 0.0);
  CHECK(Phase::wrap(1e300).degrees() == detail::exact_fmod(1e300, 360.0));
  CHECK(not Phase::of(std::numeric_limits<double>::quiet_NaN()).has_value());
}

TEST_CASE("CompoundTerms::make sorts and refuses bad terms",
          "[core][tide][constituent]") {
  using Term = CompoundTerms::Term;
  const auto made =
      CompoundTerms::make({Term{.base = name("S2"), .coefficient = 1},
                           Term{.base = name("M2"), .coefficient = -1}});
  REQUIRE(made.has_value());
  const auto terms = made->terms();
  REQUIRE(terms.size() == 2);
  CHECK(terms[0].base.view() == "M2");
  CHECK(terms[0].coefficient == -1);
  CHECK(terms[1].base.view() == "S2");
  CHECK(*made ==
        *CompoundTerms::make({Term{.base = name("M2"), .coefficient = -1},
                              Term{.base = name("S2"), .coefficient = 1}}));

  CHECK(CompoundTerms::make({}).error() == CompoundErrc::empty);
  std::vector<Term> nine;
  for (const char* n : {"A1", "B1", "C1", "D1", "E1", "F1", "G1", "H1", "I1"}) {
    nine.push_back(Term{.base = name(n), .coefficient = 1});
  }
  CHECK(CompoundTerms::make(nine).error() == CompoundErrc::too_many);
  nine.pop_back();
  CHECK(CompoundTerms::make(nine).has_value());
  CHECK(CompoundTerms::make({Term{.base = name("M2"), .coefficient = 0}})
            .error() == CompoundErrc::zero_coefficient);
  CHECK(CompoundTerms::make({Term{.base = name("M2"), .coefficient = 1},
                             Term{.base = name("M2"), .coefficient = 2}})
            .error() == CompoundErrc::duplicate_base);
  // Exact names: m2 and M2 are different bases here; the dictionary refuses
  // the pair (names unique ignoring case).
  CHECK(CompoundTerms::make({Term{.base = name("M2"), .coefficient = 1},
                             Term{.base = name("m2"), .coefficient = 1}})
            .has_value());
}

TEST_CASE("the catalogue matches libcongen every year",
          "[core][tide][astronomy][golden]") {
  // tests/fixtures/core/tide/congen_golden.csv: speeds, (V0+u) and f from
  // libcongen 1.7 for all 120 entries in 19 years from 1700 to 2200 (its
  // header has the provenance). Tolerances: speeds 1e-9 deg/h (both are the
  // 1900 rates, summed in a different order); (V0+u) 1e-6 degrees, because
  // libcongen keeps T unreduced (4e7 degrees by 2200, so 1e-7 of its own
  // rounding per term); f 1e-9 relative.
  std::ifstream in{mov::test::fixture("core/tide/congen_golden.csv")};
  REQUIRE(in);
  std::string line;
  std::size_t rows = 0;
  double worst_speed = 0.0;
  double worst_argument = 0.0;
  double worst_f = 0.0;
  while (std::getline(in, line)) {
    if (line.empty() or line.front() == '#' or line.starts_with("name,")) {
      continue;
    }
    std::vector<std::string> cells;
    std::size_t start = 0;
    for (std::size_t comma = line.find(','); comma != std::string::npos;
         comma = line.find(',', start)) {
      cells.push_back(line.substr(start, comma - start));
      start = comma + 1;
    }
    cells.push_back(line.substr(start));
    REQUIRE(cells.size() == 5);
    INFO(line);
    const auto a = catalogue_argument(cells[0]);
    REQUIRE(a.has_value());
    const Equilibrium e = tide::yearly_equilibrium(a.value_or(Argument{}),
                                                   year(std::stoi(cells[1])));
    worst_speed = std::max(
        worst_speed, std::abs(a.value_or(Argument{}).speed_deg_per_hour() -
                              std::stod(cells[2])));
    worst_argument =
        std::max(worst_argument,
                 angle_between(e.argument.degrees(), std::stod(cells[3])));
    worst_f =
        std::max(worst_f, std::abs(e.node_factor / std::stod(cells[4]) - 1.0));
    ++rows;
  }
  INFO("max |speed| " << worst_speed << ", max |V0+u| " << worst_argument
                      << ", max |f| rel " << worst_f);
  CHECK(rows == 120 * 19);
  CHECK(worst_speed < 1e-9);
  CHECK(worst_argument < 1e-6);
  CHECK(worst_f < 1e-9);
}
