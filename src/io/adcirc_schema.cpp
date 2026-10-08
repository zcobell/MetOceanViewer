// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/detail/adcirc_schema.hpp"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/units.hpp"
#include "mov/io/adcirc_ascii.hpp"

namespace mov::io::detail {

namespace {

core::SeriesMeta meta_of(core::Quantity q, core::Unit unit) {
  return core::SeriesMeta::make({.quantity = q,
                                 .label = std::string{core::info(q).long_name},
                                 .unit = std::move(unit)});
}

// A component along a grid axis: a generic quantity named as CF names it.
core::SeriesMeta grid_meta(std::string_view token, std::string label,
                           core::Unit unit) {
  // The tokens are valid and not in the registry (a test pins that).
  return core::SeriesMeta::make(
      {.quantity = core::GenericQuantity::parse(
                       {.token = token, .standard_name = token})
                       .value_or(core::GenericQuantity::value()),
       .label = std::move(label),
       .unit = std::move(unit)});
}

}  // namespace

std::vector<core::SeriesMeta> adcirc_schema(AdcircKind kind, CrsKind grid) {
  using core::Quantity;
  const core::Unit metre{core::LengthUnit::meter};
  const core::Unit speed{core::SpeedUnit::meter_per_second};
  switch (kind) {
    case AdcircKind::elevation:
      return {meta_of(Quantity::water_level, metre)};
    case AdcircKind::velocity:
      if (grid == CrsKind::projected) {
        return {
            grid_meta("sea_water_x_velocity", "grid-relative current x", speed),
            grid_meta("sea_water_y_velocity", "grid-relative current y",
                      speed)};
      }
      return {meta_of(Quantity::current_u, speed),
              meta_of(Quantity::current_v, speed)};
    case AdcircKind::pressure:
      return {meta_of(Quantity::air_pressure,
                      core::Unit{core::PressureUnit::meter_of_water})};
    case AdcircKind::wind:
      if (grid == CrsKind::projected) {
        return {grid_meta("x_wind", "grid-relative wind x", speed),
                grid_meta("y_wind", "grid-relative wind y", speed)};
      }
      return {meta_of(Quantity::wind_u, speed),
              meta_of(Quantity::wind_v, speed)};
  }
  return {};
}

}  // namespace mov::io::detail
