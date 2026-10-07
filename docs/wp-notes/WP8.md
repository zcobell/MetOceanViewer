# WP8 notes (ADCIRC ASCII, station file, HWM file)

Fold into `docs/core-design.md` §5.3 and §5.7, then delete this file. Everything here
is a choice the design left open, or a change from it, including the changes from the
post-WP8 review (neckbeard-nate, sean-parent) and the maintainer's decisions on it
(D-A to D-D below). The behaviour rules of §5.3 (dry rule, fill, `NaN`/`****`) are
implemented as written.

## What exists

`adcirc_ascii.hpp` (output + station file), `hwm_file.hpp`, and the private
`detail/model_number.hpp` (`parse_model_number`) and `detail/table_error.hpp`
(`to_format_error`) under `src/io/include/mov/io/`; sources `adcirc_ascii.cpp`,
`adcirc_station_file.cpp`, `hwm_file.cpp`, `model_number.cpp`, `table_error.cpp`. CMake
edits are appended at the end of `src/io/CMakeLists.txt` and `tests/io/CMakeLists.txt`:
`mov_io_model_text_tests`, `mov_io_alloc_tests` (see B1 below) and three fuzz targets.

Outside io: `core::normalizing_order` (timeseries.hpp, below), `LineCursor::remaining_bytes()`
(an additive accessor), and `ParseErrc::too_large`, `FormatErrc::projection_unavailable`,
`WarningCode::more_snapshots_than_header` (error.hpp, warning.hpp/.cpp, `test_read.cpp`'s
token table). `ParseErrc`/`FormatErrc` have no `describe()` yet; there is nothing to add an
entry to.

## Decisions applied

- **D-A.** Every text parser takes a `ReadContext`. `parse_adcirc_station_file(text, crs,
  ctx)` bounds the count by `ctx.limits.max_elements` and polls `ctx.stop` every 1024
  stations.
- **D-B.** One code for a limit: `ParseErrc::too_large`. Used for `max_elements` in the
  ADCIRC output (samples), the station file (stations) and the HWM file (numbers: five per
  mark). `FileError{size, file_too_large}` stays the file-size check at open. No
  `FormatErrc::too_large`: no format-level limit needed one.
- **D-C.** Any last line without a newline is cut off: its record is dropped with
  `partial_record_dropped`. (A finished ADCIRC file always ends with one; a cut file's last
  number can be a truncated one that still parses.) A blank unterminated tail is just
  whitespace, not a cut.
- **D-D.** NSnaps is a hint. Records are read to the end of the text; more than NSnaps
  gives `more_snapshots_than_header` (count: the difference, subject `NSnaps n, read m`),
  then the hot-start overlap is normalized. `trailing_text` is no longer raised here:
  garbage after the last record cannot be told from a damaged record, so it is
  `corrupt_record` if more text follows and a dropped cut-off record if it is the last
  thing. Fixture `elevation_restart_past_header.txt`: 144 planned, 100 written, restarted
  from record 90 (155 records; the restart disagrees at record 100): 144 times,
  `more_snapshots_than_header` 11, `times_reordered` 1, `duplicate_times_dropped` 11,
  `conflicting_duplicate_times` 1, and the first run's record 100 is kept.

## Declarations that differ from §5.3 and §5.7

ADCIRC output
- **`parse_adcirc_ascii(text, stations, request, ctx)`**, with `AdcircAsciiRequest{kind,
  cold_start, stations}` (a `StationSelection`; `==` defaulted), instead of `(text,
  stations, kind, cold_start)`. The selection is required. Every record time,
  `cold_start + seconds`, must be within +-2^53 ms of the epoch (`time_out_of_range`).
- **The kind is the caller's**; nothing is inferred from the file name, so
  `read_adcirc_ascii(output, station_file, crs, request, ctx)` has no suffix rule. The
  header's NCOLS must equal `column_count(kind)` (FormatError `wrong_column_count`).
- + `parse_adcirc_ascii_header(text)` / `AdcircAsciiHeader{snapshots, stations, columns}`
  are public: a caller can show NSnaps/NStations before it makes a selection. Line 1 (the
  run description) is not read and may be blank. DT and NSPOOL are not validated.
- Errors: `Error`. A station list or selection for another count is
  `station_count_mismatch`; the subject says both numbers (`NStations is 3, the selection
  is for 4`), and `wrong_column_count` says `NCOLS is 3, this output has 1`.
- **A record is** one header line (`seconds step`: exactly two fields, a number and an
  integer) and NStations lines (`index v1 [v2]`: exactly `1 + NCOLS` fields). On a
  **selected** station the index must be `i + 1`: a line out of step is a record with lines
  lost or doubled. The other lines of **unselected** stations are not read, except that a
  blank line is never a station (corrupt); damage in them goes unseen. The time field
  goes through `parse_model_number`, so `D` exponents work.
- **Partial runs.** A malformed line is `corrupt_record` if any non-blank text follows it,
  and otherwise ends the file as a cut-off record; the text ending between lines inside a
  record is the same; blank lines after the last record are ignored, a blank line inside
  one is malformed. A cut-off record gives `partial_record_dropped` (count 1, subject
  `record N`) and, if it leaves the file short of NSnaps, `fewer_snapshots_than_header`
  (count: the missing records).
- **Values.** As in C9. The N7 partner rule applies to fill (`<= -999`) only: a fill in
  either component of a vector makes both `Missing`; a `NaN`/`****` token makes only its
  own component `Missing` (the magnitude is `Missing` through `combine` either way).
  `nonfinite_masked` counts tokens, **of complete records only** (a dropped record's are not
  counted), and its subject names the first masked line (`first at line 4`).
- **Hot-start overlap** is `core::normalizing_order` over the shared axis (below); the
  gather of the columns is written once in the reader.
- **Reservation (blocker B1).** Nothing is reserved from the header. After the first
  complete record the reader reserves for `remaining bytes / that record's bytes + 1`
  records, capped by `min(NSnaps, estimate, max_elements / cells)` through
  `detail::reserve_capped`. A header claiming 2^60 records therefore costs about twice the
  text, at most. `tests/io/test_adcirc_allocation.cpp` (its own executable, since it
  replaces the global `operator new`) counts the bytes: 2.5 kB for a tiny text with a 2^60
  header, 177 kB for 100 kB of text after the first record (an honest header: 1.7 kB), and
  under 16 kB when `max_elements` is small or a station file claims 10^8 stations.
- Allocation limit: `records x selected stations x columns` over `max_elements` is
  `ParseError{too_large}`.
- Schema labels are the registry `long_name`s. No datum is stated and no `datum_unknown`
  warning is raised (the warning is a station-netCDF concept, SN §12.7).
- Structure (Parent review): a record is a `variant<Complete, CleanEnd, CutOff,
  Malformed>`; `read_values` returns `expected<void, ParseError>`; `read_records` returns
  how the input ended (`clean` or `cut_off`) or an error; the scratch row and the columns are
  shaped `[variable][selected position]` like `core::Variable::per_station`; the selection
  is `vector<optional<size_t>>`; the header parser returns the header and the cursor after
  it; the reader is built once and consumed (`read() &&`).

Station file
- `parse_adcirc_station_file(text, crs, ctx)` returns `Error`, not `ParseError`. A
  `Projector` is built once per file and its `approximation_warning()` is appended.
- **Projection failures are told apart** (Nate S3): an unknown CRS is
  `FormatError{unsupported_crs, "EPSG:n"}`, an unopenable database
  `FormatError{projection_unavailable}` (tested with `MOV_PROJ_DATA` naming a garbage
  `proj.db`), a point PROJ cannot transform `FormatError{bad_coordinates, station = i}`.
  A position that is not a `Location` stays `ParseError{out_of_range}` at the latitude field
  (latitude out of range) or the first coordinate.
- The count line is split like the others (commas and white space; `2,stations` is 2).
- + `read_adcirc_station_file(path, crs, ctx)`.
- Separators are commas and any white space; the name is the rest, words joined by single
  spaces (N6: no leading space); a NUL ends the name (C14); bytes that are not UTF-8 become
  U+FFFD with one `invalid_utf8_replaced` per station. Blank lines are skipped.
  `count_mismatch` is reported at the first extra line, or at the last line of the file
  when there are too few.
- Unreachable key/text failures map through `detail::to_format_error` like every other
  table or key failure (policy for all readers).

HWM file
- `parse_hwm_csv(text, unit, ctx)` returns `Error` (it can be `Cancelled`); the design's
  two-argument form with a `ParseError` was dropped. The stop token is polled every 1024
  rows. The limit counts numbers: marks x 5.
- A file with no mark (empty, blank, header only) is `ParseError{empty_input}`.
- **Header rule** (S5): the first non-blank line is a header iff it has **five or six
  comma fields**, something in them, and **no field looks numeric**. "Looks numeric" is
  wider than "parses": `1e999` and `nan`, `inf`, `Infinity`, `****` count, so a first line
  of `nan,nan,nan,nan,nan` is a `bad_number` error. A words-only line with another number
  of fields (`# lon lat`) is a `wrong_field_count` error, not a header.
- Columns: exactly 5 or 6 fields, fields trimmed; the sixth is not read. Everything else
  follows the WP4 contract (`checked_elevation`, `model_value`, `Location::make`);
  `not_finite` maps to `bad_number` and `out_of_range` to `out_of_range`, with the field's
  column. The first row is peeled off (no first-line flag), and a row is named fields
  (`RowText`).

## core and shared helpers

- **`core::normalizing_order(times, differs) -> NormalizingOrder{kept, report}`**
  (`timeseries.hpp`): the rule of `normalize` on indices: stable sort only if a time
  descends, the first row of each run of equal times kept, `differs(kept, dropped)` called
  once per dropped row. `kept` is the identity when `report.clean()`. `normalize` is now
  written on top of it, and the ADCIRC reader applies the same order to its shared axis, so
  there is one copy of the rule (tests: identity, order and call arguments, agreement with
  `normalize`).
- `detail::to_format_error` is exhaustive and out of line (`table_error.cpp`): `TableError`
  (every `SchemaErrc` and `StationFault` named, no generic arm), `StationKeyError`,
  `StationTextError` and `ProjectionError`. Hoist into `error.hpp` if WP7 wants it public.
- `detail::parse_model_number(token) -> expected<variant<double, NonFinite>, NumberError>`:
  `parse_double` plus what Fortran writes. `NonFinite` for `NaN`/`Inf`/`Infinity` (signed,
  any case), all-asterisk fields (unsigned: the overflow fills the whole field), and a
  number that overflows or underflows a double. Fortran forms: the `D` exponent (`1.5D+02`),
  and a **letterless exponent only as a sign and exactly three digits at the end of the
  token** (`1.5-100`; `1-5` and `1.5-10` are not numbers). The rewrite is in a 64-byte
  buffer, only for tokens with no `E`; `parse_double` still judges the result.
- **Shared helpers (adopted after WP7).** The readers use WP7's `detail::next_word(rest,
  is_separator)` (the station file's comma-or-white-space separator), `split_on_into` (HWM
  fields, then trimmed), `LineCursor::next_nonblank()`, `replace_invalid_utf8` (station
  names), `append_if_counted` (every count-gated warning in the three readers), and these
  additions, which the IMEDS reader shares: `detail::is_blank(text)` (`text.hpp`),
  `LineCursor::peek_nonblank()` (the next non-blank line without consuming it: "does any
  text follow this line?") and `detail::position_at(line, x_token, latitude_token, why)`
  (`parse_at.hpp`: a position that is not a `Location` blames the latitude when that is out
  of range, the first coordinate otherwise; the station file, the HWM file and IMEDS call it).
  The local copies (`with_valid_utf8`, `next_field`, `next_nonblank`, `first_nonblank`,
  `blank_text`, the HWM `split_fields` loop, `position_error` and the HWM latitude test) are
  gone.
  - **Order matters in the station file:** the name is cut at the first NUL (C14) by
    `joined_words` before `replace_invalid_utf8` runs, because that function turns a NUL into
    U+FFFD. The HWM `split_fields` is `split_on_into` plus a trim of each field.
  - **Behaviour differences:** none observable. `position_at` blames the first coordinate for
    `not_finite` (IMEDS used to blame the latitude); `double_at` accepts no NaN or infinity, so
    that case cannot happen.
- **`parse_*` signatures.** Every text reader follows `parse_*(text, ..., ReadContext) ->
  expected<Read<T>, Error>`. The one exception is `parse_adcirc_ascii_header(text)`
  (`expected<AdcircAsciiHeader, ParseError>`): a two-line probe with no limits to enforce, no
  cancellation and no warnings, so a context and `Read` would carry nothing. Its header comment
  says so.

## Fixtures and tests

- `tests/fixtures/io/adcirc/legacy/`: byte-exact copies of
  `MetOceanViewer/function_tests/ReadADCIRC/ASCII/{fort.61,fort.62,fort.71,fort.72,stations.csv}`
  (all under 20 kB, so no trimming and no `[legacy]` skip is needed; the 4.2 MB files are
  the netCDF ones, WP9).
- `tests/fixtures/io/adcirc/`: new, built by a throwaway generator (not committed):
  `elevation_small.txt` (5 records; fill, -999, -998.99, -950), `_partial_record`,
  `_partial_header`, `_cut_mid_line` (no final newline, cut inside a number),
  `_fewer_snapshots`, `_nonfinite`, `_corrupt_mid`, `_hot_start_overlap`,
  `_restart_past_header` (above), `_time_overflow`, `velocity_small.txt`,
  `pressure_small.txt`, `header_three_columns`, `header_only_description`, and the station
  files `stations_names/_crlf/_count_mismatch/_utm15n/_bad_latitude/_bom_blank.csv`.
- `tests/fixtures/io/hwm/`: `hwm_header`, `hwm_blank_line`, `hwm_bom_crlf`, `hwm_header_only`,
  `hwm_bad_first_line`, `hwm_four_columns`, `hwm_out_of_range`, `hwm_dry_marks`. The
  statistics tests also read WP4's `tests/fixtures/core/hwm/*.csv` and compare with
  `golden.txt` through `parse_hwm_csv` (`test_hwm_golden.cpp` includes
  `../core/hwm_fixture.hpp` for the `Golden` reader and checks the parser against the
  test-only loader mark for mark).
- Fuzz targets (`fuzz_parse_adcirc_ascii`, `_station_file`, `fuzz_parse_hwm_csv`), seeds in
  `tests/fixtures/io/parse_adcirc_ascii/` (3-byte prefix: kind, station count, selection
  mask), `parse_adcirc_station_file/`, `parse_hwm_csv/` (1-byte unit prefix). Oracles are in
  each file's header comment. The ASCII oracle "a subset parse is a view of the full parse"
  no longer skips the files where the whole dropped a cut-off record: the subset must then
  hold the same records, plus at most one more (a damaged line of an unselected station
  ends the whole's last record, not the subset's), and only the combination with a
  reordered axis is skipped.
- Regression tests: B1 (`legacy fort.62: B1`; the legacy value is
  `pow(pow(u,2)+pow(v,2),2)` = 6.66e-6, not 0.0025: the earlier comment and
  `docs/legacy-formats.md:115` had the wrong number, and the sqrt is 0.0507918, not
  0.050793; `core-design.md` §7.1 still says "0.050793…"), N2 (HWM dry rule), N5 is core's, N6,
  N7, N20 (blank lines).

## Throughput (N9)

Release build (GCC 14, `-O3`), `mov_io_model_text_tests "[.throughput]"`, one thread,
warm cache, this host:

| Input | Size | Speed |
|---|---|---|
| fort.61, 3000 stations x 400 records, 3 stations selected | 37 MB | about 2500 MB/s (14 ms): the unselected lines cost a `memchr` and a blank test |
| the same, all 3000 selected | 37 MB | about 170 MB/s (216 ms) |
| HWM file, 300000 marks | 11 MB | about 120 MB/s (98 ms) |

A 1 GB output file with a handful of stations selected reads in under a second; with every
station it is a matter of seconds and 16 bytes per sample of memory.

## Results

- 846 tests in the build pass on `dev`, `dev-clang`, `dev-libcxx`, `asan` and `release`; the
  `fuzz` workflow runs the three targets for 10 s each (also 120 s locally); `coverage`
  reports io lines at 93.7 % (WP8 files: `adcirc_ascii.cpp` 97 %, `adcirc_station_file.cpp`
  97 %, `hwm_file.cpp` 99 %, `model_number.cpp` 100 %, `table_error.cpp` 93 %); the tidy gate
  (`MOV_DEV_QT=1`) reports 0 findings; `pre-commit run --all-files` passes.
- One edit outside the WP in tests: `tests/core/hwm_fixture.hpp` (WP4)
  `REQUIRE_FALSE(values_.empty())` became `REQUIRE(not values_.empty())`. The analyzer check
  `clang-analyzer-optin.core.EnumCastOutOfRange` reports Catch2's `REQUIRE_FALSE` on a path
  through the loop when the header is included from `tests/io`; the other new tests use
  `CHECK(not x)` for the same reason.
- clang-tidy's `bugprone-unchecked-optional-access` does not accept `.value()` after a
  Catch2 `REQUIRE(opt.has_value())`; the tests use `value_or` or an `if`.
- GCC 14 `-O3 -Wnull-dereference` reports a false positive in `vector::resize` of a
  `vector<size_t>` in `normalizing_order`; it builds `kept` with `reserve` and
  `back_inserter` instead.

## Unverified

- macOS (Apple libc++) and Windows (MSVC) were not run. New standard-library surface:
  `std::format` in tests, `std::ranges::stable_sort` with a projection and
  `std::views::iota` into `back_inserter` in a header template, a global `operator new`
  replacement in `test_adcirc_allocation.cpp` (not run under MSVC), `setenv`/`_putenv_s`
  in the test support header.
- The station-file projection tests need `proj.db` (found the same way as
  `test_projection.cpp`).
