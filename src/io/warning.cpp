// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/warning.hpp"

#include <array>
#include <cstddef>
#include <string_view>

namespace mov::io {

namespace {

// Indexed by the enumerator's value; the static_assert keeps it in step.
constexpr std::array<std::string_view, 28> tokens{
    "times_reordered",
    "duplicate_times_dropped",
    "conflicting_duplicate_times",
    "legacy_sentinel_masked",
    "nonfinite_masked",
    "unrecognized_unit",
    "partial_record_dropped",
    "fewer_snapshots_than_header",
    "epoch_used",
    "time_precision_dropped",
    "header_line_skipped",
    "duplicate_station_id_renamed",
    "invalid_utf8_replaced",
    "foreign_cf",
    "crs_assumed",
    "crs_approximate",
    "datum_unknown",
    "tz_assumed_utc",
    "skipped_variable",
    "minor_newer",
    "unknown_provider",
    "unknown_quantity",
    "legacy_dialect",
    "row_shape_changed",
    "empty_station",
    "value_reads_as_missing",
    "station_id_not_written",
    "rows_omitted",
};

static_assert(static_cast<std::size_t>(WarningCode::rows_omitted) + 1 ==
                  tokens.size(),
              "a WarningCode has no token");

}  // namespace

std::string_view to_token(WarningCode code) noexcept {
  return tokens[static_cast<std::size_t>(code)];
}

}  // namespace mov::io
