// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The two halves of the v5 station netCDF reader (docs/station-netcdf.md
// section 12, the v5 column): opening a file (station_netcdf_open.cpp: the
// header, the structure, the CRS, the stations and the schema, all a catalog
// needs) and reading the samples of selected stations
// (station_netcdf_samples.cpp). Private to src/io/.

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "model_netcdf.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "mov/io/cf_time.hpp"
#include "mov/io/detail/station_groups.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/projection.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/station_netcdf.hpp"

namespace mov::io::detail::station_nc {

using Vars = std::vector<nc::VarInfo>;

/// A data variable and its wet/dry status variable, if it has one.
struct DataVar {
  nc::VarInfo var;
  std::optional<nc::VarInfo> status;
};

/// The time variable, the layout it gives, and obs_count (incomplete).
struct Timing {
  nc::VarInfo time;
  StationNcLayout layout;
  nc::DimInfo sample;  // `time` or `obs`
  std::optional<nc::VarInfo> obs_count;
};

/// The instance variables found by name (SN 4.2).
struct Instances {
  nc::VarInfo name;
  nc::VarInfo lat;
  nc::VarInfo lon;
  std::optional<nc::VarInfo> provider;
};

/// Which variable of the file is what.
struct Structure {
  std::vector<nc::VarInfo> vars;
  nc::DimInfo station;
  nc::VarInfo id;
  Instances instances;
  Timing timing;
  std::vector<DataVar> data;
};

/// An opened file: its structure and the catalog (origin, stations with their
/// sample counts, schema).
struct Opened {
  Structure structure;
  StationNcCatalog catalog;
};

/// Everything but the samples, validated.
[[nodiscard]] std::expected<Read<Opened>, Error> open_v5(const nc::File& file,
                                                         const StopToken& stop);

/// The table of the stations `selected` (file indices, in the caller's order).
[[nodiscard]] std::expected<Read<core::StationTable>, Error> read_table(
    const nc::File& file, const Opened& opened,
    std::span<const std::size_t> selected, PaddingCheck padding,
    const StopToken& stop);

// ---- row-wise reads of (station, sample) variables
// (station_netcdf_samples.cpp)
// ---------------------------------------------------------------------------

/// What a read of the rows of an (station, sample) variable needs besides the
/// file: the dimensions, how many samples each station has, and which
/// stations to read.
struct RowSpec {
  int station_dim;                        // id of the station dimension
  std::size_t sample_length;              // length of the sample dimension
  std::span<const std::size_t> counts;    // samples per station of the file
  std::span<const std::size_t> selected;  // file indices, in the caller's order
  /// How much of the padding after a station's samples must be missing
  /// (`padding_not_missing`); nullopt: it is not looked at, and not read.
  std::optional<PaddingCheck> padding;
  const StopToken& stop;
};

/// A time variable may be 64-bit integers; a data variable may not.
enum class RowRole : std::uint8_t { values, times };

using RowSink = std::function<std::expected<void, Error>(
    const SelectedStation&, std::span<const core::Sample>)>;

/// Calls sink(member, kept) for each selected station with its first
/// counts[station] samples of `var` (over (station, sample), in that order),
/// masked in the variable's own type.
[[nodiscard]] std::expected<void, Error> sample_rows(const nc::File& file,
                                                     const nc::VarInfo& var,
                                                     const RowSpec& rows,
                                                     RowRole role,
                                                     const RowSink& sink);

/// The samples of `slab` of `var`, masked in the variable's own type
/// (nc::File::read_samples); a 64-bit time is read as integers.
[[nodiscard]] std::expected<std::vector<core::Sample>, Error> read_masked(
    const nc::File& file, const nc::VarInfo& var, const nc::Slab& slab,
    RowRole role, const StopToken& stop);

/// dispatch_model_numeric for a variable in `role`: a time variable may also
/// be 64-bit integers (checked_time bounds them).
template <class Run>
[[nodiscard]] std::expected<void, Error> dispatch_role_numeric(
    RowRole role, const nc::File& file, const nc::VarInfo& var, Run&& run) {
  if (role == RowRole::times and var.type == nc::Type::int64) {
    return run.template operator()<std::int64_t>();
  }
  return dispatch_model_numeric(file, var, std::forward<Run>(run));
}

/// One station's times (nullopt: the one axis of every station): present and
/// on the clock (`time_missing`, `time_out_of_range`, with the station and
/// index), in file order.
[[nodiscard]] std::expected<core::TimeAxis, Error> times_of(
    std::span<const core::Sample> row, const CfClock& clock,
    std::string_view var, std::optional<std::size_t> station);

/// times_of, and strictly increasing (`time_not_increasing`): a v5 file's rule.
[[nodiscard]] std::expected<core::TimeAxis, Error> axis_of(
    std::span<const core::Sample> row, const CfClock& clock,
    std::string_view var, std::size_t station);

/// The samples of the `selected` stations (`counts` per file station), or
/// nullopt when the sum does not fit in std::size_t.
[[nodiscard]] std::optional<std::size_t> sum_selected(
    std::span<const std::size_t> counts, std::span<const std::size_t> selected);

/// `too_large` (an NcError about `variable`) unless `samples` (nullopt: more
/// than a std::size_t) samples of `columns` columns fit ReadLimits
/// (check_result_size).
[[nodiscard]] std::expected<void, Error> check_rows_size(
    const nc::File& file, std::string_view variable,
    std::optional<std::size_t> samples, std::size_t columns);

/// The clock of a time variable: its `units` (missing_attribute without; a
/// ParseError if they do not parse) and `calendar`, with the warnings of
/// parse_cf_time_units.
[[nodiscard]] std::expected<Read<CfClock>, Error> clock_of(
    const nc::File& file, const nc::VarInfo& time_var);

// ---- helpers the foreign and legacy readers share with the v5 reader
// (station_netcdf_open.cpp) --------------------------------------------------

/// A signed integer attribute of any width (byte, short, int, int64) as
/// int64: nullopt when absent; one inquiry of the type, then the read in
/// exactly that type. Any other type is `type_mismatch` (an EPSG code stored
/// as text or as a float is not a code, B10; unsigned types are refused, v4
/// writes `int`). `object` names the attribute in the error.
[[nodiscard]] std::expected<std::optional<std::vector<std::int64_t>>, Error>
int_att(const nc::File& file, nc::AttTarget on, nc::NcNameRef att,
        std::string_view object);

/// Whether `t` is a type int_att reads.
[[nodiscard]] constexpr bool is_signed_integer(nc::Type t) noexcept {
  return t == nc::Type::byte or t == nc::Type::short_ or t == nc::Type::int_ or
         t == nc::Type::int64;
}

/// The variable called `name`, or nullopt.
[[nodiscard]] std::optional<nc::VarInfo> named(const Vars& vars,
                                               std::string_view name);

/// The bytes of a char row without its trailing NUL padding.
[[nodiscard]] std::string trimmed(std::string row);

/// A coordinate variable (station) as numbers; a masked or non-finite value
/// is `bad_coordinates` with the station.
[[nodiscard]] std::expected<std::vector<double>, Error> coordinate(
    const nc::File& file, const nc::VarInfo& var, const StopToken& stop);

/// Where a station is: its WGS 84 Location and, when the file's CRS is
/// another one, its point in that CRS.
struct Position {
  core::Location location;
  std::optional<core::NativePoint> native;
};

/// The position of station `i` from (lon, lat) in `projector`'s CRS, or WGS 84
/// without one (`x` is longitude or easting, `y` latitude or northing).
[[nodiscard]] std::expected<Position, Error> place(
    double latitude, double longitude, std::optional<Projector>& projector,
    std::size_t i);

/// "EPSG:<digits>" (at most 9 digits), positive.
[[nodiscard]] std::optional<core::Epsg> parse_epsg(std::string_view text);

/// A one-value double attribute: nullopt when absent.
[[nodiscard]] std::expected<std::optional<double>, Error> double_att(
    const nc::File& file, nc::NcNameRef var, nc::NcNameRef att);

/// The CRS of a grid mapping variable: `epsg_code`, else its parameters
/// (latitude_longitude on the WGS 84 ellipsoid); `unsupported_crs` otherwise.
[[nodiscard]] std::expected<Read<core::Epsg>, Error> crs_of_mapping(
    const nc::File& file, const nc::VarInfo& var);

/// The unit of a `units` text (parse_unit), with `unrecognized_unit` when it
/// is a spelling outside the unit table and the registry; nullopt without
/// text.
[[nodiscard]] Read<std::optional<core::Unit>> parsed_unit(
    const std::optional<std::string>& text);

/// `meta` with the datum of the `vertical_datum` text, when it has one it can
/// carry; `datum_unknown` warnings otherwise.
[[nodiscard]] Read<core::SeriesMeta> with_datum(
    core::SeriesMeta meta, const std::optional<std::string>& text,
    std::string_view variable);

/// `text` as the token parse_vertical_datum reads: the long names of the datums
/// (`North American Vertical Datum of 1988`, `mean lower low water`) and their
/// abbreviations give the datum's token; any other text comes back trimmed, for
/// with_datum to refuse with `datum_unknown`.
[[nodiscard]] std::string datum_text(std::string_view text);

// ---- series whose times are not strictly increasing (plan decision 30.3) ----

/// Puts the series of one station in time order, as IMEDS does
/// (core::normalizing_order): rows are stably sorted by time and of equal times
/// the first is kept. `times` and every column are reordered together; the
/// report counts what was moved and dropped (clean: nothing was changed).
[[nodiscard]] core::NormalizeReport normalize_columns(
    core::TimeAxis& times, std::span<core::Column* const> columns);

/// `sum` plus `more`, field by field: the report of many series as one.
void merge_reports(core::NormalizeReport& sum,
                   const core::NormalizeReport& more);

/// The warnings of a report (`times_reordered`, `duplicate_times_dropped`,
/// `conflicting_duplicate_times`), each counted, about `subject`.
void add_normalize_warnings(std::vector<Warning>& warnings,
                            const core::NormalizeReport& report,
                            std::string_view subject);

}  // namespace mov::io::detail::station_nc
