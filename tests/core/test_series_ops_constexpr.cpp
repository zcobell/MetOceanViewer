// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/series_ops.hpp and datum_shift.hpp: the
// Bucket monoid and the Extent semilattice on small exact cases, and the shape
// of the value types.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>
#include <variant>

#include "mov/core/datum_shift.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/series_ops.hpp"
#include "test_helpers.hpp"

using mov::core::Bucket;
using mov::core::combine;
using mov::core::Dry;
using mov::core::Extent;
using mov::core::Extreme;
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
using mov::test::at_ms;

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

}  // namespace

TEST_CASE("Bucket is a regular, nothrow-movable value",
          "[core][bucket][constexpr]") {
  STATIC_REQUIRE(std::regular<Bucket>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Bucket>);
  STATIC_REQUIRE(std::regular<Extreme>);
  STATIC_REQUIRE(std::regular<ValueRange>);
  STATIC_REQUIRE(std::regular<Extent>);
  STATIC_REQUIRE(std::regular<ValueStats>);
  STATIC_REQUIRE(std::regular<QuickStats>);
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
  STATIC_REQUIRE(not Bucket{}.min().has_value());
  STATIC_REQUIRE(not Bucket{}.has_gap());
  STATIC_REQUIRE(Bucket{} == Bucket{});
  STATIC_REQUIRE(Bucket{} + whole == whole);
  STATIC_REQUIRE(whole + Bucket{} == whole);

  STATIC_REQUIRE(whole.values() == 5);
  STATIC_REQUIRE(whole.missing() == 1);
  STATIC_REQUIRE(whole.dry() == 1);
  STATIC_REQUIRE(whole.sum() == 18.0);
  STATIC_REQUIRE(whole.has_gap());
  // 5 occurs at 1, 3 and 6: the first wins; 1 occurs once.
  STATIC_REQUIRE(whole.max() == Extreme{.value = 5.0, .time = at_ms(1)});
  STATIC_REQUIRE(whole.min() == Extreme{.value = 1.0, .time = at_ms(4)});
  STATIC_REQUIRE(associative_everywhere());
}

TEST_CASE("Bucket ties keep the left operand", "[core][bucket][constexpr]") {
  constexpr Bucket early = Bucket::of(at_ms(1), v(5.0));
  constexpr Bucket late = Bucket::of(at_ms(2), v(5.0));
  STATIC_REQUIRE((early + late).max() ==
                 Extreme{.value = 5.0, .time = at_ms(1)});
  STATIC_REQUIRE((late + early).max() ==
                 Extreme{.value = 5.0, .time = at_ms(2)});
  STATIC_REQUIRE((early + late).min() ==
                 Extreme{.value = 5.0, .time = at_ms(1)});
  STATIC_REQUIRE(early + late != late + early);
  // Strictly better beats the left operand, from either side.
  constexpr Bucket higher = Bucket::of(at_ms(9), v(6.0));
  STATIC_REQUIRE((early + higher).max() ==
                 Extreme{.value = 6.0, .time = at_ms(9)});
  STATIC_REQUIRE((higher + early).max() ==
                 Extreme{.value = 6.0, .time = at_ms(9)});
  STATIC_REQUIRE((early + higher).min() ==
                 Extreme{.value = 5.0, .time = at_ms(1)});
  STATIC_REQUIRE((higher + early).min() ==
                 Extreme{.value = 5.0, .time = at_ms(1)});
}

TEST_CASE("Bucket::of classifies a sample", "[core][bucket][constexpr]") {
  STATIC_REQUIRE(Bucket::of(at_ms(0), Sample{Missing{}}).missing() == 1);
  STATIC_REQUIRE(Bucket::of(at_ms(0), Sample{}).values() == 0);
  STATIC_REQUIRE(Bucket::of(at_ms(0), Sample{Dry{}}).dry() == 1);
  STATIC_REQUIRE(Bucket::of(at_ms(0), Sample{Dry{}}).has_gap());
  STATIC_REQUIRE(Bucket::of(at_ms(0), v(1.0)).sum() == 1.0);
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

TEST_CASE("Extent combine is an exact semilattice",
          "[core][extent][constexpr]") {
  constexpr std::optional<Extent> a = box(0, 10, -1.0, 4.0);
  constexpr std::optional<Extent> b = box(5, 20, 0.0, 9.0);
  constexpr std::optional<Extent> c = times_only;
  constexpr std::optional<Extent> none;
  STATIC_REQUIRE(combine(a, b) == box(0, 20, -1.0, 9.0));
  STATIC_REQUIRE(combine(a, b) == combine(b, a));
  STATIC_REQUIRE(combine(a, a) == a);
  STATIC_REQUIRE(combine(a, none) == a);
  STATIC_REQUIRE(combine(none, a) == a);
  STATIC_REQUIRE(combine(none, none) == none);
  STATIC_REQUIRE(combine(combine(a, b), c) == combine(a, combine(b, c)));
  // A time-only extent adds no value range, from either side.
  STATIC_REQUIRE(combine(a, c) == box(0, 10, -1.0, 4.0));
  STATIC_REQUIRE(combine(c, a) == box(0, 10, -1.0, 4.0));
  STATIC_REQUIRE(combine(c, c) == c);
  STATIC_REQUIRE(combine(c, b) == box(5, 20, 0.0, 9.0));
}
