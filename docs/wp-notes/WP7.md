# WP7 notes (IMEDS, CSV export, read-chain test)

Fold into `docs/core-design.md` §5.2, §5.8, §7.2, then delete this file. Everything
here is a choice the design left open, or a change from it. Headers: `imeds.hpp`,
`csv_export.hpp`, `detail/station_names.hpp`; sources `imeds.cpp` (reader),
`imeds_write.cpp`, `csv_export.cpp`, `station_names.cpp`. Tests: `test_imeds.cpp`,
`test_imeds_format.cpp`, `test_csv_export.cpp`, `test_read_chain.cpp` (now the real
chain), `fuzz_parse_imeds.cpp`, helpers in `tests/io/table_helpers.hpp`. CMake edits
are trailing appends to `src/io/CMakeLists.txt` (`target_sources`) and
`tests/io/CMakeLists.txt` (`mov_io_text_formats_tests`, `fuzz_parse_imeds`).

## Declarations that differ from §5.2 and §5.8

- **`format_imeds` returns `expected<Read<std::string>, FormatError>`** and
  **`write_imeds` returns `expected<std::vector<Warning>, Error>`** (the design had
  `expected<std::string, FormatError>` and `expected<void, Error>`). The writer has
  two things to report that are not errors: rows whose milliseconds were cut
  (`time_precision_dropped`, count = rows) and rows dropped because they floor to the
  same second as the row before (`duplicate_times_dropped`, count = rows; the first
  is kept, as `normalize` does). Both are aggregate warnings with an empty subject.
  A non-value in the same second is not a collision.
- **`ImedsHeader`** is as designed. Line 3 is `<source> [<zone> [<datum> [<unit>]]]`:
  only the source is required (a blank line 3 is `missing_header`). The unit is the
  **rest of the line** after the datum word (so `S m-1` works); `unknown` means no
  unit. A zone other than UTC/GMT/Z, or a missing zone, is `tz_assumed_utc` (subject:
  the token); an unparsable datum is `datum_unknown` (a warning, not an error: v4 did
  not interpret the line); an `OtherUnit` outside the registry's canonical set is
  `unrecognized_unit`. `none` is no datum. Lines 1 and 2 are neither validated nor kept
  (LF §2.1 wanted the raw lines kept for round trips; the design dropped them).
- **Station blocks are told from rows by word count**: 3 words is a station
  (`name lat lon`), 6 or 7 a row, anything else `wrong_field_count` at the first word.
  So a station name has no spaces (as in the format and as the writer guarantees).
  A row before the first station is `missing_header` (line, column 0).
- **Years are 0000-9999** on read (`bad_date` otherwise, at the year word) and on write
  (`FormatError{time_out_of_range, id, station, index}`), because the format has four
  digits. Seconds 0-59 (no leap second), hour 0-23.
- **`read_imeds` and the limits.** `max_text_bytes` is `read_text_file`'s. The design
  has no error for "more elements than `max_elements`" in a text reader, so
  `read_imeds` returns `FileError{size, path, file_too_large}` once the sample count
  exceeds it (the same code WP5 uses for the byte limit). `ctx.stop` is asked before the
  file is read and every 4096 lines. `parse_imeds` has no context, so it never stops.
- **Names.** Bytes that are not well-formed UTF-8, and NUL, become U+FFFD with one
  `invalid_utf8_replaced` warning (count = names changed). C14 says legacy sources cut at
  the first NUL; that rule is for fixed-width char arrays, an IMEDS name is a text
  token, and cutting could leave an empty id. `detail::replace_invalid_utf8` and
  `detail::uniquify_ids` (WP5 left `uniquify_ids` to this package) are in
  `detail/station_names.hpp`. The id of the k-th duplicate of `A` is `A#k+1`, skipping
  an id already taken (`A, A, A#2` gives `A, A#2, A#2#2`); the `name` stays `A`. One
  `duplicate_station_id_renamed` warning per distinct name (subject: the name, count:
  renames).
- **Warning order** from the reader: header (zone, datum, unit), then per station in file
  order (`times_reordered`, `duplicate_times_dropped`, `conflicting_duplicate_times`,
  subject: the id), then `invalid_utf8_replaced`, `duplicate_station_id_renamed`,
  `legacy_sentinel_masked` (one for the file, empty subject, count = masked values).
- **Sentinels.** Exactly -99999, -9999, -DBL_MAX (bit-exact) and the **token**
  `-1.7977e+308` (any case of `e`). `parse_double` calls that token `out_of_range` (it is
  above DBL_MAX), so it is matched as text before the number is parsed. Other spellings of
  the same value (`-1.79770e308`) are errors, as is any other overflow.
- **Writer details the design left open:** an empty `source` is `MetOceanViewer`; the
  source is cleaned like a name; the empty-name fallback is `station_{k}` with `k` the
  0-based station index. The **station id is not written** (IMEDS has no id column), so a
  table whose ids differ from its names (SN files, providers) loses the id on the way
  out. Missing and Dry rows are omitted without a warning (there is no warning code for
  "non-values omitted"; add one if the UI should tell the user).
- **`format_csv`** (§5.8, with the §9.1 status column): header
  `station_id,station_name,time_utc,quantity,value,status,unit,datum`; **CRLF** line ends
  (RFC 4180); UTF-8, no byte order mark; rows ordered station, then schema column, then
  time. `time_utc` is written by hand (`yyyy-mm-ddThh:mm:ss.mmmZ`, a minus sign and as
  many digits as needed outside 0000-9999), not with `{:%FT%T}`: it does not depend on
  libc++'s chrono formatting and it defines the extended years. `quantity` is
  `core::token`, `unit` is `symbol`, `datum` the token; the last two are empty when
  unset. The injection guard covers id, name, quantity, unit and datum; a guarded cell
  that also needs quotes is `"'...`. `write_csv` streams in 64 KiB chunks.

## Findings

- LF §12 gives 15,363 non-header data lines for `mllw.imeds`. The file has 15,362 rows,
  7,681 per station (the 15,363 counted header line 1, which has 7 words). The `[legacy]`
  test pins 7,681 and the first and last value of each station of both files.
- A 6-word row whose value is `3` (an integer) is a value, not a second; the 6/7 split is
  by word count only (N3).
- A `.6f` value of -1e-7 prints `-0.000000` and reads back as -0.0, equal to 0.0 under
  `Sample`'s `==`.
- Round trip: every fixture except `duplicate_stations` reads, writes and reads to an equal
  table with no warnings; the duplicate-name file warns again (the names are still equal
  in the file).

## Fixtures

`tests/fixtures/io/imeds/` (also the seed corpus of `fuzz_parse_imeds`): `mllw_small` (no
final newline) and `msl_small` are excerpts (8 rows per station) of the legacy files; the
rest are hand-written by a throw-away generator (not committed; the files are the source):
sentinels, duplicate stations, unsorted, BOM, CRLF, tabs, six_field, mixed_fields, blank
lines, empty stations, header variants, bad dates/coordinates/numbers, UTF-8 and non-UTF-8
and NUL names. The read-only file (B3) is made at test time.

## Fuzz oracle

`fuzz_parse_imeds`: a parse error names a line that exists (or the one after the last) with
a short context and is never the internal `corrupt_record` at line 0; an accepted file
formats, re-parses with only `duplicate_station_id_renamed` / `unrecognized_unit` warnings,
and gives the same stations (ids from `uniquify_ids(imeds_name(...))`), the same value
samples at the same times, values/latitudes/longitudes equal to 6 decimals (a longitude
may wrap at +-180 after rounding), and the same datum and unit.

## Portability

No `fold_left`, `zip`, `chunk_by`, `enumerate`, `pairwise`, `join_with` or `ranges::to`;
no constant-evaluated `variant<string>`; no `std::stop_token`. The tests use no POSIX
header (the read-only test follows `test_text_file.cpp`: the precondition is an `ofstream`
open that must fail). `std::chrono::year_month_day`, `hh_mm_ss` and `floor` are used on
`sys_time`; `std::format` with integer and `{:.6f}` arguments only.

## Unverified

- macOS (Apple libc++) and Windows (MSVC): not run.
- Whether the UI wants a warning for omitted non-values on export.
