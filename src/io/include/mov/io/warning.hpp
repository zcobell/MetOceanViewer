// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mov::io {

/// What a reader noticed and fixed or ignored while still producing a value.
/// A reader reports a problem it cannot continue past as an `Error` instead.
/// Each enumerator says what its Warning's `subject` holds; text taken from a
/// file is cut to ParseError::max_context_bytes on a UTF-8 boundary, and an
/// empty subject means the code says it all.
enum class WarningCode : std::uint8_t {
  // ---- series put in time order (core::normalize and its readers) ----------
  /// Descents in a series whose rows were sorted by time. Subject: the station
  /// (IMEDS), the time variable (foreign and legacy netCDF), or empty.
  times_reordered,
  /// Rows dropped because an earlier row had the same time; subject as above.
  /// The IMEDS writer: times that collide once floored to seconds.
  duplicate_times_dropped,
  /// Dropped rows whose values differed from the row kept; subject as above.
  conflicting_duplicate_times,

  // ---- values
  // -----------------------------------------------------------------
  /// Exact legacy sentinels (-99999, -9999, -DBL_MAX) that became Missing.
  /// Subject: empty.
  legacy_sentinel_masked,
  /// NaN, Inf or a Fortran "****" that became Missing. Subject: where the
  /// first was (ADCIRC ASCII: "first at line <n>"; netCDF: the variable), or
  /// empty (IMEDS).
  nonfinite_masked,
  /// A unit outside the unit table that no registry quantity uses either; the
  /// unit is kept as text. Subject: the unit text as the file has it (not the
  /// spelling parse_unit made of it).
  unrecognized_unit,

  // ---- model text
  // -------------------------------------------------------------
  /// A record cut short at the end of the file. Subject: "record <n>".
  partial_record_dropped,
  /// Fewer records than the header's NSnaps. Subject: "NSnaps <n>, read <m>".
  fewer_snapshots_than_header,
  /// More records than NSnaps (a restart appended to the file). Subject as
  /// above.
  more_snapshots_than_header,
  /// No cold start given: times are relative to the epoch of the time units.
  /// Subject: the units text (legacy station netCDF: the default
  /// referenceDate).
  epoch_used,
  /// Sub-millisecond digits of a time were rounded away. Subject: the time
  /// units text; empty from the IMEDS writer (times floored to seconds).
  time_precision_dropped,
  /// A header line of a high-water-mark file that is not a mark. Subject: the
  /// line, trimmed and cut.
  header_line_skipped,
  /// Station ids made unique with `#2`, `#3` suffixes. Subject: the id.
  duplicate_station_id_renamed,
  /// Station names or ids whose bytes were not UTF-8, replaced. Subject:
  /// empty; the count is the stations.
  invalid_utf8_replaced,

  // ---- station netCDF (docs/station-netcdf.md section 12.7)
  // --------------------
  /// A CF file not written by v5. Subject: its `Conventions`.
  foreign_cf,
  /// No usable CRS: WGS 84 assumed. Subject: "EPSG:<n>" or the grid mapping
  /// variable that could not be used.
  crs_assumed,
  /// A projection used a ballpark or low-accuracy transformation. Subject:
  /// "EPSG:<n>".
  crs_approximate,
  /// A vertical datum text that is no datum core knows, or a datum on a
  /// quantity that cannot carry one: no datum. Subject: the text, or
  /// `<variable>:vertical_datum`.
  datum_unknown,
  /// A time zone other than UTC, read as UTC. Subject: the zone text.
  tz_assumed_utc,
  /// A variable that is not read. Subject: the variable, or
  /// `<variable>:<attribute>` when an attribute of it cannot be read.
  skipped_variable,
  /// A file of a newer minor version than this reader's. Subject: the version.
  minor_newer,
  /// A `station_provider` token core does not know: the station has no source.
  /// Subject: the token; the count is its stations.
  unknown_provider,
  /// A variable whose quantity could not be told: a generic one. Subject: the
  /// variable or its standard name.
  unknown_quantity,
  /// A legacy v4 station file. Subject: empty.
  legacy_dialect,

  // ---- line-oriented text formats (IMEDS)
  // ---------------------------------------
  /// A station's rows switched between 6 and 7 words. Subject: the station.
  row_shape_changed,
  /// A station block without rows. Subject: the station.
  empty_station,
  /// Written values that a reader masks as a sentinel. Subject: the column
  /// token (station netCDF), or empty (IMEDS).
  value_reads_as_missing,
  /// The format has no id: only the name was written. Subject: empty.
  station_id_not_written,
  /// Missing and Dry samples the format cannot hold. Subject: empty.
  rows_omitted,

  // ---- netCDF model output
  // ----------------------------------------------------
  /// The CRS the caller gave is not the kind the file's `ics` says. Subject:
  /// "ics <n> but EPSG:<code> is <geographic|projected>".
  crs_mismatch,
  /// A given cold start is not the epoch of `time:units`. Subject: the units.
  cold_start_differs,
  /// Station coordinates over (time, station): step 0 was used. Subject: the
  /// x coordinate variable.
  coordinates_from_first_step,

  // ---- station netCDF writer: what is written differs from the table
  // ----------
  /// A column stored in its canonical unit. Subject: the column token.
  unit_converted,
  /// Stations with an empty name, written as "Station <id>". Subject: empty.
  station_name_substituted,
  /// Native points not kept: the file stores WGS 84 only (SN 10.1). Subject:
  /// empty.
  native_position_dropped,

  // ---- foreign and legacy station netCDF
  // -------------------------------------
  /// A variable name that is no quantity token was given one. Subject: the
  /// variable's name.
  variable_renamed,
  /// Stations whose id was missing or empty in the file's id variable: the
  /// index (foreign) or the name (legacy) became the id. Subject: the id
  /// variable.
  station_id_substituted,
  /// A quality-flag variable of an unknown scheme. Subject: the flag variable.
  quality_flags_ignored,
  /// Samples a known quality scheme calls bad, made Missing. Subject: the data
  /// variable; the count is the samples.
  flagged_samples_masked,
  /// Samples flagged suspect, kept as they are. Subject as above.
  suspect_samples_kept,
};
/// A stable lower-case identifier, the same as the enumerator's name
/// (for logs and tests; user-facing text belongs to the app).
[[nodiscard]] std::string_view to_token(WarningCode code) noexcept;

/// `code` happened `count` times; `subject` says to what (a variable, a
/// station id, a value), and is empty when the code says it all.
struct Warning {
  WarningCode code;
  std::string subject{};
  std::size_t count{1};
  friend bool operator==(const Warning&, const Warning&) = default;
};

/// Appends `more` to `warnings`, in order: how the warnings of a later stage
/// follow those of an earlier one (Read::and_then, every reader).
inline void append(std::vector<Warning>& warnings, std::vector<Warning> more) {
  warnings.insert(warnings.end(), std::make_move_iterator(more.begin()),
                  std::make_move_iterator(more.end()));
}

/// Appends `w` when it counts something (`w.count > 0`): a reader tallies and
/// reports once, without an `if` per code.
inline void append_if_counted(std::vector<Warning>& warnings, Warning w) {
  if (w.count > 0) {
    warnings.push_back(std::move(w));
  }
}

}  // namespace mov::io
