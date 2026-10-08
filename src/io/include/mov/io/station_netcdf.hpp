// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The station time-series netCDF format of v5 (docs/station-netcdf.md, "SN"):
// CF-1.11 discrete sampling geometry, featureType timeSeries, in a netCDF-4
// file restricted to CF-classic constructs. This header has the v5 writer and
// the v5 reader (WP10a); the foreign CF and legacy dialect readers and file
// detection are WP10b.
//
// netCDF-C is not thread-safe: the caller serializes these functions with
// every other netCDF call (nc/file.hpp). Each opens its file once and closes
// it before it returns. Blocking I/O: call them from a worker.

#pragma once

#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/meta.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/io/detail/atomic_file.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io {

/// The `metoceanviewer_format` global attribute of a v5 file.
inline constexpr std::string_view station_nc_format = "station-timeseries";

/// `metoceanviewer_format_version` (SN 13): "<major>.<minor>". A reader reads
/// every minor of its major; a newer minor gives `minor_newer`.
struct StationNcVersion {
  unsigned major;
  unsigned minor;
  friend constexpr auto operator<=>(const StationNcVersion&,
                                    const StationNcVersion&) = default;
};

/// The version this writer writes and this reader implements.
inline constexpr StationNcVersion station_nc_version{.major = 1, .minor = 0};

/// "<major>.<minor>" with no leading zeros and at most 4 digits each, or
/// nullopt.
[[nodiscard]] std::optional<StationNcVersion> parse_station_nc_version(
    std::string_view text) noexcept;

/// The two layouts of one format (SN 2.2): `orthogonal` (CF 9.3.1, one
/// `time(time)` coordinate shared by every station) and `incomplete` (CF
/// 9.3.2, `time(station, obs)` padded with fill, plus `obs_count`).
enum class StationNcLayout : std::uint8_t { orthogonal, incomplete };

/// The layout the writer uses for `table`: orthogonal iff every station has
/// the same non-empty times (StationTable::single_axis), else incomplete. A
/// pure function of the data, so the same table always gets the same layout.
[[nodiscard]] StationNcLayout choose_layout(
    const core::StationTable& table) noexcept;

/// The global attributes the caller chooses (SN 5). Each is written with its
/// byte length; an absent optional is not written.
struct StationNcWriteOptions {
  std::string title{"MetOceanViewer station time series"};
  std::optional<std::string> institution{};
  std::optional<std::string> source{};
  std::optional<std::string> references{};
  std::optional<std::string> comment{};
  friend bool operator==(const StationNcWriteOptions&,
                         const StationNcWriteOptions&) = default;
};

/// Writes `table` as a v5 station file at `path`, atomically (a temporary
/// file beside it, renamed over it; on any error the old file is untouched
/// and no temporary file is left). `now` is the creation time recorded in
/// `date_created` and `history` (injected, so a write is reproducible).
///
/// What is written (SN 3-10):
///  - the layout of choose_layout; dimensions `station`, `time` (orthogonal)
///    or `obs` (incomplete), and one length dimension per char variable,
///    sized to the longest UTF-8 byte length present (at least 1);
///  - `station_id`, `station_name` (an empty name is written as
///    "Station <id>"), `station_provider` (only when a station has a source;
///    one without is an empty row), `lat`, `lon`, `crs` (WGS 84);
///  - `time` in milliseconds since 1970-01-01 as double; `obs_count` for the
///    incomplete layout;
///  - one double variable per schema column, named by its quantity token, with
///    `standard_name` (registry or generic; none for `difference` and
///    `value`), `long_name` (the label; the registry's long name or the token
///    when the label is empty), `units`, `units_metadata` on temperatures
///    ("temperature: on_scale", "temperature: difference" for a
///    `difference`), `vertical_datum` when the datum is engaged;
///  - `<token>_status` (byte; 0 dry, 1 wet, -128 unclassified) for a column
///    with at least one Dry sample, linked by `ancillary_variables`.
/// Missing and Dry samples are written as the _FillValue. Sample variables
/// are chunked (SN 3) and deflated (level 2, shuffle); padding is left
/// unwritten and reads back as fill.
///
/// Units: a registry quantity is stored in its canonical unit (SN 6),
/// converted exactly (core::conversion) when the column has another unit of
/// the same family. `difference` and generic quantities keep their unit
/// (udunits spelling; no `units` when there is none).
///
/// Errors: FormatError `empty_collection` (no stations, no columns, or no
/// samples at all: netCDF has no dimension of length 0), `noncanonical_unit`
/// (a registry quantity without a unit, or with one that does not convert to
/// the canonical unit; subject: the token), `invalid_variable_name` (a
/// generic token that is a name the format reserves, another column's status
/// variable, or longer than netCDF allows; subject: the token); FileError and
/// NcError from the atomic write.
///
/// Warnings (aggregate, in this order; what is written differs from what the
/// table holds):
///  - `unit_converted`: a column converted to its canonical unit (subject: the
///    token, count 1 per column);
///  - `station_name_substituted`: stations with an empty name (count);
///  - `native_position_dropped`: stations whose native point is not kept,
///    because the format stores WGS 84 only (SN 10.1, Q4; count);
///  - `value_reads_as_missing`: values written as the _FillValue (equal to it,
///    or not finite after the unit conversion), which every reader takes as
///    missing (subject: the token, count: samples).
[[nodiscard]] std::expected<Read<StationNcLayout>, Error> write_station_netcdf(
    const std::filesystem::path& path, const core::StationTable& table,
    const StationNcWriteOptions& options, core::Time now);

/// Every station of the file, in file order: what a caller asks for when it
/// wants the whole file without inspecting it first. It is explicit, never a
/// default (C12).
struct AllStations {
  friend constexpr bool operator==(AllStations, AllStations) = default;
};

/// Which stations to read: all of them, or a selection made for the file's
/// station count (inspect_station_netcdf gives the count).
using StationNcSelection = std::variant<AllStations, core::StationSelection>;

/// A file of this format, as written by v5 (or by any writer of the same
/// major version).
struct V5StationFile {
  /// The version the file declares (a newer minor reads with `minor_newer`).
  StationNcVersion version;
  StationNcLayout layout;
  core::StationTable table;
  friend bool operator==(const V5StationFile&, const V5StationFile&) = default;
};

/// What read_station_netcdf returns. WP10b adds the foreign CF and legacy
/// dialect files as alternatives.
using StationFile = std::variant<V5StationFile>;

/// What a v5 file holds, without its samples.
struct StationNcCatalog {
  StationNcVersion version;
  StationNcLayout layout;
  /// Every station, in file order.
  std::vector<core::FileStation> stations;
  /// The number of samples of each station (`obs_count`, or the length of
  /// `time` in the orthogonal layout), parallel to `stations`.
  std::vector<std::size_t> sample_counts;
  /// One entry per data variable, in file order: the schema a read returns.
  std::vector<core::SeriesMeta> schema;
  friend bool operator==(const StationNcCatalog&,
                         const StationNcCatalog&) = default;
};

/// The stations and the schema of a v5 file, read and validated as
/// read_station_netcdf does, without the time and data variables. Errors and
/// warnings are read_station_netcdf's, except the ones about samples.
[[nodiscard]] std::expected<Read<StationNcCatalog>, Error>
inspect_station_netcdf(const std::filesystem::path& path,
                       const ReadContext& ctx);

/// Reads the stations `which` of a v5 file into a table (stations in the order
/// of the selection; one shared axis in the orthogonal layout, one per
/// station in the incomplete one). Validation (SN 12, v5 column; every rule
/// is a hard error, none is downgraded):
///
///  - header: global `metoceanviewer_format` = "station-timeseries" (else
///    `not_this_format`: WP10b reads foreign and legacy files);
///    `metoceanviewer_format_version` present and "<major>.<minor>"
///    (`bad_version`), major 1 (`unsupported_version`); `Conventions` with a
///    CF-1.N token, N >= 6 (`missing_attribute`, `unsupported_version`);
///    `featureType` timeSeries, any case (`missing_attribute`,
///    `unsupported_layout`);
///  - structure: dimension `station` (`missing_dimension`); exactly one
///    variable with `cf_role` timeseries_id (`no_station_id`), char over
///    (station, n) (`bad_encoding`, `dimension_mismatch`); `station_name`,
///    `lat`, `lon` (`missing_variable`), `time` with `units` (a CF time unit,
///    else ParseError) and a supported `calendar` (`unsupported_calendar`);
///    `time(time)` for the orthogonal layout, `time(station, obs)` plus
///    `obs_count(station)` for the incomplete one (`unsupported_layout`,
///    `dimension_mismatch`, `missing_variable`); every variable over the sample
///    dimension is over (station, sample) in that order
///    (`dimension_mismatch`); at least one data variable
///    (`no_data_variables`); every `ancillary_variables` target that exists
///    has its data variable's dimensions (`bad_ancillary`);
///  - CRS (SN 10.1): the `grid_mapping` variable's `epsg_code` (EPSG:4326, or
///    another geographic code, projected here with the native point kept),
///    else `latitude_longitude` on the WGS 84 ellipsoid; no `grid_mapping`:
///    WGS 84 with `crs_assumed`; anything else `unsupported_crs`;
///  - values: ids unique (`duplicate_station_id`), non-empty
///    (`no_station_id`), UTF-8 without NUL after trailing NULs are cut
///    (`bad_encoding`), and so the names; `lat`/`lon` finite and in range
///    (`bad_coordinates`, with the station); times present
///    (`time_missing`), in range (`time_out_of_range`), strictly increasing
///    (`time_not_increasing`); `obs_count` in 0..obs (`bad_obs_count`); every
///    element past a station's count, of `time`, the data and the status, is
///    fill (`padding_not_missing`, with the station and index; checked over
///    the whole tail); a status flag is 0, 1 or its fill (`bad_flag`), a dry
///    sample has no value and a wet one has one (`wet_dry_inconsistent`).
///
/// A sample is Missing when it equals its variable's _FillValue (or the
/// library's default fill), is NaN, or is masked by `missing_value` or
/// `valid_*` (nc::Masking); it is Dry when its wet/dry status is 0. The
/// quantity is the variable name: a registry token, else a generic quantity
/// with the variable's `standard_name` (`unknown_quantity` unless it is
/// `value`; subject: the token). Labels are `long_name` (the token when there
/// is none), units `units` (parse_unit; `unrecognized_unit` for one outside
/// the unit table and the registry, subject: the unit), datums
/// `vertical_datum` (`datum_unknown`, and no datum, for an unknown token,
/// subject: the token, or for a datum on a quantity that cannot carry one,
/// subject: `<variable>:vertical_datum`).
///
/// Other errors: FormatError `station_count_mismatch` (a selection made for
/// another count); NcError (`too_large` when the selected samples exceed
/// ReadLimits); Cancelled. Warnings, in this order: `minor_newer` (subject:
/// the version), `crs_assumed`, `unknown_provider` (a `station_provider` token
/// core does not know: the station has no source; one warning per token,
/// counting its stations), `crs_approximate`, per data variable
/// `unknown_quantity`, `unrecognized_unit`, `datum_unknown`, and last
/// parse_cf_time_units's for `time:units`.
[[nodiscard]] std::expected<Read<StationFile>, Error> read_station_netcdf(
    const std::filesystem::path& path, const StationNcSelection& which,
    const ReadContext& ctx);

namespace detail {

/// write_station_netcdf with the stages of the atomic write exposed to a
/// FaultInjector (tests: a failure at any stage leaves the old file).
[[nodiscard]] std::expected<Read<StationNcLayout>, Error> write_station_netcdf(
    const std::filesystem::path& path, const core::StationTable& table,
    const StationNcWriteOptions& options, core::Time now,
    const FaultInjector& fault);

}  // namespace detail

}  // namespace mov::io
