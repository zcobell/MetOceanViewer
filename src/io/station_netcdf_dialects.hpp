// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The two readers of station files that v5 did not write (WP10b): foreign CF
// discrete-sampling-geometry files (station_netcdf_foreign.cpp, docs/
// station-netcdf.md 12 "Foreign") and the legacy v4 station netCDF
// (station_netcdf_legacy.cpp, SN 11). Each opens a file into its structure and
// catalog (what an inspect holds) and reads the samples of selected stations.
// Private to src/io/.

#pragma once

#include <cstddef>
#include <expected>
#include <optional>
#include <span>
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

/// An opened legacy file: the digits of its station numbers, the epoch of
/// each station's time variable, and the catalog.
struct LegacyOpened {
  int width;
  std::vector<core::Time> epochs;
  StationNcCatalog catalog;
};

[[nodiscard]] std::expected<Read<LegacyOpened>, Error> open_legacy(
    const nc::File& file, const StopToken& stop);

[[nodiscard]] std::expected<Read<core::StationTable>, Error> read_legacy(
    const nc::File& file, const LegacyOpened& opened,
    std::span<const std::size_t> selected, const StopToken& stop);

// ---- foreign CF (station_netcdf_foreign_open.cpp, ..._read.cpp)
// --------------

/// Which variable is what in a foreign CF file, and how its samples are laid
/// out (CF 9.3).
struct ForeignStructure {
  CfDsgLayout layout;
  /// The station (instance) dimension; nullopt for `single_station`.
  std::optional<nc::DimInfo> station;
  /// The sample (element) dimension: `time`, or `obs` of a ragged layout.
  nc::DimInfo sample;
  nc::VarInfo lat;
  nc::VarInfo lon;
  nc::VarInfo time;
  /// The `cf_role` timeseries_id variable and the platform name, if any.
  std::optional<nc::VarInfo> id;
  std::optional<nc::VarInfo> name;
  /// Contiguous ragged: the count variable (`sample_dimension`). Indexed
  /// ragged: the index variable (`instance_dimension`). Incomplete: the
  /// `obs_count` helper, if the file has one.
  std::optional<nc::VarInfo> row_size;
  std::optional<nc::VarInfo> index;
  std::optional<nc::VarInfo> obs_count;
  /// The data variables, in file order.
  std::vector<nc::VarInfo> data;
};

/// An opened foreign file: its structure, the samples of each station (and
/// where they are), and the catalog.
struct ForeignOpened {
  ForeignStructure s;
  std::vector<std::size_t> counts;
  /// Contiguous ragged: the first sample of each station in `obs`.
  std::vector<std::size_t> offsets;
  /// Indexed ragged: the station of each sample.
  std::vector<std::size_t> index;
  StationNcCatalog catalog;
};

[[nodiscard]] std::expected<Read<ForeignOpened>, Error> open_foreign(
    const nc::File& file, const StopToken& stop);

[[nodiscard]] std::expected<Read<core::StationTable>, Error> read_foreign(
    const nc::File& file, const ForeignOpened& opened,
    std::span<const std::size_t> selected, const StopToken& stop);

}  // namespace mov::io::detail::station_nc
