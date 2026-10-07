# WP8 notes (ADCIRC ASCII, station file, HWM file)

Fold into `docs/core-design.md` §5.3 and §5.7, then delete this file. Everything here
is a choice the design left open, or a change from it. The behaviour rules of §5.3
(dry rule, fill, `NaN`/`****`, partial runs) are implemented as written.

## What exists

`adcirc_ascii.hpp` (output + station file), `hwm_file.hpp`, and the private
`detail/model_number.hpp` (`parse_model_number`) and `detail/table_error.hpp`
(`to_format_error`) under `src/io/include/mov/io/`; sources `adcirc_ascii.cpp`,
`adcirc_station_file.cpp`, `hwm_file.cpp`, `model_number.cpp`. CMake edits are
appended at the end of `src/io/CMakeLists.txt` and `tests/io/CMakeLists.txt` (one test
executable, `mov_io_model_text_tests`, and three fuzz targets).

## Declarations that differ from §5.3 and §5.7

ADCIRC output
- **`parse_adcirc_ascii(text, stations, request, ctx)`**, with
  `AdcircAsciiRequest{kind, cold_start, stations}` (a `StationSelection`), instead of
  `(text, stations, kind, cold_start)`. The selection is required and every station
  outside it is skipped without being parsed. `ctx` carries `ReadLimits` and the stop
  token, polled once per record (the parser has to see it to cancel a 100 MB file;
  `read_*` alone could only poll before and after).
- **The kind is the caller's**; nothing is inferred from the file name, so
  `read_adcirc_ascii(output, station_file, crs, request, ctx)` has no suffix rule. The
  header's NCOLS must equal `column_count(kind)` (FormatError `wrong_column_count`).
- + `parse_adcirc_ascii_header(text)` / `AdcircAsciiHeader{snapshots, stations,
  columns}` are public: a caller can show NSnaps/NStations before it makes a
  selection. Line 1 (the run description) is not read and may be blank. DT and
  NSPOOL are not validated.
- Errors: `Error`, as in §5.3. A selection or station list for another count is
  `station_count_mismatch` (subject `selection` or `NStations`).
- **A record is** one header line (`seconds step`: exactly two fields, a number and an
  integer; the integer is what tells a station line apart from a header when lines are
  missing) and NStations lines (`index v1 [v2]`: exactly `1 + NCOLS` fields, the index
  is not read). The time field goes through `parse_model_number`, so `D` exponents work.
- **Partial runs.** The rules of Appendix A, made precise:
  - a line that is malformed (wrong field count, bad number, bad record header) is
    `corrupt_record` if any non-blank text follows it, and otherwise ends the file as a
    partial record (`partial_record_dropped`, count 1, subject `record N`);
  - the text ending between lines inside a record is the same partial record;
  - blank lines after the last record are ignored, a blank line anywhere else is
    malformed;
  - fewer complete records than NSnaps add `fewer_snapshots_than_header` with
    `count` = the missing records (subject `NSnaps n, read m`); both warnings appear for
    a dropped partial record;
  - text after NSnaps records is `trailing_text` (not in the design's list for ADCIRC; a
    run that wrote more records than its header says is not trusted);
  - **a last line with no newline** that would not complete the NSnaps-th record is
    taken as cut off, whatever it parses to. A file killed mid-write ends mid-line far
    more often than a finished one lacks its last newline, and `-9.99990000E+00` cut from
    `-9.9999000000E+004` parses as a plausible number. A finished file without its last
    newline reads in full.
  - selection does not change which lines count: a garbage line of an unselected station
    is never read, so it cannot make a record partial. (The fuzz oracle "a subset is a
    view of the whole" skips the cases where the whole dropped a record.)
- **Values.** As in C9. Two choices the design did not fix:
  - the N7 partner rule applies to fill (`<= -999`) only: a fill in either component of a
    vector makes both `Missing`; a `NaN`/`****` token makes only its own component
    `Missing` (the magnitude is `Missing` through `combine` either way);
  - `nonfinite_masked` counts tokens, not samples.
- Hot-start overlap: record times that are not strictly increasing are stable-sorted, the
  first record of each time kept (`times_reordered` = descents, `duplicate_times_dropped`,
  `conflicting_duplicate_times` = dropped records that differ in any selected value).
  This is `normalize` over a shared axis, written once for the whole table rather than
  per station.
- Allocation: the per-station columns are reserved for
  `min(NSnaps, text size / (4 (1 + NStations)))` records, so a hostile header cannot ask
  for more than the text can hold. `ReadLimits::max_elements` bounds
  `records x selected stations x columns`; exceeding it is `ParseError{out_of_range}`
  (there is no `too_large` code in `ParseErrc`/`FormatErrc`).
- Schema labels are the registry `long_name`s. No datum is stated and no `datum_unknown`
  warning is raised (the warning is a station-netCDF concept, SN §12.7).

Station file
- `parse_adcirc_station_file(text, crs)` returns `Error`, not `ParseError`: an unusable
  CRS is `FormatError{unsupported_crs, "EPSG:n"}`. A `Projector` is built once per file,
  and its `approximation_warning()` is appended. A position that does not project or is
  not a `Location` is `ParseError{out_of_range}` at the latitude field (latitude out of
  range) or the first coordinate.
- `read_adcirc_station_file(path, crs, ctx)` is added (the count of the stations has to be
  known to make a `StationSelection`).
- Separators are commas and any white space, repeated or mixed (`-90,29` and
  `-90 , 29` and tabs); the name is the rest, words joined by single spaces (N6: no
  leading space); a NUL ends the name (C14); bytes that are not UTF-8 become U+FFFD with
  one `invalid_utf8_replaced` per station. Blank lines are skipped. `count_mismatch` is
  reported at the first extra line, or at the last line of the file when there are too few.
  A BOM and CRLF are handled by `LineCursor`.

HWM file
- `parse_hwm_csv(text, unit, ctx)` returns `Error` (it can be `Cancelled`); the design's
  two-argument form with a `ParseError` was dropped. The stop token is polled every 1024
  rows. Row count above `max_elements` is `out_of_range`.
- A file with no mark (empty, blank, header only) is `ParseError{empty_input}`: v4 returned
  code 2 ("no rows") for it, and a header-only file would otherwise load as an empty map.
- Header rule: the first non-blank line is a header iff it has a non-empty field and **no
  field looks numeric**. "Looks numeric" is wider than "parses": `1e999` (too big) and
  `nan`, `inf`, `Infinity`, `****` count, so a first line of `nan,nan,nan,nan,nan` is a
  `bad_number` error and not a skipped header. A line of only commas is not a header
  either (`bad_number`).
- Columns: exactly 5 or 6 fields, fields trimmed; the sixth is not read at all (it may be
  blank or text). Everything else follows the WP4 contract: `checked_elevation` for
  ground and observed, `model_value` for modeled, `Location::make` for the position;
  `not_finite` maps to `bad_number` and `out_of_range` to `out_of_range`, with the field's
  column.

## New private helpers

- `detail::parse_model_number(token) -> expected<variant<double, NonFinite>, NumberError>`:
  `parse_double` plus what Fortran writes. `NonFinite` for `NaN`/`Inf`/`Infinity` (signed,
  any case), all-asterisk fields (unsigned: the overflow fills the whole field), and a
  number that overflows or underflows a double (`1e400`, `1e-400`). Fortran forms: a
  three-digit exponent without its letter (`1.5-100`, `1.25+100`) and the `D` exponent
  (`1.5D+02`). The rewrite happens in a 64-byte buffer and only for tokens with no `E`;
  `parse_double` still judges the result. Underflow being `NonFinite` (not 0) is a
  simplification: ADCIRC writes no such value.
- `detail::to_format_error(TableError)`: the `TableErrc` to `FormatErrc` mapping of §5.0,
  for every reader that builds a `StationTable` (WP7 and WP9 need the same; hoist it into
  `error.hpp` or keep this header). Unreachable from the ADCIRC reader, tested directly.

## Fixtures and tests

- `tests/fixtures/io/adcirc/legacy/`: byte-exact copies of
  `MetOceanViewer/function_tests/ReadADCIRC/ASCII/{fort.61,fort.62,fort.71,fort.72,stations.csv}`
  (all under 20 kB, so no trimming and no `[legacy]` skip is needed; the 4.2 MB files are
  the netCDF ones, WP9).
- `tests/fixtures/io/adcirc/`: new, hand-built by a throwaway generator (not committed):
  `elevation_small.txt` (5 records; fill, -999, -998.99, -950), `elevation_partial_record`,
  `_partial_header`, `_cut_mid_line` (no final newline, cut inside a number),
  `_fewer_snapshots`, `_nonfinite` (NaN, Infinity, ****, -Infinity, `1.25-100`),
  `_corrupt_mid`, `_hot_start_overlap`, `_time_overflow`, `velocity_small.txt`,
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
  each file's header comment; the ASCII one includes "a subset parse is a view of the
  full parse" and the HWM one runs `hwm_stats` on whatever parses (no
  `NonFiniteMoments`) and checks BOM/CRLF invariance.
- Regression tests: B1 (`legacy fort.62: B1`), N5 is core's (`parse_utc_datetime` is used
  for the cold start in one test; its TZ test is WP1's), N6, N7, N2 (HWM dry rule), N20
  (blank lines), N3/N13 are IMEDS (WP7).

## Unverified

- macOS (Apple libc++) and Windows (MSVC) were not run. New standard-library surface:
  `std::format` in tests, `std::ranges::stable_sort`/`adjacent_find` with projections,
  `std::not_fn`.
- The station-file projection tests need `proj.db` (found the same way as
  `test_projection.cpp`).

## Results

- 127 test cases in `mov_io_model_text_tests` (827 in the whole build) pass on `dev`,
  `dev-clang`, `dev-libcxx`, `asan`, `release` and `fuzz`; `coverage` reports io at 93.4 %
  of lines (WP8 files: `adcirc_ascii.cpp` 96 %, `adcirc_station_file.cpp` 96 %,
  `hwm_file.cpp` 99 %, `model_number.cpp` 100 %); the tidy gate (`MOV_DEV_QT=1`) reports
  0 findings in 86 translation units; `pre-commit run --all-files` passes.
- Each new fuzz target also ran 120 s locally from its seeds (0.4 M, 1.0 M and 1.7 M
  executions) without a finding.
- One edit outside the WP: `tests/core/hwm_fixture.hpp` (WP4) `REQUIRE_FALSE(values_.empty())`
  became `REQUIRE(not values_.empty())`. The analyzer check
  `clang-analyzer-optin.core.EnumCastOutOfRange` reports Catch2's `REQUIRE_FALSE` on a path
  through the loop when the header is included from `tests/io`; the other new tests use
  `CHECK(not x)` for the same reason.
- clang-tidy's `bugprone-unchecked-optional-access` does not accept `.value()` after a
  Catch2 `REQUIRE(opt.has_value())`; the tests use `value_or` or an `if`.
