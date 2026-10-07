// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/sample.hpp.

#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <optional>
#include <type_traits>

#include "mov/core/sample.hpp"
#include "test_helpers.hpp"

using mov::core::combine;
using mov::core::Dry;
using mov::core::finite_or_missing;
using mov::core::Missing;
using mov::core::Sample;
using mov::test::infinity;
using mov::test::quiet_nan;

namespace {

constexpr Sample finite(double v) { return Sample::of(v).value_or(Sample{}); }

constexpr int tag_of(const Sample& s) {
  return s.visit([](Missing) { return 0; }, [](Dry) { return 1; },
                 [](double) { return 2; });
}

constexpr int operation_calls() {
  int calls = 0;
  const auto count = [&calls](double a, double b) {
    ++calls;
    return a + b;
  };
  const Sample one = finite(1.0);
  (void)combine(one, Sample{Dry{}}, count);
  (void)combine(Sample{}, one, count);
  (void)combine(Sample{Dry{}}, Sample{}, count);
  (void)combine(one, one, count);  // the only call
  return calls;
}

constexpr auto add = [](double a, double b) { return a + b; };

}  // namespace

TEST_CASE("sample types are regular and cheap to move",
          "[core][sample][constexpr]") {
  STATIC_REQUIRE(std::regular<Missing>);
  STATIC_REQUIRE(std::regular<Dry>);
  STATIC_REQUIRE(std::regular<Sample>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Sample>);
  STATIC_REQUIRE(std::is_nothrow_move_assignable_v<Sample>);
  STATIC_REQUIRE(sizeof(Sample) == 16);  // C1
  // A raw double is not a Sample: the finite check cannot be skipped.
  STATIC_REQUIRE_FALSE(std::is_convertible_v<double, Sample>);
  STATIC_REQUIRE_FALSE(std::is_constructible_v<Sample, double>);
}

TEST_CASE("Sample defaults to Missing", "[core][sample][constexpr]") {
  STATIC_REQUIRE(Sample{}.is_missing());
  STATIC_REQUIRE(Sample{} == Sample{Missing{}});
  STATIC_REQUIRE(Sample{Dry{}}.is_dry());
  STATIC_REQUIRE_FALSE(Sample{Dry{}}.is_missing());
  STATIC_REQUIRE_FALSE(Sample{Dry{}} == Sample{});
}

TEST_CASE("Sample::of is nullopt exactly for non-finite values",
          "[core][sample][constexpr]") {
  STATIC_REQUIRE_FALSE(Sample::of(quiet_nan).has_value());
  STATIC_REQUIRE_FALSE(Sample::of(infinity).has_value());
  STATIC_REQUIRE_FALSE(Sample::of(-infinity).has_value());
  STATIC_REQUIRE(Sample::of(0.0).has_value());
  STATIC_REQUIRE(Sample::of(-1.5).has_value());
  STATIC_REQUIRE(Sample::of(1.7976931348623157e308).has_value());
  STATIC_REQUIRE(Sample::of(4.9e-324).has_value());  // denormal
}

TEST_CASE("Sample::value and the predicates", "[core][sample][constexpr]") {
  constexpr Sample v = finite(2.5);
  STATIC_REQUIRE(v.is_value());
  STATIC_REQUIRE_FALSE(v.is_dry());
  STATIC_REQUIRE_FALSE(v.is_missing());
  STATIC_REQUIRE(v.value() == 2.5);
  STATIC_REQUIRE_FALSE(Sample{Dry{}}.value().has_value());
  STATIC_REQUIRE_FALSE(Sample{}.value().has_value());
  STATIC_REQUIRE(finite(0.0) == finite(-0.0));
  STATIC_REQUIRE_FALSE(finite(1.0) == finite(2.0));
  STATIC_REQUIRE_FALSE(finite(0.0) == Sample{Dry{}});
}

TEST_CASE("finite_or_missing is the one lossy factory",
          "[core][sample][constexpr]") {
  STATIC_REQUIRE(finite_or_missing(3.0) == finite(3.0));
  STATIC_REQUIRE(finite_or_missing(quiet_nan).is_missing());
  STATIC_REQUIRE(finite_or_missing(infinity).is_missing());
  STATIC_REQUIRE(finite_or_missing(-infinity).is_missing());
}

TEST_CASE("Sample::visit dispatches on the alternative",
          "[core][sample][constexpr]") {
  STATIC_REQUIRE(tag_of(Sample{}) == 0);
  STATIC_REQUIRE(tag_of(Sample{Dry{}}) == 1);
  STATIC_REQUIRE(tag_of(finite(1.0)) == 2);
  STATIC_REQUIRE(finite(4.0).visit([](Missing) { return 0.0; },
                                   [](Dry) { return 0.0; },
                                   [](double x) { return x * 2.0; }) == 8.0);
}

TEST_CASE("combine: Missing beats Dry beats value",
          "[core][sample][constexpr]") {
  constexpr Sample one = finite(1.0);
  constexpr Sample two = finite(2.0);
  constexpr Sample dry{Dry{}};
  constexpr Sample missing{};

  STATIC_REQUIRE(combine(one, two, add) == finite(3.0));
  STATIC_REQUIRE(combine(missing, one, add).is_missing());
  STATIC_REQUIRE(combine(one, missing, add).is_missing());
  STATIC_REQUIRE(combine(missing, missing, add).is_missing());
  STATIC_REQUIRE(combine(missing, dry, add).is_missing());
  STATIC_REQUIRE(combine(dry, missing, add).is_missing());
  STATIC_REQUIRE(combine(dry, one, add).is_dry());
  STATIC_REQUIRE(combine(one, dry, add).is_dry());
  STATIC_REQUIRE(combine(dry, dry, add).is_dry());
}

TEST_CASE("combine turns a non-finite result into Missing",
          "[core][sample][constexpr]") {
  constexpr Sample one = finite(1.0);
  STATIC_REQUIRE(
      combine(one, one, [](double, double) { return quiet_nan; }).is_missing());
  STATIC_REQUIRE(
      combine(one, one, [](double, double) { return infinity; }).is_missing());
}

TEST_CASE("combine calls the operation only for two values",
          "[core][sample][constexpr]") {
  STATIC_REQUIRE(operation_calls() == 1);
}
