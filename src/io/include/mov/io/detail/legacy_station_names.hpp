// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The variable names of the legacy v4 station netCDF (docs/legacy-formats.md
// section 5): `time_station_0001`, `data_station_0001`, `stationLength_0001`.
// Private to mov::io (public only because the tests include it).

#pragma once

#include <cstddef>
#include <format>
#include <string>
#include <string_view>

namespace mov::io::detail {

/// `<prefix><number>` with the 1-based station `number` zero-padded to
/// `width` digits, and wider when it needs more (v4's `%04i`, which does not
/// truncate: station 10000 is `time_station_10000`).
[[nodiscard]] inline std::string legacy_variable_name(std::string_view prefix,
                                                      std::size_t number,
                                                      int width) {
  return std::format("{}{:0{}}", prefix, number, width);
}

}  // namespace mov::io::detail
