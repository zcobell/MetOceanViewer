// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The station time-series netCDF format of v5 (docs/station-netcdf.md, "SN"):
// CF-1.11 discrete sampling geometry, featureType timeSeries, in a netCDF-4
// file restricted to CF-classic constructs. This header has the v5 writer and
// read_station_netcdf, which reads a v5 file (WP10a), a foreign CF discrete
// sampling geometry `timeSeries` file or a legacy v4 station file (WP10b).
// What kind of file a path is, before any reader is chosen, is file_type.hpp's
// detect_file_type.
//
// netCDF-C is not thread-safe: the caller serializes these functions with
// every other netCDF call (nc/file.hpp). Each opens its file once and closes
// it before it returns. Blocking I/O: call them from a worker.

#pragma once

#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/meta.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/io/detail/atomic_file.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"

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

/// A CF version, as a `CF-<major>.<minor>` token of `Conventions` names it.
/// Not a StationNcVersion: the two evolve independently (SN 13).
struct CfVersion {
  unsigned major;
  unsigned minor;
  friend constexpr auto operator<=>(const CfVersion&,
                                    const CfVersion&) = default;
};

/// The CF version of a `Conventions` attribute: its first `CF-<major>.<minor>`
/// token (tokens are separated by blanks or commas, CF 2.6.1), or nullopt if
/// it has none.
[[nodiscard]] std::optional<CfVersion> parse_cf_conventions(
    std::string_view conventions) noexcept;

/// The two layouts of one format (SN 2.2): `orthogonal` (CF 9.3.1, one
/// `time(time)` coordinate shared by every station) and `incomplete` (CF
/// 9.3.2, `time(station, obs)` padded with fill, plus `obs_count`). The
/// foreign CF layouts are WP10b's own enumeration.
enum class StationNcLayout : std::uint8_t { orthogonal, incomplete };

/// The layout the writer uses for `table`: orthogonal iff every station has
/// the same non-empty times (StationTable::single_axis), else incomplete. A
/// pure function of the data, so the same table always gets the same layout.
[[nodiscard]] StationNcLayout choose_layout(
    const core::StationTable& table) noexcept;

/// The global attributes the caller chooses (SN 5). Every text must be UTF-8
/// without NUL and at most max_option_bytes long; `title` must not be empty,
/// and an empty optional text counts as absent (it is not written).
struct StationNcWriteOptions {
  static constexpr std::size_t max_option_bytes = std::size_t{1} << 16;
  std::string title{"MetOceanViewer station time series"};
  std::optional<std::string> institution{};
  std::optional<std::string> source{};
  std::optional<std::string> references{};
  std::optional<std::string> comment{};
  friend bool operator==(const StationNcWriteOptions&,
                         const StationNcWriteOptions&) = default;
};

/// Whether write_station_netcdf would write `table` with `options`, and what
/// it would write differently: the writer's checks and warnings without a
/// path, a file or I/O (for the UI, before it asks where to save). Pure.
///
/// Errors (FormatError):
///  - `empty_collection` (no stations), `no_data_variables` (no columns),
///    `no_samples` (no station has a sample: netCDF has no dimension of
///    length 0);
///  - `too_many_samples`: a station with more samples than `obs_count` (an
///    int) can count (subject: the station id);
///  - `noncanonical_unit`: a registry quantity other than `difference` without
///    a unit, or with one that does not convert to its canonical unit (SN 6;
///    subject: the token);
///  - `invalid_variable_name`: a generic token that is a name the format
///    uses (a variable or dimension of SN 4, or `<token>_status` of any column:
///    every column's status name is reserved, whether or not it has Dry
///    samples), or too long for netCDF with `_status` appended (subject: the
///    token);
///  - `bad_option`: an option text that is not UTF-8, holds a NUL, is longer
///    than max_option_bytes, or an empty title (subject: `:<attribute>`).
///
/// Warnings (aggregate, in this order; what is written differs from what the
/// table holds):
///  - `unit_converted`: a column converted to its canonical unit (subject: the
///    token, count 1 per column);
///  - `station_name_substituted`: stations with an empty name, written as
///    "Station <id>" (count);
///  - `native_position_dropped`: stations whose native point is not kept,
///    because the format stores WGS 84 only (SN 10.1, Q4; count);
///  - `value_reads_as_missing`: values written as the _FillValue (equal to it,
///    or not finite after the unit conversion), which every reader takes as
///    missing (subject: the token, count: samples).
[[nodiscard]] std::expected<std::vector<Warning>, Error>
validate_station_netcdf(const core::StationTable& table,
                        const StationNcWriteOptions& options);

/// Writes `table` as a v5 station file at `path`, atomically (a temporary
/// file beside it, renamed over it; on any error the old file is untouched
/// and no temporary file is left). Everything validate_station_netcdf checks
/// is checked before the file is created; its warnings are the result. `now`
/// is the creation time recorded in `date_created` and `history` (injected,
/// so a write is reproducible: the same table, options and time give the same
/// bytes).
///
/// What is written (SN 3-10):
///  - the layout of choose_layout; dimensions `station`, `time` (orthogonal)
///    or `obs` (incomplete), and one length dimension per char variable,
///    sized to the longest UTF-8 byte length present (at least 1);
///  - `station_id`, `station_name`, `station_provider` (only when a station
///    has a source; one without is an empty row), `lat`, `lon`, `crs`
///    (WGS 84);
///  - `time` in milliseconds since 1970-01-01 as double; `obs_count` for the
///    incomplete layout;
///  - one double variable per schema column, named by its quantity token, with
///    `standard_name` (registry or generic; none for `difference` and
///    `value`), `long_name` (the label; the registry's long name or the token
///    when the label is empty), `units`, `units_metadata` on temperature
///    units (CF 1.11 3.1.2: "temperature: on_scale" for registry quantities,
///    "temperature: difference" for `difference`, "temperature: unknown" for
///    generic ones), `vertical_datum` when the datum is engaged;
///  - `<token>_status` (byte; 0 dry, 1 wet, -128 unclassified) for a column
///    with at least one Dry sample, linked by `ancillary_variables`.
/// Missing and Dry samples are written as the _FillValue. Sample variables
/// are chunked (SN 3) and deflated (level 2, shuffle); padding is left
/// unwritten and reads back as fill. Registry quantities are stored in their
/// canonical unit, converted exactly (core::conversion); `difference` and
/// generic quantities keep theirs (udunits spelling; no `units` when there
/// is none).
///
/// Errors: validate_station_netcdf's, then FileError and NcError from the
/// atomic write. Warnings: validate_station_netcdf's.
[[nodiscard]] std::expected<std::vector<Warning>, Error> write_station_netcdf(
    const std::filesystem::path& path, const core::StationTable& table,
    const StationNcWriteOptions& options, std::chrono::sys_seconds now);

/// Every station of the file, in file order: what a caller asks for when it
/// wants the whole file without inspecting it first. It is explicit, never a
/// default (C12).
struct AllStations {
  friend constexpr bool operator==(AllStations, AllStations) = default;
};

/// Which stations to read: all of them, or a selection made for the file's
/// station count (inspect_station_netcdf gives the count).
using StationNcSelection = std::variant<AllStations, core::StationSelection>;

/// How much of the padding of an incomplete layout a read checks (SN 12.4).
enum class PaddingCheck : std::uint8_t {
  /// The first padding element of each station (the default): a read costs
  /// what the stations' samples cost.
  boundary,
  /// Every padding element: a read costs selected stations x `obs`, which a
  /// pathological file (one long station among many short ones) makes
  /// large. For tests and files of unknown origin.
  whole,
};

struct StationNcReadOptions {
  PaddingCheck padding{PaddingCheck::boundary};
  friend constexpr bool operator==(const StationNcReadOptions&,
                                   const StationNcReadOptions&) = default;
};

/// A file of this format, as written by v5 (or by any writer of the same
/// major version): the version it declares (a newer minor reads with
/// `minor_newer`) and its layout.
struct V5Origin {
  StationNcVersion version;
  StationNcLayout layout;
  friend constexpr bool operator==(const V5Origin&, const V5Origin&) = default;
};

/// The representation of a foreign CF discrete-sampling-geometry file (CF 9.3;
/// the v5 layouts are `StationNcLayout`):
///  - `orthogonal` (9.3.1): `time(time)`, data over (station, time) or (time,
///    station);
///  - `incomplete` (9.3.2): `time(station, obs)` (or transposed), padded with
///    missing values;
///  - `contiguous_ragged` (9.3.3): `time(obs)`, a count variable named by its
///    `sample_dimension` attribute gives each station's run;
///  - `indexed_ragged` (9.3.4): `time(obs)`, an index variable named by its
///    `instance_dimension` attribute gives each sample's station;
///  - `single_station` (9.2): no station dimension; scalar coordinates, data
///    over time.
enum class CfDsgLayout : std::uint8_t {
  orthogonal,
  incomplete,
  contiguous_ragged,
  indexed_ragged,
  single_station,
};

/// A CF discrete-sampling-geometry `timeSeries` file written by someone else
/// (SN 12 "Foreign"): its layout and the CF version its `Conventions` names.
struct ForeignCfOrigin {
  CfDsgLayout layout;
  CfVersion version;
  friend constexpr bool operator==(const ForeignCfOrigin&,
                                   const ForeignCfOrigin&) = default;
};

/// The two shapes of the legacy v4 station netCDF (legacy-formats.md 5; the
/// `%4.4i` and `%04i` spellings of the station number are one dialect, and
/// CRMS, dialect C, is not read). `a` carries the marks of v4's
/// `Hmdf::writeNetcdf` (a global `fileformat` attribute or a `stationId`
/// variable); `b` is a file in the same layout without them, which v4's reader
/// accepted. Both are read the same way.
enum class LegacyDialect : std::uint8_t { a, b };

/// A legacy v4 station file (SN 11).
struct LegacyOrigin {
  LegacyDialect dialect;
  friend constexpr bool operator==(const LegacyOrigin&,
                                   const LegacyOrigin&) = default;
};

/// Where a station file's table came from.
using StationFileOrigin = std::variant<V5Origin, ForeignCfOrigin, LegacyOrigin>;

/// What read_station_netcdf returns.
struct StationFile {
  core::StationTable table;
  StationFileOrigin origin;
  friend bool operator==(const StationFile&, const StationFile&) = default;
};

/// A station of a catalog and its number of samples (`obs_count`, or the
/// length of `time` in the orthogonal layout).
struct CatalogStation {
  core::FileStation station;
  std::size_t samples;
  friend bool operator==(const CatalogStation&,
                         const CatalogStation&) = default;
};

/// What a station file holds, without its samples.
struct StationNcCatalog {
  StationFileOrigin origin;
  /// Every station, in file order.
  std::vector<CatalogStation> stations;
  /// One entry per data variable, in file order: the schema a read returns.
  std::vector<core::SeriesMeta> schema;
  friend bool operator==(const StationNcCatalog&,
                         const StationNcCatalog&) = default;
};

/// The stations and the schema of a station file (v5, foreign CF or legacy),
/// read and validated as read_station_netcdf does, without the time and data
/// variables. Errors and warnings are read_station_netcdf's, except the ones
/// about samples. (A foreign incomplete layout without `obs_count` has to look
/// at its times to count each station's samples, so they are checked here.)
[[nodiscard]] std::expected<Read<StationNcCatalog>, Error>
inspect_station_netcdf(const std::filesystem::path& path,
                       const ReadContext& ctx);

/// Reads the stations `which` of a station file into a table. The kind of file
/// decides the reader (the order of SN 12.1; any other kind is
/// `not_this_format`):
///  - v5: the rules below;
///  - foreign CF (`featureType` timeSeries, CF-1.6 or later; SN 12 "Foreign",
///    CF 9.3): any of the five layouts, variables found by `cf_role`,
///    `standard_name`, `units`, `coordinates`, `sample_dimension` and
///    `instance_dimension`; `_FillValue`, `missing_value`, `valid_*`,
///    packing, int and float data and times, NC_STRING and integer ids are
///    accepted; a registry quantity when the standard name is its own and the
///    unit converts, else a generic one named after the variable (a
///    substitute token, `variable_renamed`, when the name is none); variables
///    that are not series are skipped (`skipped_variable`); warnings
///    `foreign_cf` first, then `crs_assumed`, `duplicate_station_id_renamed`,
///    `invalid_utf8_replaced`, `skipped_variable`, `variable_renamed`,
///    `unknown_quantity`, `unrecognized_unit`, `datum_unknown`, the time
///    units'. Errors: `missing_variable` (latitude, longitude, time),
///    `no_station_id`, `no_data_variables`, `unsupported_layout`,
///    `dimension_mismatch`, `bad_obs_count`, `bad_row_size`,
///    `bad_ragged_index`, `padding_not_missing`, `time_*`, `missing_attribute`
///    (`time:units`), `unsupported_calendar`, NcError (a `_FillValue` of the
///    wrong type, `too_large`);
///  - legacy v4 (SN 11): see station_netcdf_legacy.cpp; origin LegacyOrigin,
///    warnings `legacy_dialect` first, then `tz_assumed_utc`, `epoch_used`,
///    `crs_assumed`, `invalid_utf8_replaced`, `duplicate_station_id_renamed`,
///    `crs_approximate`, `unrecognized_unit`, `datum_unknown`. Errors:
///    `missing_dimension`/`missing_variable` (with the station for the
///    per-station variables), `dimension_mismatch`, `inconsistent_metadata`,
///    `unsupported_crs`, ParseError `bad_date` (`referenceDate`), NcError
///    `type_mismatch` for a `HorizontalProjectionEPSG` that is not an integer.
///
/// For a v5 file: reads the stations `which` into a table (stations in the
/// order of the selection; one shared axis in the orthogonal layout, one per
/// station in the incomplete one). Validation (SN 12, v5 column; every rule
/// is a hard error, none is downgraded):
///
///  - header: global `metoceanviewer_format` = "station-timeseries" (else
///    `not_this_format`: another kind of file);
///    `metoceanviewer_format_version` present and "<major>.<minor>"
///    (`bad_version`), major 1 (`unsupported_version`); `Conventions` with a
///    CF-1.N token, N >= 6 (`missing_attribute`, `unsupported_version`; a
///    CF-2 or later token too); `featureType` timeSeries, any case
///    (`missing_attribute`, `unsupported_layout`);
///  - structure: dimension `station` (`missing_dimension`); no unlimited
///    dimension (`unsupported_layout`, subject: the dimension); exactly one
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
///  - data variables: the name is a quantity token (a registry token, or a
///    CF name the format does not use itself; else `invalid_variable_name`);
///    a registry quantity other than `difference` has a `units` that converts
///    to its canonical unit (`noncanonical_unit`, subject: the token); the
///    target `<name>_status` of `ancillary_variables` is the wet/dry status
///    and must be byte, with `flag_values` 0, 1, `flag_meanings` "dry wet"
///    and a _FillValue other than 0 and 1 (`bad_flag`, subject: the status);
///  - CRS (SN 10.1): the `grid_mapping` variable's `epsg_code` (EPSG:4326, or
///    another geographic code, projected here with the native point kept),
///    else `latitude_longitude` on the WGS 84 ellipsoid (without ellipsoid
///    parameters: WGS 84 with `crs_assumed`); a nonzero
///    `longitude_of_prime_meridian` or anything else is `unsupported_crs`;
///    no `grid_mapping`: WGS 84 with `crs_assumed`;
///  - values: ids unique (`duplicate_station_id`), non-empty
///    (`no_station_id`), UTF-8 without NUL after trailing NULs are cut
///    (`bad_encoding`), and so the names and the providers; `lat`/`lon` finite
///    and in range (`bad_coordinates`, with the station); times present
///    (`time_missing`), in range (`time_out_of_range`), strictly increasing
///    (`time_not_increasing`); `obs_count` in 0..obs (`bad_obs_count`); the
///    padding of `time`, the data and the status is fill
///    (`padding_not_missing`, with the station and index; the first padding
///    element, or all of them with PaddingCheck::whole); a status flag is 0,
///    1 or its fill (`bad_flag`), a dry sample has no value and a wet one has
///    one (`wet_dry_inconsistent`).
///
/// A sample is Missing when it equals its variable's _FillValue (or the
/// library's default fill), is NaN, or is masked by `missing_value` or
/// `valid_*` (nc::Masking); it is Dry when its wet/dry status is 0. The
/// quantity is the variable name: a registry token, else a generic quantity
/// with the variable's `standard_name`. Labels are `long_name` (the token
/// when there is none), units `units` (parse_unit; `unrecognized_unit` for
/// one outside the unit table and the registry, subject: the unit), datums
/// `vertical_datum` (`datum_unknown`, and no datum, for an unknown token,
/// subject: the token, or for a datum on a quantity that cannot carry one,
/// subject: `<variable>:vertical_datum`). Subjects taken from the file are cut
/// to ParseError::max_context_bytes on a UTF-8 boundary.
///
/// Other errors: FormatError `station_count_mismatch` (a selection made for
/// another count); NcError (`too_large` when the samples read exceed
/// ReadLimits: the selected stations' samples, or selected stations x `obs`
/// with PaddingCheck::whole); Cancelled. Warnings, in this order:
/// `minor_newer` (subject: the version), `crs_assumed`, `unknown_provider`
/// (a `station_provider` token core does not know: the station has no
/// source; one warning per token, counting its stations), `crs_approximate`,
/// per data variable `unrecognized_unit` and `datum_unknown`, and last
/// parse_cf_time_units's for `time:units`.
[[nodiscard]] std::expected<Read<StationFile>, Error> read_station_netcdf(
    const std::filesystem::path& path, const StationNcSelection& which,
    const ReadContext& ctx);
[[nodiscard]] std::expected<Read<StationFile>, Error> read_station_netcdf(
    const std::filesystem::path& path, const StationNcSelection& which,
    const ReadContext& ctx, const StationNcReadOptions& options);

namespace detail {

/// The writer's limits; tests lower them to reach the checks.
struct StationNcWriteLimits {
  /// Samples per station: `obs_count` is an int.
  std::size_t max_station_samples =
      static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max());
};

/// validate_station_netcdf with limits.
[[nodiscard]] std::expected<std::vector<Warning>, Error>
validate_station_netcdf(const core::StationTable& table,
                        const StationNcWriteOptions& options,
                        const StationNcWriteLimits& limits);

/// write_station_netcdf with the stages of the atomic write exposed to a
/// FaultInjector (tests: a failure at any stage leaves the old file).
[[nodiscard]] std::expected<std::vector<Warning>, Error> write_station_netcdf(
    const std::filesystem::path& path, const core::StationTable& table,
    const StationNcWriteOptions& options, std::chrono::sys_seconds now,
    const FaultInjector& fault);

}  // namespace detail

}  // namespace mov::io
