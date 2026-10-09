// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The two readers of station files that v5 did not write (WP10b): foreign CF
// discrete-sampling-geometry files (station_netcdf_foreign_open.cpp,
// station_netcdf_foreign_schema.cpp, station_netcdf_foreign_read.cpp; docs/
// station-netcdf.md 12 "Foreign") and the legacy v4 station netCDF
// (station_netcdf_legacy.cpp, SN 11). Each opens a file into its structure and
// catalog (what an inspect holds) and reads the samples of selected stations.
// Private to src/io/.

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <variant>
#include <vector>

#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/station_netcdf.hpp"
#include "station_netcdf_reader.hpp"

namespace mov::io::detail::station_nc {

// ---- legacy v4 (station_netcdf_legacy.cpp)
// -----------------------------------

/// The digits of a station number in variable names.
[[nodiscard]] constexpr int digits_of(StationNumberWidth width) noexcept {
  return width == StationNumberWidth::four ? 4 : 6;
}

/// An opened legacy file: the width of its station numbers, the epoch of each
/// station's time variable, and the catalog.
struct LegacyOpened {
  StationNumberWidth width;
  std::vector<core::Time> epochs;
  StationNcCatalog catalog;
};

[[nodiscard]] std::expected<Read<LegacyOpened>, Error> open_legacy(
    const nc::File& file, const StopToken& stop);

[[nodiscard]] std::expected<Read<core::StationTable>, Error> read_legacy(
    const nc::File& file, const LegacyOpened& opened,
    std::span<const std::size_t> selected, const StopToken& stop);

// ---- foreign CF (station_netcdf_foreign_*.cpp)
// ----------------------------------

// How the samples of a foreign file are laid out (CF 9.3). Each alternative
// holds what that layout has: the station dimension (a single station has
// none) and the variable that says where the samples of each station are.

/// `time(time)`, data over (station, time) or (time, station) (9.3.1).
struct Orthogonal {
  nc::DimInfo station;
};
/// `time(station, obs)` or `time(obs, station)`, padded with missing values
/// (9.3.2); `obs_count` says how many samples each station has, if the file
/// has it (else the leading non-missing times are counted).
struct Incomplete {
  nc::DimInfo station;
  std::optional<nc::VarInfo> obs_count;
};
/// `time(obs)`, a count per station (9.3.3).
struct ContiguousRagged {
  nc::DimInfo station;
  nc::VarInfo row_size;
};
/// `time(obs)`, the station of each sample (9.3.4).
struct IndexedRagged {
  nc::DimInfo station;
  nc::VarInfo index;
};
/// No station dimension: scalar coordinates, data over time (9.2).
struct SingleStation {};

using ForeignSampling = std::variant<Orthogonal, Incomplete, ContiguousRagged,
                                     IndexedRagged, SingleStation>;

/// The layout a sampling is, as StationFile::origin names it.
[[nodiscard]] CfDsgLayout layout_of(const ForeignSampling& sampling);

/// The station dimension; nullopt for a single station.
[[nodiscard]] std::optional<nc::DimInfo> station_dim_of(
    const ForeignSampling& sampling);

/// What a known quality scheme says about the values of a flag variable that
/// `ancillary_variables` links to a data variable.
struct QualityRules {
  nc::VarInfo var;
  /// Samples with these flags are Missing (QARTOD fail and missing data, or
  /// flag meanings that say bad, fail or missing).
  std::vector<std::int64_t> bad;
  /// Samples with these flags are kept, and counted in a warning.
  std::vector<std::int64_t> suspect;
};

/// A data variable and the quality rules that apply to it.
struct ForeignData {
  nc::VarInfo var;
  std::vector<QualityRules> quality;
};

/// Which variable is what in a foreign CF file.
struct ForeignStructure {
  ForeignSampling sampling;
  /// The sample (element) dimension: `time`, or `obs`.
  nc::DimInfo sample;
  nc::VarInfo lat;
  nc::VarInfo lon;
  nc::VarInfo time;
  /// The `cf_role` timeseries_id variable, if any, and the platform name.
  std::optional<nc::VarInfo> id;
  std::optional<nc::VarInfo> name;
  /// The data variables, in file order.
  std::vector<ForeignData> data;
};

/// A matrix layout (orthogonal, incomplete): the samples are the first
/// `counts` of each station's row or column of a variable over the station
/// dimension and the sample dimension, in either order.
struct Matrix {
  nc::DimInfo station;
};
/// Where the samples of each station start in the sample dimension
/// (contiguous ragged; one entry for a single station).
struct RunStarts {
  std::vector<std::size_t> starts;
};
/// The station of each sample (indexed ragged).
struct SampleOwners {
  std::vector<std::size_t> station_of_sample;
};
using Placement = std::variant<Matrix, RunStarts, SampleOwners>;

/// An opened foreign file: its structure, the samples of each station (and
/// where they are), and the catalog.
struct ForeignOpened {
  ForeignStructure s;
  std::vector<std::size_t> counts;
  Placement placement;
  StationNcCatalog catalog;
};

[[nodiscard]] std::expected<Read<ForeignOpened>, Error> open_foreign(
    const nc::File& file, CfVersion version, const StopToken& stop);

[[nodiscard]] std::expected<Read<core::StationTable>, Error> read_foreign(
    const nc::File& file, const ForeignOpened& opened,
    std::span<const std::size_t> selected, PaddingCheck padding,
    const StopToken& stop);

}  // namespace mov::io::detail::station_nc
