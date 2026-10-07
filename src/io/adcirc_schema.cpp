// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/detail/adcirc_schema.hpp"

#include <string>
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

}  // namespace

std::vector<core::SeriesMeta> adcirc_schema(AdcircKind kind) {
  using core::Quantity;
  const core::Unit metre{core::LengthUnit::meter};
  const core::Unit speed{core::SpeedUnit::meter_per_second};
  switch (kind) {
    case AdcircKind::elevation:
      return {meta_of(Quantity::water_level, metre)};
    case AdcircKind::velocity:
      return {meta_of(Quantity::current_u, speed),
              meta_of(Quantity::current_v, speed)};
    case AdcircKind::pressure:
      return {meta_of(Quantity::air_pressure,
                      core::Unit{core::PressureUnit::meter_of_water})};
    case AdcircKind::wind:
      return {meta_of(Quantity::wind_u, speed),
              meta_of(Quantity::wind_v, speed)};
  }
  return {};
}

}  // namespace mov::io::detail
