// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <expected>
#include <filesystem>
#include <string>

#include "mov/core/station_table.hpp"
#include "mov/io/error.hpp"

namespace mov::io {

/// A station table as CSV in long format: RFC 4180, UTF-8 without a byte
/// order mark, rows ended by CRLF, one row per sample, ordered by station,
/// then schema column, then time.
///
///   station_id,station_name,time_utc,quantity,value,status,unit,datum
///   8413320,Bar Harbor,2015-07-01T00:00:00.000Z,value,2.605000,value,m,MLLW
///   8413320,Bar Harbor,2015-07-01T00:06:00.000Z,value,,missing,m,MLLW
///
/// - `time_utc` is ISO 8601 in UTC with milliseconds and a `Z`. A year
///   outside 0000-9999 is printed with as many digits as it needs (a minus
///   sign for a negative one).
/// - `value` is `{:.6f}`; it is empty unless `status` is `value`, so the
///   column stays numeric for pandas and spreadsheets. `status` is `value`,
///   `missing` or `dry`.
/// - `quantity` is the registry token (or the generic token), `unit` the
///   display symbol and `datum` the datum token; the last two are empty when
///   unset.
/// - A text cell (id, name, quantity, unit, datum) that starts with one of
///   `= + - @`, a tab or a CR gets a leading `'`, so a spreadsheet does not
///   run it as a formula. The cells the writer formats itself (time and
///   value) are exempt: `-1.5` stays a number. A cell with a comma, a double
///   quote, CR or LF is quoted, and a quote inside doubled.
///
/// A table with no stations or no columns is the header line alone.
[[nodiscard]] std::string format_csv(const core::StationTable& table);

/// format_csv, written atomically (write_file_atomic) without building the
/// whole text in memory first.
[[nodiscard]] std::expected<void, Error> write_csv(
    const std::filesystem::path& path, const core::StationTable& table);

}  // namespace mov::io
