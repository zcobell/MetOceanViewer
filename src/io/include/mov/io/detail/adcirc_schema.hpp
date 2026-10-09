// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The columns an ADCIRC station output has, whichever file it comes from.
// Private to mov::io (public only because the tests include it).

#pragma once

#include <vector>

#include "mov/core/meta.hpp"
#include "mov/io/adcirc_ascii.hpp"
#include "mov/io/projection.hpp"

namespace mov::io::detail {

/// The schema of a table read from an ADCIRC output of `kind` (design 5.3):
/// `water_level` (m); `current_u`, `current_v` (m s-1); `air_pressure` (m of
/// water); `wind_u`, `wind_v` (m s-1). Labels are the registry long names. No
/// datum. The ASCII and the netCDF reader share it, so the same run read from
/// either file gives equal tables.
///
/// The vector components of a model on a projected grid point along the grid's
/// axes, not east and north, so for `grid == CrsKind::projected` they are not
/// the registry's eastward and northward quantities but generic ones with CF's
/// grid names: `sea_water_x_velocity`,
/// `sea_water_y_velocity` and `x_wind`, `y_wind` (labels "grid-relative ...").
/// Rotating them by the meridian convergence is deferred.
[[nodiscard]] std::vector<core::SeriesMeta> adcirc_schema(AdcircKind kind,
                                                          CrsKind grid);

}  // namespace mov::io::detail
