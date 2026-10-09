// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Which reader a file belongs to, from its content (legacy-formats.md 1.1,
// docs/station-netcdf.md 12.1). Never from the file name: v4 decided by suffix
// (`.imeds`, `61`/`62`/`71`/`72`), and a renamed file was the wrong kind.

#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <variant>

#include "mov/core/overloaded.hpp"
#include "mov/io/adcirc_ascii.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/station_netcdf.hpp"

namespace mov::io {

enum class FileType : std::uint8_t {
  /// A station time series written by v5 (`read_station_netcdf`).
  station_netcdf,
  /// A CF discrete-sampling-geometry `timeSeries` file by another writer
  /// (`read_station_netcdf`).
  foreign_cf_netcdf,
  /// The legacy v4 station netCDF (`read_station_netcdf`). CRMS (dialect C) is
  /// not this: it is `unsupported_netcdf`.
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
  /// A text or binary file none of the text readers recognizes, an empty file
  /// included.
  unrecognized_text,
  /// A netCDF file of none of the kinds above: a reader for it may not exist,
  /// or the file is another convention's.
  unsupported_netcdf,
  /// A netCDF file that names a MetOceanViewer format this version does not
  /// read (another one, or a newer major version of the station format).
  other_format_netcdf,
};

/// A stable lower-case identifier equal to the enumerator's name.
[[nodiscard]] std::string_view to_token(FileType type) noexcept;

/// The kind of file a station netCDF read gave, as detect_file would have
/// said: total, so a reader that gains an origin does not compile until its
/// kind is named.
[[nodiscard]] constexpr FileType file_type_of(
    const StationFileOrigin& origin) noexcept {
  return std::visit(
      core::Overloaded{
          [](const V5Origin&) { return FileType::station_netcdf; },
          [](const ForeignCfOrigin&) { return FileType::foreign_cf_netcdf; },
          [](const LegacyOrigin&) { return FileType::legacy_station_netcdf; }},
      origin);
}

/// ADCIRC ASCII output, with the header the file has (so the caller knows the
/// shape of the data and offers only the kinds that fit).
struct AdcircAsciiDetected {
  AdcircAsciiHeader header;
  friend constexpr bool operator==(const AdcircAsciiDetected&,
                                   const AdcircAsciiDetected&) = default;
};

/// A netCDF file that names another MetOceanViewer format.
struct OtherFormatDetected {
  /// The global `metoceanviewer_format` attribute, trimmed and cut to
  /// ParseError::max_context_bytes.
  std::string name;
  friend bool operator==(const OtherFormatDetected&,
                         const OtherFormatDetected&) = default;
};

/// What detect_file found: a FileType for a kind with nothing to add (never
/// `adcirc_ascii` or `other_format_netcdf`, which have their own alternatives
/// and what they carry).
using FileDetection =
    std::variant<FileType, AdcircAsciiDetected, OtherFormatDetected>;

/// The FileType of a detection: total, like the one above.
[[nodiscard]] constexpr FileType file_type_of(
    const FileDetection& detection) noexcept {
  return std::visit(core::Overloaded{[](FileType type) { return type; },
                                     [](const AdcircAsciiDetected&) {
                                       return FileType::adcirc_ascii;
                                     },
                                     [](const OtherFormatDetected&) {
                                       return FileType::other_format_netcdf;
                                     }},
                    detection);
}

/// The kind of file at `path`, by looking inside it.
///
/// netCDF is recognized by its magic bytes (`CDF\x01`, `CDF\x02`, `CDF\x05`
/// for the classic formats, `\x89HDF\r\n\x1a\n` for netCDF-4, which may follow
/// a user block of 512 * 2^k bytes) and then by its global attributes and
/// variables, in this order: `metoceanviewer_format` (a file naming another
/// format than ours is `other_format_netcdf`, with the name), global `model` =
/// "ADCIRC", the D-Flow FM station coordinate variables, `featureType`
/// timeSeries with CF-1.6 or later, and the legacy `time_station_0001`. Any
/// other netCDF file is `unsupported_netcdf`. Text is recognized by its first
/// lines (at most 64 KiB are read): IMEDS (a first line `% IMEDS ...`, or a
/// station block after the three header lines), ADCIRC ASCII (the `NSnaps
/// NStations DT NSPOOL NCOLS` line, whose header is returned) and the
/// high-water-mark CSV (its rows, or a header over them). Anything else, an
/// empty file included, is `unrecognized_text`.
///
/// Errors: FileError when `path` is not a regular file that can be opened; an
/// NcError when the file has netCDF's magic bytes but netCDF-C cannot open it
/// (a truncated file, an HDF5 file that is not netCDF-4), or an attribute over
/// `limits.max_att_bytes`.
///
/// Blocking I/O: call it from a worker. netCDF-C is not thread-safe: the caller
/// serializes this with every other netCDF call (nc/file.hpp).
[[nodiscard]] std::expected<FileDetection, Error> detect_file(
    const std::filesystem::path& path, const ReadLimits& limits = {});

/// detect_file, and only the kind.
[[nodiscard]] std::expected<FileType, Error> detect_file_type(
    const std::filesystem::path& path, const ReadLimits& limits = {});

}  // namespace mov::io
