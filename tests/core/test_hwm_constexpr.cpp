// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/hwm.hpp.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <expected>
#include <cstddef>
#include <limits>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "hwm_helpers.hpp"
#include "mov/core/hwm.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/units.hpp"
#include "test_helpers.hpp"

using mov::core::ClassBreaksError;
using mov::core::classify;
using mov::core::Dry;
using mov::core::dry_threshold;
using mov::core::ErrorClasses;
using mov::core::HighWaterMark;
using mov::core::HwmCategory;
using mov::core::is_dry;
using mov::core::Length;
using mov::core::LengthUnit;
using mov::core::model_value;
using mov::core::modeled_error;
using mov::core::Wet;
using mov::core::WetDry;
using mov::test::feet;
using mov::test::in_metres;
using mov::test::infinity;
using mov::test::mark_m;
using mov::test::metres;
using mov::test::quiet_nan;

namespace {

constexpr std::array<Length, 7> metre_breaks{metres(-1.5), metres(-1.0),
                                             metres(-0.5), metres(0.0),
                                             metres(0.5),  metres(1.0),
                                             metres(1.5)};
constexpr std::array<Length, 7> foot_breaks{feet(-5.0), feet(-3.5), feet(-1.5),
                                            feet(0.0),  feet(1.5),  feet(3.5),
                                            feet(5.0)};

// The categories in order, so index i is "the class below break i" and
// index i + 1 is "the class from break i up".
constexpr std::array bins{HwmCategory::bin0, HwmCategory::bin1,
                          HwmCategory::bin2, HwmCategory::bin3,
                          HwmCategory::bin4, HwmCategory::bin5,
                          HwmCategory::bin6, HwmCategory::bin7};

// Observed 10 m and modeled 10 + offset: every metre-default break and the
// quarter-metre offsets used below are dyadic, so the error is exact.
constexpr HwmCategory category_of_error(double error_m,
                                        const ErrorClasses& classes) {
  return classify(mark_m(10.0, 10.0 + error_m), classes);
}

// An error exactly on break i goes to the upper class (v4: `e < c[i]` is
// strict); an error a quarter metre below it stays in the lower class.
constexpr bool every_break_goes_up() {
  constexpr auto classes = ErrorClasses::meters_default();
  for (std::size_t i = 0; i < 7; ++i) {
    const double b = in_metres(classes.breaks()[i]);
    if (category_of_error(b, classes) != bins[i + 1]) {
      return false;
    }
    if (category_of_error(b - 0.25, classes) != bins[i]) {
      return false;
    }
  }
  return true;
}

constexpr bool classes_equal_for_foot_defaults() {
  const auto made = ErrorClasses::make(foot_breaks);
  return made and *made == ErrorClasses::feet_default();
}

// Break i + 1 equal to break i, or the two swapped, at every position.
constexpr bool rejects_every_non_rising_pair() {
  const auto rejected = std::unexpected{ClassBreaksError::not_strictly_increasing};
  for (std::size_t i = 0; i + 1 < 7; ++i) {
    auto equal_pair = metre_breaks;
    equal_pair[i + 1] = equal_pair[i];
    auto swapped = metre_breaks;
    std::swap(swapped[i], swapped[i + 1]);
    if (ErrorClasses::make(equal_pair) != rejected or
        ErrorClasses::make(swapped) != rejected) {
      return false;
    }
  }
  return true;
}

}  // namespace

TEST_CASE("hwm types are value types", "[core][hwm][constexpr]") {
  STATIC_REQUIRE(std::regular<Wet>);
  STATIC_REQUIRE(std::regular<WetDry>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Wet>);
  // Location has no default, so a mark is copyable, not regular.
  STATIC_REQUIRE(std::copyable<HighWaterMark>);
  STATIC_REQUIRE(std::equality_comparable<HighWaterMark>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<HighWaterMark>);
  STATIC_REQUIRE(std::copyable<ErrorClasses>);
  STATIC_REQUIRE(std::equality_comparable<ErrorClasses>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<ErrorClasses>);
  // Seven breaks or none: there is no empty ErrorClasses.
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<ErrorClasses>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<HwmCategory>);
}

TEST_CASE("is_dry: <= -999 and nothing above", "[core][hwm][constexpr]") {
  STATIC_REQUIRE(dry_threshold == -999.0);
  STATIC_REQUIRE(is_dry(-99999.0));  // ADCIRC's fill
  STATIC_REQUIRE(is_dry(-9999.0));
  STATIC_REQUIRE(is_dry(-999.5));
  STATIC_REQUIRE(is_dry(-999.0));  // on the threshold: dry (N2)
  STATIC_REQUIRE_FALSE(is_dry(-998.999999));
  STATIC_REQUIRE_FALSE(is_dry(-998.0));
  STATIC_REQUIRE_FALSE(is_dry(-900.0));  // v4's map and axes called this dry
  STATIC_REQUIRE_FALSE(is_dry(0.0));
  STATIC_REQUIRE_FALSE(is_dry(2.5));
  STATIC_REQUIRE(is_dry(-std::numeric_limits<double>::max()));
  STATIC_REQUIRE(is_dry(-infinity));
  STATIC_REQUIRE_FALSE(is_dry(infinity));
  STATIC_REQUIRE_FALSE(is_dry(quiet_nan));  // not dry, not wet: unspecified
}

TEST_CASE("model_value applies the one dry rule",
          "[core][hwm][constexpr][regression][N2]") {
  STATIC_REQUIRE(model_value(-99999.0, LengthUnit::meter) == WetDry{Dry{}});
  STATIC_REQUIRE(model_value(-999.0, LengthUnit::foot) == WetDry{Dry{}});
  STATIC_REQUIRE(model_value(-998.0, LengthUnit::meter) ==
                 WetDry{Wet{.elevation = metres(-998.0)}});
  // The threshold is on the raw file value, before the unit is applied.
  STATIC_REQUIRE(model_value(2.0, LengthUnit::foot) ==
                 WetDry{Wet{.elevation = feet(2.0)}});
  STATIC_REQUIRE(model_value(-900.0, LengthUnit::foot) ==
                 WetDry{Wet{.elevation = feet(-900.0)}});
}

TEST_CASE("modeled_error is modeled minus observed, none when dry",
          "[core][hwm][constexpr]") {
  STATIC_REQUIRE(modeled_error(mark_m(2.5, 3.0)) ==
                 std::optional<Length>{metres(0.5)});
  STATIC_REQUIRE(modeled_error(mark_m(3.0, 2.5)) ==
                 std::optional<Length>{metres(-0.5)});
  STATIC_REQUIRE(modeled_error(mark_m(3.0, 3.0)) ==
                 std::optional<Length>{metres(0.0)});
  STATIC_REQUIRE(modeled_error(mark_m(3.0, -99999.0)) == std::nullopt);
  STATIC_REQUIRE(modeled_error(mark_m(3.0, -999.0)) == std::nullopt);
}

TEST_CASE("ErrorClasses::make accepts strictly increasing breaks",
          "[core][hwm][constexpr]") {
  STATIC_REQUIRE(ErrorClasses::make(metre_breaks).has_value());
  STATIC_REQUIRE(ErrorClasses::make(metre_breaks) ==
                 ErrorClasses::meters_default());
  STATIC_REQUIRE(classes_equal_for_foot_defaults());
  STATIC_REQUIRE(ErrorClasses::meters_default() !=
                 ErrorClasses::feet_default());
  // A tiny but strict step is still increasing.
  constexpr std::array<Length, 7> tight{metres(0.0),  metres(1e-9), metres(2e-9),
                                        metres(3e-9), metres(4e-9), metres(5e-9),
                                        metres(6e-9)};
  STATIC_REQUIRE(ErrorClasses::make(tight).has_value());
}

TEST_CASE("the default classes are the v4 defaults", "[core][hwm][constexpr]") {
  constexpr auto ft = ErrorClasses::feet_default();
  constexpr auto m = ErrorClasses::meters_default();
  STATIC_REQUIRE(ft.breaks().size() == 7);
  STATIC_REQUIRE(m.breaks().size() == 7);
  STATIC_REQUIRE(std::ranges::equal(ft.breaks(), foot_breaks));
  STATIC_REQUIRE(std::ranges::equal(m.breaks(), metre_breaks));
}

TEST_CASE("ErrorClasses::make rejects a break that does not rise",
          "[core][hwm][constexpr]") {
  STATIC_REQUIRE(rejects_every_non_rising_pair());
  constexpr std::array<Length, 7> all_equal{metres(1.0), metres(1.0),
                                            metres(1.0), metres(1.0),
                                            metres(1.0), metres(1.0),
                                            metres(1.0)};
  STATIC_REQUIRE(ErrorClasses::make(all_equal) ==
                 std::unexpected{ClassBreaksError::not_strictly_increasing});
  constexpr std::array<Length, 7> descending{metres(1.5),  metres(1.0),
                                             metres(0.5),  metres(0.0),
                                             metres(-0.5), metres(-1.0),
                                             metres(-1.5)};
  STATIC_REQUIRE(ErrorClasses::make(descending) ==
                 std::unexpected{ClassBreaksError::not_strictly_increasing});
}

TEST_CASE("classify: a dry mark is dry whatever the classes",
          "[core][hwm][constexpr]") {
  STATIC_REQUIRE(classify(mark_m(1.0, -99999.0), ErrorClasses::meters_default()) ==
                 HwmCategory::dry);
  STATIC_REQUIRE(classify(mark_m(1.0, -999.0), ErrorClasses::feet_default()) ==
                 HwmCategory::dry);
  // Just above the threshold is a wet mark with a huge negative error.
  STATIC_REQUIRE(classify(mark_m(1.0, -998.0), ErrorClasses::meters_default()) ==
                 HwmCategory::bin0);
}

TEST_CASE("classify: an error on a break goes to the upper class",
          "[core][hwm][constexpr]") {
  STATIC_REQUIRE(every_break_goes_up());
}

TEST_CASE("classify: the ends and the middle",
          "[core][hwm][constexpr]") {
  constexpr auto m = ErrorClasses::meters_default();
  STATIC_REQUIRE(category_of_error(-100.0, m) == HwmCategory::bin0);
  STATIC_REQUIRE(category_of_error(-1.75, m) == HwmCategory::bin0);
  STATIC_REQUIRE(category_of_error(-0.25, m) == HwmCategory::bin3);
  STATIC_REQUIRE(category_of_error(0.25, m) == HwmCategory::bin4);
  STATIC_REQUIRE(category_of_error(1.75, m) == HwmCategory::bin7);
  STATIC_REQUIRE(category_of_error(100.0, m) == HwmCategory::bin7);
  // A perfect model: error 0 is break 3, so it is the upper class bin4.
  STATIC_REQUIRE(category_of_error(0.0, m) == HwmCategory::bin4);
}
