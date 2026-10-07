# WP6 notes (netCDF wrapper)

Fold into `docs/core-design.md` §4, then delete this file. Everything here is a
choice §4 left open or a change from it, including the changes from the post-WP6
review (neckbeard-nate, sean-parent) and the maintainer's decisions on it.

## What exists

Public headers in `src/io/include/mov/io/netcdf/`:
- `name.hpp`: `NcName`, `NcNameRef`.
- `types.hpp`: `Type`, `Numeric`, `type_of`, `readable_as`, `dispatch_numeric`,
  `DimInfo`, `VarInfo`, `DimRange`, `Slab`, `whole`, `rows_per_block`, `Global`,
  `AttTarget`, `VarOptions`, and `detail::Hyperslab`/`unzip`.
- `masking.hpp`: `Masking<T>`, and `detail::exact_from<T>`.
- `file.hpp`: `File` (read-only), `NewFile` (the write capability) and
  `write_netcdf_atomic`.

Plus `mov/io/detail/checked_product.hpp` and `mov/io/detail/traverse.hpp`.

Sources in `src/io/netcdf/`:
- `file.cpp`: `Dataset` (the inquiries both handles share), and `File`'s open,
  close, moves and structure queries.
- `attribute.cpp`, `read.cpp`, `blocks.cpp`, `masking.cpp`.
- `write.cpp`: `NewFile` and `write_netcdf_atomic_impl`.
- `path.cpp`: `nc_path`.
- `library.cpp`, moved from `src/io/netcdf_library.cpp`.
- The private headers `nc_call.hpp` and `internal.hpp`.

Tests: `tests/io/test_nc_*.cpp` in their own executable, `mov_io_netcdf_tests`,
plus the `_constexpr` pair built from `test_nc_constexpr.cpp`.
- Fixture generator: `tests/io/support/nc_fixtures.{hpp,cpp}` (library
  `mov_nc_fixtures`).
- `--wrap` shims: `tests/io/support/nc_wrap.cpp`; the counts are in
  `nc_counts.{hpp,cpp}`.
- CTest gates: `netcdf_include_gate` and `netcdf_include_gate_rejects_violations`
  (`cmake/CheckNetcdfInclude.cmake`, `tests/cmake/netcdf_include/`), and
  `io_compile_fail_checks` (`tests/cmake/compile_fail_io/`, run by the core
  harness).

## Declarations that differ from §4.2

- **Reading and writing are two types.**
  - `File` (from `File::open`) is read-only and has no define or put members.
  - **`NewFile`** is the write capability. It can be neither copied nor moved,
    only `write_netcdf_atomic` constructs it, and the body gets it by reference. It
    carries `define_dim`, `define_var<T>`, `define_char_var`, `put_att`, `put<T>`,
    `put_char_rows` and `end_define`, and its destructor carries the abort policy.
  - The `writable` flag and the `NC_EPERM` path are gone; a compile-time concept
    test pins that `File` cannot define.
- **One source of truth for limits.**
  - `File::open(path, limits)` stores the `ReadLimits`. The data reads take only a
    `StopToken` (default: never stops), and `File::limits()` returns what open got.
  - `write_netcdf_atomic(target, limits, body)` hands `limits` to the `NewFile`,
    which uses them for the attributes it writes.
  - `ReadContext` stays the text readers' type; a netCDF reader passes
    `ctx.limits` to `open` and `ctx.stop` to each read.
- `ReadLimits` gains **`max_result_bytes`** (1 GiB). Its comment lists the real
  peak of each read.
- The data reads (`read`, `read_blocks`, `read_samples`, `read_char_rows`,
  `read_strings`) return `expected<…, io::Error>`, because they can also end in
  `Cancelled`. Everything else returns `NcError`.
- **`read_blocks<T>(name, slab, visit, stop)`** is the one partition of a bulk
  read:
  - It resolves the variable once, reuses one buffer, and polls `stop`.
  - It hands `visit(std::span<const T>, DimRange outer)` each block of
    `rows_per_block(slab, limits.slab_elements)` outer indices, with every inner
    range whole.
  - `rows_per_block` is public and `constexpr`, so WP9 can size its own work
    without re-deriving the partition.
  - `read` reads each block straight into its place in the result;
    `read_samples` masks block by block straight into the `Sample` result.
  - A block is larger than `slab_elements` only when one outer index alone is.
- **`AttTarget` is a class**, not `variant<Global, NcNameRef>`. A variant cannot
  forward a literal to `NcNameRef`'s `consteval` constructor.
  - `nc::global` is the file.
  - It has `==`.
  - `AttTarget(const NcName&&)` is deleted.
- `Type` gains **`other`** (compound, enum, opaque, vlen).
  `to_type`/`to_nc_type` are one `constexpr` table, `static_assert`ed to be
  inverse.
- `dispatch_numeric(Type, on_numeric, on_other)` is total.
- `WrapperFault` gains **`unrepresentable_path`** (Windows); `NcOp` gains
  **`sync`**.
- `NcName == std::string_view` (and literals).
- `NewFile::put` and `put_att` deduce T from a contiguous container.
  `put_char_rows` takes any range of things convertible to `std::string_view`.
- `read_char_rows` accepts rank ≥ 1: the last dimension is the row length, and
  every other index is a row (a 1-D variable is one row).
- `NcError::object` names an attribute in ncdump's notation, `var:att` or `:att`.
  One formatter (`att_object`) builds it, only on an error path
  (`Dataset::att_status`).
- A moved-from `File` keeps its path, so its `closed` errors name the file. A
  moved-from `NcName` is empty: assign to it or destroy it (documented).

## Behaviour

Close policy (review blocker B1)
- **A failed `nc_close` is never followed by `nc_abort`.**
  - The NC4 close path frees state in its release phase without nulling it
    (`libhdf5/hdf5internal.c:583/650/802`). `NC3_close` frees before it returns a
    sync error (`libsrc/nc3internal.c:1314/1350`).
  - So `nc_abort` after a failed close frees twice. The reviewer's probe got an
    assert in a debug HDF5 and a segfault in release, and the test
    "a close that fails while netCDF-C releases the file does not crash"
    reproduces it.
  - We dropped the id: `close()` reports `NcError{close}` and the handle is given up.
    netCDF-C keeps that file's entry: **one leaked id per failed close**, the
    accepted cost. The earlier "no id leaks on any path" claim is withdrawn.
- **Write handles sync first.**
  - `NewFile::finish` (stage 3) calls `nc_sync` through `nc_call`; in define mode
    that runs enddef first.
  - A failed sync has released nothing, so the file is abandoned (enddef, then
    abort) and `NcError{sync}` is reported.
  - Then `nc_close`, with the same rule as above.
- **Abandoning a write (review blocker B2).**
  - In define mode `NC4_abort` copies the path into `char[NC_MAX_NAME + 1]` with
    `strncpy` (no terminator for paths of 256 bytes or more) and `remove()`s that
    copy. A long path can therefore make it delete a different file.
  - `NewFile::abandon` calls `nc_enddef` first, ignoring its status: netCDF-C clears
    the define-mode flag before anything that can fail (`nc4_enddef_netcdf4_file`).
    Only then does it call `nc_abort`, which then deletes nothing. `TempFileGuard`
    removes the temporary file.
  - The test wraps `remove` and asserts that netCDF-C calls none under a 240-byte
    directory. It fails without the enddef.
- `File::open` opens only a regular file. A directory is `LibraryStatus{EISDIR}`,
  anything else `LibraryStatus{EINVAL}`; a FIFO would block `open()`. The path is
  copied before the call.

The choke point
- `nc_status(fn) -> int` is the single entry point. `nc_call` passes the status to
  the non-template `status_to_expected`.
- `nc_inq_libvers` also goes through it.
- In debug builds an `inline std::atomic_flag` (relaxed) detects concurrent and
  re-entrant entry and asserts: a detector, not a lock.
- **`const` does not mean concurrently callable**; the comment of `file.hpp` says so
  beside the serial-queue precondition.

Sizes
- `check_slab` runs before any allocation. It checks:
  - the rank (`rank_mismatch`);
  - each range against its dimension (`NC_EINVALCOORDS`, `NC_EEDGE`);
  - `checked_product` (`overflow`);
  - `max_elements`, and the result bytes (`count × sizeof(element)`) against
    `max_result_bytes` (`too_large`).
- Charges:
  - `read_samples` is charged 16 bytes per `Sample`.
  - `read_char_rows` is charged 2 bytes per char (read, then copied) plus
    `rows × sizeof(std::string)`. It also checks `rows ≤ max_elements` (a stride of
    0 has no bytes).
  - `read_strings` is charged `n × sizeof(std::string)` up front (and reserves
    `n`), then each string's bytes as it copies them.
- Not done: the zero-fill of `read<T>`'s result vector. Not measured.

Masking (maintainer decision on attribute types)
- `plan_masking` (not a template) reads every attribute once:
  - `missing_value`, `valid_range` (2 values), `valid_min`, `valid_max` (1 each)
    are read as int64 when the attribute is an integer type and as double when it
    is a floating type, both exact.
  - `scale_factor` and `add_offset` (1 each) are converted to double, exactly or
    `type_mismatch`.
  - `_Unsigned` is checked.
- `_FillValue` is **strict**: the variable's own type and one value
  (`type_mismatch`, `count_mismatch`). It is read with its type checked, never
  through `nc_inq_var_fill`, which copies an attribute of another type into a buffer
  sized for the variable's (`libsrc/var.c:723`, B4).
- `masking<T>` only converts. `missing_value` and `valid_*` of **another numeric
  type are accepted when every value is exactly a T**
  (`detail::exact_from<T>`, `constexpr`, with a table of edge-case tests: 2^53 + 1,
  2^63, NaN, ±∞, 0.1 to float, 0.5 to int); otherwise `type_mismatch`. Tests cover
  a netCDF4-python double `missing_value` on a float, an int64 one on an int, and an
  int `scale_factor`.
- An unsigned or text attribute is `type_mismatch`.
- `valid_range` wins over `valid_min`/`valid_max`. The NUG's implied valid range
  is not applied, as in xarray.
- Byte variables get no default fill (NUG, `docs/attribute_conventions.md`: "If
  the data type is byte and _FillValue is not explicitly defined, then the valid
  range should include all possible values"). NC_NOFILL means no fill.
- int64 → double stays in the §4.3 table for time variables only: exact below 2^53,
  which `checked_time` enforces. `read`'s comment tells non-time callers to read
  `int64_t`.

Writing
- The stages are WP5's:
  1. `check_target_replaceable`;
  2. `temp_path_for`;
  3. `nc_create(NC_NETCDF4 | NC_NOCLOBBER)`;
  4. the body;
  5. `finish` (sync, close);
  6. `commit_temp`.
- `TempFileGuard` is armed after a successful create, so a name collision never
  deletes someone else's file. The `NewFile` is declared after the guard.
- An injected fault at `body` is `FileError{write}` and at `close`
  `FileError{close}`. Errors name the target.
- `define_numeric_var` runs plain steps: define, chunking, deflate (always with
  shuffle), fill.

Other changes from the review
- `projection.cpp`: the `std::mutex` guarding the PROJ data directory is gone (no
  mutex in io, C11). `set_projection_data_dir` stores the directory once
  (`std::call_once` plus an atomic pointer that readers load); the first call wins.
  The header says so.
- `detail::traverse` (stop at the first error) replaces the hand loops in
  `var_info` and `variables`. `detail::unzip` turns a `Slab` into netCDF-C's two
  arrays; `whole` uses `transform`.

## Library defect found (not ours; for the maintainer)

netCDF-C 4.9.3 with HDF5 2.1.1 (the vcpkg pins) **segfaults inside HDF5** in this
case:
1. A file is open through one handle.
2. The same file is opened through a second handle, its `NC_STRING` data is read,
   and that handle is closed.
3. The file is opened again, and `nc_inq_var` of the string variable is called.

The stack ends in `H5F_addr_decode` from `H5T__vlen_disk_isnull`, from
`H5D_get_create_plist`, from `nc4_get_var_meta`: the vlen fill value is converted
with a null file pointer. A short program using only netCDF-C reproduces it
(round 1 of open, read strings, close). It is not in the tests, because it
crashes. The tests avoid it by never holding two handles on a file whose strings
are read.

Readers that open the same file twice at once could hit it. v5 files have no
`NC_STRING`, but foreign files may (SN §12.5). Options:
- a process-wide "already open" refusal in `File::open`;
- or documenting "one handle per file".

**Decision needed.** Not reported upstream yet.

## Paths (Windows)

- netCDF-C 4.9.3 reads the `char*` path in the **active code page** (ACP):
  `libdispatch/dpathmgr.c` `ansi2utf8` and `NCopen3`, and `nc4_H5Fopen`/`nc4_H5Fcreate`
  calling `NCpath2utf8`. The exception is a process running with the UTF-8 code page.
- `nc_path` returns the native bytes on POSIX (tested with a Latin-1 name). On
  Windows it converts to the ACP with `WC_NO_BEST_FIT_CHARS`, falls back to the 8.3
  short name, and otherwise gives `unrepresentable_path`.
- **Phase 7:** give the application manifest `activeCodePage = UTF-8` and
  `longPathAware = true`.
- On Windows the path tests issue a `WARN` instead of failing, until the CI job
  shows the outcome. **None of the Windows code has been compiled or run.**

## Templates and coverage

The typed members are thin typed calls on non-template private halves:
`plan_read`, `plan_numeric_att`, `plan_put_att`, `plan_put`, `define_numeric_var`
and `plan_masking`. `for_each_block` and `status_to_expected` are not templates
either. `tests/io/test_nc_typed.cpp` runs every typed call, and its errors, for
all six types.

## Fixtures and test infrastructure

- The generator is a library the tests call, each test into its own `ScratchDir`;
  it uses only netCDF-C.
  - The wrong-type and two-value `_FillValue` cases are hand-made CDF-1 files.
  - `sabotage_hdf5_dataset` closes an HDF5 dataset behind netCDF-C's back, which is
    how B1 is reproduced.
- `--wrap` (Linux): `nc_open`, `nc_create`, `nc_close`, `nc_abort`, `nc_sync` and
  `remove`.
  - A Catch2 listener `_Exit`s when a test case ends with `opened != closed`.
  - `fail_next_closes` really closes the file but reports `NC_EHDFERR`.
  - `fail_next_syncs` fails without syncing.
  - `remove_calls` counts netCDF-C's own deletions. The tests' files are removed
    through the shared C++ library, which is not wrapped.
  - The B1 test settles its one accepted leak by hand.
- Regression tags:
  - B5: 1200 opens over five error paths.
  - B6: missing file (no close call at all), unwritable directory.
  - B19: a fault at every atomic stage.
  - Others as before.
- The re-entry death test uses `fork()`. The child sets `SIGABRT` to the default
  and `RLIMIT_CORE` to 0.

## Not verified here (Linux x86-64 only)

- **Windows:** `nc_path`, long and non-ASCII paths, `MoveFileExW` over an HDF5 file
  just closed, MSVC `/W4`.
- **macOS:** ld64 has no `--wrap`, so the count, close-failure, sync-failure,
  `remove` and B1 tests skip.
- **Not tested:** `NC3_close`'s sync-error path (B1's other half). The policy is
  the same; there is no classic writer to provoke it.
- The structure fuzzer (§7.5) is WP10b.
