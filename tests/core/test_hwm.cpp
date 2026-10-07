// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <array>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <utility>

#include "hwm_helpers.hpp"
#include "mov/core/hwm.hpp"
#include "mov/core/units.hpp"
#include "test_helpers.hpp"

using mov::core::ClassBreaksError;
using mov::core::classify;
using mov::core::ErrorClasses;
using mov::core::HwmCategory;
using mov::core::Length;
using mov::core::modeled_error;
using mov::test::feet;
using mov::test::infinity;
using mov::test::mark_ft;
using mov::test::mark_m;
using mov::test::metres;
using mov::test::quiet_nan;

namespace {

using Breaks = std::array<Length, 7>;

Breaks metre_breaks() {
  return {metres(-1.5), metres(-1.0), metres(-0.5), metres(0.0),
          metres(0.5),  metres(1.0),  metres(1.5)};
}

constexpr std::array bins{
    HwmCategory::bin0, HwmCategory::bin1, HwmCategory::bin2, HwmCategory::bin3,
    HwmCategory::bin4, HwmCategory::bin5, HwmCategory::bin6, HwmCategory::bin7};

}  // namespace

TEST_CASE("ErrorClasses::make rejects a non-finite break at every position",
          "[core][hwm]") {
  const auto negative_nan =
      std::bit_cast<double>(std::uint64_t{0xFFF8'0000'0000'0000});
  for (const double bad : {quiet_nan, negative_nan, infinity, -infinity}) {
    for (std::size_t i = 0; i < 7; ++i) {
      auto breaks = metre_breaks();
      breaks[i] = metres(bad);
      CHECK(ErrorClasses::make(breaks) ==
            std::unexpected{ClassBreaksError::not_finite});
    }
  }
}

TEST_CASE("ErrorClasses::make reports a non-finite break before the order",
          "[core][hwm]") {
  // Out of order as well as NaN: the finite check comes first.
  auto breaks = metre_breaks();
  std::swap(breaks[0], breaks[6]);
  breaks[3] = metres(quiet_nan);
  CHECK(ErrorClasses::make(breaks) ==
        std::unexpected{ClassBreaksError::not_finite});
}

TEST_CASE("ErrorClasses::make keeps the breaks it was given", "[core][hwm]") {
  const Breaks custom{metres(-10.0), metres(-3.0), metres(-1.0), metres(0.0),
                      metres(2.0),   metres(4.0),  metres(8.0)};
  const auto made = ErrorClasses::make(custom);
  REQUIRE(made.has_value());
  const auto kept = made->breaks();
  for (std::size_t i = 0; i < 7; ++i) {
    CHECK(kept[i] == custom[i]);
  }
}

TEST_CASE("classify with custom breaks", "[core][hwm]") {
  const Breaks custom{metres(-10.0), metres(-3.0), metres(-1.0), metres(0.0),
                      metres(2.0),   metres(4.0),  metres(8.0)};
  const auto made = ErrorClasses::make(custom);
  REQUIRE(made.has_value());
  const ErrorClasses& classes = *made;
  // Observed 20 m, so the modeled value is 20 + the error, all exact.
  CHECK(classify(mark_m(20.0, 5.0), classes) == HwmCategory::bin0);    // -15
  CHECK(classify(mark_m(20.0, 10.0), classes) == HwmCategory::bin1);   // -10
  CHECK(classify(mark_m(20.0, 18.0), classes) == HwmCategory::bin2);   // -2
  CHECK(classify(mark_m(20.0, 19.0), classes) == HwmCategory::bin3);   // -1
  CHECK(classify(mark_m(20.0, 20.0), classes) == HwmCategory::bin4);   // 0
  CHECK(classify(mark_m(20.0, 21.0), classes) == HwmCategory::bin4);   // 1
  CHECK(classify(mark_m(20.0, 22.0), classes) == HwmCategory::bin5);   // 2
  CHECK(classify(mark_m(20.0, 28.0), classes) == HwmCategory::bin7);   // 8
  CHECK(classify(mark_m(20.0, -999.0), classes) == HwmCategory::dry);  // dry
}

TEST_CASE("classify with the foot defaults, errors clear of every break",
          "[core][hwm]") {
  const auto classes = ErrorClasses::feet_default();
  // Observed 10 ft; the error in feet is the third column.
  CHECK(classify(mark_ft(10.0, 4.0), classes) == HwmCategory::bin0);   // -6
  CHECK(classify(mark_ft(10.0, 5.5), classes) == HwmCategory::bin1);   // -4.5
  CHECK(classify(mark_ft(10.0, 7.0), classes) == HwmCategory::bin2);   // -3
  CHECK(classify(mark_ft(10.0, 9.0), classes) == HwmCategory::bin3);   // -1
  CHECK(classify(mark_ft(10.0, 10.5), classes) == HwmCategory::bin4);  // 0.5
  CHECK(classify(mark_ft(10.0, 12.0), classes) == HwmCategory::bin5);  // 2
  CHECK(classify(mark_ft(10.0, 14.0), classes) == HwmCategory::bin6);  // 4
  CHECK(classify(mark_ft(10.0, 16.0), classes) == HwmCategory::bin7);  // 6
}

// Length stores SI, so a feet error that is on a break in decimal can differ
// from the break by an ulp after conversion: 12.5 ft - 10 ft is not exactly
// 1.5 ft once each is multiplied by 0.3048. v4 compared in file units. The
// guarantee is therefore exact for metres and for any error that is not within
// rounding of a break; this test pins both halves (docs/wp-notes/WP4.md).
TEST_CASE("classify at a foot break is exact only up to rounding of the unit",
          "[core][hwm]") {
  const auto classes = ErrorClasses::feet_default();
  const auto breaks = classes.breaks();
  for (std::size_t i = 0; i < 7; ++i) {
    const double b = breaks[i].as(mov::core::LengthUnit::foot);
    // 1e-9 ft either side of the break is far above rounding noise.
    CHECK(classify(mark_ft(10.0, 10.0 + b + 1e-9), classes) == bins[i + 1]);
    CHECK(classify(mark_ft(10.0, 10.0 + b - 1e-9), classes) == bins[i]);
  }
}

TEST_CASE("modeled_error in feet is reported in the SI-backed Length",
          "[core][hwm]") {
  const auto maybe_error = modeled_error(mark_ft(10.0, 12.0));
  CHECK(maybe_error.has_value());
  const Length e = maybe_error.value_or(Length{});
  CHECK(mov::test::near(e.as(mov::core::LengthUnit::foot), 2.0, 1e-12));
  CHECK(mov::test::near(e.as(mov::core::LengthUnit::meter), 0.6096, 1e-12));
  CHECK(e == feet(12.0) - feet(10.0));
}

TEST_CASE("a mark compares by every field", "[core][hwm]") {
  const mov::core::HighWaterMark h{
      .location = mov::test::gulf_coast(),
      .ground = metres(1.0),
      .observed = metres(2.0),
      .modeled = mov::core::Wet{.elevation = metres(2.5)}};
  CHECK(h == mark_m(2.0, 2.5));
  CHECK(h != mark_m(2.1, 2.5));
  CHECK(h != mark_m(2.0, 2.6));
  CHECK(h != mark_m(2.0, -99999.0));
  auto moved = h;
  moved.ground = metres(1.5);
  CHECK(h != moved);
  CHECK(modeled_error(h) == std::optional<Length>{metres(0.5)});
}
