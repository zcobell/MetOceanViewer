// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Which reader a file belongs to, from its content (legacy-formats.md 1.1,
// docs/station-netcdf.md 12.1). Never from the file name: v4 decided by suffix
// (`.imeds`, `61`/`62`/`71`/`72`), and a renamed file was the wrong kind.

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string_view>

#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io {

enum class FileType : std::uint8_t {
  /// A station time series written by v5 (`read_station_netcdf`).
  station_netcdf,
  /// A CF discrete-sampling-geometry `timeSeries` file by another writer
  /// (`read_station_netcdf`).
  foreign_cf_netcdf,
  /// The legacy v4 station netCDF (`read_station_netcdf`). CRMS (dialect C) is
  /// not this: it is `unknown`.
  legacy_station_netcdf,
  /// ADCIRC netCDF station output (`read_adcirc_netcdf`).
  adcirc_netcdf,
  /// A D-Flow FM history file (`read_dflow`).
  dflow_netcdf,
  /// IMEDS text (`read_imeds`).
  imeds,
  /// ADCIRC ASCII station output, fort.61/62/71/72 (`read_adcirc_ascii`).
  adcirc_ascii,
  /// A high-water-mark CSV (`read_hwm_csv`).
  hwm_csv,
  unknown,
};

/// A stable lower-case identifier equal to the enumerator's name.
[[nodiscard]] std::string_view to_token(FileType type) noexcept;

/// The kind of file at `path`, by looking inside it.
///
/// netCDF is recognized by its magic bytes (`CDF\x01`, `CDF\x02`, `CDF\x05`
/// for the classic formats, `\x89HDF\r\n\x1a\n` for netCDF-4) and then by its
/// global attributes and variables, in this order: `metoceanviewer_format`
/// (a file naming another format than ours is `unknown`), global `model` =
/// "ADCIRC", the D-Flow FM station coordinate variables, `featureType`
/// timeSeries with CF-1.6 or later, and the legacy `time_station_0001`. Text is
/// recognized by its first lines (at most 64 KiB are read): IMEDS (a station
/// block after the three header lines), ADCIRC ASCII (the `NSnaps NStations
/// DT NSPOOL NCOLS` line) and the high-water-mark CSV (its rows, or a header
/// over them). Anything else, an empty file included, is `unknown`.
///
/// Errors: FileError when `path` is not a regular file that can be opened; an
/// NcError when the file has netCDF's magic bytes but netCDF-C cannot open it
/// (a truncated file, an HDF5 file that is not netCDF-4), or an attribute over
/// `limits.max_att_bytes`.
///
/// Blocking I/O: call it from a worker. netCDF-C is not thread-safe: the caller
/// serializes this with every other netCDF call (nc/file.hpp).
[[nodiscard]] std::expected<FileType, Error> detect_file_type(
    const std::filesystem::path& path, const ReadLimits& limits = {});

}  // namespace mov::io
