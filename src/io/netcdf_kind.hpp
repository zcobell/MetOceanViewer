// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Which of the netCDF files mov::io reads an open netCDF file is, from its
// content only (global attributes and variable names, never the file name):
// the one rule detect_file_type and the station netCDF reader share. Private
// to src/io/.

#pragma once

#include <cstdint>
#include <expected>

#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"

namespace mov::io::detail {

enum class NetcdfKind : std::uint8_t {
  /// Global `metoceanviewer_format` = "station-timeseries" (SN 12.1). A file
  /// with that attribute set to anything else is `other`, not guessed to be a
  /// foreign CF file.
  station_v5,
  /// Global `model` = "ADCIRC" (legacy-formats.md 1.1).
  adcirc,
  /// Variables `station_x_coordinate` and `station_y_coordinate`.
  dflow,
  /// `featureType` timeSeries and `Conventions` naming CF-1.6 or later.
  foreign_cf,
  /// Variable `time_station_0001` (v4's generic station netCDF), or the
  /// six-digit `time_station_000001` of a file that also has `numStations` and
  /// `stationXCoordinate`; not CRMS (dialect C has neither).
  legacy_station,
  /// A netCDF file of none of these kinds.
  other,
};

/// The kind of `file`. The order is SN 12.1's (v5, foreign CF, legacy) with the
/// two model-output kinds first among the others, because ADCIRC and D-Flow FM
/// files can carry CF attributes too and must keep their own readers.
///
/// An attribute that is not text is not that kind (so is not an error); an
/// attribute over ReadLimits::max_att_bytes is an NcError `too_large`.
[[nodiscard]] std::expected<NetcdfKind, Error> classify_netcdf(
    const nc::File& file);

}  // namespace mov::io::detail
