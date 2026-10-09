# WP10b notes (foreign CF and legacy station netCDF, file-type detection, hostile set, structure fuzzer)

Fold into `docs/core-design.md` §5.6, §5.9, §7.2, §7.3, §7.5 and `docs/station-netcdf.md`
(SN) §11, §12, then delete this file. Everything here is a choice SN or the design left open,
or a change from them. The review round (neckbeard-nate and ben-deane, owner decision 30 of
`docs/rearchitecture-plan.md` §6) is folded into every section; "Review round" lists what it
changed.

## What exists

- `src/io/include/mov/io/station_netcdf.hpp`: `StationFileOrigin` is
  `variant<V5Origin, ForeignCfOrigin{CfDsgLayout, CfVersion}, LegacyOrigin>`;
  `CfDsgLayout {orthogonal, incomplete, contiguous_ragged, indexed_ragged, single_station}`;
  `LegacyOrigin {optional<string> fileformat, bool has_station_ids, StationNumberWidth width}`
  (facts of the file, no dialect label). `read_station_netcdf` and `inspect_station_netcdf`
  dispatch on the kind of file; their signatures are unchanged.
- `src/io/include/mov/io/file_type.hpp`, `src/io/file_type.cpp`: `FileType`, `to_token`,
  `detect_file(path, limits)` -> `FileDetection` (`variant<FileType, AdcircAsciiDetected{header},
  OtherFormatDetected{name}>`), `detect_file_type` (its `FileType`), and the total constexpr
  `file_type_of` for a `FileDetection` and for a `StationFileOrigin`.
- `src/io/include/mov/io/text_file.hpp`: `read_file_prefix(path, max_bytes)` -> `FilePrefix
  {bytes, whole_file}` (opened and checked like `read_text_file`, no size limit, at most
  `max_bytes` read).
- `src/io/netcdf/` : `File::att_type` (one inquiry of an attribute's type).
- `src/core`: Kelvin in `TemperatureUnit`; `quantity_for_standard_name(name, after)`, constexpr
  (the water level pair is found in order: observed, then prediction).
- `src/io/include/mov/io/detail/legacy_station_names.hpp`: `legacy_variable_name` (`%04i` that
  widens).
- Private: `netcdf_kind.{hpp,cpp}` (`classify_netcdf` -> `variant<kind::StationV5, Adcirc, Dflow,
  ForeignCf{CfVersion}, LegacyStation, OtherFormat{name}, Unrecognized{subject}>`: the one rule
  `detect_file` and the station reader share), `station_netcdf_dialects.hpp` (the foreign
  `ForeignSampling` variant, `Placement`, `QualityRules`, the legacy and foreign opened types),
  `station_netcdf_shared.cpp` (helpers the three readers share: `text_of`, `int_att`, `coordinate`,
  `place`, `crs_of_mapping`, `parsed_unit`, `with_datum`, `datum_text`, `normalize_columns`, ...),
  `station_netcdf_foreign_facts.{hpp,cpp}`, `station_netcdf_foreign_open.cpp`,
  `station_netcdf_foreign_schema.cpp`, `station_netcdf_foreign_read.cpp`,
  `station_netcdf_legacy.cpp`. `station_netcdf_samples.cpp` exports the row-wise reader
  (`sample_rows` over a `RowSpec` whose padding is optional, `read_masked`, `times_of`, `axis_of`,
  `clock_of`), which the v5 and foreign readers share. `station_netcdf_format.hpp` has the one
  `writable_token` predicate the writer and every reader use for column tokens.
- New `WarningCode`s: `variable_renamed`, `station_id_substituted`, `quality_flags_ignored`,
  `flagged_samples_masked`, `suspect_samples_kept`; new `FormatErrc` `bad_row_size`,
  `bad_ragged_index`, `inconsistent_metadata`, `ambiguous_station_id`.
- Tests (executable `mov_io_station_netcdf_tests` unless noted): `test_station_netcdf_legacy.cpp`,
  `test_station_netcdf_foreign.cpp` (the five layouts, normalization),
  `test_station_netcdf_foreign_names.cpp` (tolerance, standard-name mapping, F4 substitutes,
  skipped variables, CRS, errors), `test_station_netcdf_foreign_review.cpp` (the review round:
  time ranking, bounded reads, padding options, names, tokens, quality flags, datums, water
  level hints, the order of the warnings), `test_station_netcdf_hostile.cpp` (the reader-level
  hostile set), `test_file_type.cpp`; `test_station_nc_handles.cpp` (in `mov_io_netcdf_tests`, with
  the `--wrap` shims: B5/B6 for the new readers and `detect_file_type`); `read_file_prefix` in
  `test_text_file.cpp`; Kelvin and `quantity_for_standard_name` in the `core` tests.
- Fixtures, written at test time with raw netCDF-C (`tests/io/support/`): `nc_build.hpp`
  (builder), `legacy_fixtures.{hpp,cpp}` (Hmdf::writeNetcdf layout, junk after NUL, 20-byte
  `referenceDate`, CRMS), `foreign_fixtures.{hpp,cpp}` (CF H.2.1 to H.2.5 and 9.2, in every
  variant the tests need). No `ncgen` exists in the dev image, so no CDL is committed.
- Structure fuzzer: `tests/io/fuzz_station_netcdf_structure.cpp`, seeds in
  `tests/fixtures/io/station_netcdf_structure/` (a clean and a spoiled input per template, the
  transposed variants, and three with a huge dimension).
- CMake: trailing appends to `src/io/CMakeLists.txt` and `tests/io/CMakeLists.txt`.

## Declarations that differ from the brief or the design

- **`detect_file_type` / `FileType`** (the work-package brief), not `detect_file_kind` /
  `FileKind` of design §5.9. Values: `station_netcdf`, `foreign_cf_netcdf`,
  `legacy_station_netcdf`, `adcirc_netcdf`, `dflow_netcdf`, `imeds`, `adcirc_ascii`, `hwm_csv`,
  and three reasons for "none of them": `unrecognized_text`, `unsupported_netcdf`,
  `other_format_netcdf` (with the format's name, from `detect_file`). The file name is never
  looked at (the design said "then suffix").
- **SN 12.3 L4 (indexed ragged) is supported**, as the brief asked ("all five layouts"); SN says
  `UnsupportedLayout`. SN is updated.
- **A file that names another `metoceanviewer_format` is `other_format_netcdf` /
  `not_this_format`**, not foreign CF (SN 12.1 read literally would make it foreign). A file
  *without* the attribute is foreign, so a v5 file whose attribute was removed reads as foreign
  CF (the v5 validation test for "format attribute absent" now expects that).
- **No dialect label.** SN §11 / legacy-formats §5 call the dialects A (v4 writer) and B (v4
  reader). They read the same way, so `LegacyOrigin` records the facts (the `fileformat`
  attribute, a `stationId` variable, the width of the station numbers). CRMS (C) is
  `unsupported_netcdf` / `not_this_format`.
- Classification order: v5 marker, ADCIRC (`model`), D-Flow (station coordinate variables),
  foreign CF, legacy. ADCIRC and D-Flow come before CF because their files can carry CF
  attributes.
- Text detection order: ADCIRC ASCII, IMEDS, HWM. ADCIRC first because the two-column rows of
  fort.62/72 look like IMEDS station lines. IMEDS by a first line that is a `%` comment naming
  IMEDS, else by structure. netCDF-4 behind an HDF5 user block (signature at 512 * 2^k) is netCDF.
- `detect_file` returns an `Error` (not a "none" kind) for a file with netCDF magic bytes that
  netCDF-C cannot open (truncated, plain HDF5), and for an attribute over `max_att_bytes`.
- The foreign CF version of a file is the one `classify_netcdf` read from `Conventions`; there
  is no invented default.

## Foreign CF reader

- Variables are found by what CF says they are: `cf_role = timeseries_id` (two: `ambiguous_station_id`
  naming both), latitude/longitude by `standard_name` or the CF 4.1/4.2 unit spellings, time
  by the ranking below, data variables by their dimensions. A variable named by any `coordinates`,
  `bounds` or `grid_mapping` attribute is not data. Among several latitude or longitude candidates
  the one a `coordinates` attribute names wins, else the first in file order.
- **Time** (review B1): of the variables with time units, `standard_name` `time` or `axis` T (not
  `bounds` targets, not scalars, not vectors over the stations), the best rank: (1) the coordinate
  variable of its dimension (its name is its dimension's), (2) listed in a `coordinates`, (3)
  `standard_name`/`axis`, (4) units alone. A tie is `unsupported_layout` naming both.
- Layout from the time variable: 1-D plus a `sample_dimension` variable = contiguous ragged; 1-D
  plus an `instance_dimension` variable = indexed ragged; 1-D with a station dimension =
  orthogonal; 1-D without = single station; 2-D = incomplete (either orientation). Data of an
  orthogonal or incomplete file may be (station, time) or (time, station). The layout is a
  `ForeignSampling` variant (each alternative holds its own station dimension and helper
  variable); where the samples are is a `Placement` variant (matrix, run starts, sample owners).
- Counts: orthogonal = time length; contiguous = the `rowSize` variable (negative, masked,
  overflowing or not adding up to the dimension = `bad_row_size`, with the station); indexed =
  occurrences of each index (outside `[0, stations)` = `bad_ragged_index`, with the sample);
  incomplete = `obs_count` if the file has it (`bad_obs_count`; a masked count is bad), else the
  leading non-missing times of each station, and a time after a missing one is
  `padding_not_missing`. The incomplete scan reads the whole time variable, so it is charged
  stations x obs against `max_elements` (a 2^40 `obs` without `obs_count` is `too_large`, from
  `inspect` too).
- **Bounded reads** (review B2): a (time, station) matrix is read as many time steps as the
  selected stations' samples reach (plus the first padding element), never the whole `obs`;
  `PaddingCheck::whole` reads and checks all of it and is `too_large` when that exceeds the limits.
  Both orientations honour `StationNcReadOptions::padding` when the file has `obs_count`.
- **Quantity**: the standard name is looked up in the registry (`quantity_for_standard_name`);
  it maps only if the file's unit converts to the canonical unit (Kelvin does) and the column is
  free. The two water levels share a standard name: a variable whose name or `long_name` has
  "predict", "tide", "astronomical" or "harmonic" is `water_level_prediction`, any other
  `water_level`; a variable of a taken quantity, or with a unit that does not convert, is generic
  with `unknown_quantity`. Eastward/northward velocity and wind names map to `current_u/v`,
  `wind_u/v` (decision 28); `sea_water_x_velocity` etc. stay generic with their name. A generic
  quantity is named after the variable. Tokens are made in two passes (`writable_token`, the
  writer's predicate: not a format name, no `<token>_status` clash, at most 249 bytes): every
  variable that can keep its name or its registry token claims it first, then the others get
  substitutes (every byte outside `[A-Za-z0-9_]` becomes `_`, a leading non-letter gets `v`, at
  most 64 bytes, then `_2`, `_3`, ...) with `variable_renamed` (subject: the variable's name).
  The label stays `long_name` or the original name.
- **Datum**: `vertical_datum`, else `geopotential_datum_name` of the variable, else that of its
  grid mapping variable, by token, alias or long name (`datum_text`); other text is
  `datum_unknown` and no datum.
- **Quality flags**: the integer `ancillary_variables` targets are flags (see SN 12.5 and decision
  30.1). Their padding is not checked; the data's is.
- **Out-of-order times** are normalized with the IMEDS rule (decision 30.3); the shared axis of an
  orthogonal file for all stations at once.
- Skipped with `skipped_variable`: variables over the station or sample dimension that do not
  have the layout's dimensions, 64-bit, unsigned and `_Unsigned` data, ancillary targets that are
  not flags, name variables that are not used (a `platform_name` or `station_name` text variable
  of another shape, or a second one), and data variables whose masking attributes cannot be read
  (subject `<variable>:<attribute>`). Silently ignored: other text variables, variables over the
  station dimension alone (altitude), anything unrelated.
- Masking is `nc::Masking<T>` of the variable's own type (`_FillValue`, `missing_value`,
  `valid_*`, the library default fill, NaN; `scale_factor`/`add_offset` after). A malformed
  attribute on the time, position or helper variables is an `NcError` from `inspect` too; on a data
  variable it skips the variable; v5 stays strict.
- CRS: the `grid_mapping` of the first data variable that has one (the extended
  `crs: lat lon` form is read). `epsg_code` of a geographic CRS: projected with the native
  point kept. Anything else (no mapping, a projection, another ellipsoid, a dangling name, a
  projected `epsg_code`) is WGS 84 with `crs_assumed`: the positions are degrees by their units
  whatever the grid is.
- Station ids: char rows, NC_STRING (a NULL element is `""`) or integers (decimal text; a masked
  value is `""`); cut at the first NUL, trimmed, invalid UTF-8 replaced (`invalid_utf8_replaced`);
  an empty id is the station's index in decimal (`station_id_substituted`); duplicates become
  `A#2` (`duplicate_station_id_renamed`). No id variable: the decimal index. The name is the text
  variable over the station dimension with `standard_name = platform_name`, else one called
  `station_name`, else the id.
- Warnings are in the order SN 12.7 gives; a test pins it.

## Legacy reader

- Names: `time_station_N`, `data_station_N`, `stationLength_N` with N padded to 4 digits (6
  accepted, widening past the padding). Required: dimension `numStations`, variables
  `stationXCoordinate`, `stationYCoordinate`, `stationName`; `stationId` optional.
- Row stride is the file's (B7). Names: cut at the first NUL (the heap junk after it is never
  looked at), white space collapsed (A8), bytes that are not UTF-8 replaced. Id: `stationId`,
  else the name, else the decimal index; the name of a station with none is `Station <id>`;
  duplicates `#2`.
- EPSG (B10): `HorizontalProjectionEPSG` of the X variable, one inquiry of its type, any signed
  integer type (byte, short, int, int64); absent: 4326 with `crs_assumed`; text, float or unsigned:
  `NcError type_mismatch` (never a code); a code PROJ does not know: `unsupported_crs`. A
  projected CRS is projected with the native point kept.
- Time: `referenceDate` of each time variable, its first 19 characters after the first NUL cut
  (B8; `T` accepted); text after them (`Z`, `+02:00`) is `tz_assumed_utc` with that text (blanks,
  UTC and GMT say nothing); absent = 1970-01-01 with `epoch_used`; not a date = `ParseError
  bad_date`. Seconds from int64, int or double variables, masked in their own type (the int64
  default fill is `time_missing`). `timezone` other than utc/gmt = `tz_assumed_utc` (once per
  distinct text). A series not strictly increasing is put in order (decision 30.3).
- Data: float or double, masked in the variable's type (B9: the default fill is masked, `-99999`
  is a value). The one column is the generic `value`; `units` and `datum` come from the data
  variables, which must agree (`inconsistent_metadata`, subject `units`/`datum`, the station);
  `MHW` and the aliases and long names parse (N8).
- Zero stations is `empty_collection`; a station without samples has an empty series.

## Hostile set (`test_station_netcdf_hostile.cpp`)

| File | Expected |
|---|---|
| 2^31 stations (v5, foreign, legacy `numStations`) on chunked variables | `NcError too_large` (the dimension), also from `inspect` |
| time 2^40 (v5/foreign orthogonal) | `read`: `too_large`; `inspect`: ok |
| obs 2^40, few samples per station (v5, foreign with `obs_count`, both orientations) | ok (boundary read); `PaddingCheck::whole`: `too_large` |
| obs 2^40 foreign without `obs_count` | `too_large` from `read` and `inspect` |
| `stationLength_0001` 2^40 | `read`: `too_large`; `inspect`: ok (2^40 samples) |
| global or variable attribute of 1 MiB + 1 | `NcError too_large`; an attribute never read costs nothing |
| `time:units` of 10 kB | `ParseError bad_time_units`, context at most 120 bytes |
| NC_STRING `Conventions`: one / two | read / `missing_attribute :Conventions` |
| `_FillValue` of another type, with two values, text (classic file, renamed in the bytes) on a foreign data variable | the variable is skipped (`bad:_FillValue`); in a v5 file, or on `time`/`lat`: `NcError type_mismatch` / `count_mismatch` |
| `valid_range` of one value, `add_offset` of two, text `scale_factor`, unrepresentable `missing_value`, a float `valid_range` on a short | foreign data variable skipped (`no_data_variables` if none is left); v5 `NcError` |
| NC_STRING id with a NULL element; id dimension of length 0 | foreign: the index, `station_id_substituted`; v5 `unsupported_layout` for the dimension |
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
| the reviewer's probes (a decoy time variable, a time-major 2^40 matrix, a scalar `platform_name`, a packed `valid_range`, `foo` with `foo_status`) | `test_station_netcdf_foreign_review.cpp`, `test_station_netcdf_hostile.cpp` |

## Structure fuzzer

`fuzz_station_netcdf_structure`: the first byte picks a template (free schema, v5 orthogonal or
incomplete, foreign orthogonal / incomplete / contiguous / indexed / single, legacy, ADCIRC,
D-Flow); the rest spoil it (a missing variable, another type, a vocabulary attribute, a huge
dimension on a chunked variable, a classic file, a decoy time-units variable, a data variable
labelled `time`). The schema is written with netCDF-C to a file, then `detect_file`,
`inspect_station_netcdf`, `read_station_netcdf` (boundary and whole padding), the ADCIRC and D-Flow
inspect and read run on it with small `ReadLimits` (16384 elements, 4 KiB attributes, 4 MiB
results). Oracle: no crash or UB or hang; no allocation over 256 MB (`__asan_default_options` sets
`max_allocation_size_mb`); the tables are consistent (columns as long as times, times increasing,
inspect equals read in origin, stations and schema, the file holds at least what a read returns);
`detect_file` agrees with the origin the reader reports (`file_type_of`) and never names a station
kind the reader refuses; **a template no byte spoiled is a valid file**: it must open and read, and
the table must be the ids, times and values that were written (every foreign layout, transposed
matrices, v5, legacy). Input bytes that are all 1 (all 5: transposed) after the template's byte
spoil nothing; `MOV_FUZZ_TRACE=1` prints whether an input is exact. The harness aborts, naming the
line, when the scratch directory or the file cannot be created.
`MOV_FUZZ_SCRATCH` moves the file to a faster directory (the build tree is on NFS here, which
made an execution ~60 ms instead of ~15 ms). Harness defects the runs found and fixed: an
unbounded attribute loop on exhausted input, classic files with huge dimensions (netCDF-3
fills every variable at `enddef`), a double-to-`long long` UB in the harness's own data write,
a v5 template with transposed matrices (not valid v5), the catalog's sample count against a
normalized read.

Runs (Clang libFuzzer with ASan and UBSan, `MOV_FUZZ_SCRATCH` in the container's `/tmp`, seeds
from `tests/fixtures/io/station_netcdf_structure/`, about 16 executions per second):

| Run | Result |
|---|---|
| ctest `fuzz_station_netcdf_structure` (fuzz workflow, `MOV_FUZZ_SECONDS` = 10) | passed, 11 s |
| 60 s, 120 s | each found a harness-model defect (a v5 template with transposed matrices; the catalog's sample count against a normalized read), fixed |
| 180 s | clean, 2981 runs |
| 300 s | found the legacy template's per-station units and datum (the readers refuse stations that disagree); the template now counts that as spoiled, the input is a seed |
| 300 s | clean, 4984 runs, peak RSS 264 MB |
| 600 s | clean, 9119 runs, peak RSS 279 MB |

## Regressions (plan 1.2)

B4 (float data, legacy and foreign), B5 (handles test), B6 (missing file, `test_station_nc_handles`
and the legacy test), B7 (name_len 7, 50, 300, 150-byte names), B8 (referenceDate 19/20/120
bytes), B9 (default fill, explicit fill, NaN, `-99999` is a value), B10 (missing, text, unknown
EPSG, unsigned types), N8 (MHW), A8 (white space), Nate S1 (junk after the NUL), C14.

## Review round (neckbeard-nate, ben-deane; owner decision 30)

Blockers: the time variable is ranked, a tie is refused (B1); the time-major incomplete read is
bounded by the samples (B2). Should-fix: masking faults skip a foreign data variable (S1);
`platform_name` only over the station dimension (S2); `writable_token` is one predicate for the
writer, the v5 reader and the foreign reader, with two-pass naming (S3); the padding options hold
for the foreign layouts with `obs_count`, `RowSpec::padding` is optional (S4); `ambiguous_station_id`
(S5); the fuzzer checks unspoiled templates, has the decoy and the transposed seeds, fails loudly
(S6); `referenceDate` text after the date is said (S7); `ForeignSampling` and `Placement` variants
replace the `-1` sentinel and the dead branches; `classify_netcdf` and `detect_file` return
variants and carry what they found; `variable_renamed` names the variable only. Owner decisions:
quality flags, datums, normalization, the water level pair, Kelvin, the name rules, id
substitution. Nits: see the commit message and the headers (StationAxis variant, Roles built
once, `read_file_prefix`, dead `Opened::origin` gone, `projector_of` through `collect_read`,
`optional<MetaNotes>`, `read_series`, checked sample sums, HDF5 user block, the IMEDS banner,
hoisted indexed allocations, masked integer ids, shared helpers in one file).

## For the owner: what is left open

1. Positions are assumed WGS 84 whenever the grid mapping is not a geographic `epsg_code`
   (NAD83 GRS80 ellipsoids differ by about a metre).
2. Profile, trajectory and `z`-dependent variables (3-D) are skipped; station altitude is
   dropped.
3. The incomplete layout without `obs_count` reads the whole time matrix to count samples.
   Fine for the files seen; a very large padded matrix is `too_large`.
4. Legacy: stations whose units or datum differ are refused (`inconsistent_metadata`); v4 never
   wrote that, but a hand-edited file could.
5. Text detection: a header-only HWM CSV is `unrecognized_text` (nothing to tell it from any
   CSV); an IMEDS file with no "IMEDS" banner is recognized by its structure after the header.
6. The quality-flag meanings are matched by substring ("bad", "fail", "missing"): a scheme whose
   words contain them for another reason is masked wrongly.

## Not verified here

- Windows and macOS (nothing platform-specific added besides `read_file_prefix`, whose Windows
  branch mirrors `read_text_file`'s and is only compiled by the MSVC cross-check).
- No real foreign or legacy file was available: every fixture is built from the CF document and
  the legacy source. A real NOAA/IOOS/GLOS file, a real v4 `Hmdf::writeNetcdf` output and a real
  D-Flow file should be tried before release. The QARTOD flag values are from the IOOS QARTOD
  manuals as the owner described them.
- The structure fuzzer ran for the durations above; it is a sampler, not a proof.
