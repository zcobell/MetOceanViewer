# WP5 notes (io foundations)

Fold into `docs/core-design.md`, then delete this file. Everything here is a
choice the design left open, or a change from it, including the changes from the
post-WP5 review (neckbeard-nate, conor-hoekstra) and the maintainer's decisions on
it. The declarations otherwise match §4.4 (stages), §5.0, §5.1 and §5.9.

## What exists

`error`, `warning`, `read`, `read_limits`, `text_file` (read and atomic write),
`cf_time`, `projection` under `src/io/include/mov/io/`; the private helpers
`detail/{text,line_cursor,parse_at,atomic_file}.hpp` beside them; sources in
`src/io/`, plus the private `src/io/file_handle.hpp`. Tests in `tests/io/`, fuzz
targets `fuzz_parse_double`, `fuzz_line_cursor`, `fuzz_parse_cf_time_units`, seeds in
`tests/fixtures/io/<target>/`.

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
  `column` refers to the text as the parser was given it; `context` is raw input and
  the UI must escape it (documented on the class).
- `FileOp` gains `create`, `close`, `fsync_dir` and `permissions`. `FormatError` has
  `station` and `index` defaulted to `nullopt` (designated initializers may omit them).
- `Warning::subject` and `count` have defaults (`{}` and `1`). + `to_token(WarningCode)`.
  New codes: `crs_approximate`, `time_precision_dropped`.
- `lift<V>` is constrained on `std::constructible_from<V, E>`, so a lift that cannot
  work is a compile error at the call.
- `Read<T>`: `transform` requires an object result; `and_then` requires a `Read<U>`
  result; + `pure(x)`. `expected<Read<T>, E>` is `WriterT Warnings (Either E)` (an
  error discards the warnings so far; documented in the header). The monad laws are
  tested for `and_then` and `and_then_read`: left and right identity, associativity,
  and the error as a left and right zero that stops the chain.
  `and_then_read(expected<Read<T>, E>&&, f)` needs `E2` *implicitly* convertible to `E`.
- `tests/io/test_read_chain.cpp` uses stubs for `ImedsFile`, `StationTable`, `convert`
  and the exporter, over the real core units, `io::Error`, `Read` and `lift`. WP7
  replaces the stubs and keeps the type assertion.

`read_text_file` (§5.0)
- Returns the bytes as stored: the BOM, CRs and NULs are kept. `LineCursor` strips
  exactly one BOM (so "once" is a property of the pair; a second BOM is content).
- The file is **opened first** (`O_RDONLY | O_NONBLOCK | O_CLOEXEC`, so a FIFO cannot
  hang the reader; Windows `CreateFileW` with `FILE_FLAG_BACKUP_SEMANTICS`), then
  `fstat`ed (`GetFileInformationByHandle`), then read from the same handle: what is
  checked is what is read. This replaces `status` + `file_size` + `ifstream` by name.
- Errors (compare `ec == std::errc::x`; the category is generic or system): not found
  → `open`/`no_such_file_or_directory`; directory → `open`/`is_a_directory`; any other
  non-regular file (`/dev/null`, a FIFO) → `open`/`invalid_argument`; size over
  `max_text_bytes` → `size`/`file_too_large` (before allocating); the real `errno` of a
  failed open or read, never a zero code. A file that shrinks while read gives what was
  there; one that grows is cut at the size first seen (no test for either race).
- B3 test: copy the fixture, chmod 0444, assert opening for write *fails* (SKIP as
  root), then read.

`write_file_atomic` and the shared mechanism (§4.4)
- **The body returns `std::expected<void, E>`**, E convertible to `io::Error`
  (concept `AtomicTextBody`), and `write_file_atomic` returns `expected<void,
  io::Error>`, the netCDF §4.4 shape. A body's error is returned unchanged and leaves the
  target byte-identical; a `void`, `bool` or `int` body is a compile error (tested with
  concepts). It was `FileError` and ignored the body's result (review blocker).
- `detail/atomic_file.hpp` holds the mechanism WP6 reuses: `AtomicStage`,
  `FaultInjector{.fail_at, .code, .temp_suffix}`, `temp_path_for`,
  `check_target_replaceable` (stage 0), `commit_temp` (stages 4–6, with the permission
  copy) and `TempFileGuard`. WP6 does stages 1–3 with `nc_create(NC_NOCLOBBER)` and
  `nc_close`, keeps a guard, and calls `commit_temp`. The error always names the *target*.
  (`create_exclusive` is gone: the text writer opens its file in one step.)
- **Temporary name** `.mov-<16 hex>.tmp` in the target's directory (fixed length: a
  255-byte target name cannot make it too long). 64 random bits from
  `std::random_device`, no retry on collision.
- **Exclusive create in one step (no reopen by name):** `fopen(temp, "wbx")`
  (`O_EXCL`/`CREATE_NEW`) and the body writes through a small `std::streambuf` over that
  `FILE*` (`StdioStreambuf`; no buffer of its own). A failed write, flush or `fclose`
  keeps the C library's `errno` (`ENOSPC` and friends); with none, `io_error`. `errno` is
  cleared before each call so a stale value cannot leak in.
- **Target checks:** an existing regular file without owner-write permission is
  `permission_denied` (stage 0, `FileOp::open`) and nothing is created; otherwise the
  new file gets the existing file's permission bits (owner/group/others) before the
  rename (`FileOp::permissions` on failure). A symbolic link at the target is replaced,
  not followed.
- **Directory fsync (`FileOp::fsync_dir`):** `EINVAL`, `ENOTSUP` and `EOPNOTSUPP` from
  it mean the file system has no such operation and are **success**, not an error (no
  durability flag; say if the maintainer wants one). Any other failure comes *after* the
  rename: the target is the complete new file, the temporary file is gone, the error says
  the rename may not survive a crash. The test pins both.
- **macOS:** `fcntl(F_FULLFSYNC)` first; `fsync` only if that fails with
  `ENOTSUP`/`EINVAL`/`ENOTTY`, any other failure is returned.
- **Windows:** `FlushFileBuffers` on the temporary file; `MoveFileExW(REPLACE_EXISTING |
  WRITE_THROUGH)` (the call returns after the move is flushed; documented in the code) with
  up to 5 attempts 20 ms apart on `ACCESS_DENIED`/`SHARING_VIOLATION` (scanners and
  indexers); no directory step. **None of this is verified; see below.**
- An injected fault returns `fault.code` (default `io_error`) at that stage; `body` and
  `close` faults happen after the real stage ran. The `directory fsync` exemption applies
  to injected codes too, which is how it is tested.
- `body` may throw: the guard removes the temporary file and the exception propagates.

Text helpers (`detail/text.hpp`, `line_cursor.hpp`, `parse_at.hpp`)
- `next_word(string_view& rest) -> optional<string_view>` and `skip_space` are the
  one whitespace primitive: `split_ws`, the CF scanner and `split_ws_into` use them.
- `split_ws_into(line, span<string_view> out) -> count` (non-allocating, counts no
  further than `out.size() + 1`) and `split_ws<N>(line) -> optional<array<…, N>>`
  (exactly N words) for the readers' hot path.
- `parse_double`: `expected<double, NumberError>` (`empty`, `bad_syntax`,
  `out_of_range`). The grammar is checked first by hand
  (`[+-]? (digits [. digits?] | . digits) ([eE] [+-]? digits)?`), with `strip_sign`
  shared with `parse_int` and the digit counts kept; only then does a converter run, so
  neither can accept hex, `nan`, `inf`, whitespace or a trailing character. `1e400` and a
  nonzero value that rounds to zero (`1e-400`, `2e-324`) are `out_of_range`; denormals
  are values; `-0` keeps its sign.
- Two implementations are always compiled: `parse_double_from_chars` (only when
  `__cpp_lib_to_chars >= 201611L`) and `parse_double_strtod` (`strtod_l` with an owned
  "C" locale object; `<xlocale.h>` on Apple). `parse_double` calls the first when present,
  so **macOS (Apple libc++ without floating `from_chars`) runs the `strtod_l` path**.
- Locale test: sets `de_DE.UTF-8` (also `de_DE.utf8`, `de_DE`, `de-DE`), asserts the
  precondition (`strtod("1,5") == 1.5`, `strtod("1.5") == 1.0` there), then checks every
  path. When the locale is missing it **fails** if `MOV_REQUIRE_LOCALES` is defined
  (CMake option, `ON` in the `base` preset) or the environment has
  `MOV_REQUIRE_LOCALES=1`; otherwise it skips.
- Also: `parse_int<I>`, `to_lower_ascii`, `to_upper_ascii` (core's `to_upper`, the same
  hunk v5 has), `cut_at_nul`, `reserve_capped`. Left for the work packages that need
  them: `parse_model_number` (WP8), `uniquify_ids` (WP7).
- `detail::at(line, token, code)` builds the `ParseError` for a token that is a view into
  a `LineCursor::Line` (line number, byte column of the token, the line as context);
  `double_at` / `int_at<I>` are `parse_double` / `parse_int` with the `NumberError` →
  `ParseErrc` mapping done once (`bad_number`/`bad_integer`, `out_of_range`).
- `split_ws`, `split_on` and the `LineCursor` constructor return views, so each has a
  deleted overload for an rvalue `std::string` (a constrained template; literals and
  `string_view` still work) besides `MOV_LIFETIMEBOUND` (Clang and MSVC; Clang does not
  diagnose a view held in a *vector*). Concept tests pin it.
- `LineCursor`: 1-based; strips one CR per line, also on a last line without `\n`;
  `"\n"` is one empty line, `""` has none; `at_end()`, `lines_read()`.

`cf_time` (§5.1)
- **`parse_cf_time_units` returns `expected<Read<CfTimeUnits>, ParseError>`.** Grammar,
  lenient for foreign files (CF §4.4; `core::parse_utc_datetime`, ADCIRC's text, stays
  strict): `<unit> since <Y-M-D>[( |T)h:m[:s[.f…]]] [zone]`.
  - Units: `millisecond(s) second(s) minute(s) hour(s) day(s)` and the UDUNITS
    abbreviations `d h hr min s sec` (the multi-letter ones also with a plural `s`), any
    ASCII case. The table has the singular words once; a plural adds `s`.
  - Four-digit year; **one or two digits** for month, day, hour, minute, second
    (`1800-1-1 0:0:0.0`). Exactly one space or `T` between date and clock; hour and minute
    are required with a clock.
  - A fraction of any length is **rounded half up to the millisecond**
    (`…00.9996` → next second); a nonzero digit beyond the millisecond that was dropped
    gives one `time_precision_dropped` warning (subject: the text); `.000000` loses
    nothing and warns nothing.
  - Zone: none (UTC), `Z`, `UTC`, or `±hh`, `±hh:mm`, `±hhmm` up to 23:59, **applied**
    (`00:00 -06:00` is 06:00 UTC); the offset digits are read with core's
    `DateTimeCursor`, errors at the sign. Other white space is ignored.
  - Errors are `ParseError` with line 1 and a byte column: `bad_time_units` (unit, `since`,
    zone), `bad_date` (the column of the character that does not fit, or of the field whose
    value is out of range), `trailing_text`. The text is the context.
  - Probes in the tests: `hours since 1800-1-1 00:00:0.0`,
    `seconds since 1970-01-01 00:00:00.000000`.
- `parse_cf_time_units` is not `constexpr` (a `ParseError` holds a `std::string`).
  `unit_ms`, `parse_cf_calendar` and `gregorian_reform` are.
- `parse_cf_calendar(std::optional<std::string_view>)`: absent → `standard`;
  `standard`, `gregorian` → `standard`; `proleptic_gregorian`; anything else (the empty
  string included) → `nullopt`. Case-insensitive, trimmed.
- **`CfClock` replaces the free `to_time`.** `CfClock::make(units, calendar)` is the
  reproducibility check, once per variable: the `standard` calendar with an epoch before
  1582-10-15 is `CfClockError::epoch_before_gregorian_reform`, which a reader reports as
  `FormatErrc::unsupported_calendar`. `clock.at(value)` (floating or integral, not `bool`)
  is then only the per-value range check: `core::checked_time`, and under `standard` a
  result before the reform is `nullopt` (→ `time_out_of_range` with the index). A
  `CfClock` cannot be made for a pair that is wrong in every value, so a reader cannot
  forget the check.
- Year 0000–9999 is the range of an epoch (the grammar's).

`projection` (§5.9, C19)
- vcpkg: `proj` with `default-features: false` (no `net`/curl, no `tiff`). **What PROJ
  does without grids:** it still plans a real operation where it knows one, and states
  its accuracy: NAD83 to WGS 84 (EPSG:26915 → 4326) is a datum shift stated as **4 m**
  (so it warns), WGS 84-based CRSs (UTM zones on WGS 84, Web Mercator) state nothing and
  are exact. It falls back to a **ballpark** transformation (a null shift, metres to
  kilometres off) only where it knows no operation for the point, e.g. a NAD83 point
  outside North America. My first notes called every no-grid case "a ballpark of about a
  metre"; that was wrong.
- + `class Projector` (`make(Epsg)`, `to_location(Xy)`, `crs()`, `accuracy()`,
  `approximation_warning()`): a PROJ context and transformation built once, for a reader
  with many points. Not thread-safe; EPSG:4326 holds no PROJ objects.
  `to_location(NativePoint)` builds a Projector for the call (a few ms) and takes a fast
  path for 4326 that builds none; the per-thread cache is gone.
- **Approximation warning (maintainer decision):** `Projector::accuracy()` records
  `points`, `sampled_points`, `ballpark_points` and the worst accuracy PROJ states
  (`proj_trans_get_last_used_operation`, `proj_coordoperation_has_ballpark_transformation`,
  `proj_coordoperation_get_accuracy`). `approximation_warning()` is a
  `WarningCode::crs_approximate` (subject `EPSG:n`, count = points converted) when a sampled
  point was ballpark or the worst stated accuracy exceeds 1 m. **Cost, measured** (100000
  points of EPSG:26915, debug build, PROJ release): 0.07 s without asking, **21 s** asking
  for the operation of every point (about 200 µs each: PROJ builds an operation object per
  call; there is no cheap identity to cache on). So the operation is asked for a sample:
  each of the first 256 points, then one in 64 (0.48 s for 100000); a file of up to 256
  stations is checked entirely. Readers (WP7 and later) call `approximation_warning()` after
  converting and append it to their warnings.
- Coordinates are x then y (easting/northing or lon/lat) in and out, whatever the EPSG
  axis order (`proj_normalize_for_visualization`). `Projector::to_location` is
  `project(…).transform_error(lift).and_then(Location::make…)`.
- Errors: `unknown_crs` (no code, or not a geographic or projected CRS: a vertical `5703`
  or geocentric `4978` would give meaningless angles), `transform_failed` (no pipeline, or
  a non-finite result, `1e15 m` in UTM), **`database_unavailable`** (`proj.db` not found or
  not a database: `proj_context_get_database_path` and a metadata query).
- **`proj.db`:** `set_projection_data_dir(dir)` (call once at application startup; it is
  mutex-protected) names the directory; the environment variable `MOV_PROJ_DATA` (not
  empty) overrides it, for a user or a test; with neither PROJ looks where it was built to
  and in its own `PROJ_DATA`. **No build-tree path is compiled in** (the earlier
  `MOV_PROJ_DATA_DIR` definition is gone). The tests find the vcpkg copy through a
  compile definition of `test_projection.cpp` (`tests/io/CMakeLists.txt`), which warns at
  configure when `proj.db` is not found. **Packaging (Phase 1, `docs/packaging.md`) ships
  `proj.db` and calls the setter.** Note that a search path *adds* nothing to what PROJ also tries (its
  compiled-in default can find a database anyway), so the tests provoke
  `database_unavailable` with a file named `proj.db` that is not a database.
  **Changed by packaging:** the "compiled-in default" is a copy of proj.db built into the
  static PROJ, which PROJ also falls back to when the file it is given cannot be opened.
  A configured directory now means exactly `<dir>/proj.db` (`proj_context_set_database_path`
  plus a check of the path PROJ reports), and `projection_database_path()` reports it;
  `docs/packaging.md`, PROJ data.
- PROJ logging is off (`PJ_LOG_NONE`); a failure is a result, not stderr noise.
- Goldens: `tests/io/utm_reference.py` computes UTM 15N (Krueger series, GRS80) and Web
  Mercator without PROJ; the tests agree to 1e-7 degrees. The thread test uses
  `std::thread` + `join`, not `std::jthread` (not confirmed on Xcode 16's libc++).

## Build and CI

- `cmake/ProjectOptions.cmake`: my `MOV_REQUIRE_LOCALES` edit had eaten the space in
  `option(MOV_ENABLE_CACHE "..."`, silently turning ccache off. Fixed.
- `tools/dev/Dockerfile`: `locales` and `RUN locale-gen de_DE.UTF-8 && locale -a | grep ...`
  (the build fails if it did not take).
- `.github/actions/setup-locales` is used by the Linux `build-test` leg and by
  `sanitizers`, `fuzz` and `coverage`. The macOS runner ships `de_DE.UTF-8`; the Windows
  leg relies on the UCRT accepting `de-DE` (unverified).
- `tools/dev/README.md` documents `-DMOV_REQUIRE_LOCALES=OFF` for a native machine.
- `tests/support/include/mov/test/scratch_dir.hpp`: `ScratchDir`, `write_bytes`,
  `read_bytes`, `entry_names`.

## Fuzz oracles

- `fuzz_parse_double`: both paths agree bit for bit; the verdict equals a `std::regex`
  of the grammar (tokens up to 128 bytes); an integer token of up to 15 digits has exactly
  the integer's value; the shortest `to_chars` text of the result parses back to the same
  bits.
- `fuzz_line_cursor`: `LineCursor` against a model (`split_on('\n')`, BOM skipped, empty
  last field dropped, one CR chomped); `split_ws` complete (tokens concatenate to exactly
  the non-space characters) and maximal (bounded by space or the ends) and equal to
  `split_ws_into`; `split_on` rejoins; `truncate_utf8` never cuts inside a sequence and
  keeps valid UTF-8 valid.
- `fuzz_parse_cf_time_units`: errors lie inside the text; accepted units round-trip through
  the canonical text without a warning; a metamorphic check (the same instant as local
  time + `+05:30` with an abbreviated unit parses to the same units); `CfClock::at` stays
  within ±2^53 ms for a double and an integer, which agree on an integral value. Seeds
  have a 16-byte header (a double, an int64).

## Test and tooling findings

- A deleted overload used in a `requires` expression *in a test body* is a hard error;
  the checks are concepts (`CanSplitWs<std::string>`), a substitution context.
- `CHECK_THROWS_AS(expr)` of a `[[nodiscard]]` `expected` trips
  `bugprone-unused-return-value`; cast to `void`.
- Clang's `-Wshadow` flags a local named `last`/`day`/`month` under
  `using namespace std::chrono`.
- GCC 14 at `-O3 -Wnull-dereference` reports false positives in
  `std::string(istreambuf_iterator, istreambuf_iterator)` (`read_bytes` now uses
  `ostringstream << rdbuf()`) and on `front()` of a `string_view` GCC cannot see is
  non-empty (`starts_with` and a guarded `remove_prefix` instead).
- Catch2's `TEMPLATE_TEST_CASE` rejects duplicate type names: `int64_t` and `long` are
  one type on Linux and two on macOS, so the lists use the fundamental types.
- A tool-call guard in this environment refuses commands containing the substring "git"
  (`digits`, `.github`) inside heredocs; scripts were written to files instead.
- `pre-commit run` (staged files) cannot work in a worktree under `run.sh`: it needs to
  write git objects and the main repository's git directory is mounted read-only. Use
  `tools/dev/run.sh pre-commit run --all-files`.

## Not verified here (Linux x86-64 only)

- Windows: all `_WIN32` code (`CreateFileW` reading, `FlushFileBuffers`,
  `MoveFileExW` and its retry, `_wfopen`, `_create_locale`/`_strtod_l`, `windows.h`
  hygiene); `de-DE` as the locale name; whether the `[posix]` tests behave.
- macOS: the `strtod_l` path as the production path; `<xlocale.h>`; `F_FULLFSYNC` and its
  fallback rule.
- MSVC: `[[msvc::lifetimebound]]`, `/W4` on the new headers.
- Durability (power loss) is untestable; only the sequence and the failure handling are.
- Tests that need a non-root user skip as root.
- Untested read races in `read_text_file`, and a real `ENOSPC` (only the injected code
  and the stream-failed fallback are tested).
- Nate's N11 (a note only) is not acted on.
