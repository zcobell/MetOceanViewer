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
  V5Origin origin;
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

/// `code` about `subject`, as an io::Error result.
[[nodiscard]] std::unexpected<Error> invalid(
    FormatErrc code, std::string subject,
    std::optional<std::size_t> station = std::nullopt,
    std::optional<std::size_t> index = std::nullopt);

/// Text from a file as a warning or error subject: at most
/// ParseError::max_context_bytes, cut on a UTF-8 boundary.
[[nodiscard]] std::string subject_of(std::string_view text);

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
  PaddingCheck padding;
  /// Whether the padding read after a station's samples must be missing
  /// (`padding_not_missing`); false: it is not looked at.
  bool check_padding;
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

/// One station's times: present, on the clock, strictly increasing.
[[nodiscard]] std::expected<core::TimeAxis, Error> axis_of(
    std::span<const core::Sample> row, const CfClock& clock,
    std::string_view var, std::size_t station);

/// The clock of a time variable: its `units` (missing_attribute without; a
/// ParseError if they do not parse) and `calendar`, with the warnings of
/// parse_cf_time_units.
[[nodiscard]] std::expected<Read<CfClock>, Error> clock_of(
    const nc::File& file, const nc::VarInfo& time_var);

// ---- helpers the foreign and legacy readers share with the v5 reader
// (station_netcdf_open.cpp) --------------------------------------------------

/// A text attribute of `on`, cut at its first NUL; nullopt when absent or not
/// text.
[[nodiscard]] std::expected<std::optional<std::string>, Error> text_of(
    const nc::File& file, nc::AttTarget on, nc::NcNameRef att);

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

/// `meta` with the datum of the `vertical_datum` text, when it has one it can
/// carry; `datum_unknown` warnings otherwise.
[[nodiscard]] Read<core::SeriesMeta> with_datum(
    core::SeriesMeta meta, const std::optional<std::string>& text,
    std::string_view variable);

}  // namespace mov::io::detail::station_nc
