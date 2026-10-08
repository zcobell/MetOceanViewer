# WP9 notes (ADCIRC netCDF, D-Flow FM, block-read measurement)

Fold into `docs/core-design.md` §4.1, §5.4, §5.5, §7.2, §7.3, then delete this file.
Everything here is a choice the design left open, or a change from it. The
declarations otherwise match §5.4 and §5.5.

## What exists

Public headers in `src/io/include/mov/io/`:
- `adcirc_netcdf.hpp`: `AdcircNcCatalog`, `AdcircNcRequest`, `inspect_adcirc_netcdf`,
  `read_adcirc_netcdf`, and `detail::read_adcirc_netcdf` (with a `GroupingPolicy`, for the
  tests and the measurement).
- `dflow.hpp`: `DflowDerived` (+ `to_token`, `parse_dflow_derived`), `DflowSource`, `Flat`,
  `Layered`, `Layer`, `AtLayer`, `DflowChoice`, `DflowCatalog`, `DflowRequest`,
  `inspect_dflow`, `read_dflow`.
- `detail/station_groups.hpp` (`GroupingPolicy`, `grouping_for`, `station_groups`) and
  `detail/adcirc_schema.hpp` (`adcirc_schema`, shared with the ASCII reader).

Sources in `src/io/`: `adcirc_netcdf.cpp`, `dflow.cpp`, `adcirc_schema.cpp`, and the private
`model_netcdf.{hpp,cpp}` (what both readers share: `require_dim/var/shape`, the station list,
the time axis, `plan_groups`, `gather`, `assemble_table`). The CMake edits are trailing appends
to `src/io/CMakeLists.txt` and `tests/io/CMakeLists.txt`.

Tests in `tests/io/`: `test_adcirc_netcdf.cpp`, `test_dflow.cpp`, `test_station_groups.cpp`,
the hidden `measure_adcirc_netcdf.cpp`, and `model_nc_support.hpp`; one executable,
`mov_io_model_netcdf_tests`. Fixtures: `tests/io/support/model_fixtures.{hpp,cpp}` (library
`mov_nc_fixtures`; raw netCDF-C, like WP6's generator, whose `Raw` handle moved to
`support/nc_raw.hpp`).

Outside the WP: `adcirc_ascii.cpp` now takes its schema from `detail::adcirc_schema` (the two
copies could drift; a test compares the two readers' tables); `nc::File::chunk_shape` is new
(below); the WP6 tests that held two handles on one file were changed (below).

## One handle per file (maintainer decision after WP6)

- `File::open` documents the rule. In debug builds `file.cpp` keeps the list of the files a
  `File` has open, and `open` asserts on a second open of the same file. The list is touched only
  inside `nc_status` (the same choke point and debug entry check as every netCDF-C call, so a
  second thread using it is caught too); there is no mutex. Release builds keep no list.
- "The same file" is `std::filesystem::equivalent`, not equal canonical paths: a symbolic link, a
  hard link and `dir/./x.nc` are caught. A moved `File` is still open; a closed one, a dropped one
  and a failed `open` are not in the list.
- Death tests (`test_nc_call.cpp`, fork, Linux, skipped in release): same path, other spelling,
  symlink, hard link, moved handle (abort); closed or destroyed handle, two different files,
  failed opens (no abort).
- Seven WP6 test cases (thirteen ctest entries) opened a second handle while the first was alive
  (to open the same fixture with other `ReadLimits`). They now do the limited opens first, or close
  or scope the first handle. The B5 count test makes 1000 opens (it made 1200: its fifth path
  opened twice); the counts it pins are 1000.
- Both readers open their file once and close it before they return. Their tests open the file
  with `nc::File` right after every read and failed read, which asserts in a debug build if a
  handle leaked; `test_model_nc_handles.cpp` (in `mov_io_netcdf_tests`, with the `--wrap` shims,
  so also in release) counts 11 opens and 11 closes per round over eleven calls that end in a
  value, `cold_start_required`, `missing_variable`, `station_count_mismatch`, `Cancelled` and a
  file that is not a history file (B5).

## ADCIRC netCDF (§5.4)

Declarations that differ:
- `AdcircNcRequest{kind, cold_start, crs, stations}`: the caller names the kind (as WP8's ASCII
  request does), and the catalog still reports the one the file has (`kind`). A kind the file
  lacks is `missing_variable` (`partner_variable_missing` for a missing `v-vel`/`windy`).
- `AdcircNcCatalog` has `variables` (the kind's data variable names), `time_units` (the text as
  written) and `parsed_time_units` (nullopt for a placeholder) besides `kind`, `stations`, `times`.
- Detection is a global `model` of `ADCIRC` (cut at a NUL, trimmed); anything else is
  `not_this_format`, for `inspect` and for `read`. Variable search order is the design's; a lone
  `v-vel` or `windy` is `not_this_format`.
- **The file's CRS is not read.** The coordinates are `x`/`y` in `crs`, which the caller states
  (design: every model reader requires an `Epsg`). `HorizontalProjectionEPSG` and `EPSG` belong to
  the generic dialects (WP10b); the ADCIRC files carry neither. The legacy fixtures store `x` and
  `y` as **floats**: the stations are the floats' values (`-90.01270294189453125`).
- Station names: `station_name` is optional; bytes before the first NUL, `simplified()`, bytes
  that are not UTF-8 replaced (`invalid_utf8_replaced`, count: names), empty gives `Station <id>`.
  The row length is the file's (tested for 10, 50 and 300; v4's D-Flow reader used 200).
- **Clock.** With `cold_start`: seconds after it, calendar proleptic Gregorian, and the `units`
  attribute is not read (the legacy fixtures say `seconds since Met`). Without: `units` and
  `calendar` of `time` through `parse_cf_time_units` / `parse_cf_calendar`, with an `epoch_used`
  warning whose subject is the units text. Units that are absent or do not parse are the NCDATE
  placeholder: `cold_start_required` (never `ParseError`). A placeholder that parses, such as
  `1970-01-01`, is taken at its word; the `epoch_used` warning is what tells the user. An epoch
  before 1582-10-15 in the standard calendar is `unsupported_calendar`.
- Time: int64 times are read as integers; others through `read_samples`; a masked or NaN time is
  `time_missing`, one the clock refuses `time_out_of_range`, a time that is not above its
  predecessor `time_not_increasing`, all with the index (and not station-specific, which
  `StationTable::make` would make it). The read never gets as far as the values then.
- **Values.** Dry is judged on the *unpacked* value, before the attribute masking: at or below
  -999 is `Dry` for elevation, so the file's `_FillValue = -99999` gives `Dry`, not `Missing`;
  the library's default fill (9.97e36) and `missing_value` are `Missing`. For the other outputs a
  value at or below -999, or one the attributes mask, is fill: `Missing`, and a fill in either
  component empties both (N7). NaN and infinities empty their own component only and are counted
  in `nonfinite_masked` (subject: the first variable that had one), as in the ASCII reader.
  A packed `zeta` whose fill unpacks to below -999 is therefore `Dry` as well (no ADCIRC file is
  packed; pressure is the tested packed case).
- **64-bit integer and unsigned data are refused** (`type_mismatch`, the variable named). A first
  version let `dispatch_numeric`'s "other" arm succeed and produced an all-`Missing` column; the
  test over `int64` and `ubyte` data catches it.
- Limits: `selected x times x columns` over `max_elements`, or its `Sample` bytes over
  `max_result_bytes`, is `NcError{too_large}` before any value is read. A catalog with more
  stations than `max_elements` is `too_large` before a list is made.

## D-Flow FM (§5.5)

Declarations that differ:
- Dimensions are found by name (`time`, `stations`, `name_len`, and `laydim` if present) and every
  variable is matched to them by id equality, never by position or by a name-to-id map (B12).
  The layer count is `laydim`'s length; `laydimw` is only ever a reason to leave a variable out
  (B12: v4 asked `laydimw` and counted `laydim`). Tests: `laydimw` without `laydim` (no layered
  variable, no error), `laydim` without `laydimw` (layered), `stations` before `time`.
- `Layer::make(const Layered&, one_based)` and `AtLayer{Layered, Layer}` are the design's.
  `read_dflow` also checks the layer against the file's own `laydim` (a stale catalog is
  `layer_out_of_range`, index the one-based layer), a layer for a flat variable and none for a
  layered one are `dimension_mismatch`.
- A catalog entry is offered for each numeric variable (byte, short, int, float, double) over
  `(time, stations)` or `(time, stations, laydim)`; other types over those dimensions are
  skipped with `skipped_variable` (an int64 counter, say). After the file's variables come the
  derived ones in v4's order whose inputs are all there with one shape (the 3-D speed needs
  layered `x`, `y` and `z_velocity`): `3D_current_speed`, `2D_current_speed`,
  `2D_current_direction`, `wind_speed`, `wind_direction`. These tokens are what v4 sessions stored
  (`to_token`, `parse_dflow_derived`). The wind of a 3-D file is flat (N16: no layer is passed).
- Time: `time:units` must exist (`missing_attribute`) and parse (`ParseError`, from
  `parse_cf_time_units`, so `bad_time_units` for an unknown unit and `bad_date`), `calendar` is
  optional. The catalog carries the `CfTimeUnits` and `CfCalendar`. Tests: seconds with and without
  `+00:00`, minutes, hours with a one-digit month, days with a `T` and `Z`, a negative zone.
- **No dry rule, no hard-coded fill.** A value is `Missing` if its variable's `Masking` says so
  (`_FillValue`, `missing_value`, valid range, the library default) or it is NaN (counted).
  Tests: a bed level of -1500 is a value; `-999` is a value when `_FillValue` is -9999 and
  `Missing` when it is -999; a variable without `_FillValue` masks 9.97e36, not -999.
- Quantities: `waterlevel`, `x_velocity`, `y_velocity`, `windx`, `windy` are the registry's; a
  name that is a registry token is that; a valid token is a `GenericQuantity` with the variable's
  `standard_name`; anything else is `value` with `unknown_quantity`. The label is `long_name`
  (else the name). The unit is `units` through `parse_unit`; absent means the registry quantity's
  own unit, or none for a generic one; an `OtherUnit` the registry does not use warns
  `unrecognized_unit`. As in the design (§5.5), `x_velocity` / `y_velocity` are `current_u` /
  `current_v` whatever the grid: SN §6 says "only when the model grid is geographic", and in a
  projected model they point along the grid's axes, not east and north. The reader does not
  know the grid (the file's CRS is not read), so it does not apply that condition; a decision for
  the owner is whether `crs != EPSG:4326` should make them generic.
- Derived series are `core::VectorSeries::magnitude` / `cartesian_direction` / `magnitude3` of
  each station's columns, so labels and units are core's (§2.9). The schema's meta comes from the
  same call on empty series, so it exists for an empty selection. Components in different or
  unknown units are `noncanonical_unit`.
- **The file's CRS is not read** (as for ADCIRC): `crs` is the caller's. D-Flow FM writes
  `station_lon`/`station_lat` for spherical models and a `projected_coordinate_system` variable;
  neither is consulted, because no real his file was available to check them against (plan D26).
  The synthetic files follow the manual's names; **unverified against a real file**: that
  `station_x_coordinate` is present in a spherical model, and the `time:units` strings (LF A6).

## Chunk-aware reads (Nate S7) and the measurement

`rows_per_block` is read_blocks' partition and is not re-derived. What the readers decide is which
stations share a `read_blocks` call and how long its time window is:
- `File::chunk_shape(name)` (new, additive) returns the chunk sizes or nullopt for contiguous or
  compact storage (a classic file's too).
- `grouping_for(chunk_shape, station_axis)`: selected stations in the same chunk column (their
  index divided by the station chunk size) share one read of the span from the first to the last
  of them; stations in different columns do not. Not chunked reads like one column.
- `rows_per_window`: `File::read_blocks` checks its slab as a whole against `max_elements` and
  `max_result_bytes` although it holds one block, so a group wider than the limit allows is read
  in windows of time steps that fit. Two far-apart stations of a huge file read (slowly) instead
  of failing `too_large`; tested with a small `max_elements`.

Measured on `tests/io/measure_adcirc_netcdf.cpp` (`mov_io_model_netcdf_tests "[.measure]"`, release
preset, GCC 14, Xeon E5-2696 v4, ext4, page cache warm): a 1000-station x 10000-step `zeta` file of
doubles (80 MB) in four chunk layouts; best of three, milliseconds, for a read of the selected
stations into a `StationTable`. "per column" reads each station alone (v4's strided reads),
"one block" reads time blocks over the whole span of the selection, "chunk-aware" is what the
readers do.

| chunks (time x station) | selection | per column | one block | chunk-aware |
|---|---|---:|---:|---:|
| netCDF-C default (1 x 1000) | 1 station | 96.1 | 96.8 | 93.9 |
| | 10 neighbours | 845.7 | 108.3 | 106.8 |
| | 10 spread (every 100th) | 841.0 | 80.8 | 80.2 |
| | 100 neighbours | 9301.3 | 137.5 | 127.6 |
| | 100 spread (every 10th) | 8306.4 | 131.3 | 127.0 |
| | all 1000 | 89881.6 | 468.2 | 468.1 |
| 100 x 100 | 1 station | 7.5 | 6.8 | 6.9 |
| | 10 neighbours | 13.0 | 9.6 | 9.5 |
| | 10 spread | 40.4 | 57.3 | 38.9 |
| | 100 neighbours | 73.9 | 49.8 | 39.6 |
| | 100 spread | 97.2 | 101.1 | 74.7 |
| | all 1000 | 634.2 | 393.2 | 365.2 |
| 1000 x 10 | 1 station | 2.7 | 2.5 | 2.4 |
| | 10 neighbours | 5.0 | 4.2 | 4.2 |
| | 10 spread | 6.2 | 70.1 | 6.2 |
| | 100 neighbours | 33.7 | 31.4 | 25.6 |
| | 100 spread | 63.1 | 118.0 | 64.0 |
| | all 1000 | 396.5 | 404.5 | 300.5 |
| 10000 x 1 (columns) | 1 station | 2.4 | 2.4 | 2.4 |
| | 10 neighbours | 4.5 | 5.1 | 4.4 |
| | 10 spread | 4.5 | 667.3 | 4.5 |
| | 100 neighbours | 29.1 | 61.8 | 30.1 |
| | 100 spread | 29.4 | 863.0 | 36.9 |
| | all 1000 | 369.2 | 1184.4 | 348.8 |

Reading the table:
- On the layout netCDF-C gives a variable with an unlimited time dimension (one chunk per time
  step, all stations in it; ADCIRC writes it so), a read per station touches every chunk of the
  file each time: 1000 stations take **90 s**, a block read **0.47 s** (190x), and even ten
  stations spread across the file take 10x longer.
- On a layout chunked by station, a block read over a spread selection reads every chunk column
  in between: 150x slower for ten stations a hundred apart (667 against 4.5 ms).
- Neither fixed strategy is right for both, so the choice follows the chunk columns; the
  chunk-aware column is within noise of the best (or better, where the groups are narrower than the
  whole span) in all 24 rows.
- A single station is the same either way. The first `read_adcirc_netcdf` of a file also pays for
  the time axis, coordinates and names (a few ms here).
- Not measured: compressed files (deflate makes each chunk touched more expensive, which only
  strengthens the case for touching fewer), cold cache, network file systems, a time-chunked
  layout with chunks much shorter than the block (HDF5's chunk cache decides).

## Fixtures and tests

- `make_adcirc_nc(path, AdcircNc)`: float and double (and int16 packed, int64, ubyte) data, any
  value function, `_FillValue` / `missing_value` / `scale_factor` / `add_offset`, `station_name`
  with junk after the NUL and any row length, `time` not dimension 0, transposed data, int64 time,
  time with a fill value, any `time:units`, a missing `model`, explicit chunking.
- `make_dflow_nc(path, DflowNc)`: `laydim` and `laydimw` of different lengths, flat, layered and
  interface variables, float or double, per-variable units / names / fill, any `time:units` and
  `calendar`, blank or NUL name padding at any `name_len`, `stations` before `time`, and each of
  the dimensions and variables left out.
- `[legacy]` (skip when `MetOceanViewer/function_tests` is absent): the four 4.2 MB ADCIRC files
  are read in place: catalog (names `Station One...`, float coordinates, the `seconds since Met`
  placeholder, `cold_start_required` without a cold start), first and last values pinned to 17
  digits (fort.61's first station is dry throughout), and the ASCII and netCDF output of the run
  agree to 1e-9 (`tests/fixtures/io/adcirc/legacy/fort.6x`), the schema and times exactly.
- Regression tags: B4 (float variables and coordinates, ADCIRC and D-Flow), B5 (open and close
  counts), B11 (station name stride at lengths other than 200 and 64, D-Flow time units), B12
  (dimensions by name and id, `laydim`/`laydimw`, no hard-coded fill, missing dimension or
  variable), B2 (a file that is not a history file, and one that is), N7 (partner fill), N16
  (layers 0 and n + 1; wind without a layer), B1 (the magnitude from the netCDF `u-vel`/`v-vel`).
  B10 (an EPSG attribute read with the wrong type) has no place here: neither format carries an
  EPSG attribute, the CRS is the caller's, and the exact-type rule is WP6's `numeric_att` (tested
  there) for the generic dialects of WP10b. B15 is a writer bug. Mutations run by hand: a name
  stride of 200 and a hard-coded `-999 ==` each fail 9 test cases.

## Gates

All green on `dev`, `dev-clang`, `dev-libcxx`, `asan`, `release`, `fuzz` and `coverage` (the
death tests skip in `release` and `fuzz`, which define `NDEBUG`), the tidy gate (0 findings in 130
translation units) and `pre-commit run --all-files`. Line coverage (gcovr) is 94 % over `src/`
and 96 % over `src/io` (floors 80 % and 90 %); `adcirc_netcdf.cpp` 91 %, `dflow.cpp` 88 %,
`model_netcdf.cpp` 92 %, `station_groups.hpp` 100 %. The lines left are the forwarding of a
netCDF-C failure (`return std::unexpected{x.error()}`) and template instantiations nobody uses.

## Deviations from the brief

- No fuzz target: a netCDF reader's input is a structure, which is the structure fuzzer of WP10b.
- `AdcircNcRequest` / `DflowRequest` carry `crs` (the design's), and the file's own CRS is ignored
  (above).
- `File::chunk_shape` is a wrapper addition (the brief said to use the wrapper API).
- The WP6 tests and the B5 count changed (above).
- `adcirc_ascii.cpp` was touched (the shared schema), a hunk apart from the text-helper
  consolidation the other agent is doing.

## Not verified here (Linux x86-64 only)

- Windows and macOS: not run. New library surface: `std::filesystem::equivalent` in the debug
  detector (Windows opens the file to compare), `fork()` death tests (skipped on Windows).
- Real D-Flow FM files, and ADCIRC files written with a real NCDATE.
- The measurement is one machine, one file system and a warm cache.
