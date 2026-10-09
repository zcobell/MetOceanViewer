// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io {

// ADCIRC's ASCII station output (fort.61, fort.62, fort.71, fort.72) and the
// station file that names its stations (LF section 3.1). The output holds no
// station positions and no start time: a station file and a cold start go
// with it.

/// Which output a file is. The caller says; nothing is inferred from the file
/// name.
enum class AdcircKind : std::uint8_t {
  elevation,  // fort.61: water level (water_level, m)
  velocity,   // fort.62: current_u, current_v (m s-1)
  pressure,   // fort.71: air_pressure (m of water)
  wind,       // fort.72: wind_u, wind_v (m s-1)
};

/// The value columns on each station line: one for elevation and pressure,
/// two (x then y) for velocity and wind.
[[nodiscard]] constexpr std::size_t column_count(AdcircKind kind) noexcept {
  return (kind == AdcircKind::velocity or kind == AdcircKind::wind) ? 2U : 1U;
}

/// The second line of an output file: `NSnaps NStations DT NSPOOL NCOLS
/// [FileFmtVersion: n]`. DT and NSPOOL are not needed and not checked.
struct AdcircAsciiHeader {
  std::size_t snapshots;  // records the run meant to write
  std::size_t stations;
  std::size_t columns;  // NCOLS
  friend constexpr bool operator==(const AdcircAsciiHeader&,
                                   const AdcircAsciiHeader&) = default;
};

/// Line 1 is a free-text run description and is not read (it may be blank).
/// A probe, not a reader (the convention that a `parse_*` takes a
/// `ReadContext` and returns `expected<Read<T>, Error>` is for the readers
/// below): it looks at two lines, so it needs no limits, cannot be cancelled
/// and has no warnings.
/// Errors: `empty_input` (no text), `missing_header` (no second line),
/// `wrong_field_count` (fewer than five fields), `bad_integer` and
/// `out_of_range` (a count that is not a non-negative integer).
[[nodiscard]] std::expected<AdcircAsciiHeader, ParseError>
parse_adcirc_ascii_header(std::string_view text);

/// The station file: the number of stations, then one line per station,
/// `lon[,| ]lat[ name...]` (longitude first). Commas and white space both
/// separate; a blank line is skipped. The coordinates are in `crs` (x then y)
/// and become WGS84 Locations here; a station in another CRS keeps its native
/// point. The id of a station is its 0-based index in the file (the row order
/// of the output); the name is the remaining words joined by single spaces (a
/// NUL ends it; bytes that are not UTF-8 become U+FFFD with a warning), empty
/// when there are none. `source` is `adcirc`.
///
/// Errors: `empty_input`; `bad_integer` / `out_of_range` for the count;
/// `too_large` when the count is over `ctx.limits.max_elements`;
/// `wrong_field_count`, `bad_number` or `out_of_range` for a line, the last
/// also when the position is not a valid Location; `count_mismatch` when the
/// number of lines is not the count (the line is the first one that does not
/// fit, or the last line of the file when there are too few); FormatErrors:
/// `unsupported_crs` when `crs` is not a geographic or projected CRS,
/// `projection_unavailable` when the projection database cannot be opened,
/// `bad_coordinates` (with the station's index) when PROJ cannot transform a
/// point; Cancelled (`ctx.stop`, polled every 1024 stations).
/// Warnings: `invalid_utf8_replaced`, and `crs_approximate` when the
/// projection was not exact.
[[nodiscard]] std::expected<Read<std::vector<core::FileStation>>, Error>
parse_adcirc_station_file(std::string_view text, core::Epsg crs,
                          const ReadContext& ctx);

/// parse_adcirc_station_file on a file.
[[nodiscard]] std::expected<Read<std::vector<core::FileStation>>, Error>
read_adcirc_station_file(const std::filesystem::path& path, core::Epsg crs,
                         const ReadContext& ctx);

/// What to read from an output file. `stations` is required (never an
/// implicit "all"); its station count must be the file's, and the table's
/// stations follow its order.
struct AdcircAsciiRequest {
  AdcircKind kind;
  /// The start of the model clock: the record times are seconds after it. A
  /// run's start is not in the file; parse it with core::parse_utc_datetime.
  /// Every record time, cold_start + seconds, must be within +-2^53 ms of the
  /// epoch (core::max_abs_time_ms), the bound a file time keeps (SN 7).
  core::Time cold_start;
  core::StationSelection stations;
  friend bool operator==(const AdcircAsciiRequest&,
                         const AdcircAsciiRequest&) = default;
};

/// An output file as a table with one shared time axis and the selected
/// stations only (the other stations' lines are skipped, not parsed).
///
/// `stations` is the whole station file, in file order; its size must be the
/// header's NStations. Schema by kind: `water_level` (m); `current_u`,
/// `current_v` (m s-1); `air_pressure` (m of water); `wind_u`, `wind_v`
/// (m s-1). No datum is stated, none is assumed.
///
/// Values:
///  - elevation: a value at or below -999 (ADCIRC writes -99999) is `Dry`;
///  - every other output: a value at or below -999 is fill, so `Missing`, and
///    a fill in either component of a vector makes both `Missing`;
///  - NaN, Inf and Fortran's `****` (and a number no double holds) are
///    `Missing` with a `nonfinite_masked` warning counting the tokens.
///
/// The header's NSnaps is a hint, not a bound: records are read to the end of
/// the text (a restart appends records past it).
///  - the complete records are kept;
///  - a record cut off at the end of the text is dropped with
///    `partial_record_dropped`: its header or station lines are missing, or
///    its last line has no newline (a finished ADCIRC file always ends with
///    one, and a file cut by a crash or a copy ends mid-line, where a
///    truncated number still parses);
///  - fewer complete records than NSnaps give `fewer_snapshots_than_header`
///    and more give `more_snapshots_than_header` (count: the difference);
///  - a malformed line with more text after it is `ParseError::corrupt_record`:
///    a record header must be a number and an integer, a station line its
///    1-based index in order (on the stations selected) and exactly
///    `column_count(kind)` values, and no line of a station may be blank. The
///    other lines of unselected stations are not read, so damage in them goes
///    unseen. Garbage after the last record is such a line, and a cut-off
///    record when nothing follows it; there is no separate trailing-text error.
/// Record times that do not increase (a hot start overlaps the run it
/// restarts) are sorted, and of equal times the first record is kept, with
/// `times_reordered`, `duplicate_times_dropped` and
/// `conflicting_duplicate_times` warnings.
///
/// Other errors: FormatError `wrong_column_count` (NCOLS is not
/// `column_count(kind)`), `station_count_mismatch` (the station list or the
/// selection is for another count); `time_out_of_range` (a record time
/// beyond +-2^53 ms from the epoch); ParseError `too_large` (more than
/// `ctx.limits.max_elements` samples); Cancelled (`ctx.stop`, polled once per
/// record). Nothing is reserved from the header: the buffers grow with the
/// records that are there, after a first estimate from the size of the first.
[[nodiscard]] std::expected<Read<core::StationTable>, Error> parse_adcirc_ascii(
    std::string_view text, std::span<const core::FileStation> stations,
    const AdcircAsciiRequest& request, const ReadContext& ctx);

/// Reads the station file, then the output, and pairs them. The warnings of
/// the station file come first.
[[nodiscard]] std::expected<Read<core::StationTable>, Error> read_adcirc_ascii(
    const std::filesystem::path& output,
    const std::filesystem::path& station_file, core::Epsg crs,
    const AdcircAsciiRequest& request, const ReadContext& ctx);

}  // namespace mov::io
