// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <optional>

#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/io/error.hpp"
#include "mov/io/projection.hpp"

// The FormatError for the failures a reader meets when it builds core values
// (design section 5.0). A reader that checks its input first cannot reach
// most of these, and maps them through here anyway, so that none is lost and
// no reader invents its own code.

namespace mov::io::detail {

/// A StationTable::make failure; the station and index are kept.
[[nodiscard]] FormatError to_format_error(const core::TableError& e);

/// A station id or name that core refused. `station` is the 0-based position
/// of the station in the file.
[[nodiscard]] FormatError to_format_error(core::StationKeyError e,
                                          std::size_t station);
[[nodiscard]] FormatError to_format_error(core::StationTextError e,
                                          std::size_t station);

/// A projection failure: an unknown CRS is `unsupported_crs`, a missing
/// database `projection_unavailable`, and a point PROJ could not transform
/// `bad_coordinates`. The subject is "EPSG:n".
[[nodiscard]] FormatError to_format_error(const ProjectionError& e,
                                          std::optional<std::size_t> station);

}  // namespace mov::io::detail
