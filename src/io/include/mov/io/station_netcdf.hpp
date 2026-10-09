// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The station time-series netCDF format of v5 (docs/station-netcdf.md, "SN"):
// CF-1.11 discrete sampling geometry, featureType timeSeries, in a netCDF-4
// file restricted to CF-classic constructs. This header has the v5 writer and
// read_station_netcdf, which reads a v5 file, a foreign CF discrete sampling
// geometry `timeSeries` file or a legacy v4 station file.
// What kind of file a path is, before any reader is chosen, is file_type.hpp's
// detect_file.
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
/// layouts of foreign CF files are CfDsgLayout.
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
///    because the format stores WGS 84 only (SN 10.1; count);
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
/// default: a model file can hold more stations than anyone wants read.
struct AllStations {
  friend constexpr bool operator==(AllStations, AllStations) = default;
};

/// Which stations to read: all of them, or a selection made for the file's
/// station count (inspect_station_netcdf gives the count).
using StationNcSelection = std::variant<AllStations, core::StationSelection>;

/// How much of the padding of an incomplete layout a read checks (SN 12.4):
/// of a v5 file, and of a foreign one that has `obs_count`. (A foreign file
/// without `obs_count` has its samples counted by the leading non-missing
/// times, which looks at all of them: there is nothing left to check.)
enum class PaddingCheck : std::uint8_t {
  /// The padding a read goes through anyway: after the samples of the longest
  /// selected station, one more element. A read costs what the stations'
  /// samples cost (the default).
  boundary,
  /// Every padding element: a read costs selected stations x `obs`, which a
  /// pathological file (one long station among many short ones) makes
  /// large, and `too_large` when it exceeds ReadLimits. For tests and files of
  /// unknown origin.
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

/// The digits of the station number in the variable names of a legacy file:
/// `time_station_0001` (v4's `%04i`, which widens past 9999) or
/// `time_station_000001` (the six-digit spelling of the CRMS dialect, accepted
/// when the file has the station coordinates).
enum class StationNumberWidth : std::uint8_t { four, six };

/// A legacy v4 station file (SN 11): what the file says about itself, not a
/// label. v4's `Hmdf::writeNetcdf` writes a global `fileformat` and a
/// `stationId` variable; v4's reader accepted files without them. Both read
/// the same way.
struct LegacyOrigin {
  /// The global `fileformat` attribute, if the file has a text one.
  std::optional<std::string> fileformat;
  /// The file has a `stationId` variable.
  bool has_station_ids;
  StationNumberWidth width;
  friend bool operator==(const LegacyOrigin&, const LegacyOrigin&) = default;
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
/// length of `time` in the orthogonal layout): what the file holds. A read of
/// a foreign or legacy file that puts a series in order drops repeated times,
/// so it can return fewer.
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

/// Reads the stations `which` of a station file into a table: a v5 file, a
/// foreign CF discrete-sampling-geometry `timeSeries` file (CF-1.6 or later)
/// or a legacy v4 station file, told apart by their content as detect_file
/// tells them (any other file is `not_this_format`). The stations come in the
/// order of the selection; an orthogonal file gives one time axis shared by
/// every station, the other layouts one axis per station. `origin` says which
/// kind of file it was and what it declared.
///
/// docs/station-netcdf.md is the contract of each kind: section 11 for legacy
/// files, section 12 for v5 and foreign ones, and section 12.9 for the errors
/// and the order of the warnings of all three. Besides those, any read can
/// fail with `station_count_mismatch` (a selection made for another station
/// count), an NcError (`too_large` when the samples it would hold exceed
/// ReadLimits) or Cancelled. `options.padding` says how much of the padding
/// of an incomplete layout is checked (PaddingCheck).
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
