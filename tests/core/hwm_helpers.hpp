// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Builders shared by the HWM tests. Constexpr, so STATIC_REQUIRE can use them.

#pragma once

#include "mov/core/geo.hpp"
#include "mov/core/hwm.hpp"
#include "mov/core/units.hpp"

namespace mov::test {

[[nodiscard]] constexpr mov::core::Length metres(double v) noexcept {
  return mov::core::Length::in(v, mov::core::LengthUnit::meter);
}

[[nodiscard]] constexpr mov::core::Length feet(double v) noexcept {
  return mov::core::Length::in(v, mov::core::LengthUnit::foot);
}

[[nodiscard]] constexpr double in_metres(mov::core::Length v) noexcept {
  return v.as(mov::core::LengthUnit::meter);
}

/// A fixed, valid position: the statistics do not look at it.
[[nodiscard]] constexpr mov::core::Location gulf_coast() noexcept {
  // The coordinates are in range, so make succeeds.
  return *mov::core::Location::make({.lat = 29.98, .lon = -90.01});
}

/// A mark in metres. `modeled_raw` goes through model_value, so a value at or
/// below the dry threshold is a Dry mark exactly as a file reader builds it.
[[nodiscard]] constexpr mov::core::HighWaterMark mark_m(
    double observed, double modeled_raw) noexcept {
  return mov::core::HighWaterMark{
      .location = gulf_coast(),
      .ground = metres(1.0),
      .observed = metres(observed),
      .modeled = mov::core::model_value(modeled_raw,
                                        mov::core::LengthUnit::meter)};
}

/// The same, with every value in feet.
[[nodiscard]] constexpr mov::core::HighWaterMark mark_ft(
    double observed, double modeled_raw) noexcept {
  return mov::core::HighWaterMark{
      .location = gulf_coast(),
      .ground = feet(1.0),
      .observed = feet(observed),
      .modeled = mov::core::model_value(modeled_raw,
                                        mov::core::LengthUnit::foot)};
}

}  // namespace mov::test
