# WP6 notes (netCDF wrapper)

Fold into `docs/core-design.md` §4, then delete this file. Everything here is a
choice §4 left open or a change from it. The declarations otherwise match §4.2.

## What exists

Public headers in `src/io/include/mov/io/netcdf/`: `name.hpp` (`NcName`,
`NcNameRef`), `types.hpp` (`Type`, `Numeric`, `type_of`, `readable_as`,
`dispatch_numeric`, `DimInfo`, `VarInfo`, `DimRange`, `Slab`, `whole`, `Global`,
`AttTarget`, `VarOptions`), `masking.hpp` (`Masking<T>`), `file.hpp` (`File`,
`write_netcdf_atomic`); `mov/io/detail/checked_product.hpp`. Sources in
`src/io/netcdf/`: `file.cpp` (open, close, moves, structure), `attribute.cpp`,
`read.cpp`, `masking.cpp`, `write.cpp` (define/put, `write_netcdf_atomic_impl`),
`path.cpp` (`nc_path`), `blocks.cpp` (`for_each_block`), `library.cpp` (moved from `src/io/netcdf_library.cpp`), and
the private `nc_call.hpp` and `internal.hpp`.

Tests: `tests/io/test_nc_*.cpp` in their own executable, `mov_io_netcdf_tests`
(plus the `_constexpr` pair from `test_nc_constexpr.cpp`). The fixture generator is
`tests/io/support/nc_fixtures.{hpp,cpp}` (library `mov_nc_fixtures`). The `--wrap`
shims are in `tests/io/support/nc_wrap.cpp`, the counts in `nc_counts.{hpp,cpp}`.
CTest gates: `netcdf_include_gate`, `netcdf_include_gate_rejects_violations`
(`cmake/CheckNetcdfInclude.cmake`, `tests/cmake/netcdf_include/`), and
`io_compile_fail_checks` (`tests/cmake/compile_fail_io/`, run by the core
harness).

## Declarations that differ from §4.2

- `ReadLimits`, `ReadContext` and `StopToken` come from `mov/io/read_limits.hpp`
  (WP5); there is no `nc::` copy. Cancellation polls `StopToken`, never
  `std::stop_token`.
- **The data reads (`read`, `read_samples`, `read_char_rows`, `read_strings`) return
  `expected<…, io::Error>`**, not `NcError`, because they can also end in
  `Cancelled`. The error is an `NcError` or `Cancelled`. Everything else returns
  `NcError`.
- **`AttTarget` is a class, not `variant<Global, NcNameRef>`.** A variant's
  converting constructor cannot forward a literal to the `consteval` constructor of
  `NcNameRef`. With the class, `file.text_att("zeta", "units")` checks both literals
  at compile time. A global attribute is `nc::global`. `AttTarget(const NcName&&)` is
  deleted, like `NcNameRef`'s.
- `Type` gains **`other`** (compound, enum, opaque, vlen). Without it, `variables()`
  of a file holding any user-defined type would fail.
- `dispatch_numeric(Type, on_numeric, on_other)` takes a handler for the
  non-numeric types, so it is total. `readable_as<T>(Type)` (the §4.3 table as a
  `constexpr` predicate) and `whole(const VarInfo&) -> Slab` are public.
- `WrapperFault` gains **`unrepresentable_path`** (Windows only; see Paths).
- `File` gains `is_open()` and `path()`. The `path()` is the opened file, or the
  target of an atomic write.
- `Masking<T>` gains `masks(T)`, the attribute test without unpacking.
- `NcError::object` names an attribute in ncdump's notation: `var:att`, or `:att`
  for a global one. A dimension or variable is named by its own name.

## Behaviour §4 left open

Open and close
- `File::open` opens only a regular file. A directory gives `LibraryStatus{EISDIR}`,
  any other non-regular file `LibraryStatus{EINVAL}`. netCDF-C reports system
  errors as positive errno values, so these fit its codes. The check exists because
  a FIFO blocks `open()` inside HDF5. A missing file is left to netCDF-C (`ENOENT`).
- Close policy: the destructor of a read handle closes and ignores the result. A
  write handle destroyed open is aborted. `close() &&` reports. Whenever `nc_close`
  fails, `nc_abort` follows. In netCDF-C 4.9.3, a failed `nc_close` keeps the id in
  its list (`dfile.c:1300`), and `nc_abort` always frees it (`dfile.c:1247`). So no
  id leaks on any path. The `--wrap` tests check this with an injected close failure.
- `nc_call.hpp`: `nc_status(fn) -> int` is the single entry point; `nc_call(op,
  object, file, fn)` passes its status to the non-template
  `status_to_expected`, which builds the `NcError`. Debug builds guard each call with an
  `inline std::atomic_flag`: `test_and_set`, then assert it was clear. That catches
  concurrent entry and re-entry. It does not check which thread owns the file. There
  is no lock (C11). Release builds check nothing. No lint enforces that every
  `nc_*` call goes through it; in the current sources each one is a lambda passed to
  `nc_status`/`nc_call`, which a reviewer can grep.

Sizes and reads
- `check_slab` runs before any allocation. It checks the rank (`rank_mismatch`),
  then each range against its dimension, returning netCDF-C's own codes
  (`NC_EINVALCOORDS` for a start past the end, `NC_EEDGE` for a count past it). Then
  `checked_product` (`overflow`), then `max_elements` (`too_large`).
- Blocks (`blocks.cpp`, `for_each_block`, a `std::function` visitor): the split dimension is the outermost
  one whose inner dimensions hold at most `slab_elements` elements. Each block
  covers a run of that dimension, with the outer dimensions at one index. So every
  block is contiguous in the row-major result, and the read goes straight into the
  output vector. `slab_elements` of 0 is treated as 1. `stop` is polled before every
  block, the first one included. The tests check, over many shapes and block sizes,
  that the blocks cover the slab exactly once, in order.
- `read_char_rows` also refuses more rows than `max_elements`. A name dimension of
  length 0 has no bytes, so the byte count alone would not bound the
  `std::string` objects.
- `read_strings` caps the total string bytes at `max_text_bytes` (`too_large`). It
  reads in blocks, and each block's strings are freed through `nc_call` by an RAII
  holder, even when copying throws.
- `text_att`: an `NC_STRING` attribute must hold one string (`count_mismatch`); a
  NULL string is `""`. `numeric_att` checks `length * sizeof(T)` against
  `max_att_bytes`.
- 64-bit integers: netCDF-C takes `long long`. Where `int64_t` is `long` (LP64
  Linux), reads and writes go through a `long long` buffer per block, because a
  `reinterpret_cast` is banned.

Masking (`masking.cpp`)
- `_FillValue` is read with its type checked (`numeric_att<T>`), never through
  `nc_inq_var_fill`. For a mistyped attribute, `nc_inq_var_fill` does an untyped
  `nc_get_att` into a buffer sized for the variable (`libsrc/var.c:723`), which is B4
  again. A wrong type is `type_mismatch` and more than one value is `count_mismatch`.
  The library writes neither; the fixtures for them are hand-made classic files.
- Without `_FillValue`, the default fill comes from `nc_inq_var_fill` in the exact
  type. NC_NOFILL means no fill. **Byte variables get no default fill**, so -127
  stays a value. The NUG says so (netCDF-C `docs/attribute_conventions.md`: "If
  the data type is byte and _FillValue is not explicitly defined, then the valid
  range should include all possible values"), and ncdump and netCDF4-python do the
  same.
- `missing_value`, `valid_min`, `valid_max` and `valid_range` must have the
  variable's type (CF §2.5.1), else `type_mismatch`. The NUG also allows a wider
  signed type for the `valid_*` attributes of byte data; that is refused here.
  `valid_range` must hold 2 values and the others 1, else `count_mismatch`. When
  `valid_range` is present it is used and `valid_min`/`valid_max` are ignored.
  The NUG's implied valid range (none given: the side of the fill value beyond it
  is invalid) is not applied, as in xarray.
- `scale_factor` and `add_offset`: one `float` or `double` (read as `double`, which
  is exact), else `type_mismatch` or `count_mismatch`. An absent factor or offset is
  not applied, so -0.0 keeps its sign. A value that overflows on unpacking is
  `Missing`.
- `_Unsigned`: after `cut_at_nul`, `simplified` and lower-casing, the value `"true"`
  gives `unsupported_unsigned`. Any other text is ignored. A non-text `_Unsigned`
  attribute gives `type_mismatch`.
- `read_samples` keeps both the raw `vector<T>` and the `vector<Sample>`. Both are
  bounded by `max_elements`.

Writing
- `write_netcdf_atomic(target, body)`: `body` returns `expected<void, E>` with E
  convertible to `io::Error` (concept `AtomicNcBody`, reusing WP5's
  `is_expected_void_v`). It must not close or move the `File`. If it does, stage 3
  reports `closed`, or the moved handle aborts and the write fails. The stages are
  WP5's: `check_target_replaceable`, `temp_path_for`, `nc_create(NC_NETCDF4 |
  NC_NOCLOBBER)`, the body, `close() &&`, then `commit_temp`.
  - The `TempFileGuard` is armed **after** a successful create, so a name collision
    (`NC_EEXIST`) never deletes someone else's file. The `File` is declared after the
    guard, so it is aborted or closed before the file is removed.
  - An injected fault at `body` is `FileError{write}` and at `close` it is
    `FileError{close}`, matching the text writer. A real close failure is
    `NcError{close}`. Errors name the target, not the temporary file.
- `put_char_rows` requires `rows.size()` to equal the first dimension
  (`count_mismatch`). A longer row is `name_too_long`. Rows are copied by byte length
  and NUL-padded.
- `define_var` applies the options in the order chunking, deflate (always with
  shuffle), fill. A `chunks` rank that differs from the dimensions is
  `rank_mismatch`.
- A write call on a read handle reaches netCDF-C, which returns `NC_EPERM`.

## Paths (Windows; §4.1 asked for verification)

The pinned netCDF-C **4.9.3 does not take UTF-8 on Windows**. `nc_open` and
`nc_create` treat the `char*` as text in the **active code page** (ACP):
`libdispatch/dpathmgr.c` (`ansi2utf8`, `NCopen3`, `NCfopen`) and
`libhdf5/hdf5open.c`/`hdf5create.c` (`nc4_H5Fopen`/`nc4_H5Fcreate` call
`NCpath2utf8`) convert ACP to UTF-8 for HDF5. The one exception is a process whose
ACP is UTF-8 (an application manifest with `activeCodePage` set to `UTF-8`,
Windows 10 1903+); then the bytes are taken as UTF-8. Checked by reading the 4.9.3
source in the vcpkg download cache.

`detail::nc_path`: on POSIX it returns the native bytes unchanged; a non-UTF-8 name
works and is tested. On Windows it converts the wide path to the ACP with
`WC_NO_BEST_FIT_CHARS`; "é" must not become "e". When the ACP cannot represent the
path, it falls back to the 8.3 short name, of the file, or of its directory for a
file not yet created (the temporary name is ASCII). With neither,
`unrepresentable_path`.

**Recommendation for Phase 7 (packaging):** give `metoceanviewer.exe` a manifest
with `activeCodePage = UTF-8` and `longPathAware = true`. Every path then reaches
netCDF-C as UTF-8, and the short-name fallback becomes dead code. Without
`longPathAware` (plus the `LongPathsEnabled` registry value), HDF5's `_wopen` of a
path over 260 characters probably fails; only a short name below 260 would help.

Tests (`test_nc_paths.cpp`): a non-ASCII directory and file name, and a path over
300 characters, are written atomically and read back. On Linux both must succeed.
On Windows a failure must be a clean `NcError`, reported with `WARN` rather than
failing, until the Windows job shows what happens. **None of the Windows code has
been compiled or run.**

Templates
- Each typed member (`read`, `numeric_att`, `put_att`, `put`, `define_var`,
  `masking`) is a thin typed call on a non-template private half that does every
  check: `plan_read`, `plan_numeric_att`, `plan_put_att`, `plan_put`,
  `define_numeric_var`, `plan_masking`. `nc_call`'s error path and
  `for_each_block` are not templates either. The typed code is mostly the
  `nc_get_*`/`nc_put_*` call. Before this split, gcov counted each check once per
  type, and io line coverage fell to about 70%.
  `tests/io/test_nc_typed.cpp` runs every typed call, and its errors, for all six
  types.

## Fixtures and test infrastructure

- **The generator is a library the tests call, not a `FIXTURES_SETUP`
  executable.** Each test writes the files it needs into its own `ScratchDir`. That
  keeps ctest's one-process-per-test-case runs independent and parallel-safe. It
  still uses only raw netCDF-C, never `mov::io`. The two `_FillValue` cases netCDF-C
  refuses to write (wrong type; two values) are classic CDF-1 files assembled byte
  by byte (`Cdf1` in `nc_fixtures.cpp`).
- The hostile set here is the one the WP6 task named: a 2^40 dimension (plus a
  2^40 × 2^40 × 2^30 variable for `overflow`), an attribute of 1 MiB + 1 byte, a NULL
  `NC_STRING` element (netCDF-C 4.9.3 writes it; the wrapper reads it as `""`), the
  wrong-type and two-value `_FillValue`, an unlimited `name_len` of length 0, a time
  that is all fill, time not at dimension or variable id 0, and an EPSG stored as
  text. §7.3's reader-level set (station 2^31, `obs_count`, 3-D time, …) stays with
  WP10b.
- `--wrap` (Linux only; GNU ld and lld both have it): `mov_io_netcdf_tests` is
  linked with `-Wl,--wrap=nc_open,--wrap=nc_create,--wrap=nc_close,--wrap=nc_abort`.
  Static linking means mov_io's calls and the generator's are both counted. A
  Catch2 listener `_Exit`s with a message when a test case ends with
  `opened != closed`. An `nc_abort` of a valid id counts as a close. The shim can
  also make the next `nc_close` fail (`fail_next_closes`), which tests the close
  policy. B5 opens 1200 files across five error paths and counts.
- Re-entry death test: `fork()`. The child resets `SIGABRT` to the default (Catch2
  would report it) and nests two `nc_status` calls; the parent expects `SIGABRT`.
  It runs only in debug builds on POSIX; release and fuzz builds skip it.
- `NC_HAS_HDF5 == 1` is `static_assert`ed in `file.cpp` and checked in a test. The
  version test pins `4.9.3`; update it with the vcpkg baseline.
- The threading notice ("the caller must serialize …") is in the header comment of
  `file.hpp` and on the class and `write_netcdf_atomic`, not repeated on each of the
  25 members.

## Not verified here (Linux x86-64 only)

- Windows: `nc_path` (ACP conversion, short names), long and non-ASCII paths,
  `MoveFileExW` replacing an HDF5 file just closed, MSVC `/W4` on the new code.
- macOS: `--wrap` does not exist in ld64, so the count tests and the leak listener
  are skipped. RAII is still exercised; the counting runs only on Linux CI.
- The structure fuzzer (§7.5) is WP10b. No fuzz target was added here.
