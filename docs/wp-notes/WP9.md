# WP9 notes (ADCIRC netCDF, D-Flow FM, block-read measurement)

Fold into `docs/core-design.md` §4.1, §5.4, §5.5, §7.2, §7.3, then delete this file.
Everything here is a choice the design left open, or a change from it, including the
changes from the post-WP9 review (neckbeard-nate, sean-parent) and the maintainer's
decisions on it (plan decision 28, Q2 to Q4 below). The declarations otherwise match
§5.4 and §5.5.

## What exists

Public headers in `src/io/include/mov/io/`:
- `adcirc_netcdf.hpp`: `adcirc_variables`, `TimeUnitsAttr`, `AdcircNcCatalog`,
  `AdcircNcRequest`, `inspect_adcirc_netcdf`, `read_adcirc_netcdf`, and
  `detail::read_adcirc_netcdf` (with a `GroupingPolicy`, for the tests and the measurements).
- `dflow.hpp`: `DflowDerived` (+ `to_token`, `parse_dflow_derived`), `DflowSource`, `Flat`,
  `Layered` (catalog entries); `FlatChoice`, `AtLayer`, `DflowChoice`, `DflowRequest`
  (requests); `DflowCatalog`, `inspect_dflow`, `read_dflow`.
- `detail/station_groups.hpp` (`GroupingPolicy`, `grouping_for`, `contiguous_stride`,
  `station_groups`) and `detail/adcirc_schema.hpp` (`adcirc_schema(kind, grid)`, shared with the
  ASCII reader).
- `projection.hpp` gained `CrsKind`, `Projector::kind()` and `crs_kind(Epsg)`.

Sources in `src/io/`: `adcirc_netcdf.cpp`, `dflow.cpp`, `adcirc_schema.cpp`, and the private
`model_netcdf.{hpp,cpp}` (what both readers share: `require_dim/var/shape/selection`, the station
list, `read_calendar`, `read_time_axis`, `dispatch_model_numeric`, `plan_groups`, `gather`,
`assemble_table`). The CMake edits are trailing appends to `src/io/CMakeLists.txt` and
`tests/io/CMakeLists.txt`.

Tests in `tests/io/`: `test_adcirc_netcdf.cpp`, `test_dflow.cpp`, `test_station_groups.cpp`
(one executable, `mov_io_model_netcdf_tests`), `test_model_nc_handles.cpp` (in
`mov_io_netcdf_tests`, which has the `--wrap` shims), the hidden `measure_adcirc_netcdf.cpp`,
and `model_nc_support.hpp`. Fixtures: `tests/io/support/model_fixtures.{hpp,cpp}` (library
`mov_nc_fixtures`; raw netCDF-C, like WP6's generator, whose `Raw` handle moved to
`support/nc_raw.hpp`).

Outside the WP: `adcirc_ascii.cpp` takes its schema from `detail::adcirc_schema` (the two
copies could drift; a test compares the two readers' tables) and its grid from the station
file's CRS; the wrapper changes are in `WP6.md` ("Changes made in WP9"); three new
`WarningCode`s (`crs_mismatch`, `cold_start_differs`, `coordinates_from_first_step`, appended
after `rows_omitted`).

## One handle per file (maintainer decision after WP6)

- `File::open` documents the rule. In debug builds `file.cpp` keeps the list of the files a
  `File` has open, **by identity** (`st_dev`/`st_ino`; Windows: volume serial number and file
  index, written but not compiled here), taken when the file is opened, and `open` asserts on a
  second open of the same file. The spelling of the path and the working directory at either open
  do not matter, nor does a symbolic or hard link. A file that cannot be examined (anything but
  "not there", which the open then reports) is an assert of its own: "I could not tell" is not
  "it is not open". The list is touched only inside `nc_status` (the choke point's debug entry
  check, so a second thread using it is caught too); there is no mutex. Release builds keep no list.
- Death tests (`test_nc_call.cpp`, fork, skipped in release): same path, other spelling, symlink,
  hard link, moved handle, and one file named relative to two different working directories
  (abort); two different files with one relative name from two directories, a closed or destroyed
  handle, two files, failed opens (no abort). The child's standard error goes through a pipe and
  the abort must be the one for a second handle (glibc's `assert` text; other libcs only check
  the abort).
- Seven WP6 test cases (thirteen ctest entries) opened a second handle while the first was alive.
  They now do the limited opens first, or close or scope the first. The B5 count test makes 1000
  opens (it made 1200: its fifth path opened twice).
- Both readers open their file once and close it before they return. Their tests open the file
  with `nc::File` right after every read and failed read (asserts in a debug build if a handle
  leaked), and `test_model_nc_handles.cpp` counts 11 opens and 11 closes per round over eleven
  calls whose **first round is asserted to end in what it names** (value,
  `cold_start_required`, `missing_variable`, `station_count_mismatch`, `Cancelled`, a file that is
  not a history file), also in release (B5).

## Vector components and the CRS (plan decision 28)

- `crs_kind(Epsg)` / `Projector::kind()`: `geographic` for any PROJ geographic 2D or 3D CRS
  (EPSG:4326, 4269, 4979, ...), `projected` for a projected CRS; others are `unknown_crs`.
- Geographic: the registry's `current_u`/`current_v` and `wind_u`/`wind_v`. Projected:
  generic quantities `sea_water_x_velocity`, `sea_water_y_velocity`, `x_wind`, `y_wind` (token and
  standard name the CF name; labels "grid-relative current x", "grid-relative wind y", ...), which
  `VectorSeries::make` refuses and `assume_components` pairs; speed is the same, the derived
  direction's label starts "grid-relative". Scalars (elevation, pressure) are unaffected.
  Rotation by the meridian convergence is deferred.
- ADCIRC netCDF and D-Flow take the grid from the `crs` of the request; the ASCII reader from the
  native points of its station file (stations in WGS84 have none), so the three give the same
  schema for the same run. Tests: geographic non-4326, projected velocity/wind/scalar, the ASCII
  reader on a UTM station file, D-Flow flat and derived.
- ADCIRC `ics` (global, int): 1 is Cartesian, 2 spherical; any other value or none says nothing.
  `ics` 2 with a projected `crs`, or 1 with a geographic one, is a `crs_mismatch` warning (subject
  `ics 2 but EPSG:26915 is projected`), from `inspect` and `read`. The legacy files have `ics = 2`.
- **Reverses** the first version's note: `x_velocity` is no longer `current_u` whatever the grid.

## ADCIRC netCDF (§5.4)

Declarations that differ:
- `AdcircNcRequest{kind, cold_start, crs, stations}` (with `==`): the caller names the kind (as
  WP8's ASCII request does), and the catalog still reports the one the file has (`kind`). A kind
  the file lacks is `missing_variable` (`partner_variable_missing` for a missing `v-vel`/`windy`,
  found once, in `data_variables`).
- `adcirc_variables(kind)` names the kind's data variables (one table; the arity of
  `adcirc_schema` is a test); the catalog has `kind`, `stations`, `times` and `time_units`, an
  `optional<TimeUnitsAttr{text, optional<CfTimeUnits> parsed}>` (empty `parsed` is a placeholder).
- Detection is a global `model` of `ADCIRC` (cut at a NUL, trimmed); anything else, **including an
  attribute that is not text**, is `not_this_format`, for `inspect` and `read`. Variable search order
  is the design's; a lone `v-vel` or `windy` is `not_this_format`.
- **The file's CRS is not read** beyond `ics`. The coordinates are `x`/`y` in `crs`, which the
  caller states. `HorizontalProjectionEPSG` and `EPSG` belong to the generic dialects (WP10b). The
  legacy fixtures store `x` and `y` as **floats**: the stations are the floats' values.
- Station names: `station_name` is optional; bytes before the first NUL, `simplified()`, bytes
  that are not UTF-8 replaced (`invalid_utf8_replaced`, count: names), empty gives `Station <id>`.
  The row length is the file's (tested for 10, 50 and 300; v4's D-Flow reader used 200). The whole
  file's coordinates and names are read however few stations are selected (they are small next to
  the data).
- **Clock.** With `cold_start`: seconds after it, calendar proleptic Gregorian; `time:units` is
  only looked at: if it parses and its epoch is **more than a second** from the cold start,
  `cold_start_differs` (subject: the units text). A placeholder or no units says nothing.
  Without: `units` and `calendar` (`read_calendar`: `cut_at_nul`, absent is standard) through
  `parse_cf_time_units`, with an `epoch_used` warning whose subject is the units text. Units that
  are absent or do not parse are the NCDATE placeholder: `cold_start_required` (never
  `ParseError`). A placeholder that parses, such as `1970-01-01`, is taken at its word; the
  `epoch_used` warning is what tells the user. An epoch before 1582-10-15 in the standard calendar
  is `unsupported_calendar`.
- Time: **an int64 time is read as integers and masked by its own attributes** (`masking<int64_t>`,
  so a `_FillValue` or the default fill is `time_missing`, tested with the fill at index 0); others
  through `read_samples`. A masked or NaN time is `time_missing`, one the clock refuses
  `time_out_of_range`, a time not above its predecessor `time_not_increasing`, all with the index
  and not station-specific. The read never gets as far as the values then. One helper builds the
  axis from either.
- **Values.** Dry is judged on the *unpacked* value, before the attribute masking: at or below
  -999 is `Dry` for elevation, so the file's `_FillValue = -99999` gives `Dry`, not `Missing`;
  the library's default fill (9.97e36) and `missing_value` are `Missing`. For the other outputs a
  value at or below -999, or one the attributes mask, is fill: `Missing`. **N7**: each component
  is read into `Gathered{columns, flat fill bitmap}`; one `combine_partner_fill` pass makes both
  components `Missing` wherever either was fill. NaN and infinities empty their own component only
  and are counted in `nonfinite_masked` (subject: the first variable that had one). A packed `zeta`
  whose fill unpacks to below -999 is therefore `Dry` as well (no ADCIRC file is packed).
- **Types.** `dispatch_model_numeric` (shared with D-Flow) runs the read for double, float, int8,
  int16 and int32 only and fails `type_mismatch` for anything else, 64-bit and unsigned included;
  there is no int64 instantiation. A first version let the "other" arm succeed and produced an
  all-`Missing` column; the test over `int64` and `ubyte` data catches it.
- Limits: `selected x times x columns` over `max_elements`, or its `Sample` bytes over
  `max_result_bytes`, is `NcError{too_large}` before any value is read (the reader's own result).
  A catalog with more stations than `max_elements` is `too_large` before a list is made. The
  reads themselves are `read_blocks`, checked per block.

## D-Flow FM (§5.5)

Declarations that differ:
- Dimensions are found by name (`time`, `stations`, `name_len`, and `laydim` if present) and every
  variable is matched to them by id equality, never by position or by a name-to-id map (B12).
  The layer count is `laydim`'s length; `laydimw` is only ever a reason to leave a variable out
  (B12: v4 asked `laydimw` and counted `laydim`).
- **Requests (sean-parent).** A request carries only what selects data: `FlatChoice{source}` or
  `AtLayer`, whose only constructor is `AtLayer::make(const Layered&, one_based)` (so the layer is
  one that catalog entry has; `Layer` is gone as a public type). `long_name` and the layer count are
  display data of the catalog and are not in a request. `read_dflow` also checks the layer against
  the file's own `laydim` (a stale catalog is `layer_out_of_range`, index the one-based layer); a
  layer for a flat variable and none for a layered one are `dimension_mismatch`. Requests have `==`.
- A catalog entry is offered for each variable over `(time, stations)` or `(time, stations,
  laydim)` whose type `nc::sample_readable` accepts; other types over those dimensions are skipped
  with `skipped_variable`. The shape of a variable is one value, `VarShape{no|flat|layered, layers}`
  (no flag and out-parameter). After the file's variables come the derived ones in v4's order whose
  inputs are all there with one shape (a transform over the input names, then `adjacent_find`; the 3-D
  speed needs layered inputs): `3D_current_speed`, `2D_current_speed`, `2D_current_direction`,
  `wind_speed`, `wind_direction`. The inputs of each are a span over a `constexpr` array in the
  table, indexed by the enumerator. These tokens are what v4 sessions stored.
- **Station coordinates over `(time, stations)`** (a model with moving stations): step 0 is read,
  with the warning `coordinates_from_first_step` (subject: the x variable's name). Over `(stations)`
  as before.
- Time: `time:units` must exist (`missing_attribute`) and parse (`ParseError`), `calendar` is
  optional (`read_calendar`, shared with ADCIRC). The catalog carries the `CfTimeUnits` and
  `CfCalendar`.
- **No dry rule, no hard-coded fill.** A value is `Missing` if its variable's `Masking` says so or
  it is NaN (counted).
- Quantities: `waterlevel`, `x_velocity`, `y_velocity`, `windx`, `windy` are the registry's on a
  geographic CRS and the grid-relative generics on a projected one; a name that is a registry token
  is that; a valid token is a `GenericQuantity` with the variable's `standard_name`; anything else
  is `value` with `unknown_quantity`. The label is `long_name` (else the name). The unit is `units`
  through `parse_unit`; absent means the registry quantity's own unit, or none for a generic one; an
  `OtherUnit` the registry does not use warns `unrecognized_unit`.
- **Derived series (Nate S4, Parent 7).** The components' columns are **moved** into one station's
  series at a time, the derived samples are moved out, so the peak is the input columns plus one
  station's series and one copy of the axis (it was a copy of the axis and both columns for every
  station at once). `VectorSeries::make` for the registry pair, `assume_components` for the
  generic pair; `VectorErrc` and `VerticalErrc` map by case (units to `noncanonical_unit`, the
  rest to `dimension_mismatch`). Not done: a column-wise derivation in core (`magnitude(span u,
  span v)` and a derived-meta function), which would also remove the axis copy; it is a change to
  `vector_series` that WP2 owns, left for the owner.
- `Setup` holds the resolved `Listed` values (copies of `VarInfo`), not pointers into itself.
- **The file's CRS is not read** (as for ADCIRC): `crs` is the caller's. D-Flow FM writes
  `station_lon`/`station_lat` for spherical models and a `projected_coordinate_system` variable;
  neither is consulted, because no real his file was available to check them against (plan D26).
  **Unverified against a real file**: that `station_x_coordinate` is present in a spherical model,
  the `time:units` strings (LF A6), and the `(time, stations)` coordinates of Q2.

## Chunk-aware reads and the measurements

`rows_per_block` and the limits on a block are `read_blocks`' (WP6, changed in WP9 so that the
limits apply to each block and not to the whole slab, which deletes the reader's window loop). What
the readers decide is which stations share a `read_blocks` call and what the library keeps in memory
between blocks:
- `File::chunk_shape(name)` returns the chunk sizes or nullopt (contiguous, compact, classic).
- `grouping_for(chunk_shape, station_axis)`: **with chunks**, selected stations in the same chunk
  column (index divided by the chunk size along the stations, found by the id of the station
  dimension, not a position) share one read of the span from the first to the last of them;
  stations in different columns do not. **Without chunks**, stations within `contiguous_stride`
  (1024) of each other share a read, others are read one at a time. `GroupingPolicy::chunk` is
  asserted positive.
- `reserve_chunk_cache`: when the chunk of a variable is bigger than netCDF-C's default cache
  (16 MiB) and fits `max_result_bytes`, the cache is made one chunk big.

Method: `tests/io/measure_adcirc_netcdf.cpp`, hidden, three cases (`[measure][chunks]`,
`[measure][contiguous]`, `[measure][deflate]`), release preset, GCC 14, Xeon E5-2696 v4 (20
threads, other jobs ran on the host), ext4. Each figure is the median of seven timed reads
(five for the deflated files; three when the warm-up took more than 5 s, marked `n=3`) after one
untimed read of the same request; the range is beside it. "per column" reads each station alone
(v4's strided reads), "one block" reads the whole span of the selection, "the readers'" is
`grouping_for`. **Read the figures as orders of magnitude**: the host was shared, the page cache warm,
and one file system tested.

### Chunked storage, 1000 stations x 10000 steps of doubles (80 MB), milliseconds

**Chunks: netCDF-C default (1 step x 1000 stations)**

| selection | per column | one block | the readers' |
|---|---:|---:|---:|
| 1 station | 115.2 (109.4-116.5) | 115.4 (114.2-117.4) | 115.7 (114.0-117.7) |
| 10 neighbours | 1072.6 (1062.5-1091.7) | 130.6 (127.0-134.6) | 131.5 (129.4-133.1) |
| 10 spread (every 100th) | 1003.4 (889.4-1106.9) | 100.5 (98.8-103.7) | 100.5 (99.3-116.2) |
| 100 neighbours | 9074.6 (8884.5-9278.5) n=3 | 136.9 (136.0-144.7) | 140.2 (138.2-146.0) |
| 100 spread (every 10th) | 8386.2 (8342.9-8435.6) n=3 | 127.1 (118.5-133.6) | 126.2 (120.3-130.4) |
| all 1000 | 85079.3 (78475.2-88871.5) n=3 | 534.0 (451.1-605.8) | 495.9 (489.3-503.8) |

**Chunks: 100 steps x 100 stations**

| selection | per column | one block | the readers' |
|---|---:|---:|---:|
| 1 station | 6.2 (5.9-8.1) | 7.4 (6.8-7.6) | 6.7 (6.4-6.9) |
| 10 neighbours | 14.1 (13.1-14.3) | 9.7 (9.1-10.5) | 10.0 (9.4-10.7) |
| 10 spread (every 100th) | 41.2 (40.5-45.2) | 55.0 (54.2-57.2) | 39.7 (38.9-41.3) |
| 100 neighbours | 73.2 (72.2-76.2) | 46.2 (41.8-48.1) | 35.1 (32.8-48.0) |
| 100 spread (every 10th) | 96.6 (92.4-107.2) | 104.2 (100.6-120.8) | 88.1 (83.9-102.4) |
| all 1000 | 736.9 (700.7-750.0) | 494.0 (483.3-520.3) | 496.9 (474.9-517.1) |

**Chunks: 1000 steps x 10 stations**

| selection | per column | one block | the readers' |
|---|---:|---:|---:|
| 1 station | 4.0 (3.8-4.5) | 3.8 (3.6-3.9) | 3.9 (3.8-4.2) |
| 10 neighbours | 7.8 (7.4-8.6) | 7.0 (6.6-7.3) | 6.8 (6.6-7.0) |
| 10 spread (every 100th) | 11.8 (11.6-12.5) | 167.2 (161.4-173.6) | 11.3 (11.1-11.7) |
| 100 neighbours | 49.2 (48.9-51.1) | 51.8 (50.9-54.2) | 36.9 (36.6-40.4) |
| 100 spread (every 10th) | 79.2 (78.0-88.1) | 259.8 (211.9-269.2) | 76.8 (75.6-79.9) |
| all 1000 | 481.1 (456.6-540.9) | 618.7 (601.9-663.3) | 352.5 (345.1-362.2) |

**Chunks: 10000 steps x 1 station (columns)**

| selection | per column | one block | the readers' |
|---|---:|---:|---:|
| 1 station | 4.1 (3.9-4.5) | 3.6 (3.4-4.3) | 3.4 (3.3-3.7) |
| 10 neighbours | 6.5 (6.4-6.7) | 8.2 (7.8-9.4) | 7.6 (6.8-8.2) |
| 10 spread (every 100th) | 7.7 (7.4-8.6) | 718.8 (693.8-733.4) | 6.7 (6.6-7.1) |
| 100 neighbours | 43.4 (41.8-44.3) | 81.2 (78.1-87.9) | 41.5 (41.3-43.6) |
| 100 spread (every 10th) | 41.6 (41.0-46.1) | 928.4 (912.2-960.5) | 37.4 (33.9-40.4) |
| all 1000 | 384.6 (372.4-436.1) | 1190.7 (1160.1-1254.6) | 383.0 (361.8-410.0) |

- On the layout netCDF-C gives a variable with an unlimited time dimension (one chunk per time
  step, all stations in it; ADCIRC writes it so), a read per station touches every chunk of the
  file each time: 1000 stations take about 85 s, a block read 0.5 s (two orders of magnitude), and
  ten stations spread across the file still take ten times longer.
- On a layout chunked by station, a block read over a spread selection reads every chunk column in
  between: two orders of magnitude slower for ten stations a hundred apart (0.7 s against 7 ms).
- Neither fixed strategy is right for both, so the choice follows the chunk columns; the readers'
  column is within noise of the better one, or better where the groups are narrower than the
  span, in all the rows.

### Contiguous storage, 10000 stations x 2000 steps of doubles (160 MB), milliseconds

**Storage: netCDF-4, fixed dimensions, no chunks (contiguous)**

| selection | per column | one block | the readers' |
|---|---:|---:|---:|
| 2 far apart (0, 9999) | 58.2 (57.9-58.8) | 37.8 (37.5-38.7) | 58.2 (57.9-59.2) |
| 2 apart by 1000 | 58.2 (57.8-58.7) | 34.3 (33.9-35.1) | 34.3 (33.8-34.5) |
| 2 apart by 100 | 59.1 (58.3-60.8) | 31.7 (31.3-32.0) | 32.0 (31.5-32.5) |
| 2 apart by 10 | 58.6 (58.5-59.1) | 31.2 (31.0-31.5) | 31.0 (30.8-31.5) |
| 2 apart by 1 (neighbours) | 58.4 (58.2-58.6) | 31.3 (30.8-31.6) | 31.3 (30.9-31.6) |
| 10 neighbours | 274.2 (272.6-276.4) | 31.7 (31.3-33.1) | 31.7 (31.5-32.1) |
| 10 spread (every 1000th) | 270.2 (268.7-272.5) | 37.2 (37.1-37.3) | 37.2 (36.8-38.3) |
| 100 neighbours | 2651.8 (2641.4-2665.8) | 37.8 (37.5-38.0) | 37.5 (37.3-38.0) |

**Storage: classic CDF-1, record variable**

| selection | per column | one block | the readers' |
|---|---:|---:|---:|
| 2 far apart (0, 9999) | 24.4 (24.2-24.8) | 91.9 (90.6-93.6) | 23.6 (23.5-24.6) |
| 2 apart by 1000 | 25.7 (25.6-26.0) | 23.9 (23.5-24.7) | 23.9 (23.6-24.4) |
| 2 apart by 100 | 25.8 (25.4-26.2) | 19.4 (19.2-19.5) | 19.4 (19.3-19.8) |
| 2 apart by 10 | 25.8 (25.4-26.0) | 18.7 (18.5-18.8) | 18.5 (18.3-18.6) |
| 2 apart by 1 (neighbours) | 25.2 (25.1-25.3) | 18.3 (18.2-18.5) | 18.3 (18.2-18.4) |
| 10 neighbours | 79.9 (78.4-81.3) | 18.9 (18.7-19.1) | 19.0 (18.8-19.1) |
| 10 spread (every 1000th) | 85.9 (85.5-86.6) | 88.4 (87.6-89.9) | 87.8 (86.2-91.6) |
| 100 neighbours | 696.0 (659.0-711.1) | 25.8 (25.5-26.1) | 25.8 (25.2-26.2) |

**Storage: 64-bit offset CDF-2, fixed dimensions**

| selection | per column | one block | the readers' |
|---|---:|---:|---:|
| 2 far apart (0, 9999) | 18.2 (18.1-18.4) | 83.2 (82.5-84.6) | 17.5 (17.3-17.8) |
| 2 apart by 1000 | 18.5 (18.3-18.6) | 16.0 (15.7-16.1) | 15.9 (15.8-16.2) |
| 2 apart by 100 | 17.4 (17.3-17.7) | 11.3 (11.0-11.3) | 11.2 (10.9-11.4) |
| 2 apart by 10 | 17.3 (16.9-17.5) | 10.5 (10.3-10.7) | 10.6 (10.2-10.8) |
| 2 apart by 1 (neighbours) | 17.8 (17.5-17.8) | 10.6 (10.5-10.9) | 10.7 (10.6-11.1) |
| 10 neighbours | 70.6 (70.1-73.1) | 10.6 (10.4-11.0) | 10.6 (10.6-10.8) |
| 10 spread (every 1000th) | 78.3 (77.9-79.2) | 78.6 (78.4-80.4) | 79.7 (78.9-81.0) |
| 100 neighbours | 646.5 (630.6-669.4) | 16.6 (16.5-17.3) | 16.7 (16.2-17.5) |

- A classic file (CDF-1 record variable, CDF-2) shows the gap Parent 2 asked about: two stations
  9999 apart read about four times faster separately (24 ms against 92 ms), two stations 1000 apart
  the same either way, and neighbours a block read about 1.4 times faster. `contiguous_stride =
  1024` follows that.
- **Contiguous netCDF-4** shows the reverse, by less: a block read is 1.5 times faster even for the
  far-apart pair (38 ms against 58 ms), because HDF5 reads a contiguous hyperslab differently from
  the classic library. The finite stride therefore costs 1.5 times there and gains four times in
  classic files; neither is more than an order of magnitude, and the stride is one constant.

### Deflated chunks longer than a block

1000 stations x 10000 steps, deflate level 1, the readers' grouping; `slab_elements` is the block
size in elements (the default 2^20 is 1048 steps of 1000 stations).

Without `reserve_chunk_cache`:

| chunks | slab_elements | 1 station | all 1000 |
|---|---|---:|---:|
| deflated 10000 steps x 100 stations (1.0 M elements) | 2^20 | 13.6 (13.3-14.0) | 628.4 (624.6-640.7) |
| deflated 10000 steps x 100 stations (1.0 M elements) | 2^23 | 16.3 (16.0-16.9) | 631.5 (628.8-641.5) |
| deflated 10000 steps x 100 stations (1.0 M elements) | 2^25 | 16.5 (16.3-16.7) | 641.5 (633.8-645.8) |
| deflated 2000 steps x 1000 stations (2 M elements) | 2^20 | 144.5 (143.1-147.2) | 616.0 (607.5-620.7) |
| deflated 2000 steps x 1000 stations (2 M elements) | 2^23 | 146.2 (145.3-150.9) | 832.2 (825.1-834.9) |
| deflated 2000 steps x 1000 stations (2 M elements) | 2^25 | 146.7 (141.8-149.4) | 879.3 (846.4-897.3) |
| deflated 10000 steps x 1000 stations (10 M elements) | 2^20 | 200.5 (197.8-204.9) | 2389.7 (2249.7-2507.1) |
| deflated 10000 steps x 1000 stations (10 M elements) | 2^23 | 194.4 (192.5-200.8) | 1040.9 (1028.8-1077.8) |
| deflated 10000 steps x 1000 stations (10 M elements) | 2^25 | 185.3 (177.9-197.2) | 881.4 (873.1-901.7) |

With it (what the readers do):

| chunks | slab_elements | 1 station | all 1000 |
|---|---|---:|---:|
| deflated 10000 steps x 100 stations (1.0 M elements) | 2^20 | 14.3 (13.9-14.7) | 531.2 (496.2-550.7) |
| deflated 10000 steps x 100 stations (1.0 M elements) | 2^23 | 12.0 (12.0-13.0) | 541.0 (509.2-562.7) |
| deflated 10000 steps x 100 stations (1.0 M elements) | 2^25 | 12.5 (11.9-13.2) | 520.4 (501.9-553.1) |
| deflated 2000 steps x 1000 stations (2 M elements) | 2^20 | 125.4 (118.3-133.2) | 557.6 (533.6-580.9) |
| deflated 2000 steps x 1000 stations (2 M elements) | 2^23 | 127.1 (120.4-132.9) | 770.7 (739.9-830.7) |
| deflated 2000 steps x 1000 stations (2 M elements) | 2^25 | 130.6 (123.4-135.7) | 838.9 (820.8-855.5) |
| deflated 10000 steps x 1000 stations (10 M elements) | 2^20 | 211.7 (206.6-228.8) | 704.2 (677.1-746.9) |
| deflated 10000 steps x 1000 stations (10 M elements) | 2^23 | 237.4 (226.7-252.8) | 956.1 (881.8-1060.1) |
| deflated 10000 steps x 1000 stations (10 M elements) | 2^25 | 204.3 (200.0-213.2) | 927.4 (914.9-971.0) |

- The chunk of 10000 x 1000 (80 MB) is ten blocks long and larger than the default cache, so each
  block decompressed it again: reading all 1000 stations took 2.4 s. With the cache one chunk big
  it takes 0.7 s (3.4 times faster), at the price of 80 MB of cache while the file is open, and
  larger blocks do no better (0.9 s): **the cache is the fix, alignment of the blocks to
  `chunk[0]` is not needed**. Chunks of 1 M and 2 M elements are cached by default and unchanged.
- One station of a 10000 x 1000 chunk still costs the whole chunk (about 200 ms): the price of the
  layout, not of the reader.

Not measured: cold cache, network file systems, deflate levels other than 1, float data.

## Fixtures and tests

- `make_adcirc_nc(path, AdcircNc)`: float, double, int8/16/32 (packed), int64 and ubyte data, any
  value function, `_FillValue` / `missing_value` / `scale_factor` / `add_offset`, `station_name`
  with junk after the NUL and any row length, `time` not dimension 0, transposed data, int64 time,
  time with a fill value, any `time:units` and `calendar`, a missing, other or non-text `model`,
  `ics`, explicit chunking, deflate, CDF-1/CDF-2/netCDF-4.
- `make_dflow_nc(path, DflowNc)`: `laydim` and `laydimw` of different lengths, flat, layered and
  interface variables, float, double, integer or 64-bit, per-variable units / names / fill / text
  or numeric attributes, any `time:units` and `calendar`, blank or NUL name padding at any
  `name_len`, `stations` before `time`, coordinates over `(time, stations)`, and each of the
  dimensions and variables left out.
- `[legacy]` (skip when `MetOceanViewer/function_tests` is absent): the four 4.2 MB ADCIRC files
  are read in place: catalog (names `Station One...`, float coordinates, the `seconds since Met`
  placeholder, `ics = 2`, `cold_start_required` without a cold start), first and last values pinned
  to 17 digits (fort.61's first station is dry throughout), and the ASCII and netCDF output of the
  run agree to 1e-9 (`tests/fixtures/io/adcirc/legacy/fort.6x`), the schema and times exactly.
- Regression tags: B4 (float variables and coordinates), B5 (open and close counts), B11 (station
  name stride, D-Flow time units), B12 (dimensions by name and id, `laydim`/`laydimw`, no hard-coded
  fill, missing dimension or variable), B2, N7, N16, D28 (the grid decides the components). The
  magnitude from the netCDF `u-vel`/`v-vel` is tagged as **parity** with the ASCII B1 test, not as
  B1. B10 has no place here: neither format carries an EPSG attribute, the CRS is the caller's, and
  the exact-type rule is WP6's `numeric_att` (tested there) for WP10b. B15 is a writer bug.
  Mutations run by hand (first version): a name stride of 200 and a hard-coded `-999 ==` each fail
  9 test cases.

## Review items and what became of them

| Item | Result |
|---|---|
| Q1 (decision 28) | above; plan §6 decision 28; `crs_mismatch` for `ics` |
| Q2 | step 0 of `(time, stations)` coordinates, `coordinates_from_first_step` |
| Q3 | measured (above): the chunk cache, not alignment |
| Q4 | `cold_start_differs` beyond 1 s |
| Sean Q | `AtLayer::make` only; requests carry source and layer |
| S1 | identity at open; assert on lookup failure; chdir and message death tests |
| S2, S3 | int64 time masked; non-text `model` is `not_this_format` |
| S4 + Parent 7 | columns moved into the derivation; core column-wise derivation noted |
| S5 + Parent 3 | `Gathered{columns, fill}` + `combine_partner_fill` |
| Parent 1 | WP6 `read_blocks` limits per block; windows deleted |
| Parent 2 + S6 | contiguous measurement; `contiguous_stride`; medians and ranges |
| Parent 4, 5, 6, 8 | `Setup` by value; `VarShape`; `dispatch_model_numeric` / `sample_readable`; `TimeUnitsAttr` and `adcirc_variables` |
| Nits | done, except: station metadata is still read for the whole file (documented); `chunk_by` and `enumerate` are comments where a raw loop stands in |

## Gates

All on the final tree, Linux x86-64 (Docker dev image): `dev`, `dev-clang`, `dev-libcxx`,
`asan`, `release` (1122 tests each), `fuzz` (1136), `coverage` workflows pass;
`pre-commit run --all-files` (stable on the second run); the tidy gate (131 translation units,
0 findings).

Coverage (gcovr, lines): **io 93.1%** overall (6639 of 7134; the gate is 90%); the WP9 files:
`adcirc_netcdf.cpp` 90%, `dflow.cpp` 89%, `model_netcdf.cpp` 90%, `adcirc_schema.cpp` 87%,
`netcdf/file.cpp` 91%, `station_groups.hpp` 100%. What stays uncovered is mostly the error
returns of the library calls (allocation, a netCDF call failing on a file that opened) and the
Windows identity code.


## Deviations from the brief

- No fuzz target: a netCDF reader's input is a structure, which is the structure fuzzer of WP10b.
- `AdcircNcRequest` / `DflowRequest` carry `crs` (the design's), and the file's own CRS is ignored
  except ADCIRC's `ics`.
- The wrapper changed (`WP6.md`): `chunk_shape`, `reserve_chunk_cache`, `sample_readable`, the
  per-block limits of `read_blocks`, the identity-based detector.
- `adcirc_ascii.cpp` was touched (the shared schema and grid), a hunk apart from the text-helper
  consolidation the other agent is doing; three `WarningCode`s were appended after `rows_omitted`;
  `docs/rearchitecture-plan.md` has decision 28.
- The reader returns the schema of `adcirc_schema(kind, grid)`; the design's registry mapping for
  projected grids is replaced by decision 28.

## Not verified here (Linux x86-64 only)

- Windows and macOS: not run. New library surface: the Windows `file_identity`
  (`CreateFileW` + `GetFileInformationByHandle`, not compiled), `fork()` death tests (skipped on
  Windows; the message match is glibc's).
- Real D-Flow FM files, and ADCIRC files written with a real NCDATE.
- The measurements: one machine, a shared host, one file system, warm cache.
