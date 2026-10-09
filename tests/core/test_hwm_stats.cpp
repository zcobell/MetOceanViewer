// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "hwm_fixture.hpp"
#include "hwm_helpers.hpp"
#include "mov/core/hwm.hpp"
#include "mov/core/hwm_stats.hpp"
#include "mov/core/units.hpp"
#include "test_helpers.hpp"

using mov::core::classify;
using mov::core::DegenerateObserved;
using mov::core::ErrorClasses;
using mov::core::Free;
using mov::core::HighWaterMark;
using mov::core::HwmStats;
using mov::core::HwmStatsError;
using mov::core::Intercept;
using mov::core::Length;
using mov::core::LengthUnit;
using mov::core::Moments;
using mov::core::NonFiniteMoments;
using mov::core::NoWetMarks;
using mov::core::ThroughOrigin;
using mov::core::TooFewForFreeFit;
using mov::core::wet_moments;
using mov::test::Golden;
using mov::test::in_metres;
using mov::test::load_hwm_csv;
using mov::test::mark_ft;
using mov::test::mark_m;
using mov::test::metres;
using mov::test::near;
using mov::test::near_abs;
using mov::test::raw_mark;

namespace {

// ---- deterministic data -----------------------------------------------------

// splitmix64: the same sequence on every platform, unlike std::*_distribution.
class Random {
 public:
  explicit Random(std::uint64_t seed) : state_{seed} {}

  double unit() {  // uniform in [0, 1)
    state_ += 0x9E37'79B9'7F4A'7C15;
    std::uint64_t z = state_;
    z = (z ^ (z >> 30)) * 0xBF58'476D'1CE4'E5B9;
    z = (z ^ (z >> 27)) * 0x94D0'49BB'1331'11EB;
    z ^= z >> 31;
    return static_cast<double>(z >> 11) * 0x1.0p-53;
  }
  double between(double lo, double hi) { return lo + ((hi - lo) * unit()); }
  std::size_t below(std::size_t n) {
    return static_cast<std::size_t>(unit() * static_cast<double>(n));
  }

 private:
  std::uint64_t state_;
};

struct Cloud {
  double offset;  // observed values sit around this
  double spread;  // and vary by this much
  double dry_fraction;
};

// y = 0.9 x + 0.2 + noise; a dry_fraction of the marks are dry.
std::vector<HighWaterMark> make_cloud(std::size_t n, const Cloud& cloud,
                                      std::uint64_t seed) {
  Random random{seed};
  std::vector<HighWaterMark> marks;
  marks.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    const double x = cloud.offset + random.between(0.0, cloud.spread);
    const double y = (0.9 * x) + 0.2 + random.between(-0.3, 0.3);
    const bool dry = random.unit() < cloud.dry_fraction;
    marks.push_back(mark_m(x, dry ? -99999.0 : y));
  }
  return marks;
}

// Relative 1e-12, or absolute 1e-12 for a value that is exactly 0 in the
// golden.
bool agrees(double a, double b) {
  return near(a, b, 1e-12) or near_abs(a, b, 1e-12);
}

long double wide(double v) { return static_cast<long double>(v); }

// ---- comparisons ------------------------------------------------------------

// Relative 1e-12 per field, with each field's own scale: a mean against the
// larger of itself and the spread (it may be near zero), the M2 sums against
// themselves, the cross sum against the geometric mean of the other two.
bool moments_near(const Moments& a, const Moments& b, double rel = 1e-12) {
  const auto n = static_cast<double>(std::max<std::size_t>(a.n(), 1));
  const auto mean_near = [&](double u, double v, double m2) {
    const double scale =
        std::max({std::abs(u), std::abs(v), std::sqrt(m2 / n)});
    return std::abs(u - v) <= rel * scale;
  };
  const double cross_scale = std::sqrt(a.m2x() * a.m2y());
  return a.n() == b.n() and a.seen() == b.seen() and
         mean_near(a.mean_x(), b.mean_x(), a.m2x()) and
         mean_near(a.mean_y(), b.mean_y(), a.m2y()) and
         mean_near(a.mean_e(), b.mean_e(), a.m2e()) and
         near(a.m2x(), b.m2x(), rel) and near(a.m2y(), b.m2y(), rel) and
         near(a.m2e(), b.m2e(), rel) and
         std::abs(a.cxy() - b.cxy()) <= rel * cross_scale;
}

HwmStats stats_of(std::span<const HighWaterMark> marks, Intercept mode) {
  const auto result = hwm_stats(marks, mode);
  REQUIRE(result.has_value());
  return *result;
}

// value_or, not *: a missing value gives -1, which no check here accepts (and
// the tests that need presence check it separately).
double r2_of(const HwmStats& s) { return s.r_squared.value_or(-1.0); }
double sigma_of(const HwmStats& s) {
  return in_metres(s.error_stddev.value_or(metres(-1.0)));
}

double slope_of(const HwmStats& s) {
  return std::visit([](const auto& fit) { return fit.slope; }, s.fit);
}

// ---- the legacy closed forms (calculateStats, highwatermarks.cpp:105-176),
// over raw sums, for the free fit. Correct only for well-scaled data, which is
// what the fixtures are.
struct LegacyFree {
  double slope;
  double intercept;
  double r_squared;
};

LegacyFree legacy_free(std::span<const HighWaterMark> marks) {
  double n = 0;
  double sx = 0;
  double sy = 0;
  double sxy = 0;
  double sxx = 0;
  double syy = 0;
  for (const HighWaterMark& h : marks) {
    const auto* wet = std::get_if<mov::core::Wet>(&h.modeled);
    if (wet == nullptr) {
      continue;
    }
    const double x = in_metres(h.observed);
    const double y = in_metres(wet->elevation);
    n += 1;
    sx += x;
    sy += y;
    sxy += x * y;
    sxx += x * x;
    syy += y * y;
  }
  const double denominator = (n * sxx) - (sx * sx);
  const double r = ((n * sxy) - (sx * sy)) /
                   std::sqrt(denominator * ((n * syy) - (sy * sy)));
  return {.slope = ((n * sxy) - (sx * sy)) / denominator,
          .intercept = ((sy * sxx) - (sx * sxy)) / denominator,
          .r_squared = r * r};
}

}  // namespace

// ---- Moments: monoid laws ---------------------------------------------------

TEST_CASE("Moments identity is exact on real data",
          "[core][hwm_stats][moments]") {
  const auto marks =
      make_cloud(300, {.offset = 3.0, .spread = 2.0, .dry_fraction = 0.2}, 11);
  for (std::size_t i = 0; i <= marks.size(); i += 37) {
    const Moments m = wet_moments(std::span{marks}.first(i));
    CHECK(Moments{} + m == m);
    CHECK(m + Moments{} == m);
  }
}

TEST_CASE("Moments join is associative within 1e-12",
          "[core][hwm_stats][moments]") {
  Random random{2026};
  const auto marks =
      make_cloud(400, {.offset = 5.0, .spread = 4.0, .dry_fraction = 0.1}, 5);
  const std::span<const HighWaterMark> all{marks};
  for (int trial = 0; trial < 200; ++trial) {
    const std::size_t i = random.below(marks.size());
    const std::size_t j = i + random.below(marks.size() - i);
    const Moments a = wet_moments(all.first(i));
    const Moments b = wet_moments(all.subspan(i, j - i));
    const Moments c = wet_moments(all.subspan(j));
    CHECK(moments_near((a + b) + c, a + (b + c)));
  }
}

TEST_CASE("folding the halves matches folding the whole",
          "[core][hwm_stats][moments]") {
  for (const std::uint64_t seed : {1U, 2U, 3U}) {
    const auto marks = make_cloud(
        1000, {.offset = 2.0, .spread = 3.0, .dry_fraction = 0.3}, seed);
    const std::span<const HighWaterMark> all{marks};
    const Moments whole = wet_moments(all);
    const std::size_t half = marks.size() / 2;
    const Moments halves =
        wet_moments(all.first(half)) + wet_moments(all.subspan(half));
    CHECK(moments_near(whole, halves));
    // And a three-way split, joined in either grouping.
    const Moments thirds = wet_moments(all.first(333)) +
                           wet_moments(all.subspan(333, 400)) +
                           wet_moments(all.subspan(733));
    CHECK(moments_near(whole, thirds));
  }
}

TEST_CASE("the fold is deterministic for a given order",
          "[core][hwm_stats][moments]") {
  const auto marks =
      make_cloud(500, {.offset = 1.0, .spread = 1.0, .dry_fraction = 0.1}, 9);
  CHECK(wet_moments(marks) == wet_moments(marks));
  const auto first = hwm_stats(marks, Intercept::free);
  const auto second = hwm_stats(marks, Intercept::free);
  CHECK(first == second);  // bit for bit
}

// ---- Moments: the numerics --------------------------------------------------

TEST_CASE("Moments survives data far from zero", "[core][hwm_stats][moments]") {
  // x = 1e8 + i for i in 0..9 and y = 2x - 1.9e8: the textbook
  // sum(x^2) - n mean^2 subtracts two numbers near 1e17, where one ulp is 16.
  // The M2 sums are exact here: m2x = n (n^2 - 1) / 12 = 82.5.
  std::vector<HighWaterMark> marks;
  for (int i = 0; i < 10; ++i) {
    const double x = 1e8 + i;
    marks.push_back(raw_mark(x, (2.0 * x) - 1.9e8));
  }
  const Moments m = wet_moments(marks);
  CHECK(m.n() == 10);
  CHECK(near(m.mean_x(), 1e8 + 4.5, 1e-14));
  CHECK(near(m.m2x(), 82.5, 1e-12));
  CHECK(near(m.m2y(), 4.0 * 82.5, 1e-12));
  CHECK(near(m.cxy(), 2.0 * 82.5, 1e-12));
  const HwmStats s = stats_of(marks, Intercept::free);
  CHECK(near(slope_of(s), 2.0, 1e-12));
  CHECK(near(in_metres(std::get<Free>(s.fit).intercept), -1.9e8, 1e-12));
  REQUIRE(s.r_squared.has_value());
  CHECK(near(r2_of(s), 1.0, 1e-12));
}

TEST_CASE("Moments agrees with a two-pass reference on many marks",
          "[core][hwm_stats][moments]") {
  const auto marks = make_cloud(
      100000, {.offset = 1.0e4, .spread = 10.0, .dry_fraction = 0.25}, 77);
  // Two-pass in long double: exact means first, then centred sums.
  long double n = 0;
  long double sx = 0;
  long double sy = 0;
  for (const HighWaterMark& h : marks) {
    if (const auto* wet = std::get_if<mov::core::Wet>(&h.modeled)) {
      n += 1;
      sx += wide(in_metres(h.observed));
      sy += wide(in_metres(wet->elevation));
    }
  }
  const long double mx = sx / n;
  const long double my = sy / n;
  long double sxx = 0;
  long double sxy = 0;
  long double syy = 0;
  for (const HighWaterMark& h : marks) {
    if (const auto* wet = std::get_if<mov::core::Wet>(&h.modeled)) {
      const long double dx = wide(in_metres(h.observed)) - mx;
      const long double dy = wide(in_metres(wet->elevation)) - my;
      sxx += dx * dx;
      sxy += dx * dy;
      syy += dy * dy;
    }
  }
  const Moments m = wet_moments(marks);
  CHECK(static_cast<long double>(m.n()) == n);
  CHECK(near(m.m2x(), static_cast<double>(sxx), 1e-11));
  CHECK(near(m.m2y(), static_cast<double>(syy), 1e-11));
  CHECK(near(m.cxy(), static_cast<double>(sxy), 1e-11));
  const HwmStats s = stats_of(marks, Intercept::free);
  CHECK(near(slope_of(s), static_cast<double>(sxy / sxx), 1e-11));
  REQUIRE(s.r_squared.has_value());
  CHECK(near(r2_of(s), static_cast<double>((sxy * sxy) / (sxx * syy)), 1e-11));
}

// ---- golden numbers from golden.py for the HWM fixtures ---------------------

namespace {

void check_against_golden(const std::string& fixture, LengthUnit unit) {
  const Golden golden{fixture};
  const auto marks = load_hwm_csv(fixture, unit);
  CHECK(marks.size() == golden.count("total"));

  const auto expected_codes = golden.words("categories");
  REQUIRE(expected_codes.size() == marks.size());
  const bool feet = unit == LengthUnit::foot;
  const ErrorClasses classes =
      feet ? ErrorClasses::feet_default() : ErrorClasses::meters_default();
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
      REQUIRE_FALSE(result.has_value());
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
      CHECK(agrees(r2_of(*result), *r2));
    }
    CHECK(agrees(in_metres(result->mean_error), golden.number("mean_error")));
    const auto sigma = golden.maybe_number("error_stddev");
    REQUIRE(result->error_stddev.has_value() == sigma.has_value());
    if (sigma) {
      CHECK(agrees(sigma_of(*result), *sigma));
    }
    if (mode == Intercept::free) {
      CHECK(agrees(in_metres(std::get<Free>(result->fit).intercept),
                   golden.number("free.intercept")));
    }
  }
}

}  // namespace

TEST_CASE("golden: hwm_basic.csv (metres, two dry, one error on a break)",
          "[core][hwm_stats][golden]") {
  check_against_golden("hwm_basic.csv", LengthUnit::meter);
}

TEST_CASE("golden: hwm_ft.csv (feet, one dry)", "[core][hwm_stats][golden]") {
  check_against_golden("hwm_ft.csv", LengthUnit::foot);
}

TEST_CASE("golden: hwm_allDry.csv (no statistics)",
          "[core][hwm_stats][golden]") {
  check_against_golden("hwm_allDry.csv", LengthUnit::meter);
}

TEST_CASE("golden: hwm_one_wet.csv (origin only)",
          "[core][hwm_stats][golden]") {
  check_against_golden("hwm_one_wet.csv", LengthUnit::meter);
}

TEST_CASE("golden: hwm_ties.csv (a decimal tie on every metre break)",
          "[core][hwm_stats][golden]") {
  check_against_golden("hwm_ties.csv", LengthUnit::meter);
}

TEST_CASE(
    "golden: hwm_ties_ft.csv (ties on every foot break, 1e-5 ft either "
    "side of one)",
    "[core][hwm_stats][golden]") {
  check_against_golden("hwm_ties_ft.csv", LengthUnit::foot);
}

TEST_CASE("hwm_basic hand check", "[core][hwm_stats][golden]") {
  // Six wet marks, x = 1..6; read off the CSV, no library involved.
  const auto marks = load_hwm_csv("hwm_basic.csv", LengthUnit::meter);
  const HwmStats s = stats_of(marks, Intercept::free);
  CHECK(s.total == 8);
  CHECK(s.wet == 6);
  CHECK(near(in_metres(s.mean_error), 2.5 / 6.0, 1e-14));  // e sum = 2.5
  CHECK(near(slope_of(s), 17.0 / 14.0, 1e-14));            // cxy 21.25/m2x 17.5
}

// ---- the fit alternative matches the mode -----------------------------------

TEST_CASE("the requested intercept mode is the fit that comes back",
          "[core][hwm_stats][regression][N1]") {
  const auto marks = load_hwm_csv("hwm_basic.csv", LengthUnit::meter);
  const HwmStats free_fit = stats_of(marks, Intercept::free);
  const HwmStats origin = stats_of(marks, Intercept::through_origin);
  CHECK(std::holds_alternative<Free>(free_fit.fit));
  CHECK(std::holds_alternative<ThroughOrigin>(origin.fit));
  // Same data, different lines (v4 always returned the origin one).
  CHECK(slope_of(free_fit) != slope_of(origin));
  CHECK(std::abs(slope_of(free_fit) - slope_of(origin)) > 0.05);
  CHECK(std::abs(r2_of(free_fit) - r2_of(origin)) > 0.05);
  // And the same through the moments.
  const Moments m = wet_moments(marks);
  CHECK(hwm_stats(m, Intercept::free) == free_fit);
  CHECK(hwm_stats(m, Intercept::through_origin) == origin);
}

TEST_CASE("the intercept of a free fit is a Length, in metres",
          "[core][hwm_stats][regression][N1]") {
  // y = 2x + 0.5 ft exactly: the intercept is 0.5 ft, which is 0.1524 m.
  std::vector<HighWaterMark> marks;
  for (const double x : {1.0, 2.0, 4.0, 8.0}) {
    marks.push_back(mark_ft(x, (2.0 * x) + 0.5));
  }
  const HwmStats s = stats_of(marks, Intercept::free);
  CHECK(near(slope_of(s), 2.0, 1e-12));
  const Length intercept = std::get<Free>(s.fit).intercept;
  CHECK(near(intercept.as(LengthUnit::foot), 0.5, 1e-12));
  CHECK(near(intercept.as(LengthUnit::meter), 0.1524, 1e-12));
}

// ---- R^2
// ---------------------------------------------------------------------

TEST_CASE("through-origin R^2 is uncentred (D25)",
          "[core][hwm_stats][regression]") {
  // y is nearly constant at 10 while x = 1, 2, 3: through the origin the line
  // is poor, but R^2 = 1 - SSres / sum(y^2) is still about 0.85. v4's
  // 1 - SSres / sum((y - ybar)^2) is hugely negative here.
  const std::array marks{mark_m(1.0, 10.1), mark_m(2.0, 9.9),
                         mark_m(3.0, 10.0)};
  const HwmStats s = stats_of(marks, Intercept::through_origin);
  const double sxy = (1.0 * 10.1) + (2.0 * 9.9) + (3.0 * 10.0);
  const double sxx = 1.0 + 4.0 + 9.0;
  const double slope = sxy / sxx;
  const double syy = (10.1 * 10.1) + (9.9 * 9.9) + (10.0 * 10.0);
  const double ss_res = ((10.1 - slope) * (10.1 - slope)) +
                        ((9.9 - (2.0 * slope)) * (9.9 - (2.0 * slope))) +
                        ((10.0 - (3.0 * slope)) * (10.0 - (3.0 * slope)));
  CHECK(near(slope_of(s), slope, 1e-12));
  REQUIRE(s.r_squared.has_value());
  CHECK(near(r2_of(s), 1.0 - (ss_res / syy), 1e-12));
  const double ybar = (10.1 + 9.9 + 10.0) / 3.0;
  const double ss_tot_centred = ((10.1 - ybar) * (10.1 - ybar)) +
                                ((9.9 - ybar) * (9.9 - ybar)) +
                                ((10.0 - ybar) * (10.0 - ybar));
  const double v4_r_squared = 1.0 - (ss_res / ss_tot_centred);
  CHECK(v4_r_squared < -100.0);
  CHECK(r2_of(s) > 0.0);
  CHECK(r2_of(s) <= 1.0);
}

TEST_CASE("R^2 of an exact line is 1, in both modes",
          "[core][hwm_stats][regression]") {
  std::vector<HighWaterMark> marks;
  for (const double x : {1.0, 2.0, 3.0, 4.0, 5.0}) {
    marks.push_back(mark_m(x, 1.25 * x));
  }
  const HwmStats origin = stats_of(marks, Intercept::through_origin);
  const HwmStats free_fit = stats_of(marks, Intercept::free);
  CHECK(near(slope_of(origin), 1.25, 1e-12));
  CHECK(near(slope_of(free_fit), 1.25, 1e-12));
  CHECK(near(r2_of(origin), 1.0, 1e-12));
  CHECK(near(r2_of(free_fit), 1.0, 1e-12));
  CHECK(r2_of(origin) <= 1.0);
  CHECK(r2_of(free_fit) <= 1.0);
}

TEST_CASE("R^2 stays within [0, 1] on noisy data",
          "[core][hwm_stats][regression]") {
  for (std::uint64_t seed = 1; seed <= 20; ++seed) {
    const auto marks = make_cloud(
        50, {.offset = 0.0, .spread = 5.0, .dry_fraction = 0.1}, seed);
    for (const Intercept mode : {Intercept::free, Intercept::through_origin}) {
      const HwmStats s = stats_of(marks, mode);
      REQUIRE(s.r_squared.has_value());
      CHECK(r2_of(s) >= 0.0);
      CHECK(r2_of(s) <= 1.0);
    }
  }
}

TEST_CASE("R^2 is absent when its denominator is zero",
          "[core][hwm_stats][regression]") {
  // Modeled all 2: the free fit is flat (slope 0) and r is 0/0.
  const std::array flat{mark_m(1.0, 2.0), mark_m(2.0, 2.0), mark_m(3.0, 2.0)};
  const HwmStats free_fit = stats_of(flat, Intercept::free);
  CHECK(slope_of(free_fit) == 0.0);
  CHECK_FALSE(free_fit.r_squared.has_value());
  // Modeled all 0: sum(y^2) is 0 and so is the slope through the origin.
  const std::array zero{mark_m(1.0, 0.0), mark_m(2.0, 0.0)};
  const HwmStats origin = stats_of(zero, Intercept::through_origin);
  CHECK(slope_of(origin) == 0.0);
  CHECK_FALSE(origin.r_squared.has_value());
}

// ---- sigma uses n - 1 -------------------------------------------------------

TEST_CASE("the error standard deviation divides by n - 1",
          "[core][hwm_stats][regression][D17]") {
  // Errors -1 and +1: mean 0, sum of squares 2. n - 1 gives sqrt(2); v4's n
  // gave 1.
  const std::array marks{mark_m(1.0, 0.0), mark_m(2.0, 3.0)};
  const HwmStats s = stats_of(marks, Intercept::free);
  CHECK(near(in_metres(s.mean_error), 0.0, 1e-12));
  REQUIRE(s.error_stddev.has_value());
  CHECK(near(sigma_of(s), std::sqrt(2.0), 1e-14));
}

TEST_CASE("the error standard deviation is the same in both modes",
          "[core][hwm_stats][regression][D17]") {
  const auto marks = load_hwm_csv("hwm_basic.csv", LengthUnit::meter);
  const HwmStats free_fit = stats_of(marks, Intercept::free);
  const HwmStats origin = stats_of(marks, Intercept::through_origin);
  CHECK(free_fit.error_stddev == origin.error_stddev);
  CHECK(free_fit.mean_error == origin.mean_error);
}

TEST_CASE("one wet mark has no standard deviation",
          "[core][hwm_stats][regression][D17]") {
  const std::array marks{mark_m(2.0, 3.5), mark_m(2.0, -99999.0)};
  const HwmStats s = stats_of(marks, Intercept::through_origin);
  CHECK(s.wet == 1);
  CHECK(s.total == 2);
  CHECK(near(in_metres(s.mean_error), 1.5, 1e-14));
  CHECK_FALSE(s.error_stddev.has_value());
  CHECK_FALSE(s.r_squared.has_value());  // one point fits exactly
  CHECK(near(slope_of(s), 1.75, 1e-14));
}

// ---- one dry rule -----------------------------------------------------------

TEST_CASE("one dry threshold decides what is in the statistics",
          "[core][hwm_stats][regression][N2]") {
  // v4: stats used > -999, SStot > -9999, map and axes > -900. Here every
  // row with a raw modeled value <= -999 is out of everything; the rest, even
  // -900 and -998.5, are wet.
  constexpr std::array<double, 13> raw{2.0,     -99999.0, 3.0, -9999.0, 4.5,
                                       -5000.0, -999.0,   5.5, -998.5,  6.0,
                                       -900.0,  7.0,      0.5};
  std::vector<HighWaterMark> marks;
  std::vector<HighWaterMark> wet_only;
  double x = 1.0;
  for (const double r : raw) {
    marks.push_back(mark_m(x, r));
    if (r > -999.0) {
      wet_only.push_back(mark_m(x, r));
    }
    x += 1.0;
  }
  REQUIRE(wet_only.size() == 9);
  const HwmStats s = stats_of(marks, Intercept::through_origin);
  CHECK(s.total == 13);
  CHECK(s.wet == 9);
  // The statistics equal those of the wet marks alone, bit for bit, except
  // that the moments saw the dry ones go by.
  const HwmStats alone = stats_of(wet_only, Intercept::through_origin);
  CHECK(s.fit == alone.fit);
  CHECK(s.r_squared == alone.r_squared);
  CHECK(s.mean_error == alone.mean_error);
  CHECK(s.error_stddev == alone.error_stddev);
  // classify uses the same rule.
  const auto classes = ErrorClasses::meters_default();
  CHECK(classify(marks[3], classes) == mov::core::HwmCategory::dry);   // -9999
  CHECK(classify(marks[6], classes) == mov::core::HwmCategory::dry);   // -999
  CHECK(classify(marks[8], classes) == mov::core::HwmCategory::bin0);  // -998.5
  CHECK(classify(marks[10], classes) == mov::core::HwmCategory::bin0);  // -900
}

// ---- errors
// ------------------------------------------------------------------

TEST_CASE("no wet marks: the error keeps the total",
          "[core][hwm_stats][errors]") {
  const std::array dry{mark_m(1.0, -99999.0), mark_m(2.0, -999.0),
                       mark_m(3.0, -9999.0)};
  for (const Intercept mode : {Intercept::free, Intercept::through_origin}) {
    const auto result = hwm_stats(dry, mode);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == HwmStatsError{NoWetMarks{.total = 3}});
    const auto none = hwm_stats(std::span<const HighWaterMark>{}, mode);
    REQUIRE_FALSE(none.has_value());
    CHECK(none.error() == HwmStatsError{NoWetMarks{.total = 0}});
  }
}

TEST_CASE("a free fit needs two wet marks", "[core][hwm_stats][errors]") {
  const std::array marks{mark_m(1.0, -99999.0), mark_m(2.0, 2.5),
                         mark_m(3.0, -99999.0)};
  const auto free_fit = hwm_stats(marks, Intercept::free);
  REQUIRE_FALSE(free_fit.has_value());
  CHECK(free_fit.error() == HwmStatsError{TooFewForFreeFit{.wet = 1}});
  // Through the origin one mark is enough.
  CHECK(hwm_stats(marks, Intercept::through_origin).has_value());
}

TEST_CASE("an observed value that never varies has no free fit",
          "[core][hwm_stats][errors]") {
  const std::array marks{mark_m(2.0, 1.0), mark_m(2.0, 3.0), mark_m(2.0, 5.0)};
  const auto free_fit = hwm_stats(marks, Intercept::free);
  REQUIRE_FALSE(free_fit.has_value());
  CHECK(free_fit.error() == HwmStatsError{DegenerateObserved{}});
  // Through the origin it is fine: slope = sum(xy)/sum(x^2) = 18/12.
  const auto origin = hwm_stats(marks, Intercept::through_origin);
  REQUIRE(origin.has_value());
  CHECK(near(std::get<ThroughOrigin>(origin->fit).slope, 1.5, 1e-14));
}

TEST_CASE("an observed value of zero everywhere has no origin fit",
          "[core][hwm_stats][errors]") {
  const std::array marks{mark_m(0.0, 1.0), mark_m(0.0, 2.0)};
  const auto origin = hwm_stats(marks, Intercept::through_origin);
  REQUIRE_FALSE(origin.has_value());
  CHECK(origin.error() == HwmStatsError{DegenerateObserved{}});
}

TEST_CASE("errors are checked in a fixed order", "[core][hwm_stats][errors]") {
  // No wet marks wins over everything.
  const auto all_dry =
      hwm_stats(std::array{mark_m(0.0, -99999.0)}, Intercept::free);
  CHECK(all_dry.error() == HwmStatsError{NoWetMarks{.total = 1}});
  // One wet mark with x == 0: too few (free) is reported before degenerate.
  const auto one = hwm_stats(std::array{mark_m(0.0, 1.0)}, Intercept::free);
  CHECK(one.error() == HwmStatsError{TooFewForFreeFit{.wet = 1}});
  // The same mark through the origin is degenerate.
  const auto origin =
      hwm_stats(std::array{mark_m(0.0, 1.0)}, Intercept::through_origin);
  CHECK(origin.error() == HwmStatsError{DegenerateObserved{}});
}

// ---- agreement with v4's free-fit closed forms
// -------------------------------

TEST_CASE("the free fit agrees with the legacy sums on the fixtures",
          "[core][hwm_stats][legacy]") {
  for (const auto& [name, unit] :
       {std::pair{"hwm_basic.csv", LengthUnit::meter},
        std::pair{"hwm_ft.csv", LengthUnit::foot}}) {
    const auto marks = load_hwm_csv(name, unit);
    const LegacyFree legacy = legacy_free(marks);
    const HwmStats s = stats_of(marks, Intercept::free);
    CHECK(near(slope_of(s), legacy.slope, 1e-12));
    CHECK(near(in_metres(std::get<Free>(s.fit).intercept), legacy.intercept,
               1e-9));
    REQUIRE(s.r_squared.has_value());
    CHECK(near(r2_of(s), legacy.r_squared, 1e-12));
  }
}

TEST_CASE("the free fit agrees with the legacy sums on random data",
          "[core][hwm_stats][legacy]") {
  for (std::uint64_t seed = 1; seed <= 10; ++seed) {
    const auto marks = make_cloud(
        200, {.offset = 1.0, .spread = 6.0, .dry_fraction = 0.2}, seed);
    const LegacyFree legacy = legacy_free(marks);
    const HwmStats s = stats_of(marks, Intercept::free);
    CHECK(near(slope_of(s), legacy.slope, 1e-9));
    CHECK(near(in_metres(std::get<Free>(s.fit).intercept), legacy.intercept,
               1e-9));
    CHECK(near(r2_of(s), legacy.r_squared, 1e-9));
  }
}

// ---- unit independence
// -------------------------------------------------------

TEST_CASE("slope and R^2 do not depend on the file's unit",
          "[core][hwm_stats]") {
  // The same survey in feet and in metres (x metres = x/0.3048 feet).
  std::vector<HighWaterMark> in_feet;
  std::vector<HighWaterMark> in_metres_marks;
  const auto m_to_ft = [](double m) { return m / 0.3048; };
  for (const double x : {1.0, 2.0, 3.5, 5.0, 6.5}) {
    const double y = (1.1 * x) - 0.3;
    in_feet.push_back(mark_ft(m_to_ft(x), m_to_ft(y)));
    in_metres_marks.push_back(mark_m(x, y));
  }
  for (const Intercept mode : {Intercept::free, Intercept::through_origin}) {
    const HwmStats a = stats_of(in_feet, mode);
    const HwmStats b = stats_of(in_metres_marks, mode);
    CHECK(near(slope_of(a), slope_of(b), 1e-12));
    CHECK(near(r2_of(a), r2_of(b), 1e-12));
    CHECK(near(in_metres(a.mean_error), in_metres(b.mean_error), 1e-9));
    CHECK(near(sigma_of(a), sigma_of(b), 1e-9));
  }
}

// ---- degrees of freedom
// ------------------------------------------------------

TEST_CASE("R^2 needs a degree of freedom", "[core][hwm_stats][regression]") {
  const std::array three{mark_m(1.0, 1.5), mark_m(2.0, 1.75),
                         mark_m(3.0, 3.75)};
  // Free: a line through two points is exact, so R^2 = 1 says nothing.
  CHECK_FALSE(stats_of(std::span{three}.first(2), Intercept::free)
                  .r_squared.has_value());
  CHECK(stats_of(three, Intercept::free).r_squared.has_value());
  // Through the origin: one point is exact; two are not.
  CHECK_FALSE(stats_of(std::span{three}.first(1), Intercept::through_origin)
                  .r_squared.has_value());
  CHECK(stats_of(std::span{three}.first(2), Intercept::through_origin)
            .r_squared.has_value());
  // The other statistics are still there with two points.
  const HwmStats two = stats_of(std::span{three}.first(2), Intercept::free);
  CHECK(near(slope_of(two), 0.25, 1e-14));
  CHECK(two.error_stddev.has_value());
}

// ---- the count of marks seen travels with the moments
// -------------------------

TEST_CASE("total is the number of marks folded, wet or dry",
          "[core][hwm_stats][moments]") {
  const std::array marks{mark_m(1.0, 2.0), mark_m(2.0, -99999.0),
                         mark_m(3.0, 4.5), mark_m(4.0, -999.0),
                         mark_m(5.0, 6.0)};
  const Moments m = wet_moments(marks);
  CHECK(m.seen() == 5);
  CHECK(m.n() == 3);
  const HwmStats s = stats_of(marks, Intercept::free);
  CHECK(s.total == 5);
  CHECK(s.wet == 3);
  // The Moments overload reads the same total from the moments.
  CHECK(hwm_stats(m, Intercept::free) == hwm_stats(marks, Intercept::free));
  // Dry-only moments keep their count in the error.
  const Moments dry =
      Moments::of(mark_m(1.0, -99999.0)) + Moments::of(mark_m(2.0, -999.0));
  const auto none = hwm_stats(dry, Intercept::through_origin);
  REQUIRE_FALSE(none.has_value());
  CHECK(none.error() == HwmStatsError{NoWetMarks{.total = 2}});
}

// ---- the fold is the explicit left fold, bit for bit
// -------------------------

TEST_CASE("wet_moments equals the explicit left fold bit for bit",
          "[core][hwm_stats][moments]") {
  // Decimal data (hundredths), like a survey file: the values are not special
  // in binary, so an out-of-order or regrouped sum would show.
  std::vector<HighWaterMark> marks;
  Random random{404};
  for (int i = 0; i < 1000; ++i) {
    const double x = static_cast<double>(100 + random.below(900)) / 100.0;
    const double y = static_cast<double>(50 + random.below(1100)) / 100.0;
    marks.push_back(mark_m(x, i % 9 == 0 ? -99999.0 : y));
  }
  Moments by_hand;
  for (const HighWaterMark& h : marks) {
    by_hand = by_hand + Moments::of(h);
  }
  CHECK(wet_moments(marks) == by_hand);
  // The result genuinely depends on the order of the joins, which is why the
  // order is fixed: the reversed fold agrees to 1e-12 but not bit for bit.
  Moments reversed;
  for (const HighWaterMark& h : marks | std::views::reverse) {
    reversed = reversed + Moments::of(h);
  }
  CHECK(moments_near(by_hand, reversed));
  CHECK(by_hand != reversed);
}

// ---- non-finite and overflowing data ----------------------------------------

namespace {

void check_non_finite(const std::vector<HighWaterMark>& marks) {
  for (const Intercept mode : {Intercept::free, Intercept::through_origin}) {
    const auto result = hwm_stats(marks, mode);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error() == HwmStatsError{NonFiniteMoments{}});
  }
}

}  // namespace

TEST_CASE("a NaN observed value is reported, not laundered",
          "[core][hwm_stats][errors]") {
  check_non_finite({mark_m(1.0, 2.0), raw_mark(mov::test::quiet_nan, 3.0),
                    mark_m(3.0, 4.0)});
}

TEST_CASE("an infinite modeled value is reported",
          "[core][hwm_stats][errors]") {
  check_non_finite(
      {mark_m(1.0, 2.0), raw_mark(2.0, mov::test::infinity), mark_m(3.0, 4.0)});
  check_non_finite({mark_m(1.0, 2.0), raw_mark(2.0, -mov::test::infinity),
                    mark_m(3.0, 4.0)});
}

TEST_CASE("finite values whose squares overflow are reported",
          "[core][hwm_stats][errors]") {
  // 1e200 squared is 1e400: every input is finite, the sums are not.
  check_non_finite(
      {raw_mark(1e200, 1e200), raw_mark(2e200, 3e200), raw_mark(3e200, 2e200)});
}

TEST_CASE("one bad mark among many is enough", "[core][hwm_stats][errors]") {
  auto marks =
      make_cloud(100, {.offset = 1.0, .spread = 2.0, .dry_fraction = 0.0}, 3);
  marks[57] = raw_mark(1.0, mov::test::quiet_nan);
  check_non_finite(marks);
}

TEST_CASE("non-finite moments are reported before a short or flat fit",
          "[core][hwm_stats][errors]") {
  // One wet mark would be TooFewForFreeFit, but its value is NaN.
  const auto one = hwm_stats(std::array{raw_mark(mov::test::quiet_nan, 1.0)},
                             Intercept::free);
  REQUIRE_FALSE(one.has_value());
  CHECK(one.error() == HwmStatsError{NonFiniteMoments{}});
  // A dry mark's observed value never reaches the sums: no wet marks wins.
  const HighWaterMark dry_nan{.location = mov::test::gulf_coast(),
                              .ground = metres(0.0),
                              .observed = metres(mov::test::quiet_nan),
                              .modeled = mov::core::Dry{}};
  const auto none = hwm_stats(std::array{dry_nan}, Intercept::free);
  REQUIRE_FALSE(none.has_value());
  CHECK(none.error() == HwmStatsError{NoWetMarks{.total = 1}});
}

// ---- a long exact line
// -------------------------------------------------------

TEST_CASE("100000 marks on an exact line give R^2 = 1 and the slope back",
          "[core][hwm_stats][regression]") {
  // Dyadic x (quarters from 1 to about 1024) and y = 1.25 x: every mark is
  // exactly on the line, so the rounding in the moments is all that is left.
  // R^2 must stay within a few n ulps of 1 and never above it (hwm_stats
  // asserts the same bound in debug builds before it clamps).
  std::vector<HighWaterMark> marks;
  marks.reserve(100000);
  for (std::size_t i = 0; i < 100000; ++i) {
    const double x = 1.0 + (0.25 * static_cast<double>(i % 4093));
    marks.push_back(mark_m(x, 1.25 * x));
  }
  for (const Intercept mode : {Intercept::free, Intercept::through_origin}) {
    const HwmStats s = stats_of(marks, mode);
    CHECK(near(slope_of(s), 1.25, 1e-12));
    CHECK(near(r2_of(s), 1.0, 1e-9));
    CHECK(r2_of(s) <= 1.0);
    CHECK(s.wet == 100000);
  }
}
