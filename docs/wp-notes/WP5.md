# WP5 notes (io foundations)

Fold into `docs/core-design.md`, then delete this file. Everything here is a
choice the design left open, or a change from it. The declarations otherwise match
§4.4 (stages), §5.0, §5.1 and §5.9.

## What exists

`error`, `warning`, `read`, `read_limits`, `text_file` (read and atomic write),
`cf_time`, `projection` under `src/io/include/mov/io/`; the private helpers
`detail/{text,line_cursor,atomic_file}.hpp` beside them; sources in `src/io/`.
Tests in `tests/io/`, fuzz targets `fuzz_parse_double`, `fuzz_line_cursor`,
`fuzz_parse_cf_time_units`, seeds in `tests/fixtures/io/<target>/`.

## Declarations that differ from §4–5

Layout and shared types
- The `detail` headers live in `src/io/include/mov/io/detail/` (as core's do), not in
  `src/io/detail/`: the tests and fuzz targets include them. They stay out of the
  documented API.
- `ReadLimits` and `ReadContext` are in `mov/io/read_limits.hpp`, namespace `mov::io`,
  not in `nc::` (§4.2): the text readers take them too. WP6 should use
  `mov::io::ReadLimits` and drop the `nc::` copy.
- `io::Error` keeps `Cancelled` (§5.0 lists it; the task text did not).
- `ParseError` is a class, not an aggregate, so the 120-byte, UTF-8-boundary bound on
  `context` cannot be bypassed: `ParseError::make(code, {.line = 3, .column = 7}, text)`,
  accessors `code() line() column() context()`. `detail::truncate_utf8` backs up over
  at most three continuation bytes; text that is not UTF-8 is cut at the limit.
- `FileOp` gains `create` (the temporary file) and `close`. `FormatError` has
  `station` and `index` defaulted to `nullopt` (designated initializers may omit them).
- `Warning::subject` and `count` have defaults (`{}` and `1`). + `to_token(WarningCode)`.
- `lift<V>` is constrained on `std::constructible_from<V, E>`, so a lift that cannot
  work is a compile error at the call (`not std::invocable<decltype(lift<Error>), int>`).
- `Read<T>`: `transform` requires an object result; `and_then` requires a `Read<U>`
  result. `and_then_read(expected<Read<T>, E>&&, f)` needs `E2` *implicitly*
  convertible to `E`; when the first stage fails its error is returned and its warnings
  are not (an error carries none). `Read` has `==` (defaulted).
- `tests/io/test_read_chain.cpp` uses stubs for `ImedsFile`, `StationTable`, `convert`
  and the exporter, over the real core units, `io::Error`, `Read` and `lift`. WP7
  replaces the stubs and keeps the type assertion.

`read_text_file` (§5.0)
- Returns the bytes as stored: the BOM, CRs and NULs are kept. `LineCursor` strips
  exactly one BOM (so "once" is a property of the pair; a second BOM is content).
- Errors: not found → `open`/`no_such_file_or_directory`; directory →
  `open`/`is_a_directory`; any other non-regular file (`/dev/null`, a FIFO) →
  `open`/`invalid_argument`; size over `max_text_bytes` → `size`/`file_too_large`
  (checked before allocating); a failed stream open → `open` with `errno`. A file that
  shrinks while being read returns what was read; one that grows is cut at the size
  first seen. Those two read races have no test.
- B3 test: copy the fixture, chmod 0444, assert opening for write *fails* (SKIP as
  root), then read.

`write_file_atomic` and the shared mechanism (§4.4)
- `detail/atomic_file.hpp` holds the mechanism WP6 reuses: `AtomicStage`,
  `FaultInjector{.fail_at, .temp_suffix}`, `temp_path_for` (`<target>.<16 hex>.tmp`,
  64 random bits from `std::random_device`), `create_exclusive` (`fopen` mode `"wbx"`,
  so `O_EXCL`/`CREATE_NEW`; no retry on collision), `commit_temp` (stages 4–6) and
  `TempFileGuard`. WP6 does stages 1–3 with `nc_create(NC_NOCLOBBER)`/`nc_close`, keeps
  a `TempFileGuard`, and calls `commit_temp`. The error always names the *target*.
- The text writer opens the temporary file a second time with `std::ofstream` after
  the exclusive create; the file is empty and ours, so there is no window.
- An injected fault returns `errc::io_error` at that stage. `body` and `close` faults
  happen after the real stage ran.
- **Deviation from "on any failure the target is byte-identical":** a failure of
  `fsync_dir` (stage 6) happens after the rename. The target is then the complete new
  file, the temporary file is gone, and the error says the rename may not survive a
  crash. The test pins this.
- `body` may throw: the guard removes the temporary file and the exception propagates.
  A stream in a failed state after `body` is `FileOp::write`.
- Not preserved: permissions of the replaced file (a new file gets the default), and a
  symbolic link at the target is replaced, not followed.
- macOS: `fcntl(F_FULLFSYNC)` first, `fsync` if the file system rejects it. Windows:
  `FlushFileBuffers` on the temporary file, `MoveFileExW(REPLACE_EXISTING |
  WRITE_THROUGH)`, and no directory step. **Neither is verified; see below.**

Text helpers (`detail/text.hpp`, `detail/line_cursor.hpp`)
- `parse_double`: `expected<double, NumberError>` (`empty`, `bad_syntax`,
  `out_of_range`). The grammar is checked first by hand
  (`[+-]? (digits [. digits?] | . digits) ([eE] [+-]? digits)?`); only then does a
  converter run, so neither can accept hex, `nan`, `inf`, whitespace or a trailing
  character. `1e400` and a nonzero value that rounds to zero (`1e-400`, `2e-324`) are
  `out_of_range`; denormals (`3e-324`, `1e-310`) are values; `-0` keeps its sign. This
  rule (not "whatever the converter says") is what makes the two paths agree.
- Two implementations are always compiled: `parse_double_from_chars` (only when
  `__cpp_lib_to_chars >= 201611L`) and `parse_double_strtod` (`strtod_l`/`_strtod_l`
  with an owned "C" locale object). `parse_double` calls the first when present, so
  **macOS (Apple libc++ without floating `from_chars`) runs the `strtod_l` path**.
  Tests run every token through every available path; `fuzz_parse_double` asserts the
  paths agree bit for bit.
- Locale test: sets `de_DE.UTF-8` (also tries `de_DE.utf8`, `de_DE`, `de-DE`), asserts
  the precondition (`strtod("1,5") == 1.5` and `strtod("1.5") == 1.0` there), then
  checks `1.5`/`1,5` for every path. When the locale is missing it **fails** if
  `MOV_REQUIRE_LOCALES` is defined (CMake option, set `ON` by `base` in
  `CMakePresets.json`, so dev, CI and the image) or the environment has
  `MOV_REQUIRE_LOCALES=1`; otherwise it skips.
- Added beyond the task text, from §5.0: `parse_int<I>` (whole token, `[+-]?digits`,
  `out_of_range` for overflow and for `-` on unsigned types), `to_lower_ascii`,
  `to_upper_ascii`, `cut_at_nul`, `reserve_capped`. Left for the work packages that
  need them: `parse_model_number` (WP8) and `uniquify_ids` (WP7).
- `split_ws` and `split_on` return `vector<string_view>`. `MOV_LIFETIMEBOUND` is
  `[[clang::lifetimebound]]`/`[[msvc::lifetimebound]]` (empty on GCC), but Clang does not
  diagnose a view held in a *vector*, so each also has a deleted overload for an rvalue
  `std::string` (a constrained template, so literals and `string_view` still work), and
  so does the `LineCursor` constructor. A test pins that with concepts.
- `LineCursor`: 1-based; strips one CR per line, also on a last line without `\n`;
  `"\n"` is one empty line, `""` has none; adds `at_end()` and `lines_read()`.

`cf_time` (§5.1)
- Grammar: `<unit> since <yyyy-mm-dd>[( |T)hh:mm[:ss[.f[f[f]]]]] [zone]`. Units
  `millisecond(s) second(s) minute(s) hour(s) day(s)`, any ASCII case; no abbreviations.
  The date and clock are `core::parse_utc_datetime`'s (four-digit year, two-digit
  fields, **exactly one space or `T`** between them; two spaces are `bad_time_units`
  at the clock). Zone: none (UTC), `Z`, `UTC`, or `±hh`, `±hh:mm`, `±hhmm` up to
  23:59, **applied** (`00:00 -06:00` is 06:00 UTC). Surrounding and repeated white
  space elsewhere is ignored.
- Errors are `ParseError` with line 1 and a byte column: `bad_time_units` (unit, `since`,
  zone), `bad_date` (the reference time; the column of `parse_utc_datetime`'s error
  shifted into the text), `trailing_text`. The full text is the context (truncated).
- `parse_cf_time_units` is not `constexpr` (a `ParseError` holds a `std::string`).
  `unit_ms`, `parse_cf_calendar`, `to_time` and `gregorian_reform` are.
- `parse_cf_calendar(std::optional<std::string_view>)`: absent → `standard`;
  `standard`, `gregorian` → `standard`; `proleptic_gregorian`; anything else (the empty
  string included) → `nullopt`. Case-insensitive, trimmed.
- `to_time` is a template over floating and integral value types (not `bool`), calling
  `core::checked_time`. Under `standard` it rejects a result **or an epoch** before
  1582-10-15: an old epoch makes every result a mixed-calendar date.
- Year 0000–9999 is the range of an epoch (the grammar's).

`projection` (§5.9, C19)
- vcpkg: `proj` with `default-features: false` (no `net`/curl, no `tiff`). There are no
  TIFF grids, so a datum shift that needs one falls back to PROJ's ballpark
  transformation (about a metre for NAD83 to WGS84; more for NAD27). EPSG:26915,
  3857 and other pure-math CRSs are unaffected.
- + `class Projector` (`make(Epsg)`, `to_location(Xy)`, `crs()`): a PROJ context and
  transformation built once, for a reader with many points. Not thread-safe. EPSG:4326
  holds no PROJ objects. `to_location(NativePoint)` keeps the latest `Projector` per
  thread, so converting the stations of one file costs one setup.
- Coordinates are x then y (easting/northing or lon/lat) in and out, whatever the EPSG
  axis order (`proj_normalize_for_visualization`).
- `ProjectionErrc::unknown_crs` also covers a code that is not a geographic or projected
  CRS (a vertical `5703` or geocentric `4978` would otherwise yield meaningless angles).
  `transform_failed`: no pipeline, or a non-finite result (`1e15 m` in UTM). The
  error variant is `variant<ProjectionError, LocationError>` as in §5.9.
- `proj.db`: `PROJ_DATA` if set, else the PROJ install the build found
  (`MOV_PROJ_DATA_DIR`, a compile definition of `projection.cpp`, set by
  `src/io/CMakeLists.txt` from `PROJ_DIR`). **Packaging (Phase 7) must ship `proj.db`
  and set `PROJ_DATA`** (or add a setter); the build-tree path is useless on a user's
  machine.
- Goldens: `tests/io/utm_reference.py` computes UTM 15N (Krueger series, GRS80) and
  Web Mercator without PROJ; the tests agree to 1e-7 degrees.

## Build and CI

- `tools/dev/Dockerfile`: `locales` in the apt list and
  `RUN locale-gen de_DE.UTF-8 && locale -a | grep ...` (the build fails if it did not
  take). The image tag changes, so `run.sh` rebuilds it.
- `.github/actions/setup-locales` (apt `locales`, `locale-gen de_DE.UTF-8`, verify) is
  used by the Linux `build-test` leg and by `sanitizers`, `fuzz` and `coverage`. The
  macOS runner ships `de_DE.UTF-8`; the Windows leg relies on the UCRT accepting
  `de-DE` (unverified).
- `tools/dev/README.md` documents `-DMOV_REQUIRE_LOCALES=OFF` for a native machine.
- `tests/support/include/mov/test/scratch_dir.hpp`: `ScratchDir`, `write_bytes`,
  `read_bytes`, `entry_names` (shared by later io tests).

## Test and tooling findings

- A deleted overload used in a `requires` expression *in a test body* is a hard error;
  the checks are concepts (`CanSplitWs<std::string>`), which are a substitution context.
- `CHECK_THROWS_AS(expr)` of a `[[nodiscard]]` `expected` trips
  `bugprone-unused-return-value`; cast to `void`.
- Clang's `-Wshadow` flags a local named `last`/`day`/`month` under
  `using namespace std::chrono`.
- GCC 14 at `-O3 -Wnull-dereference` reports a false positive inside
  `std::string(istreambuf_iterator, istreambuf_iterator)`; `read_bytes` uses
  `ostringstream << rdbuf()`.
- Catch2's `TEMPLATE_TEST_CASE` rejects duplicate type names: `int64_t` and `long` are
  one type on Linux and two on macOS, so the lists use the fundamental types.
- `pre-commit run` (staged files) cannot work in a worktree under `run.sh`: it needs to
  write git objects and the main repository's git directory is mounted read-only. Use
  `tools/dev/run.sh pre-commit run --all-files`.

## Not verified here (Linux x86-64 only)

- Windows: `FlushFileBuffers`/`MoveFileExW`/`_wfopen`/`_create_locale` code is compiled
  only under `_WIN32`; `de-DE` for the locale test; whether the `[posix]`-tagged tests
  (permissions) behave; `Windows.h` macro hygiene (`WIN32_LEAN_AND_MEAN`, `NOMINMAX`).
- macOS: the `strtod_l` path as the production path; `F_FULLFSYNC`; `<xlocale.h>`
  visibility of `strtod_l`/`newlocale` through `<cstdlib>`/`<clocale>`.
- MSVC: `[[msvc::lifetimebound]]`, `/W4` on the new headers.
- fsync/rename durability itself (power loss) is untestable here; only the sequence and
  the failure handling are tested.
- Tests that need a non-root user (read-only file, unwritable directory) skip as root.
