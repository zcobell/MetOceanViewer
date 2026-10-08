# WP10a notes (station netCDF: v5 writer and reader, format-compliance job)

Fold into `docs/core-design.md` §5.6, §7.2 and `docs/station-netcdf.md` (SN), then delete
this file. Everything here is a choice SN or the design left open, or a change from them,
including the changes from the post-WP10a review (neckbeard-nate, ben-deane) and the
maintainer's decisions on it (table at the end).

## What exists

- `src/io/include/mov/io/station_netcdf.hpp`:
  - versions: `station_nc_format`, `StationNcVersion` (+ `station_nc_version`,
    `parse_station_nc_version`), `CfVersion` (+ `parse_cf_conventions`);
  - writing: `StationNcLayout`, `choose_layout`, `StationNcWriteOptions`,
    `validate_station_netcdf` (the writer's verdict without a file), `write_station_netcdf`;
  - reading: `AllStations`, `StationNcSelection`, `PaddingCheck`, `StationNcReadOptions`,
    `V5Origin`, `StationFileOrigin`, `StationFile`, `CatalogStation`, `StationNcCatalog`,
    `inspect_station_netcdf`, `read_station_netcdf`;
  - `detail::StationNcWriteLimits`, `detail::validate_station_netcdf` (with limits) and
    `detail::write_station_netcdf` (with a `FaultInjector`).
- Sources: `station_netcdf.cpp` (versions, layout), `station_netcdf_write.cpp`, and the
  reader in three parts behind the private `station_netcdf_reader.hpp`:
  `station_netcdf_open.cpp` (header, structure, CRS, stations, schema: everything a
  catalog holds), `station_netcdf_samples.cpp` (times and samples of a selection),
  `station_netcdf_read.cpp` (entry points). `station_netcdf_format.hpp` (private) has the
  names, attribute values, WKT and chunk rule. The reader reuses WP9's `model_netcdf.hpp`
  helpers (`require_dim`, `optional_text`, `read_time_axis`, `read_calendar`, `make_clock`,
  `plan_groups`, `check_result_size`, `dispatch_model_numeric`).
- `read.hpp` gains `collect` (applicative: runs thunks in order, stops at the first error,
  yields a tuple) and `collect_read` (the same over `Read`, warnings in stage order).
- `nc::DimInfo` gains `unlimited` (`nc_inq_unlimdims`), set by `File` and `NewFile`.
- `core::StationTable::single_axis` short-circuits on a shared pooled axis.
- Appended enumerators: `WarningCode` `unit_converted`, `station_name_substituted`,
  `native_position_dropped`; `FormatErrc` `invalid_variable_name`, `no_samples`,
  `too_many_samples`, `bad_option`.
- Tests: `tests/io/test_station_netcdf_{write,read,validation,golden}.cpp` (executable
  `mov_io_station_netcdf_tests`), helpers `station_nc_support.hpp`; in `tests/io/support/`:
  `station_nc_canonical.{hpp,cpp}` (library `mov_station_nc_canonical`: the SN §9 tables
  and a registry file), `nc_header_dump.{hpp,cpp}` (an `ncdump -h` printer and the ncdump
  probe file, in `mov_nc_fixtures`), `nc_edit.hpp` (raw netCDF-C editor).
  `tests/io/station_nc_fixtures.cpp` is the (non-test) executable the compliance check runs.
  Golden CDL: `tests/fixtures/io/station_netcdf/*.cdl`, each the output of the real
  `ncdump -h`. `collect` tests in `test_read.cpp`, the `unlimited` test in `test_nc_file.cpp`.
- Compliance: `tools/check_cf.py`, `tools/cf_waivers.txt`, `tools/compliance/{Dockerfile,
  setup.sh,requirements.txt}`, `tools/check_station_netcdf.sh`, and the `format-compliance`
  job in `.github/workflows/ci.yml`.
- CMake edits are trailing appends to `src/io/CMakeLists.txt` and `tests/io/CMakeLists.txt`.

## Declarations that differ from design §5.6

- **Writers return `expected<std::vector<Warning>, Error>`** (as `write_imeds`; the layout is
  `choose_layout(table)`). The warnings say what is written differently from the table, in
  this order: `unit_converted` (per converted column, subject the token),
  `station_name_substituted` (count), `native_position_dropped` (count; SN §10.1 Q4 keeps
  WGS 84 only), `value_reads_as_missing` (per column: values written as the `_FillValue`).
  They are computed from the plan (`warnings_of(Plan)`), not pushed while planning.
- **`validate_station_netcdf(table, options)`**: pure, no path, the writer's errors and
  warnings; what the UI calls before it asks where to save. The verdict depends on the
  schema, the stations and the options, never on whether a sample is Dry.
- `now` is a `std::chrono::sys_seconds`.
- **`read_station_netcdf(path, StationNcSelection, ctx[, StationNcReadOptions])`.**
  `StationNcSelection = variant<AllStations, core::StationSelection>`: `AllStations` is the
  explicit "whole file", never a default (C12). Stations come back in selection order. The
  options choose `PaddingCheck::boundary` (default) or `whole`.
- **`StationFile { core::StationTable table; StationFileOrigin origin; }`**,
  `StationFileOrigin = variant<V5Origin{StationNcVersion, StationNcLayout}>`; WP10b adds
  `ForeignCfOrigin{CfDsgLayout, CfVersion}` and `LegacyOrigin{dialect}`.
  `StationNcLayout` stays the two v5 layouts; foreign layouts get WP10b's own `CfDsgLayout`.
- **`inspect_station_netcdf(path, ctx) -> Read<StationNcCatalog>`**: origin,
  `CatalogStation{station, samples}` per station, schema; the same validation as a read up
  to the samples.
- `CfVersion` is its own type (`parse_cf_conventions`: the first `CF-<major>.<minor>`
  token); `StationNcVersion` is the format's.
- `choose_layout` is `noexcept` but not `constexpr` (`StationTable::single_axis` is not).

## Writer

- Everything that can be refused is decided first (`plan_write`, also behind
  `validate_station_netcdf`), so no file is created for a refused table; the body of
  `write_netcdf_atomic` only defines and puts.
- Errors: `empty_collection` (no stations), `no_data_variables` (no columns), `no_samples`
  (no station has one), `too_many_samples` (a station above `INT32_MAX`, what `obs_count`
  can count; the limit is a `detail::StationNcWriteLimits` field so a test reaches it),
  `noncanonical_unit`, `invalid_variable_name`, `bad_option`.
- **Units.** A registry quantity other than `difference` is stored in its canonical unit:
  no unit is `noncanonical_unit`, so is a unit `core::conversion` cannot convert. The
  conversion is the `Affine` applied per value while a row is written, not
  `core::convert(StationTable, ...)`, which takes the table by value (a copy of a model
  table). The identity is skipped, so `-0.0` keeps its sign; a converted value that is not
  finite is written as fill (`value_reads_as_missing`), as `convert` makes it Missing.
  `difference` and generic columns keep their unit, written with `udunits()`.
- `units_metadata` on every temperature unit (CF 1.11 §3.1.2): `temperature: on_scale` for
  registry quantities, `temperature: difference` for `difference`, `temperature: unknown`
  for generic ones (the IOOS checker accepts it: the registry canonical file has one).
- `long_name` is the label; an empty label is written as the registry's long name (generic:
  the token). `standard_name` is the registry's, or the generic quantity's own; none when
  empty (`optional`, F7).
- **Reserved names (schema only, F2).** A generic token may not be a name the format uses
  (`station`, `time`, `obs`, the three length dimensions, `station_id`, `station_name`,
  `station_provider`, `lat`, `lon`, `elevation`, `crs`, `obs_count`; the list is built from
  the `NcNameRef` constants), nor `<token>_status` of **any** column (every column reserves
  its status name, Dry samples or not), nor be longer than 249 bytes (`NC_MAX_NAME` with
  `_status` appended, for every column). No escaping (Q2).
- **Options (N6).** UTF-8, no NUL, at most `max_option_bytes` (64 KiB), a non-empty title;
  an empty optional text counts as absent.
- `station_provider` is written only when a station has a source; a station without one is
  an empty row. Length dimensions are the longest UTF-8 byte length, at least 1.
- Samples are written one station row at a time (a model table is never copied whole);
  the incomplete layout writes only each station's `n` samples and leaves the padding
  unwritten, so it reads back as fill (`NC_FILL`).
- Chunks: SN §3's rule on `(station, sample)`; `time(time)` and the instance variables are
  contiguous; deflate 2 with shuffle on every sample variable.
- Determinism: two writes of the same table at the same `now` are byte-identical (tested).
- **Idempotence (F16):** writing a table read back from a v5 file gives no warnings, and the
  file reads back equal (tested with a table that needed every normalization).

## Reader

- **Identification.** The station id is the one variable with `cf_role = timeseries_id`
  (none or several: `no_station_id`); it must be `char (station, n)` (`bad_encoding`,
  `dimension_mismatch`). `station_name`, `lat`, `lon`, `time`, `obs_count` are found by their
  SN §4.2 names; identification by `standard_name`/`units` is WP10b's foreign reader.
- **Header.** `Conventions` needs a `CF-1.N` token with N >= 6; no CF token, an older one or
  a CF-2 token is `unsupported_version` (N4: CF 2 would be another convention).
- **Structure.** No variable may use an unlimited dimension (`unsupported_layout`, subject
  the dimension; S4). With that, a v5 file cannot have zero stations or a zero-length sample
  dimension (netCDF has no fixed dimension of length 0), so there is no separate zero check.
- **Layout.** `time(time)`: orthogonal. `time(station, obs)` with `obs_count(station)`:
  incomplete. Anything else: `unsupported_layout` (subject `time`).
- **Data variables.** Every variable over the sample dimension must be over
  `(station, sample)` (`dimension_mismatch`); those that are, other than `time` and the
  targets of an `ancillary_variables`, are the data variables. Variables not over the sample
  dimension are ignored (what a newer minor version needs, SN §13). A data variable of a type
  `read_samples` refuses (64-bit, unsigned) is `NcError{type_mismatch}`.
- **Names (S5).** A data variable's name must be a quantity token: a registry token, or a
  CF name the format does not use itself; anything else is `invalid_variable_name`, in
  `read_schema`, so inspect and read agree. A repeated token is `duplicate_quantity`
  (subject the token). Generic quantities v5 wrote read back with **no** warning (F4).
- **Units (S2).** A registry quantity other than `difference` needs a `units` that
  converts to its canonical unit (`noncanonical_unit`, subject the token). Another unit of the
  family is kept (core converts).
- **Wet/dry status (S1).** Every `ancillary_variables` target that exists must have its data
  variable's dimensions (`bad_ancillary`). The target named `<data>_status` is the wet/dry
  status and must be exactly SN §8.2's: byte, `flag_values` the bytes 0 1, `flag_meanings`
  `dry wet`, a `_FillValue` (if any) that is neither flag; anything else is `bad_flag`
  (an attribute of another type too; other library errors propagate). Other ancillary
  variables (a newer minor's) are ignored. Flags are read raw: a flag outside `valid_range`
  is seen (`bad_flag`), not masked.
- **Padding (B1).** In the incomplete layout each group of selected stations reads
  `min(obs, max n_s + 1)` elements of each row: its longest station's samples and the first
  padding element, which must be fill (SN §12.4's boundary check); whatever else of a
  station's padding that read covers is checked too. A read therefore costs the selected
  stations' samples, and `check_result_size` charges exactly those. `PaddingCheck::whole`
  reads and checks every padding element and is charged selected stations × `obs`. Only the
  samples kept become `core::Sample`s; the padding is checked in the raw type.
- `obs_count` is read as integers (any integer type) without masking: a fill value is
  negative and so `bad_obs_count`.
- **CRS (SN §10.1, S3).** At most one grid mapping (two: `unsupported_crs`, subject the
  second). A nonzero `longitude_of_prime_meridian` is `unsupported_crs`. `epsg_code`
  `EPSG:<1-9 digits>`: 4326 as is, another geographic code projected with a `Projector`
  (native point kept, `crs_approximate` when PROJ says so; a point it cannot project is
  `bad_coordinates`, subject `lon, lat`), a projected or unknown one `unsupported_crs`.
  Without `epsg_code`: `latitude_longitude` whose ellipsoid parameters, when present, are
  WGS 84's; absent ones are WGS 84 with `crs_assumed` (subject the grid mapping). No
  `grid_mapping` at all: WGS 84 with `crs_assumed`.
- Strings: trailing NULs are cut, nothing else; an embedded NUL or bytes that are not UTF-8
  are `bad_encoding` (id, name, and provider, N3), an empty id `no_station_id`, a repeated
  one `duplicate_station_id`. Provider tokens core does not know become no source with
  `unknown_provider` (one per token, counting its stations).
- Subjects taken from the file (tokens, units, ids, versions) are cut to 120 bytes on a
  UTF-8 boundary (`subject_of`, N3).
- Times: any CF time unit (`parse_cf_time_units`) and supported calendar; orthogonal through
  WP9's `read_time_axis` (errors without a station), incomplete per station (errors with the
  station and the index).
- Independent stages are combined with `collect`/`collect_read` (F5); no `operator*` on an
  `expected` is taken before every stage has succeeded.
- Error names of SN §12 → `FormatErrc`: MissingStructure → `missing_dimension` /
  `missing_variable` / `missing_attribute`; NoStationId → `no_station_id`; DuplicateStationId
  → `duplicate_station_id`; BadCoordinates → `bad_coordinates` (subject `lat`, `lon` or
  `lon, lat`); NonMonotonicTime → `time_not_increasing`; BadObsCount → `bad_obs_count`;
  PaddingNotMissing → `padding_not_missing`; BadEncoding → `bad_encoding`; WetDryInconsistent
  → `wet_dry_inconsistent` (subject the data variable); BadFlag → `bad_flag` (subject the
  status); UnsupportedVersion → `unsupported_version` (also `Conventions`, subject
  `:Conventions`); BadVersion → `bad_version`; `featureType` other than timeSeries or an
  unlimited dimension → `unsupported_layout`.

## Findings

- **ncdump prints double attributes with 15 significant digits**: `_FillValue =
  9.96920996838687e+36`, not SN §9's `9.969209968386869e+36` (rendered by a script). SN §9.1
  is updated; the committed CDL is the real `ncdump -h` (netCDF-C 4.9.2, Ubuntu 24.04).
  ncdump 4.9 also prints a newline in a text attribute as `\n` on the same line.
- **Library defect (not ours; for the maintainer): renaming a variable with the vcpkg pins
  breaks the file.** netCDF-C 4.9.3 with HDF5 2.1.1: `nc_open(NC_WRITE)`, `nc_redef`,
  `nc_rename_var(obs_count -> count)`, `nc_close` all return 0, but after reopening,
  `nc_inq_var` gives `NC_EHDFERR` for every variable with dimensions (the scalar `crs` and the
  renamed one still answer). The same rename through netCDF4-python (netCDF-C 4.9.3 with
  HDF5 1.14.6) works, and our library reads that file. Renaming the coordinate variable
  `time` fails at once ("Problem with HDF5 dimscales"). A 20-line netCDF-C program reproduces
  it (not committed). v5 never renames, so it matters only to tests: the validation tests
  build what editing cannot make from scratch (`make_skeleton`, raw netCDF-C). A `_FillValue`
  must also be set in the same define session as its variable (`Editor` scope).
- `single_axis` on 1000 stations sharing one pooled axis of 100000 times: 41 ms per call
  before the short-circuit, about 1 µs after (release build, ten calls, the dev container on
  the shared host).
- cfchecker 4.1.0 reports 0 errors and 0 warnings on all three canonical files (CF-1.8 copy)
  with INFO only: `crs_wkt` syntax not verified (all files) and `obs_count` has no `units`
  (incomplete layout). The SN §14.2 INFO about `cf_role` no longer appears.
- IOOS compliance-checker 6.1.0 `cf:1.11`: 0 errors, and exactly the one waived warning
  (time `units_metadata`) on each file; `temperature: unknown`, `difference` and `value`
  (no `standard_name`) included.

## Compliance job

- `tools/check_cf.py DIR --cdl CDL_DIR [--tables DIR] [--waivers FILE]` checks every
  `station_timeseries_*.nc` of DIR:
  - IOOS `cf:1.11`: fails on an error, or on a warning no waiver matches **from its start**
    (each waiver names its section, `§4.4 Time Coordinate: ...`, N5);
  - `cfchecks -v 1.8 -s -a -r` with the pinned tables on a copy with `Conventions = CF-1.8`:
    fails on ERROR/WARN lines, on a nonzero exit status, or on a missing summary (S6);
  - xarray `open_dataset` (for the two SN §9 files the decoded ids, names, times, values,
    fills and padding are compared with the C++ tables);
  - `ncdump -h` against the golden CDL (application version masked), and `ncdump_probe.nc`
    against its CDL, which pins the C++ header dumper to the real ncdump;
  - a waiver that matched nothing fails the run (N5).
- The canonical files: SN §9.2 (orthogonal) and §9.1 (incomplete), both with a dry sample and
  so a status variable, and a registry file with every registry quantity, a `value` column
  with a datum, a grid-relative generic, a generic temperature and a `difference` (every
  standard name, unit and `units_metadata` the writer can produce).
- **Pins (S6, N9).** `tools/compliance/setup.sh` downloads the CF standard name table v95,
  the area type table v13 (versioned URLs) and the standardized region list v5 (no versioned
  URL: the cfconventions.org repository at commit `83a9da12`), each checked by SHA-256 (the
  name table is 4.5 MB, over the 1 MB `check-added-large-files` limit, so none is committed).
  The Python set is installed with `pip --require-hashes` (one SHA-256 per file, CPython 3.12
  x86-64; `cfunits` is an sdist), and `netcdf-bin`, `libudunits2-0` and `udunits-bin` are
  pinned to the Ubuntu 24.04 release versions. Both checkers read only these tables
  (`CF_STANDARD_NAME_TABLE` is set from `--tables`): `tools/check_station_netcdf.sh` runs the
  check container with `--network none`, and it passes.
- Locally: `tools/check_station_netcdf.sh` builds `station_nc_fixtures` in the dev container,
  writes `build/format-compliance/`, builds the checker image (tagged by a hash of its
  inputs) and runs `check_cf.py` in it, offline.
- Negative controls, by hand: a copy with an invalid `standard_name` and a wrong temperature
  unit fails both checkers and the golden CDL; an extra waiver that matches nothing fails the
  run.
- Making the job *required* on `v5` is a branch-protection setting, not in the repository.

## Review round (neckbeard-nate, ben-deane; maintainer decisions)

| Item | Result |
|---|---|
| Nate B1 (blocker) | boundary reads `min(obs, max n_s + 1)` per group; `PaddingCheck::whole` opt-in, charged selected × obs; only kept samples become `Sample`s; regression test on a 50-station file with one 10000-sample station, bounded by `max_elements = 1000` (boundary passes, whole is `too_large`), plus a test that whole sees padding the boundary read does not |
| S1 | `<data>_status` validated (type, flag_values, flag_meanings, fill); `bad_flag`; non-type errors propagate; the test that locked in the silent behaviour now expects `bad_flag` |
| S2 | registry units must convert to the canonical unit on read (`noncanonical_unit`) |
| S3 | missing ellipsoid → `crs_assumed`; nonzero prime meridian → `unsupported_crs` |
| S4 | `DimInfo::unlimited`; unlimited dimensions refused; zero lengths therefore impossible |
| S5 | non-token or reserved names → `invalid_variable_name` in `read_schema`; duplicate tokens → `duplicate_quantity` (subject the token) |
| S6 | area-type and region tables pinned, `-a`/`-r` passed, offline run, nonzero exit fails |
| N1–N9 | done; N7 measured above; N9 hashes and apt pins as above |
| F1, Q1 | `StationNcLayout` unchanged; `StationFile{table, origin}` with `V5Origin`; catalog carries the origin |
| F2, Q2 | schema-only name checks; `validate_station_netcdf` preview; the two tests updated |
| F4 | no warning for v5 generic quantities; WP10b mangles unparsable foreign names (`variable_renamed`) |
| F5 | `collect` / `collect_read` in `read.hpp`, used at the early-exit sites (writer dims and plan, reader instances, structure, stations, CRS, schema texts, open) |
| F13 | writers return `expected<vector<Warning>, Error>` |
| F6–F12, F14–F16 | adopted: warnings from the plan, `optional` standard name, reserved names from the constants (`elevation` constant), `CfVersion`, `CatalogStation`, options validated, `sys_seconds`, one `std::visit` for the selection, distinct writer codes, `lifted` → `as_io_error`, the idempotence test |

## Gates

All on the final tree of the review round, Linux x86-64 (Docker dev image): `dev`,
`dev-clang`, `dev-libcxx`, `asan`, `release` (1189 tests each), `fuzz` (1203), `coverage`
workflows pass; the tidy gate (143 translation units, 0 findings); `pre-commit run
--all-files` (stable on the second run); `tools/check_station_netcdf.sh`, offline
(`--network none`): 5 of 5 checks pass.

Coverage (gcovr, lines): io 91.4% (8424 of 9216), core 97.2%, overall 92.5%;
`station_netcdf_open.cpp` 94%, `station_netcdf_write.cpp` 92%, `station_netcdf_read.cpp`
96%, `station_netcdf.cpp` 97%, `station_netcdf_samples.cpp` 59% (gcov counts each line
once per instantiation of the row readers; the float and integer instantiations, which no
v5 writer produces, are the misses).

Tidy notes: Catch2's `CHECK_FALSE` makes clang-analyzer report an out-of-range
`ResultDisposition` cast inside Catch2 (`EnumCastOutOfRange`) in long test cases;
the station tests use `CHECK(not ...)` instead.

## Deviations from the brief

- No fuzz target: the reader's input is a structure (WP10b's structure fuzzer).
- The golden CDL check runs twice: in every preset with the C++ dumper (`nc_header_dump`,
  raw netCDF-C), and in the compliance job with the real ncdump; the probe file keeps the
  two in step. ncdump was not added to the dev image.
- `tools/compliance/` is a separate image rather than a venv in the dev image.
- `dev-msvc-xwin` is not in this tree (it is on `v5` after this branch's base), so it was not
  run.

## Not verified here

- Windows and macOS (not built here; nothing platform-specific was added).
- The CI job's first run (not pushed). `setup.sh` uses `sudo apt-get` on the runner, and the
  hashed wheels assume the runner's CPython 3.12 on x86-64.
- Panoply, ncview and NCO (SN §14.2, a manual check before release).
