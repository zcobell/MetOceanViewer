// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mov::io {

/// What a reader noticed and fixed or ignored while still producing a value.
/// A reader reports a problem it cannot continue past as an `Error` instead.
enum class WarningCode : std::uint8_t {
  // normalize() report
  times_reordered,
  duplicate_times_dropped,
  conflicting_duplicate_times,
  // value level
  legacy_sentinel_masked,  // an exact legacy sentinel became Missing (C9)
  nonfinite_masked,        // NaN, Inf or a Fortran "****" became Missing
  unrecognized_unit,
  // model text
  partial_record_dropped,
  fewer_snapshots_than_header,
  more_snapshots_than_header,  // a restart appended records past NSnaps
  epoch_used,  // no cold start given: times are relative to the epoch
  time_precision_dropped,  // sub-millisecond digits of a time were rounded away
  header_line_skipped,
  duplicate_station_id_renamed,
  invalid_utf8_replaced,
  // station netCDF, SN section 12.7
  foreign_cf,
  crs_assumed,
  crs_approximate,  // a projection used a ballpark or low-accuracy
                    // transformation
  datum_unknown,
  tz_assumed_utc,
  skipped_variable,
  minor_newer,
  unknown_provider,
  unknown_quantity,
  legacy_dialect,
  // line-oriented text formats (IMEDS)
  row_shape_changed,       // a station's rows switched between 6 and 7 words
  empty_station,           // a station block without rows
  value_reads_as_missing,  // written text that a reader masks as a sentinel
  station_id_not_written,  // the format has no id: only the name was written
  rows_omitted,            // Missing and Dry samples the format cannot hold
  // netCDF model output
  crs_mismatch,  // the CRS the caller gave is not the kind the file says (ics)
  cold_start_differs,  // a given cold start is not the epoch of time:units
  coordinates_from_first_step,  // station coordinates over time: step 0 used
  // station netCDF writer: what is written differs from the table
  unit_converted,            // a column stored in its canonical unit
  station_name_substituted,  // an empty name written as "Station <id>"
  native_position_dropped,   // the file keeps WGS 84 only (SN 10.1)
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

/// Appends `w` when it counts something (`w.count > 0`): a reader tallies and
/// reports once, without an `if` per code.
inline void append_if_counted(std::vector<Warning>& warnings, Warning w) {
  if (w.count > 0) {
    warnings.push_back(std::move(w));
  }
}

}  // namespace mov::io
