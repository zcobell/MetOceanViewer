// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/io/adcirc_ascii.hpp"
#include "mov/io/cf_time.hpp"
#include "mov/io/detail/station_groups.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io {

// ADCIRC's netCDF station output (fort.61.nc, fort.62.nc, fort.71.nc,
// fort.72.nc; LF section 3.2): dimensions `time` and `station`; variables
// `time(time)`, `x(station)`, `y(station)`, optionally `station_name(station,
// namelen)`, and the data, `(time, station)`: `zeta` (61), `u-vel` and `v-vel`
// (62), `pressure` (71), `windx` and `windy` (72). The global attribute
// `model` is "ADCIRC".
//
// netCDF-C is not thread-safe: the caller serializes these functions with
// every other netCDF call (nc/file.hpp). Each opens the file once and closes
// it before it returns. Blocking I/O: call them from a worker.

/// The names of the data variables of a kind: `zeta`; `u-vel`, `v-vel`;
/// `pressure`; `windx`, `windy`. The columns of the table read have the same
/// number, in this order.
[[nodiscard]] std::vector<std::string> adcirc_variables(AdcircKind kind);

/// The `units` attribute of `time`, as written, and what it says when it is a
/// CF time unit with a date. ADCIRC writes "seconds since <NCDATE>" and, when
/// the run had no usable NCDATE, a placeholder such as "seconds since Met"
/// (`parsed` is then empty).
struct TimeUnitsAttr {
  std::string text;
  std::optional<CfTimeUnits> parsed;
  friend bool operator==(const TimeUnitsAttr&, const TimeUnitsAttr&) = default;
};

/// What a station output file holds.
struct AdcircNcCatalog {
  /// The first of zeta, u-vel, pressure, windx the file has (that order);
  /// adcirc_variables(kind) names its data variables.
  AdcircKind kind;
  /// Every station, in file order: id the 0-based index, name from
  /// `station_name` (empty without one), position projected to WGS84.
  std::vector<core::FileStation> stations;
  /// The length of `time` (the unlimited dimension's current length).
  std::size_t times;
  /// The `units` attribute of `time`, if the file has a text one.
  std::optional<TimeUnitsAttr> time_units;
  friend bool operator==(const AdcircNcCatalog&,
                         const AdcircNcCatalog&) = default;
};

/// The structure of a station output file, without its data.
///
/// `crs` is the CRS of `x` and `y` (the mesh's); the positions become WGS84
/// here, and a station in another CRS keeps its native point. The file does
/// not say which CRS that is.
///
/// Errors: FormatError `not_this_format` (no global `model` = "ADCIRC", or
/// only `v-vel` / `windy` of a vector), `missing_dimension` (`time`,
/// `station`), `missing_variable` (`time`, `x`, `y`, or none of the data
/// variables), `partner_variable_missing` (`u-vel` without `v-vel`, `windx`
/// without `windy`), `dimension_mismatch` (a variable not over the dimensions
/// named above, in that order), `unsupported_crs`, `projection_unavailable`,
/// `bad_coordinates` (a position that PROJ cannot transform or that is not a
/// Location, with the station); NcError (including `too_large`);
/// Cancelled. Warnings: `invalid_utf8_replaced` (names), `crs_approximate`,
/// `crs_mismatch` (the global `ics` says Cartesian and `crs` is geographic,
/// or spherical and `crs` is projected: one of them is wrong), and the ones
/// of parse_cf_time_units for `time:units`. The whole file's stations are
/// read (every coordinate and name), however few are selected.
[[nodiscard]] std::expected<Read<AdcircNcCatalog>, Error> inspect_adcirc_netcdf(
    const std::filesystem::path& path, core::Epsg crs, const ReadContext& ctx);

/// What to read. `kind` says which data variables (the file must have them),
/// `crs` is the CRS of `x` and `y`, and `stations` is required (never an
/// implicit "all"); it must be made for the file's station count.
struct AdcircNcRequest {
  AdcircKind kind;
  /// The start of the model clock. ADCIRC counts seconds from its cold start,
  /// and the file's own record of it (NCDATE) is often a placeholder: when
  /// this is given, `time` is that many seconds after it, whatever `units`
  /// says; if `units` parses and its epoch is more than a second from this,
  /// `cold_start_differs` says so. Otherwise the unit and epoch of `units`
  /// are used, with an `epoch_used` warning; if `units` is absent or no CF
  /// time unit (a placeholder), `cold_start_required`.
  std::optional<core::Time> cold_start;
  core::Epsg crs;
  core::StationSelection stations;
  friend bool operator==(const AdcircNcRequest&,
                         const AdcircNcRequest&) = default;
};

/// The selected stations of a station output as a table with one shared time
/// axis and the schema of `adcirc_schema(kind, grid)`: `water_level` (m);
/// `current_u`, `current_v` (m s-1); `air_pressure` (m of water); `wind_u`,
/// `wind_v` (m s-1). If `crs` is projected the vector components are along the
/// grid's axes, not east and north, and are the generic
/// `sea_water_x_velocity`, `sea_water_y_velocity` and `x_wind`, `y_wind`
/// (labels "grid-relative ..."; pair them with
/// VectorSeries::assume_components). The stations follow the order of the
/// selection.
///
/// Values, in the data variable's own type (float, double or a packed integer;
/// never 64-bit integers: `type_mismatch`), with _FillValue, the library's
/// default fill, missing_value, valid_min/max/range and scale_factor/add_offset
/// honoured (Masking):
///  - elevation (`zeta`): an (unpacked) value at or below -999 is `Dry`
///    (ADCIRC writes -99999 for a dry node), whatever the _FillValue says,
///    so the fill is Dry and not Missing; any other masked value is `Missing`;
///  - every other output: a value at or below -999 is fill, so `Missing`, and
///    a fill in either component of a vector makes both `Missing` (v4 tested
///    only the first);
///  - NaN and infinities are `Missing` and counted in `nonfinite_masked`
///    (subject: the first variable that had one).
///
/// The data is read in blocks of whole time steps (File::read_blocks) over
/// the stations of a group, and gathered. Selected stations in the same chunk
/// column of the file share a read; stations in different columns do not
/// (detail::station_groups, GroupingPolicy): on the layout ADCIRC writes,
/// 1000 stations read as one block in 0.5 s against 85 s read one by one.
///
/// Errors (those of inspect_adcirc_netcdf, and): FormatError
/// `station_count_mismatch` (the selection was made for another count),
/// `cold_start_required`, `unsupported_calendar` (the epoch of `units` is
/// before 1582-10-15 in the standard calendar), `time_missing` /
/// `time_out_of_range` / `time_not_increasing` (with the index of the time),
/// NcError `too_large` (selected stations x times x columns over
/// `ReadLimits::max_elements`, or their samples over `max_result_bytes`).
/// Warnings: those of inspect_adcirc_netcdf, and `epoch_used`,
/// `cold_start_differs`, `nonfinite_masked`.
[[nodiscard]] std::expected<Read<core::StationTable>, Error> read_adcirc_netcdf(
    const std::filesystem::path& path, const AdcircNcRequest& request,
    const ReadContext& ctx);

namespace detail {

/// read_adcirc_netcdf with the grouping of the selected stations into reads
/// chosen by the caller (tests/io/measure_adcirc_netcdf.cpp, which compares
/// the groupings, and the tests); nullopt is what read_adcirc_netcdf does:
/// grouping_for the chunk shape of the data variable.
[[nodiscard]] std::expected<Read<core::StationTable>, Error> read_adcirc_netcdf(
    const std::filesystem::path& path, const AdcircNcRequest& request,
    const ReadContext& ctx, std::optional<GroupingPolicy> policy);

}  // namespace detail

}  // namespace mov::io
