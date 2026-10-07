// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

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
  epoch_used,  // no cold start given: times are relative to the epoch
  header_line_skipped,
  duplicate_station_id_renamed,
  invalid_utf8_replaced,
  // station netCDF, SN section 12.7
  foreign_cf,
  crs_assumed,
  datum_unknown,
  tz_assumed_utc,
  skipped_variable,
  minor_newer,
  unknown_provider,
  unknown_quantity,
  legacy_dialect,
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

}  // namespace mov::io
