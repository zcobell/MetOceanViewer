// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/series_ops.hpp and datum_shift.hpp: the
// Bucket monoid and the Extent semilattice on small exact cases, the
// Calibration factory, and the shape of the value types.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>
#include <variant>

#include "mov/core/datum_shift.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/series_ops.hpp"
#include "test_helpers.hpp"

using mov::core::Bucket;
using mov::core::Calibration;
using mov::core::Dry;
using mov::core::Extent;
using mov::core::Extreme;
using mov::core::join;
using mov::core::Missing;
using mov::core::NotALengthSeries;
using mov::core::QuickStats;
using mov::core::ResidualErrc;
using mov::core::Sample;
using mov::core::ShiftError;
using mov::core::TimeOverflow;
using mov::core::UnknownSourceDatum;
using mov::core::ValueRange;
using mov::core::ValueStats;
using mov::core::ValueSummary;
using mov::core::Window;
using mov::test::at_ms;
using mov::test::near;

namespace {

constexpr Sample v(double x) { return Sample::of(x).value_or(Sample{}); }

// Dyadic data, so every sum is exact: values 2, 5, 5, 1, 5 with a Missing and a
// Dry mixed in, at 1 ms steps.
constexpr std::array<Bucket, 7> run{Bucket::of(at_ms(0), v(2.0)),
                                    Bucket::of(at_ms(1), v(5.0)),
                                    Bucket::of(at_ms(2), Sample{Missing{}}),
                                    Bucket::of(at_ms(3), v(5.0)),
                                    Bucket::of(at_ms(4), v(1.0)),
                                    Bucket::of(at_ms(5), Sample{Dry{}}),
                                    Bucket::of(at_ms(6), v(5.0))};

constexpr Bucket fold(std::size_t from, std::size_t to) {
  Bucket b{};
  for (std::size_t i = from; i < to; ++i) {
    b = b + run[i];
  }
  return b;
}

constexpr bool associative_everywhere() {
  for (std::size_t i = 0; i <= run.size(); ++i) {
    for (std::size_t j = i; j <= run.size(); ++j) {
      const Bucket a = fold(0, i);
      const Bucket b = fold(i, j);
      const Bucket c = fold(j, run.size());
      if (not((a + b) + c == a + (b + c) and
              (a + b) + c == fold(0, run.size()))) {
        return false;
      }
    }
  }
  return true;
}

constexpr Bucket whole = fold(0, run.size());

constexpr std::optional<Extreme> min_of(const Bucket& b) {
  return b.summary().transform(&ValueSummary::min);
}
constexpr std::optional<Extreme> max_of(const Bucket& b) {
  return b.summary().transform(&ValueSummary::max);
}
constexpr std::optional<Extreme> first_of(const Bucket& b) {
  return b.summary().transform(&ValueSummary::first);
}
constexpr std::optional<Extreme> last_of(const Bucket& b) {
  return b.summary().transform(&ValueSummary::last);
}
constexpr std::optional<double> sum_of(const Bucket& b) {
  return b.summary().transform(&ValueSummary::sum);
}
constexpr std::optional<double> mean_of(const Bucket& b) {
  return b.summary().transform(&ValueSummary::mean);
}

}  // namespace

TEST_CASE("the series-operation value types are regular and nothrow-movable",
          "[core][bucket][constexpr]") {
  STATIC_REQUIRE(std::regular<Bucket>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Bucket>);
  // A ValueSummary exists only inside a Bucket: no default constructor.
  STATIC_REQUIRE(std::copyable<ValueSummary>);
  STATIC_REQUIRE(std::equality_comparable<ValueSummary>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<ValueSummary>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<ValueSummary>);
  STATIC_REQUIRE(std::regular<Extreme>);
  STATIC_REQUIRE(std::regular<ValueRange>);
  STATIC_REQUIRE(std::regular<Extent>);
  STATIC_REQUIRE(std::regular<ValueStats>);
  STATIC_REQUIRE(std::regular<QuickStats>);
  STATIC_REQUIRE(std::regular<Window>);
  STATIC_REQUIRE(std::regular<Calibration>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Calibration>);
  STATIC_REQUIRE(std::regular<TimeOverflow>);
  STATIC_REQUIRE(std::regular<NotALengthSeries>);
  STATIC_REQUIRE(std::regular<UnknownSourceDatum>);
  STATIC_REQUIRE(std::regular<ShiftError>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Extent>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<ShiftError>);
  STATIC_REQUIRE(
      std::is_same_v<std::underlying_type_t<ResidualErrc>, std::uint8_t>);
}

TEST_CASE("Bucket small cases", "[core][bucket][constexpr]") {
  STATIC_REQUIRE(Bucket{}.values() == 0);
  STATIC_REQUIRE(not Bucket{}.summary().has_value());
  STATIC_REQUIRE(not Bucket{}.has_gap());
  STATIC_REQUIRE(Bucket{} == Bucket{});
  STATIC_REQUIRE(Bucket{} + whole == whole);
  STATIC_REQUIRE(whole + Bucket{} == whole);

  STATIC_REQUIRE(whole.values() == 5);
  STATIC_REQUIRE(whole.missing() == 1);
  STATIC_REQUIRE(whole.dry() == 1);
  STATIC_REQUIRE(sum_of(whole) == 18.0);
  STATIC_REQUIRE(mean_of(whole) == 3.6);
  STATIC_REQUIRE(whole.has_gap());
  // 5 occurs at 1, 3 and 6: the first wins; 1 occurs once.
  STATIC_REQUIRE(max_of(whole) == Extreme{.value = 5.0, .time = at_ms(1)});
  STATIC_REQUIRE(min_of(whole) == Extreme{.value = 1.0, .time = at_ms(4)});
  STATIC_REQUIRE(first_of(whole) == Extreme{.value = 2.0, .time = at_ms(0)});
  STATIC_REQUIRE(last_of(whole) == Extreme{.value = 5.0, .time = at_ms(6)});
  STATIC_REQUIRE(associative_everywhere());
}

TEST_CASE("Bucket ties keep the left operand", "[core][bucket][constexpr]") {
  constexpr Bucket early = Bucket::of(at_ms(1), v(5.0));
  constexpr Bucket late = Bucket::of(at_ms(2), v(5.0));
  constexpr Extreme at_1{.value = 5.0, .time = at_ms(1)};
  constexpr Extreme at_2{.value = 5.0, .time = at_ms(2)};
  STATIC_REQUIRE(max_of(early + late) == at_1);
  STATIC_REQUIRE(max_of(late + early) == at_2);
  STATIC_REQUIRE(min_of(early + late) == at_1);
  STATIC_REQUIRE(early + late != late + early);
  // first is the left operand's, last the right's.
  STATIC_REQUIRE(first_of(early + late) == at_1);
  STATIC_REQUIRE(last_of(early + late) == at_2);
  STATIC_REQUIRE(first_of(late + early) == at_2);
  STATIC_REQUIRE(last_of(late + early) == at_1);
  // Strictly better beats the left operand, from either side.
  constexpr Bucket higher = Bucket::of(at_ms(9), v(6.0));
  STATIC_REQUIRE(max_of(early + higher) ==
                 Extreme{.value = 6.0, .time = at_ms(9)});
  STATIC_REQUIRE(max_of(higher + early) ==
                 Extreme{.value = 6.0, .time = at_ms(9)});
  STATIC_REQUIRE(min_of(early + higher) == at_1);
  STATIC_REQUIRE(min_of(higher + early) == at_1);
}

TEST_CASE("Bucket::of classifies a sample", "[core][bucket][constexpr]") {
  STATIC_REQUIRE(Bucket::of(at_ms(0), Sample{Missing{}}).missing() == 1);
  STATIC_REQUIRE(Bucket::of(at_ms(0), Sample{}).values() == 0);
  STATIC_REQUIRE(Bucket::of(at_ms(0), Sample{Dry{}}).dry() == 1);
  STATIC_REQUIRE(Bucket::of(at_ms(0), Sample{Dry{}}).has_gap());
  STATIC_REQUIRE(sum_of(Bucket::of(at_ms(0), v(1.0))) == 1.0);
  // A gap changes no value fact, and a value changes no gap count.
  constexpr Bucket gap_then_value =
      Bucket::of(at_ms(0), Sample{}) + Bucket::of(at_ms(1), v(4.0));
  STATIC_REQUIRE(first_of(gap_then_value) ==
                 Extreme{.value = 4.0, .time = at_ms(1)});
  STATIC_REQUIRE(gap_then_value.missing() == 1);
}

TEST_CASE("Bucket sums neither overflow nor make a NaN",
          "[core][bucket][constexpr]") {
  constexpr double huge = 1.7e308;
  constexpr Bucket a = Bucket::of(at_ms(0), v(huge));
  constexpr Bucket b = Bucket::of(at_ms(1), v(huge));
  constexpr Bucket c = Bucket::of(at_ms(2), v(-huge));
  constexpr Bucket d = Bucket::of(at_ms(3), v(-huge));
  // Every grouping of +huge, +huge, -huge, -huge is the same, and zero.
  STATIC_REQUIRE(((a + b) + c) + d == a + (b + (c + d)));
  STATIC_REQUIRE(((a + b) + c) + d == (a + b) + (c + d));
  STATIC_REQUIRE(((a + b) + c) + d == (a + (b + c)) + d);
  STATIC_REQUIRE(mean_of(((a + b) + c) + d) == 0.0);
  STATIC_REQUIRE(sum_of(((a + b) + c) + d) == 0.0);
  // The sum of the first two is out of range in a double; the mean is not.
  STATIC_REQUIRE(near(mean_of(a + b + c).value_or(0.0), huge / 3.0));
  STATIC_REQUIRE(near(mean_of(a + b).value_or(0.0), huge));
}

namespace {

constexpr Extent box(std::int64_t a, std::int64_t b, double lo, double hi) {
  return {.first = at_ms(a),
          .last = at_ms(b),
          .values = ValueRange{.min = lo, .max = hi}};
}
constexpr Extent times_only{
    .first = at_ms(5), .last = at_ms(6), .values = std::nullopt};

}  // namespace

TEST_CASE("Extent join is an exact semilattice", "[core][extent][constexpr]") {
  constexpr std::optional<Extent> a = box(0, 10, -1.0, 4.0);
  constexpr std::optional<Extent> b = box(5, 20, 0.0, 9.0);
  constexpr std::optional<Extent> c = times_only;
  constexpr std::optional<Extent> none;
  STATIC_REQUIRE(join(a, b) == box(0, 20, -1.0, 9.0));
  STATIC_REQUIRE(join(a, b) == join(b, a));
  STATIC_REQUIRE(join(a, a) == a);
  STATIC_REQUIRE(join(a, none) == a);
  STATIC_REQUIRE(join(none, a) == a);
  STATIC_REQUIRE(join(none, none) == none);
  STATIC_REQUIRE(join(join(a, b), c) == join(a, join(b, c)));
  // A time-only extent adds no value range, from either side.
  STATIC_REQUIRE(join(a, c) == box(0, 10, -1.0, 4.0));
  STATIC_REQUIRE(join(c, a) == box(0, 10, -1.0, 4.0));
  STATIC_REQUIRE(join(c, c) == c);
  STATIC_REQUIRE(join(c, b) == box(5, 20, 0.0, 9.0));
}

TEST_CASE("Calibration accepts finite coefficients only",
          "[core][calibration][constexpr]") {
  constexpr double nan = mov::test::quiet_nan;
  constexpr double inf = mov::test::infinity;
  STATIC_REQUIRE(Calibration::make({.scale = 2.0, .offset = -1.0}).has_value());
  STATIC_REQUIRE(Calibration::make({.scale = 0.0, .offset = 0.0}).has_value());
  STATIC_REQUIRE_FALSE(
      Calibration::make({.scale = nan, .offset = 0.0}).has_value());
  STATIC_REQUIRE_FALSE(
      Calibration::make({.scale = 1.0, .offset = nan}).has_value());
  STATIC_REQUIRE_FALSE(
      Calibration::make({.scale = inf, .offset = 0.0}).has_value());
  STATIC_REQUIRE_FALSE(
      Calibration::make({.scale = 1.0, .offset = -inf}).has_value());
  STATIC_REQUIRE(Calibration{}.affine() == mov::core::Affine{});
  STATIC_REQUIRE(Calibration::make({.scale = 2.0, .offset = 3.0})
                     .value_or(Calibration{})
                     .affine() ==
                 mov::core::Affine{.scale = 2.0, .offset = 3.0});
}
