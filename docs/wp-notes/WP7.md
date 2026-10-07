# WP7 notes (IMEDS, CSV export, read-chain test)

Fold into `docs/core-design.md` §5.0, §5.2, §5.8, §7.2, then delete this file. Everything
here is a choice the design left open, or a change from it, including the changes from the
post-WP7 review (neckbeard-nate, conor-hoekstra) and the maintainer's decisions on it.

Headers: `imeds.hpp`, `csv_export.hpp`, `detail/station_names.hpp`, `detail/civil_time.hpp`;
additions to `detail/text.hpp`, `detail/line_cursor.hpp`, `warning.hpp`, `error.hpp`.
Sources: `imeds.cpp` (reader), `imeds_write.cpp`, `csv_export.cpp`, `station_names.cpp`.
Tests: `test_imeds.cpp`, `test_imeds_format.cpp`, `test_csv_export.cpp`,
`test_text_helpers.cpp`, `test_civil_time.cpp`, `test_read_chain.cpp` (the real chain),
`fuzz_parse_imeds.cpp`, helpers in `tests/io/table_helpers.hpp`. CMake edits are trailing
appends to `src/io/CMakeLists.txt` (`target_sources`) and `tests/io/CMakeLists.txt`
(`mov_io_text_formats_tests`, `fuzz_parse_imeds`).

## Cross-reader conventions (maintainer decisions D-A, D-B), for WP8 to follow

- **A text `parse_*` takes a `ReadContext`** and enforces it itself:
  `parse_imeds(text, ctx) -> expected<Read<ImedsFile>, Error>`. The error is `io::Error`
  (not `ParseError`), because a parse can be `Cancelled`; `lift<Error>` does not convert a
  narrower variant. `read_imeds` is `read_text_file` + the same driver (`drive` in
  `imeds.cpp`: header lines with `next()`, then `next_nonblank()`, stop polled every 4096
  lines, then `finish(stop)`, which polls every 4096 stations). `parse_imeds` and
  `read_imeds` each ask the stop token once before they start; the driver does not.
- **`ParseErrc::too_large`** (exactly this name) is "a ReadContext limit was exceeded".
  `line` is the line where the reader noticed; `context` is `"<used> <unit>, limit <n>"`
  (units: `elements`, `bytes`); there is no separate count field. `FileError{size,
  file_too_large}` is only the file-size check in `read_text_file`. A string over
  `max_text_bytes` given to `parse_imeds` is `too_large` at line 1. There is no
  `describe()` in the tree yet, so none was extended.
- **Elements.** `max_elements` counts samples plus **16 per station** (`station_cost`): a
  station costs a few hundred bytes (block, id, name, axis and column vectors), as much as
  sixteen samples, and a file of nothing but station lines would otherwise be unbounded.
  Checked in the line loop. A header line over 4 KiB is `too_large` as well.
- **Shared helpers** (WP8 adopted them; its local copies are gone):
  `detail/station_names.hpp` (`replace_invalid_utf8` returns `{core::StationText,
  replaced}`; `uniquify_ids`); `detail::split_on_into(text, delimiter, span)` (the comma
  counterpart of `split_ws_into`); `detail::next_word(rest, is_separator)` (the one-argument
  form is `is_space`); `detail::is_blank(text)`; `LineCursor::next_nonblank()`, `peek_blank()`
  and `peek_nonblank()`; `detail::position_at` (`parse_at.hpp`); `append_if_counted(warnings, w)`
  in `warning.hpp`; `detail/civil_time.hpp` (below).
- New warning codes (appended after `legacy_dialect`): `row_shape_changed`, `empty_station`,
  `value_reads_as_missing`, `station_id_not_written`, `rows_omitted`.

## Declarations that differ from §5.2 and §5.8

- **Reader.** Line 3 is `<source> [<zone> [<datum> [<unit>]]]`, split with `next_word` three
  times (the rest of the line, trimmed, is the unit, so `S m-1` works; `unknown` is no unit).
  Only the source is required. A zone other than UTC/GMT/Z (or none) is `tz_assumed_utc`; an
  unparsable datum is `datum_unknown` (a warning); an `OtherUnit` outside the registry's set
  is `unrecognized_unit`. **v4 header with the datum left out:** a lone third word that is not
  a datum but is a unit of a family (`NOAA UTC ft`) is the unit. Warning subjects are cut
  UTF-8-safely to `ParseError::max_context_bytes`. Lines 1 and 2 are neither validated nor kept.
- **Rows.** 3 words is a station, 6 or 7 a row, anything else `wrong_field_count`. A 3-word
  line of three integer tokens **right after a row** is `wrong_field_count` (a row cut short;
  a station with an integer name and integer coordinates can only start a block or follow a
  station). A row before the first station is `missing_header`. Years 0000-9999, seconds 0-59.
  A switch between 6 and 7 words within a station is `row_shape_changed` (count: switches);
  a station without rows is `empty_station`. Both are per station, subject the (cut) id.
- **Non-finite values.** `nan`, `inf`, `infinity` (any case, optionally signed) and a run of
  `*` are Missing with `nonfinite_masked` (one count for the file). Exact sentinels as before:
  -99999, -9999, -DBL_MAX bit-exact, and the token `-1.7977e+308` (`parse_double` calls it
  out of range). `1e999` stays an error.
- **Warning order** from the reader: header (zone, datum, unit); per station in file order
  (`empty_station`, `row_shape_changed`, `times_reordered`, `duplicate_times_dropped`,
  `conflicting_duplicate_times`); `invalid_utf8_replaced`; `duplicate_station_id_renamed` (one
  per distinct name); `legacy_sentinel_masked`; `nonfinite_masked`.
- **Names.** Bytes that are not UTF-8, and NUL, become U+FFFD (`invalid_utf8_replaced`, count
  = names). C14's "cut at the first NUL" is for fixed-width char arrays; an IMEDS name is a
  text token and cutting could leave it empty. Duplicate ids: the k-th later `A` is `A#k+1`,
  skipping ids taken (`A, A, A#2` gives `A, A#2, A#2#2`); the `name` stays `A`.
- **Writer.** `format_imeds` returns `expected<Read<std::string>, FormatError>` and
  `write_imeds` `expected<std::vector<Warning>, Error>` (the design had a plain string and
  `void`). Both run `scan_imeds` first (one column; unit not named "unknown", which would read
  back as no unit: `noncanonical_unit`; the first and last value row of each station inside
  [0000-01-01, 10000-01-01) by time-point comparison, `time_out_of_range` with station and
  row; counts for the warnings), so `emit_imeds` has no failure path. `write_imeds` streams
  64 KiB chunks through `write_file_atomic` and stops when the stream fails (a full disk).
  Aggregate warnings, empty subject, in this order: `station_id_not_written` (stations whose id
  is not the name as written; IMEDS has no id column), `rows_omitted` (Missing and Dry),
  `time_precision_dropped` (written rows whose milliseconds were cut), `duplicate_times_dropped`
  (rows dropped for sharing a second with the row before; **the two counts are a partition**:
  a dropped row is not also a cut row), `value_reads_as_missing` (written values whose
  `{:.6f}` text is `-9999.000000` or `-99999.000000`, or that are -DBL_MAX exactly; they are
  written, because a reader masks exactly those). The `{:14.6f}` width is a minimum: 17-digit
  and 1e300 values widen the row (pinned by a test). An empty name is `station_{k}`, k the
  0-based station index. `underscored` is `unique` + `replace_if`.
- **`format_csv`** (§5.8, §9.1): header
  `station_id,station_name,time_utc,quantity,value,status,unit,datum`; CRLF line ends; UTF-8,
  no byte order mark; rows ordered station, then schema column, then time. Cells with `,`,
  **`;`** (the field separator of decimal-comma locales), `"`, CR or LF are quoted. The guard
  `'` covers id, name, quantity, unit and datum. `write_csv` streams in 64 KiB chunks;
  `format_csv` moves the buffer out (no second copy). `time_utc` is `yyyy-mm-ddThh:mm:ss.mmmZ`
  with a minus sign and as many digits as needed outside 0000-9999.
- **Calendar.** `detail/civil_time.hpp` has Hinnant's `days_from_civil` / `civil_from_days`
  (public domain) on `int64`, `civil_fields(Time)`, `time_of(CivilTime)`, `days_in_month`,
  and the IMEDS bounds as `Time` constants with `in_imeds_range(t)`. chrono's
  `year_month_day` has a 16-bit year (+-32767) and `max_abs_time_ms` is about +-285,000
  years; neither the reader nor the writer builds one. **Goldens:** +max is
  `287396-10-12T08:59:00.991Z` and -max is **`-283457-03-21T15:00:59.009Z`** (the review
  message said `-03-22`; an independent Python calendar walk and the `civil_from_days` agree on
  `-03-21`, so that is what is pinned).

## Findings

- LF §12 gives 15,363 non-header data lines for `mllw.imeds`. The file has 15,362 rows,
  7,681 per station (the 15,363 counted header line 1, which has 7 words).
- **Cost measured** (release build, `getrusage` peak, includes the generated text; a
  throw-away test, not committed): 9M station lines (151 MB) stop at the 8.4M-station
  limit after 2.4 s with peak RSS 876 MB; 2M stations all named `A` (20 MB) parse and
  assemble in 4.0 s with peak 962 MB (about 450 bytes per station once assembled; the
  unordered_map/set of `uniquify_ids` is in that); 10M sorted rows of one station (250 MB)
  2.8 s, peak 751 MB; the same unsorted (sort and dedupe path) 4.1 s, peak 675 MB. So a row
  is about 40-50 bytes at peak and the default `max_elements` (2^27) allows several GB for a
  text file: the default is the design's "about 1 GiB of doubles" and is generous for a
  `Sample` plus `Time` plus the copies of a parse.
- A `.6f` value of -1e-7 prints `-0.000000` and reads back as -0.0, equal to 0.0 under
  `Sample`'s `==`.
- Round trip: every fixture reads, writes and reads to an equal table. The second read
  warns only for what the file still says (`duplicate_station_id_renamed`, `empty_station`).

## Fixtures

`tests/fixtures/io/imeds/` (also the seed corpus of `fuzz_parse_imeds`): `mllw_small` (no
final newline) and `msl_small` are excerpts (8 rows per station) of the legacy files; the
rest are hand-written by a throw-away generator (not committed; the files are the source).
Added in the review round: `truncated_row_mid/eof`, `shape_per_station`, `empty_station_eof`,
`nonfinite`, `header_unit_no_datum`, `sentinel_collision` (the fuzz seed for
`value_reads_as_missing`), `int_coordinates`. The read-only file (B3) is made at test time.

## Fuzz oracle (`fuzz_parse_imeds`)

A parse error is a `ParseError` naming an existing line (or the one after the last), never the
internal `corrupt_record`; the samples that are not values are bounded by the masked counts; an
accepted file formats and re-parses to the same stations (ids from `uniquify_ids(imeds_name)`,
names, locations and values to 6 decimals, a longitude may wrap), with value rows conserved
(`values(T2) + value_reads_as_missing == values(T)`), the same datum and unit and only the
warnings the text still earns; and, when nothing is masked by the text and every value is under
1e9 (at most 15 significant digits), `format(parse(format(T))) == format(T)` byte for byte.
Mutation checks: `{:14.6f}` to `{:14.3f}` is caught in 30 s.

## Portability

No `fold_left`, `zip`, `chunk_by`, `enumerate`, `pairwise`, `join_with` or `ranges::to`; no
constant-evaluated `variant<string>`; no `std::stop_token`. No `year_month_day` in the readers
or writers. `std::format` with integer, string and `{:.6f}` arguments, and `format_to_n` for
the sentinel text. The tests use no POSIX header (the read-only and TZ tests follow
`test_text_file.cpp` and `test_time.cpp`).

## Unverified

- macOS (Apple libc++) and Windows (MSVC): not run.
- The "stream failed, stop the export" path of `write_imeds` / `write_csv` (a full disk) has
  no test; the flush callback returning false is not reachable without a failing stream.
- Whether the UI wants `rows_omitted` shown on export.
