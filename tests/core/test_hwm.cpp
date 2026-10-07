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

using mov::core::checked_elevation;
using mov::core::ClassBreaksError;
using mov::core::classify;
using mov::core::ElevationError;
using mov::core::ErrorClasses;
using mov::core::HwmCategory;
using mov::core::Length;
using mov::core::LengthUnit;
using mov::core::model_value;
using mov::core::modeled_error;
using mov::core::WetDry;
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

// Observed and modeled are decimals with 2 places (hundredths of a foot), built
// as integer / 100.0 so each is the double nearest the decimal, like a parsed
// file value. Their difference is a break exactly in decimal arithmetic, but in
// doubles (and after the 0.3048 on each side) it is often an ulp off: v4 put
// those ties below the break; the 1 nm grid puts every one in the upper class.
TEST_CASE("a decimal tie goes up", "[core][hwm][regression]") {
  const auto classes = ErrorClasses::feet_default();
  constexpr std::array<int, 7> break_hundredths{-500, -350, -150, 0,
                                                150,  350,  500};
  int ties = 0;
  for (int observed = -1000; observed <= 2000; observed += 7) {
    for (std::size_t i = 0; i < 7; ++i) {
      const double x = observed / 100.0;
      const double y = (observed + break_hundredths[i]) / 100.0;
      CHECK(classify(mark_ft(x, y), classes) == bins[i + 1]);
      ++ties;
    }
  }
  CHECK(ties > 2000);
}

TEST_CASE("a decimal tie goes up in metres, to the millimetre",
          "[core][hwm][regression]") {
  const auto classes = ErrorClasses::meters_default();
  constexpr std::array<int, 7> break_thousandths{-1500, -1000, -500, 0,
                                                 500,   1000,  1500};
  for (int observed = -500; observed <= 3000; observed += 11) {
    for (std::size_t i = 0; i < 7; ++i) {
      const double x = observed / 1000.0;
      const double y = (observed + break_thousandths[i]) / 1000.0;
      CHECK(classify(mark_m(x, y), classes) == bins[i + 1]);
    }
  }
}

TEST_CASE("a decimal tie goes up on custom decimal breaks",
          "[core][hwm][regression]") {
  const Breaks custom{metres(-0.3), metres(-0.2), metres(-0.1), metres(0.1),
                      metres(0.2),  metres(0.3),  metres(0.4)};
  const auto made = ErrorClasses::make(custom);
  REQUIRE(made.has_value());
  const ErrorClasses& classes = *made;
  // (observed, modeled) whose decimal difference is break i; 0.8 - 0.7 is
  // 0.10000000000000009 and 0.9 - 0.6 is 0.30000000000000004 in doubles.
  constexpr std::array<std::array<double, 2>, 7> ties{{{0.9, 0.6},
                                                       {0.9, 0.7},
                                                       {0.9, 0.8},
                                                       {0.7, 0.8},
                                                       {0.6, 0.8},
                                                       {0.5, 0.8},
                                                       {0.4, 0.8}}};
  for (std::size_t i = 0; i < 7; ++i) {
    CHECK(classify(mark_m(ties[i][0], ties[i][1]), classes) == bins[i + 1]);
  }
}

TEST_CASE("values 1e-6 ft either side of a break are told apart",
          "[core][hwm]") {
  const auto classes = ErrorClasses::feet_default();
  const auto breaks = classes.breaks();
  for (std::size_t i = 0; i < 7; ++i) {
    const double b = breaks[i].as(mov::core::LengthUnit::foot);
    // 1e-6 ft is about 300 nm: far above the 1 nm grid and the rounding noise.
    CHECK(classify(mark_ft(10.0, 10.0 + b + 1e-6), classes) == bins[i + 1]);
    CHECK(classify(mark_ft(10.0, 10.0 + b - 1e-6), classes) == bins[i]);
  }
}

TEST_CASE("class breaks must be on distinct grid points and in range",
          "[core][hwm]") {
  // Two breaks beyond +-1e6 m saturate to the same grid point.
  const Breaks far{metres(-3.0), metres(-2.0),  metres(-1.0), metres(0.0),
                   metres(1.0),  metres(2.0e6), metres(3.0e6)};
  CHECK(ErrorClasses::make(far) ==
        std::unexpected{ClassBreaksError::not_strictly_increasing});
  const Breaks edge{metres(-3.0), metres(-2.0), metres(-1.0),    metres(0.0),
                    metres(1.0),  metres(2.0),  metres(999999.0)};
  CHECK(ErrorClasses::make(edge).has_value());
}

TEST_CASE("model_value and checked_elevation reject what cannot be a level",
          "[core][hwm]") {
  const auto negative_nan =
      std::bit_cast<double>(std::uint64_t{0xFFF8'0000'0000'0000});
  for (const double bad : {quiet_nan, negative_nan, infinity}) {
    CHECK(model_value(bad, LengthUnit::meter) ==
          std::unexpected{ElevationError::not_finite});
    CHECK(checked_elevation(bad, LengthUnit::foot) ==
          std::unexpected{ElevationError::not_finite});
  }
  CHECK(checked_elevation(-infinity, LengthUnit::meter) ==
        std::unexpected{ElevationError::not_finite});
  // -infinity is below the dry threshold, so the model value is dry.
  CHECK(model_value(-infinity, LengthUnit::meter) == WetDry{mov::core::Dry{}});
  for (const LengthUnit unit :
       {LengthUnit::meter, LengthUnit::foot, LengthUnit::nautical_mile}) {
    CHECK(model_value(1e308, unit) ==
          std::unexpected{ElevationError::out_of_range});
    CHECK(checked_elevation(-1e308, unit) ==
          std::unexpected{ElevationError::out_of_range});
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
