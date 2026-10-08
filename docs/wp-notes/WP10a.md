# WP10a notes (station netCDF: v5 writer and reader, format-compliance job)

Fold into `docs/core-design.md` §5.6, §7.2 and `docs/station-netcdf.md` (SN), then delete
this file. Everything here is a choice SN or the design left open, or a change from them.

## What exists

- `src/io/include/mov/io/station_netcdf.hpp`: `station_nc_format`, `StationNcVersion` (+
  `station_nc_version`, `parse_station_nc_version`), `StationNcLayout`, `choose_layout`,
  `StationNcWriteOptions`, `write_station_netcdf`, `AllStations`, `StationNcSelection`,
  `V5StationFile`, `StationFile`, `StationNcCatalog`, `inspect_station_netcdf`,
  `read_station_netcdf`, and `detail::write_station_netcdf` (with a `FaultInjector`).
- Sources: `station_netcdf.cpp` (version, layout, the Conventions test),
  `station_netcdf_write.cpp`, `station_netcdf_read.cpp`, the private
  `station_netcdf_format.hpp` (names, attribute values, WKT, chunk rule). The reader reuses
  WP9's `model_netcdf.hpp` helpers (`require_dim`, `optional_text`, `read_time_axis`,
  `read_calendar`, `make_clock`, `plan_groups`, `check_result_size`,
  `dispatch_model_numeric`).
- `WarningCode` gains `unit_converted`, `station_name_substituted`,
  `native_position_dropped` (appended); `FormatErrc` gains `invalid_variable_name`
  (appended).
- Tests: `tests/io/test_station_netcdf_{write,read,validation,golden}.cpp` (executable
  `mov_io_station_netcdf_tests`), helpers `station_nc_support.hpp`; in `tests/io/support/`:
  `station_nc_canonical.{hpp,cpp}` (library `mov_station_nc_canonical`: the SN §9 tables),
  `nc_header_dump.{hpp,cpp}` (an `ncdump -h` printer and the ncdump probe file, in
  `mov_nc_fixtures`), `nc_edit.hpp` (raw netCDF-C editor). `tests/io/station_nc_fixtures.cpp`
  is the (non-test) executable the compliance check runs. Golden CDL:
  `tests/fixtures/io/station_netcdf/*.cdl`, each the output of the real `ncdump -h`.
- Compliance: `tools/check_cf.py`, `tools/cf_waivers.txt`, `tools/compliance/{Dockerfile,
  setup.sh,requirements.txt}`, `tools/check_station_netcdf.sh`, and the `format-compliance`
  job in `.github/workflows/ci.yml`.
- CMake edits are trailing appends to `src/io/CMakeLists.txt` and `tests/io/CMakeLists.txt`.

## Declarations that differ from design §5.6

- **The writer returns `expected<Read<StationNcLayout>, Error>`.** Like `write_imeds`, it
  reports what it writes differently from the table, as aggregate warnings in this order:
  `unit_converted` (per converted column, subject the token), `station_name_substituted`
  (count), `native_position_dropped` (count; SN §10.1 Q4 keeps WGS 84 only),
  `value_reads_as_missing` (per column: values equal to the `_FillValue`, which every reader
  masks).
- **`read_station_netcdf(path, StationNcSelection, ctx)`.** The design's signature had no
  selection; C12 requires one. `StationNcSelection = variant<AllStations,
  core::StationSelection>`: `AllStations` is the explicit "whole file" (no inspection needed
  first), never a default. Stations come back in selection order.
- **`StationFile = variant<V5StationFile>`** for now, so the signature is already WP10b's;
  WP10b adds `ForeignCfFile` and `LegacyStationFile`. Every non-v5 file is `not_this_format`
  until then.
- `V5StationFile::version` is a `StationNcVersion{major, minor}`, not a string.
  `parse_station_nc_version` accepts 1-4 digits per part, no sign, no leading zero.
- `StationNcLayout` has `orthogonal` and `incomplete` only; WP10b adds the reader-only ones.
- `choose_layout` is `noexcept` but not `constexpr` (`StationTable::single_axis` is not).
- **`inspect_station_netcdf(path, ctx) -> Read<StationNcCatalog>`** (new): version, layout,
  stations, per-station sample counts, schema; the same validation as a read up to the
  samples. A caller builds a selection from it.
- **`FormatErrc::invalid_variable_name`** (new): a generic token the format cannot take as a
  variable name (see below). The writer errors are therefore `empty_collection`,
  `noncanonical_unit`, `invalid_variable_name`, and I/O.

## Writer

- Everything that can be refused is decided first (`plan_write`), so no file is created for
  a refused table; the body of `write_netcdf_atomic` only defines and puts.
- `empty_collection`: no stations, no columns, or no samples at all.
- **Units.** A registry quantity other than `difference` is stored in its canonical unit:
  no unit is `noncanonical_unit`, so is a unit `core::conversion` cannot convert. The
  conversion is the `Affine` applied per value while a row is written, not
  `core::convert(StationTable, ...)`, which takes the table by value (a copy of a model
  table). The identity is skipped, so `-0.0` keeps its sign; a converted value that is not
  finite is Missing, as `convert` makes it. `difference` and generic columns keep their
  unit, written with `udunits()`; no unit, no `units`.
- `units_metadata`: on every temperature unit, `"temperature: difference"` for a
  `difference` (CF 1.11 §3.1.2), `"temperature: on_scale"` otherwise.
- `long_name` is the label; an empty label is written as the registry's long name (generic:
  the token), and so reads back. SN §9's lower-case long names are the canonical tables'
  labels.
- `standard_name`: the registry's, or the generic quantity's own when not empty.
- `vertical_datum` whenever the meta datum is engaged (SN §4.3, design §9.2).
- **Reserved names.** A generic token may not be `station`, `time`, `obs`, the three length
  dimensions, `station_id`, `station_name`, `station_provider`, `lat`, `lon`, `elevation`,
  `crs`, `obs_count` (variables, and dimensions: a variable named like a dimension it is not
  the coordinate of breaks xarray), nor another column's `<token>_status`, nor be longer
  than `NC_MAX_NAME` (with `_status`, when it gets one): `invalid_variable_name`.
- `station_provider` is written only when a station has a source; a station without one is
  an empty row. Length dimensions are the longest UTF-8 byte length, at least 1.
- Samples are written one station row at a time (a model table is never copied whole);
  the incomplete layout writes only each station's `n` samples and leaves the padding
  unwritten, so it reads back as fill (`NC_FILL`), including `time`, the data and the status.
- Chunks: SN §3's rule on `(station, sample)`; `time(time)` and the instance variables are
  contiguous; deflate 2 with shuffle on every sample variable.
- Determinism: two writes of the same table at the same `now` are byte-identical (tested).

## Reader

- **Identification.** The station id is the one variable with `cf_role = timeseries_id`
  (none or several: `no_station_id`); it must be `char (station, n)` (`bad_encoding`,
  `dimension_mismatch`). `station_name`, `lat`, `lon`, `time`, `obs_count` are found by their
  SN §4.2 names; identification by `standard_name`/`units` (SN §12.2) is the foreign-file
  reader's (WP10b).
- **Layout.** `time(time)`: orthogonal. `time(station, obs)` with `obs_count(station)`:
  incomplete. Anything else: `unsupported_layout` (subject `time`).
- **Data variables.** Every variable over the sample dimension must be over
  `(station, sample)` (`dimension_mismatch` otherwise: SN §12.6 "dimension/variable
  mismatch"); those that are, other than `time` and the targets of an
  `ancillary_variables`, are the data variables. Variables not over the sample dimension
  are ignored, which is what a newer minor version needs (SN §13). A data variable of a type
  `read_samples` refuses (64-bit, unsigned) is `NcError{type_mismatch}`.
- **Ancillary and wet/dry.** Every `ancillary_variables` target that exists must have its
  data variable's dimensions (`bad_ancillary`). A target is the wet/dry status when it is
  byte, `flag_meanings` is `dry wet` and `flag_values` is `0, 1`; other ancillary variables
  are ignored. Flags are read raw: a flag outside `valid_range` must be seen (`bad_flag`),
  not masked.
- **Padding: the whole tail is always checked**, for `time`, the data and the status
  (SN §12.4 asks for the boundary element always and the tail in a strict mode). The rows
  stream past whole anyway, so the strict check costs nothing.
- `obs_count` is read as integers (any integer type) without masking: a fill value is
  negative and so `bad_obs_count`.
- **CRS (SN §10.1).** The data variables must name at most one grid mapping (two:
  `unsupported_crs`, subject the second). `epsg_code` `EPSG:<1-9 digits>`: 4326 as is,
  another geographic code projected with a `Projector` (native point kept, `crs_approximate`
  when PROJ says so), a projected or unknown one `unsupported_crs`. Without `epsg_code`:
  `grid_mapping_name = latitude_longitude` whose `semi_major_axis` and
  `inverse_flattening`, **when present**, are WGS 84's; absent parameters are read as WGS 84
  without a warning. No `grid_mapping` at all: WGS 84 with `crs_assumed`.
- Strings: trailing NULs are cut, nothing else; an embedded NUL or bytes that are not UTF-8
  are `bad_encoding` (id and name), an empty id `no_station_id`, a repeated one
  `duplicate_station_id` (subject the id, station the later one). Provider tokens core does
  not know become no source with `unknown_provider`.
- Times: any CF time unit (`parse_cf_time_units`) and supported calendar; orthogonal through
  WP9's `read_time_axis` (errors without a station), incomplete per station (errors with the
  station and the index).
- Error names of SN §12 → `FormatErrc`: MissingStructure → `missing_dimension` /
  `missing_variable` / `missing_attribute`; NoStationId → `no_station_id`; DuplicateStationId
  → `duplicate_station_id`; BadCoordinates → `bad_coordinates` (subject `lat` or `lon`);
  NonMonotonicTime → `time_not_increasing`; BadObsCount → `bad_obs_count`; PaddingNotMissing
  → `padding_not_missing`; BadEncoding → `bad_encoding`; WetDryInconsistent →
  `wet_dry_inconsistent` (subject the data variable); BadFlag → `bad_flag` (subject the
  status); UnsupportedVersion → `unsupported_version` (also a `Conventions` without CF-1.6+,
  subject `:Conventions`); BadVersion → `bad_version`; `featureType` other than timeSeries →
  `unsupported_layout`.
- Not checked: that `station` is a fixed dimension (the wrapper has no unlimited-dimension
  query; an unlimited one of length 0 has no stations to read).

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
  build what editing cannot make (a missing variable or dimension, a 2-D time over the time
  dimension, no data variable) from scratch (`make_skeleton`, raw netCDF-C).
- cfchecker 4.1.0 now reports 0 errors and 0 warnings on all three canonical files (CF-1.8
  copy) with INFO only: `crs_wkt` syntax not verified (all files) and `obs_count` has no
  `units` (incomplete layout). The SN §14.2 INFO about `cf_role` no longer appears.
- IOOS compliance-checker 6.1.0 `cf:1.11`: 0 errors, and exactly the one waived warning
  (time `units_metadata`) on each file, `difference` and `value` variables (no
  `standard_name`) included.

## Compliance job

- `tools/check_cf.py DIR --cdl CDL_DIR [--standard-names XML] [--waivers FILE]` checks every
  `station_timeseries_*.nc` of DIR: IOOS `cf:1.11` (fails on an error or an unwaived
  warning), `cfchecks -v 1.8` on a copy with `Conventions = CF-1.8` (fails on ERROR/WARN),
  xarray `open_dataset` (for the two SN §9 files the decoded ids, names, times, values, fills
  and padding are compared with the C++ tables), and `ncdump -h` against the golden CDL
  (application version masked); and `ncdump_probe.nc` against its CDL, which pins the C++
  header dumper to the real ncdump.
- The canonical files: SN §9.2 (orthogonal) and §9.1 (incomplete), both with a dry sample and
  so a status variable, and a registry file with every registry quantity, a `value` column
  with a datum, a grid-relative generic and a `difference` (every standard name and unit the
  writer can produce).
- **The CF standard name table v95 is not committed** (4.5 MB, over the 1 MB
  `check-added-large-files` limit): `tools/compliance/setup.sh` downloads it by version and
  checks its SHA-256. All Python packages are pinned (`tools/compliance/requirements.txt`,
  the full resolved set). The image and the CI job both run `setup.sh`.
- Locally: `tools/check_station_netcdf.sh` builds `station_nc_fixtures` in the dev container,
  writes `build/format-compliance/`, builds the checker image (tagged by a hash of its
  inputs) and runs `check_cf.py` in it.
- A negative control was run by hand: a copy with an invalid `standard_name` and a wrong
  temperature unit fails both checkers and the golden CDL.
- Making the job *required* on `v5` is a branch-protection setting, not in the repository.

## Gates

All on the final tree, Linux x86-64 (Docker dev image): `dev`, `dev-clang`, `dev-libcxx`,
`asan`, `release` (1176 tests each), `fuzz` (1190), `coverage` workflows pass; the tidy
gate (141 translation units, 0 findings); `pre-commit run --all-files` (stable on the
second run); `tools/check_station_netcdf.sh` (4 of 4 files pass).

Coverage (gcovr, lines): io 91.8% (8048 of 8764), core 97.2%, overall 92.9%;
`station_netcdf_write.cpp` 92%, `station_netcdf_read.cpp` 84% (most of the misses are the
float and integer instantiations of the row readers, which v5 files never reach, and the
error returns of library calls), `station_netcdf.cpp` 97%.

Tidy notes: Catch2's `CHECK_FALSE` makes clang-analyzer report an out-of-range
`ResultDisposition` cast inside Catch2 (`EnumCastOutOfRange`) in long test cases;
the station tests use `CHECK(not ...)` instead.

## Deviations from the brief

- No fuzz target: the reader's input is a structure (WP10b's structure fuzzer).
- The golden CDL check runs twice: in every preset with the C++ dumper (`nc_header_dump`,
  raw netCDF-C), and in the compliance job with the real ncdump; the probe file keeps the
  two in step. ncdump was not added to the dev image.
- `tools/compliance/` is a separate image rather than a venv in the dev image.

## Not verified here

- Windows and macOS (not built here; nothing platform-specific was added).
- The CI job's first run (not pushed). `setup.sh` uses `sudo apt-get` on the runner.
- Panoply, ncview and NCO (SN §14.2, a manual check before release).
