// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Which of the netCDF files mov::io reads an open netCDF file is, from its
// content only (global attributes and variable names, never the file name):
// the one rule detect_file and the station netCDF reader share. Private to
// src/io/.

#pragma once

#include <expected>
#include <string>
#include <variant>

#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/station_netcdf.hpp"

namespace mov::io::detail {

namespace kind {

/// Global `metoceanviewer_format` = "station-timeseries" (SN 12.1).
struct StationV5 {};
/// Global `model` = "ADCIRC" (legacy-formats.md 1.1).
struct Adcirc {};
/// Variables `station_x_coordinate` and `station_y_coordinate`.
struct Dflow {};
/// `featureType` timeSeries and `Conventions` naming CF-1.6 or later (CF 1;
/// the version is the first such token, not an assumption).
struct ForeignCf {
  CfVersion version;
};
/// Variable `time_station_0001` (v4's generic station netCDF), or the
/// six-digit `time_station_000001` of a file that also has `numStations` and
/// `stationXCoordinate`; not CRMS (dialect C has neither).
struct LegacyStation {};
/// A file that names its own format, and it is not ours: another or a newer
/// MetOceanViewer format (a future major version may keep the CF attributes,
/// so it is not guessed to be a foreign CF file).
struct OtherFormat {
  /// The attribute's text, trimmed.
  std::string name;
};
/// A netCDF file of none of these kinds. `subject` is the attribute that came
/// closest to deciding (`:featureType`, or `:Conventions` when the
/// featureType fits but the CF version does not).
struct Unrecognized {
  std::string subject;
};

}  // namespace kind

using NetcdfKind =
    std::variant<kind::StationV5, kind::Adcirc, kind::Dflow, kind::ForeignCf,
                 kind::LegacyStation, kind::OtherFormat, kind::Unrecognized>;

/// The kind of `file`. The order is SN 12.1's (v5, foreign CF, legacy) with the
/// two model-output kinds first among the others, because ADCIRC and D-Flow FM
/// files can carry CF attributes too and must keep their own readers.
///
/// An attribute that is not text is not that kind (so is not an error); an
/// attribute over ReadLimits::max_att_bytes is an NcError `too_large`.
[[nodiscard]] std::expected<NetcdfKind, Error> classify_netcdf(
    const nc::File& file);

}  // namespace mov::io::detail
