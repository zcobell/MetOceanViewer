# WP10b notes (foreign CF and legacy station netCDF, file-type detection, hostile set, structure fuzzer)

Fold into `docs/core-design.md` §5.6, §5.9, §7.2, §7.3, §7.5 and `docs/station-netcdf.md`
(SN) §11, §12, then delete this file. Everything here is a choice SN or the design left open,
or a change from them.

## What exists

- `src/io/include/mov/io/station_netcdf.hpp`: `StationFileOrigin` is now
  `variant<V5Origin, ForeignCfOrigin{CfDsgLayout, CfVersion}, LegacyOrigin{LegacyDialect}>`;
  `CfDsgLayout {orthogonal, incomplete, contiguous_ragged, indexed_ragged, single_station}`;
  `LegacyDialect {a, b}`. `read_station_netcdf` and `inspect_station_netcdf` dispatch on the
  kind of file; their signatures are unchanged.
- `src/io/include/mov/io/file_type.hpp`, `src/io/file_type.cpp`: `FileType`, `to_token`,
  `detect_file_type(path, limits)`.
- `src/io/include/mov/io/text_file.hpp`: `read_text_prefix(path, max_bytes)` (opened and checked
  like `read_text_file`, no size limit, at most `max_bytes` read).
- `src/io/include/mov/io/detail/legacy_station_names.hpp`: `legacy_variable_name` (`%04i` that
  widens).
- Private: `netcdf_kind.{hpp,cpp}` (`classify_netcdf`: the one rule `detect_file_type` and the
  station reader share), `station_netcdf_dialects.hpp`, `station_netcdf_foreign_open.cpp`,
  `station_netcdf_foreign_read.cpp`, `station_netcdf_legacy.cpp`. `station_netcdf_open.cpp` exports
  the helpers it shares (`text_of`, `named`, `coordinate`, `place`, `crs_of_mapping`,
  `with_datum`, `Position`); `station_netcdf_samples.cpp` exports the row-wise reader
  (`sample_rows` over a `RowSpec`, `read_masked`, `axis_of`, `clock_of`), which the v5 and
  foreign readers share.
- New `WarningCode::variable_renamed`; new `FormatErrc` `bad_row_size`, `bad_ragged_index`,
  `inconsistent_metadata`.
- Tests (executable `mov_io_station_netcdf_tests` unless noted): `test_station_netcdf_legacy.cpp`,
  `test_station_netcdf_foreign.cpp` (the five layouts), `test_station_netcdf_foreign_names.cpp`
  (tolerance, standard-name mapping, F4 substitutes, skipped variables, CRS, errors),
  `test_station_netcdf_hostile.cpp` (the reader-level hostile set), `test_file_type.cpp`;
  `test_station_nc_handles.cpp` (in `mov_io_netcdf_tests`, with the `--wrap` shims: B5/B6 for the
  new readers and `detect_file_type`); `read_text_prefix` in `test_text_file.cpp`.
- Fixtures, written at test time with raw netCDF-C (`tests/io/support/`): `nc_build.hpp`
  (builder), `legacy_fixtures.{hpp,cpp}` (Hmdf::writeNetcdf layout, junk after NUL, 20-byte
  `referenceDate`, CRMS), `foreign_fixtures.{hpp,cpp}` (CF H.2.1 to H.2.5 and 9.2, in every
  variant the tests need). No `ncgen` exists in the dev image, so no CDL is committed.
- Structure fuzzer: `tests/io/fuzz_station_netcdf_structure.cpp`, seeds in
  `tests/fixtures/io/station_netcdf_structure/` (a clean and a spoiled input per template).
- CMake: trailing appends to `src/io/CMakeLists.txt` and `tests/io/CMakeLists.txt`.

## Declarations that differ from the brief or the design

- **`detect_file_type` / `FileType`** (the work-package brief), not `detect_file_kind` /
  `FileKind` of design §5.9. Values: `station_netcdf`, `foreign_cf_netcdf`,
  `legacy_station_netcdf`, `adcirc_netcdf`, `dflow_netcdf`, `imeds`, `adcirc_ascii`, `hwm_csv`,
  `unknown`. The file name is never looked at (the design said "then suffix").
- **SN 12.3 L4 (indexed ragged) is supported**, as the brief asked ("all five layouts"); SN says
  `UnsupportedLayout`. SN is updated.
- **A file that names another `metoceanviewer_format` is `unknown`/`not_this_format`**, not foreign
  CF (SN 12.1 read literally would make it foreign). A file *without* the attribute is foreign,
  so a v5 file whose attribute was removed reads as foreign CF (the v5 validation test for
  "format attribute absent" now expects that).
- **`LegacyDialect`**: SN §11 / legacy-formats §5 call the dialects A (v4 writer) and B (v4
  reader). They read the same way, so `a` means "the file carries the writer's marks" (a global
  `fileformat` attribute or a `stationId` variable) and `b` any other file in that layout. It is
  informational. CRMS (C) is `not_this_format` / `unknown`.
- Classification order: v5 marker, ADCIRC (`model`), D-Flow (station coordinate variables),
  foreign CF, legacy. ADCIRC and D-Flow come before CF because their files can carry CF
  attributes.
- Text detection order: ADCIRC ASCII, IMEDS, HWM. ADCIRC first because the two-column rows of
  fort.62/72 look like IMEDS station lines.
- `detect_file_type` returns an `Error` (not `unknown`) for a file with netCDF magic bytes that
  netCDF-C cannot open (truncated, plain HDF5), and for an attribute over `max_att_bytes`.

## Foreign CF reader

- Variables are found by what CF says they are: `cf_role = timeseries_id` (more than one:
  `no_station_id`), latitude/longitude by `standard_name` or the CF 4.1/4.2 unit spellings, time
  by `standard_name`, `axis` or a parsable `<unit> since` `units` (not `bounds` targets), data
  variables by their dimensions. A variable named by any `coordinates`, `bounds` or
  `grid_mapping` attribute is not data. Among several candidates the one a `coordinates`
  attribute names wins, else the first in file order.
- Layout from the time variable: 1-D plus a `sample_dimension` variable = contiguous ragged; 1-D
  plus an `instance_dimension` variable = indexed ragged; 1-D with a station dimension =
  orthogonal; 1-D without = single station; 2-D = incomplete (either orientation). Data of an
  orthogonal or incomplete file may be (station, time) or (time, station).
- Counts: orthogonal = time length; contiguous = the `rowSize` variable (negative, masked,
  overflowing or not adding up to the dimension = `bad_row_size`, with the station); indexed =
  occurrences of each index (outside `[0, stations)` = `bad_ragged_index`, with the sample);
  incomplete = `obs_count` if the file has it (`bad_obs_count`), else the leading non-missing
  times of each station, and a time after a missing one is `padding_not_missing`.
  The incomplete scan reads the whole time variable, so it is charged stations x obs against
  `max_elements` (a 2^40 `obs` without `obs_count` is `too_large`, from `inspect` too; with
  `obs_count` the read is as cheap as for v5).
- Quantity (decision F4): the standard name is looked up in the registry (the first row wins, so
  `water_surface_height_above_reference_datum` is `water_level`, never
  `water_level_prediction`); it maps only if the file's unit converts to the canonical unit, and
  only the first variable per quantity. Eastward/northward velocity and wind names map to
  `current_u/v`, `wind_u/v` (decision 28); `sea_water_x_velocity` etc. stay generic with their
  name. A matching standard name with a unit that does not convert (Kelvin, no unit) is
  generic with `unknown_quantity`. A generic quantity is named after the variable; a name that
  is not a free token is replaced (every byte outside `[A-Za-z0-9_]` becomes `_`, a leading
  non-letter gets `v`, at most 64 bytes, then `_2`, `_3`, ... until the token is not a registry
  token, a reserved name or already taken) with `variable_renamed` (subject
  `original -> token`). The label stays `long_name` or the original name.
- Skipped with `skipped_variable`: variables over the station or sample dimension that do not
  have the layout's dimensions, 64-bit, unsigned and `_Unsigned` data, and the targets of
  `ancillary_variables` (quality flags: not applied, see the owner questions). Silently ignored:
  text variables, variables over the station dimension alone (altitude), anything unrelated.
- Masking is `nc::Masking<T>` of the variable's own type (`_FillValue`, `missing_value`,
  `valid_*`, the library default fill, NaN; `scale_factor`/`add_offset` after). A malformed
  attribute (a `_FillValue` of another type, a `valid_range` of one value) is an `NcError`
  from `inspect` too.
- CRS: the `grid_mapping` of the first data variable that has one (the extended
  `crs: lat lon` form is read). `epsg_code` of a geographic CRS: projected with the native
  point kept. Anything else (no mapping, a projection, another ellipsoid, a dangling name, a
  projected `epsg_code`) is WGS 84 with `crs_assumed`: the positions are degrees by their units
  whatever the grid is.
- Station ids: char rows, NC_STRING (a NULL element is `""`) or integers (decimal text); cut at
  the first NUL, trimmed, invalid UTF-8 replaced (`invalid_utf8_replaced`); duplicates become
  `A#2` (`duplicate_station_id_renamed`); an empty id is `no_station_id`. No id variable: the
  decimal index. The name is a char/NC_STRING variable with `standard_name = platform_name`,
  else the id.

## Legacy reader

- Names: `time_station_N`, `data_station_N`, `stationLength_N` with N padded to 4 digits (6
  accepted, widening past the padding). Required: dimension `numStations`, variables
  `stationXCoordinate`, `stationYCoordinate`, `stationName`; `stationId` optional.
- Row stride is the file's (B7). Names: cut at the first NUL (the heap junk after it is never
  looked at), white space collapsed (A8), bytes that are not UTF-8 replaced. Id: `stationId`,
  else the name, else the decimal index; the name of a station with none is `Station <id>`;
  duplicates `#2`.
- EPSG (B10): `HorizontalProjectionEPSG` of the X variable, any integer type; absent: 4326 with
  `crs_assumed`; text or float: `NcError type_mismatch` (never a code); a code PROJ does not
  know: `unsupported_crs`. A projected CRS is projected with the native point kept.
- Time: `referenceDate` of each time variable, its first 19 characters after the first NUL cut
  (B8; `T` accepted); absent = 1970-01-01 with `epoch_used`; not a date = `ParseError bad_date`.
  Seconds from int64, int or double variables, masked in their own type (the int64 default fill
  is `time_missing`). `timezone` other than utc/gmt = `tz_assumed_utc` (once per distinct text).
- Data: float or double, masked in the variable's type (B9: the default fill is masked, `-99999`
  is a value). The one column is the generic `value`; `units` and `datum` come from the data
  variables, which must agree (`inconsistent_metadata`, subject `units`/`datum`, the station);
  `MHW` and the aliases parse (N8).
- Zero stations is `empty_collection`; a station without samples has an empty series.

## Hostile set (`test_station_netcdf_hostile.cpp`)

| File | Expected |
|---|---|
| 2^31 stations (v5, foreign, legacy `numStations`) on chunked variables | `NcError too_large` (the dimension), also from `inspect` |
| time 2^40 (v5/foreign orthogonal) | `read`: `too_large`; `inspect`: ok |
| obs 2^40, few samples per station (v5, foreign with `obs_count`) | ok (boundary read); `PaddingCheck::whole`: `too_large` |
| obs 2^40 foreign without `obs_count` | `too_large` from `read` and `inspect` |
| `stationLength_0001` 2^40 | `read`: `too_large`; `inspect`: ok (2^40 samples) |
| global or variable attribute of 1 MiB + 1 | `NcError too_large`; an attribute never read costs nothing |
| `time:units` of 10 kB | `ParseError bad_time_units`, context at most 120 bytes |
| NC_STRING `Conventions`: one / two | read / `missing_attribute :Conventions` |
| `_FillValue` of another type, with two values, text (classic file, renamed in the bytes) | `NcError type_mismatch` / `count_mismatch` / `type_mismatch` (`bad:_FillValue`) |
| `valid_range` of one value, `add_offset` of two, text `scale_factor`, unrepresentable `missing_value` | `NcError count_mismatch` / `type_mismatch` |
| NC_STRING id with a NULL element | `no_station_id` (the station) |
| id dimension of length 0 | foreign `no_station_id`; v5 `unsupported_layout` |
| time all fill | `time_missing`, index 0 |
| time dimension not dimension 0 | reads |
| `HorizontalProjectionEPSG` as NC_CHAR | `NcError type_mismatch` |
| `obs_count` -1 or past `obs` | `bad_obs_count` (the station) |
| 1-D id variable | v5 `dimension_mismatch`; foreign `missing_variable latitude` |
| 3-D `time` | `unsupported_layout` (`time`) |
| (time, station) data in a v5 file | `dimension_mismatch` (the variable) |
| 5000 data variables | reads; over the limit `too_large` |
| `coordinates` / `ancillary_variables` / `bounds` / `grid_mapping` naming nothing | ignored (`crs_assumed` for the mapping) |
| ragged index out of range; row sizes not adding up; negative; a `rowSize` of doubles | `bad_ragged_index`; `bad_row_size`; `bad_row_size`; `NcError type_mismatch` |
| `instance_dimension` naming no dimension; both helpers for one dimension | `missing_dimension`; `unsupported_layout` |
| referenceDate of 1 MiB / 1 MiB + 1 | `ParseError bad_date` / `too_large` |
| text, empty, directory, truncated netCDF-4 | `NcError` (`open`) |

## Structure fuzzer

`fuzz_station_netcdf_structure`: the first byte picks a template (free schema, v5 orthogonal or
incomplete, foreign orthogonal / incomplete / contiguous / indexed / single, legacy, ADCIRC,
D-Flow); the rest spoil it (a missing variable, another type, a vocabulary attribute, a huge
dimension on a chunked variable, a classic file). The schema is written with netCDF-C to a file,
then `detect_file_type`, `inspect_station_netcdf`, `read_station_netcdf` (boundary and whole
padding), the ADCIRC and D-Flow inspect and read run on it with small `ReadLimits`
(16384 elements, 4 KiB attributes, 4 MiB results). Oracle: no crash or UB or hang; no
allocation over 256 MB (`__asan_default_options` sets `max_allocation_size_mb`); the tables
are consistent (columns as long as times, times increasing, inspect equals read in origin,
stations and schema); `detect_file_type` agrees with the origin the reader reports and never
names a station kind the reader refuses.
`MOV_FUZZ_SCRATCH` moves the file to a faster directory (the build tree is on NFS here, which
made an execution ~60 ms instead of ~15 ms). Harness defects the run found and fixed: an
unbounded attribute loop on exhausted input, classic files with huge dimensions (netCDF-3
fills every variable at `enddef`), a double-to-`long long` UB in the harness's own data write.

## Regressions (plan 1.2)

B4 (float data, legacy and foreign), B5 (handles test), B6 (missing file, `test_station_nc_handles`
and the legacy test), B7 (name_len 7, 50, 300, 150-byte names), B8 (referenceDate 19/20/120
bytes), B9 (default fill, explicit fill, NaN, `-99999` is a value), B10 (missing, text, unknown
EPSG), N8 (MHW), A8 (white space), Nate S1 (junk after the NUL), C14.

## For the owner: ambiguities in real-world foreign files

1. Quality flags (`ancillary_variables`, CF `flag_values`) are skipped with a warning, not
   applied: a flagged-bad sample is a value. Should they mask? (Needs the flag semantics per
   source.)
2. `water_surface_height_above_reference_datum` has no datum in a foreign file unless it has our
   non-CF `vertical_datum`; a NOAA/IOOS file's datum text (`geopotential_datum_name`, a comment)
   is not read.
3. A standard name whose unit core does not know (`K`) stays generic. Adding Kelvin to core
   would map `air_temperature`.
4. Only the first variable per registry quantity gets it (observed vs predicted water level
   share a standard name and cannot be told apart).
5. Out-of-order or duplicate times are errors (`time_not_increasing`), not normalized like
   IMEDS rows. Real files do contain them.
6. Positions are assumed WGS 84 whenever the grid mapping is not a geographic `epsg_code`
   (NAD83 GRS80 ellipsoids differ by about a metre).
7. Profile, trajectory and `z`-dependent variables (3-D) are skipped; station altitude is
   dropped.
8. A station name needs `standard_name = platform_name`; files that call it `station_name`
   without it get the id as the name (NOAA files usually put `cf_role` on the name).
9. Integer ids are accepted; float ids are `bad_encoding`.
10. The incomplete layout without `obs_count` reads the whole time matrix to count samples.
    Fine for the files seen; a very large padded matrix is `too_large`.
11. Legacy: stations whose units or datum differ are refused (`inconsistent_metadata`); v4 never
    wrote that, but a hand-edited file could.
12. Text detection: a header-only HWM CSV is `unknown` (nothing to tell it from any CSV); an
    IMEDS file with no "IMEDS" word is recognized by its structure after the header.

## Not verified here

- Windows and macOS (nothing platform-specific added besides `read_text_prefix`, whose Windows
  branch mirrors `read_text_file`'s and is only compiled by the MSVC cross-check).
- No real foreign or legacy file was available: every fixture is built from the CF document and
  the legacy source. A real NOAA/IOOS/GLOS file, a real v4 `Hmdf::writeNetcdf` output and a real
  D-Flow file should be tried before release.
- The structure fuzzer ran for the durations in the report below; it is a sampler, not a proof.
