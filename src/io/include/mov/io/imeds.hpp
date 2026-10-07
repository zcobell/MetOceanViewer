// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/units.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"

namespace mov::io {

/// What the third line of an IMEDS file says: `<source> <time zone> <datum>
/// [<unit>]`. The first two lines are free text and are not kept.
struct ImedsHeader {
  std::string source;
  /// The token as written ("UTC"). Times are always read as UTC; a zone other
  /// than UTC, GMT or Z is reported (`tz_assumed_utc`) and otherwise ignored.
  std::string time_zone;
  std::optional<core::VerticalDatum> datum;
  std::optional<core::Unit> unit;
  friend bool operator==(const ImedsHeader&, const ImedsHeader&) = default;
};

/// An IMEDS file: its header and one table with a single column, the generic
/// `value` quantity, carrying the header's unit and datum. Station ids are the
/// station names (made unique), every station has `DataSource::user`.
struct ImedsFile {
  ImedsHeader header;
  core::StationTable table;
  friend bool operator==(const ImedsFile&, const ImedsFile&) = default;
};

/// Parses IMEDS text (docs/legacy-formats.md section 2, with the v5 rules of
/// docs/core-design.md section 5.2).
///
///   line 1, 2   free text
///   line 3      <source> [<time zone> [<datum> [<unit>]]]
///   then        blocks of: <name> <lat> <lon>
///                          <yyyy> <mm> <dd> <hh> <mi> [<ss>] <value>   (0..n)
///
/// - Fields are separated by any run of ASCII white space; a UTF-8 byte order
///   mark, CRLF line ends and a missing final newline are accepted. Blank lines
///   after the header are skipped.
/// - A line with 3 words starts a station, one with 6 or 7 words is a data row
///   (6: no seconds, so `2015 07 01 00 00 2.605` is the value 2.605 at second
///   0, N3), anything else is `wrong_field_count`. A 3-word line of three
///   integers right after a row is a row cut short (`wrong_field_count`), not a
///   station. Every number is checked: a bad date, number or coordinate is a
///   ParseError with the line and column. A data row before the first station
///   is `missing_header`. Years are 0000-9999.
/// - The coordinates (latitude first!) go through `Location::make`; a
///   longitude in [180, 360] is wrapped.
/// - The exact legacy sentinels -99999, -9999, -DBL_MAX and v4's printed
///   -1.7977e+308 become Missing (one `legacy_sentinel_masked` warning with the
///   count). `nan`, `inf`, `infinity` (any case, signed) and a Fortran `****`
///   become Missing too (`nonfinite_masked`). Nothing else is masked: -99998.9
///   and -999 are values (C9).
/// - Each station's rows go through `normalize` (`times_reordered`,
///   `duplicate_times_dropped`, `conflicting_duplicate_times`, subject the
///   station id). A station without rows is `empty_station`; a station whose
///   rows switch between 6 and 7 words is `row_shape_changed` (count: the
///   switches). Equal station names get `#2`, `#3` ... suffixes in the id
///   (`duplicate_station_id_renamed`, subject the name, count the renamed).
///   Bytes in a name that are not UTF-8, and NUL, become U+FFFD
///   (`invalid_utf8_replaced`).
/// - Header warnings: `tz_assumed_utc`, `datum_unknown`, `unrecognized_unit`.
///   A v4 header may have a unit where the datum goes ("NOAA UTC ft"): a lone
///   third word that is not a datum but is a unit of a family is the unit.
///
/// `ctx.limits` is enforced here: more than `max_text_bytes` of text, or more
/// than `max_elements` samples (a station costs as much as 16) is
/// `ParseErrc::too_large`; a header line over 4 KiB likewise. `ctx.stop` is
/// polled every 4096 lines and 4096 stations (`Cancelled`).
[[nodiscard]] std::expected<Read<ImedsFile>, Error> parse_imeds(
    std::string_view text, const ReadContext& ctx);

/// parse_imeds on a file: `read_text_file` first (a file over
/// `limits.max_text_bytes` is `FileError{size, file_too_large}` before it is
/// read). Blocking I/O: call it from a worker.
[[nodiscard]] std::expected<Read<ImedsFile>, Error> read_imeds(
    const std::filesystem::path& path, const ReadContext& ctx);

/// The file as IMEDS text (D15, byte-pinned):
///
///   % IMEDS generic format
///   % year month day hour min sec value
///   {source}    UTC    {datum | none}    {unit symbol | unknown}
///   {name}    {lat:.6f}    {lon:.6f}
///   {yyyy:04} {mm:02} {dd:02} {hh:02} {mi:02} {ss:02} {value:14.6f}
///
/// The width 14 is a minimum: a value that needs more digits (1e300 has 300)
/// is wider and the columns do not line up; readers split on white space.
///
/// The table must have exactly one column (`wrong_column_count`); IMEDS has
/// no way to say which of several quantities a block holds. A unit that is
/// literally named "unknown" cannot be told from no unit (`noncanonical_unit`).
/// Times are floored to whole seconds; a time outside the years 0000-9999 is
/// `time_out_of_range`. Each run of white space or `,` in a name becomes `_`
/// (an empty name becomes `station_{k}`, k from 0), and so in `source`. Lines
/// end in `\n`.
///
/// What is written differently from what the table holds is reported, as
/// aggregate warnings (empty subject, count = rows or stations), in this order:
/// - `station_id_not_written`: stations whose id is not the name as written
///   (IMEDS has no id column);
/// - `rows_omitted`: Missing and Dry samples (N18: v4 printed -DBL_MAX);
/// - `time_precision_dropped`: written rows whose milliseconds were cut;
/// - `duplicate_times_dropped`: rows dropped because they floor to the same
///   second as the row before (the first is kept; a dropped row is not also
///   counted as precision-dropped);
/// - `value_reads_as_missing`: written values whose text a reader masks as a
///   legacy sentinel (`-9999.000000`, `-99999.000000`, or -DBL_MAX).
[[nodiscard]] std::expected<Read<std::string>, FormatError> format_imeds(
    const core::StationTable& table,
    std::string_view source = "MetOceanViewer");

/// format_imeds, streamed through an atomic write (write_file_atomic) in 64 KiB
/// chunks. The checks that can fail run before the file is opened. The
/// warnings are format_imeds's.
[[nodiscard]] std::expected<std::vector<Warning>, Error> write_imeds(
    const std::filesystem::path& path, const core::StationTable& table,
    std::string_view source = "MetOceanViewer");

namespace detail {

/// The name as the writer prints it: each run of white space or `,` becomes
/// one `_`; an empty name is `station_{index}`.
[[nodiscard]] std::string imeds_name(std::string_view name, std::size_t index);

/// Whether the writer's text for `value` (`{:.6f}`) is one a reader masks:
/// -9999.000000, -99999.000000, or -DBL_MAX exactly.
[[nodiscard]] bool reads_as_missing(double value);

}  // namespace detail

}  // namespace mov::io
