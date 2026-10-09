# MetOceanViewer v5 — Phase 2 design: `mov::core` and `mov::io`

Status: **as built** (Phase 2 end, 2026-10-09). The public headers under
`src/core/include/mov/core/` and `src/io/include/mov/io/` are the contract: every
declaration documents its own preconditions, errors, ownership and threading. This page
holds what a header cannot: the decisions and their reasons, the measured numbers, the
test plan, and what is deferred or unverified. Where this page and a header disagree,
the header is right and this page has a bug.

Inputs:
- `docs/rearchitecture-plan.md` (plan; decisions D1–D31)
- `docs/legacy-formats.md` (LF)
- `docs/provider-apis.md` (PA)
- `docs/v5-extras.md` (EX)
- `docs/station-netcdf.md` (SN, the normative file spec; §5.6 here is its C++ API)

Bug tags: `B#` = plan §1.2, `N#` = LF §14, `A#` = LF §13 ambiguity, `D#` = plan §6 decision.

---

## 0. Decisions (summary)

| # | Topic | Decision |
|---|---|---|
| C1 | Sample | `Sample` = `Missing` \| `Dry` \| finite `double` (16 B), **finite by construction**: `Sample::of(double) -> optional<Sample>`. The single lossy total factory is `finite_or_missing(double)`. `Dry` is allowed on any quantity. |
| C2 | Time order | `TimeSeries` times are **strictly increasing** (owner, SN-Q6). Lenient sources (IMEDS, the ADCIRC ASCII hot-start overlap, foreign and legacy station netCDF, D30.3) go through the total `normalize` / `normalizing_order` rule (stable sort, first of equal times kept) and report through warnings. v5 station files and model netCDF call `make` and reject disorder (`time_not_increasing`). |
| C3 | Meta | `SeriesMeta::make` is total (no datum field); a datum enters only through `assume_datum`, which checks `datum_applicable(quantity)` (a datum-carrying registry quantity or a generic one). An engaged unit or datum changes only through `convert` or `shift`; `assume_unit` and `assume_datum` apply only when the field is unset (`already_set` otherwise, even for the same unit). |
| C4 | File records | `StationTable` (core) is the record every file reader returns and every file writer takes: an ordered schema of `SeriesMeta` unique by token, an axis pool, and per station one axis index plus one sample column per schema entry. Model output has one shared axis, so no time vector is duplicated per station. Indices are the strong types `StationIndex` and `ColumnIndex`. |
| C5 | Positions | Projected to WGS84 `Location` at the read boundary (`FileStation.location`). The native point is kept as optional metadata. |
| C6 | Units | `Measure<UnitEnum>` (Length, Speed, Pressure, Discharge) in SI. The runtime `Unit` variant is built only through `parse_unit` (`""` → `nullopt`). The affine `Temperature` value type is deferred to Phase 3; `TemperatureUnit` (celsius, fahrenheit, kelvin) stays as a `Unit` alternative. |
| C7 | Quantities | `QuantityId = variant<GenericQuantity, Quantity>`, default the generic `value`. The registry is SN §6 plus `difference` (§2.5). `GenericQuantity` keeps the token and standard name and can only be built by parsing. |
| C8 | Errors | Errors are narrow per function inside core. io and each layer above have **one** error variant (`io::Error`). Provided: a generic `lift<V>` and `Read<T>` (value plus warnings) as a writer monad (`transform`, `and_then`, `and_then_read`, and the applicative `collect` / `collect_read`). `describe()` is called only at the edge. |
| C9 | Dry and sentinel rules | D16 (`raw <= -999` → `Dry`) applies **only to model-output readers**: ADCIRC elevation (ASCII and nc) and HWM modeled values. IMEDS masks only the **exact** legacy sentinels −99999, −9999, −DBL_MAX and −1.7977e+308 (v4's printed −DBL_MAX, N18), giving `Missing` plus a count warning. ADCIRC non-elevation outputs: `raw <= -999` → `Missing` (fill, N7 partner rule). D-Flow: no dry rule (SN §8.2). Fortran `NaN`/`Inf`/`****` tokens in model text → `Missing` plus a count warning. |
| C10 | HWM | Moments monoid with private state, folded left in order. σ uses `n−1` (D17). R² through the origin is **uncentred**, `1 − SSres/Σy²` (D25). `LinearFit = variant<ThroughOrigin, Free>`. |
| C11 | Threading | **No mutex in io.** io's netCDF functions are documented as not thread-safe. The **caller serializes**: providers and app own one serial queue (`QThreadPool` with `maxThreadCount(1)` plus `QtConcurrent::run(&pool, …)`). Every `nc_*` call goes through one choke point, which asserts against concurrent or re-entrant entry in debug builds. Reads go in bounded blocks and poll an `io::StopToken` (a predicate wrapper, not `std::stop_token`) between them. |
| C12 | Sizes and names | Every count is computed with `checked_product`. `ReadLimits` (elements, attribute bytes, text bytes, elements per block, result bytes) is enforced before allocation, and exceeding it gives `too_large`. Selection is required (`StationSelection`), never defaulted to all. Every netCDF name parameter is an `NcNameRef` (non-empty, NUL-terminated, at most `NC_MAX_NAME` bytes, no embedded NUL; literals are checked `consteval`). |
| C13 | Time arithmetic | Every file time goes through `checked_time` (a floating-point and an integral overload, both taking the unit as `std::chrono::milliseconds`). It rejects non-finite values, offsets beyond 2^53 ms and overflow. `CfTimeUnits` stores an integer unit in milliseconds; `CfClock` checks the (units, calendar) pair once per variable. |
| C14 | Ids and names | ADCIRC and D-Flow ids are the 0-based index. A station without a name keeps an empty one: no reader makes one up, a display shows the id, and only the station netCDF writer substitutes `"Station <id>"`. ADCIRC netCDF reads `station_name`. Legacy sources cut names at the first NUL, then collapse white space; bytes that are not UTF-8 become U+FFFD with a warning. v5 CF files keep names exactly; an embedded NUL there is `bad_encoding`. Duplicate ids in lenient sources (IMEDS, foreign and legacy netCDF) get `#2`, `#3` suffixes plus a warning. |
| C15 | Station netCDF | Reads v5 (L1/L2), foreign CF-DSG (all five layouts: CF 9.3.1–9.3.4 and the scalar station of 9.2) and legacy v4 files. The CRMS dialect is `not_this_format`. Writes v5 only. The `<q>_status` variable is written iff at least one sample of that column is `Dry`. `vertical_datum` is written whenever the meta datum is engaged. |
| C16 | Atomic writes | Unique temp file created `NC_NOCLOBBER` / `O_EXCL` → body → sync and close → fsync file → rename → fsync dir (§4.4). The netCDF write capability is `nc::NewFile`, which only `write_netcdf_atomic` constructs; a failed body abandons the file (end define mode, then `nc_abort`) and the temp file is removed. A fault can be injected at every stage. |
| C17 | IMEDS write (D15) | Values `{:14.6f}`, coordinates `{:.6f}`, times floored to whole seconds, non-values omitted. |
| C18 | CSV (D27, §9.1) | Long format, one row per sample, with a `status` column. Text cells starting with `= + - @ \t \r` get a `'` prefix (formula injection). Numeric and time cells, which the writer formats itself, are exempt. |
| C19 | Projection | `to_location` and `Projector` live in **io** with PROJ as a private dependency (core stays standard-library only). Projection runs at the read boundary. |

---

## 1. Toolchain constraints

### 1.1 Rules

| Feature | Rule |
|---|---|
| Floating `from_chars` (Apple libc++ lacks it through LLVM 20) | Only through `io::detail::parse_double`. Gate `__cpp_lib_to_chars >= 201611L`; the fallback is `strtod_l` / `_strtod_l` with an owned C locale object. Both implementations are always compiled and tested, and macOS runs the `strtod_l` path in production. |
| `std::chrono::parse`, `std::chrono::tzdb`, `zoned_time`, `std::print` | Not used (missing or inline-only on Apple targets for LLVM 18–19). Dates are parsed by hand (`core::parse_utc_datetime`, `io::parse_cf_time_units`, `io::detail/civil_time.hpp`), all on `sys_time`. |
| `views::zip`, `chunk_by`, `pairwise`, `enumerate`, `fold_left`, `join_with`, `ranges::to`, `ranges::iota` | Not used. Ordered folds use `std::accumulate` (and `transform_reduce` only where the join is exact); `TimeSeries::points()` is `iota \| transform`; runs of equal times are found with `find_if`. |
| `std::stop_token` | Not used (Apple libc++ ships it only with `-fexperimental-library`). `io::StopToken` wraps a predicate, which also lets the app pass `QPromise::isCanceled`. |
| `constexpr` `<cmath>` (`fabs`, `llround`; P0533) | Not used in constant expressions (Clang 20 with libstdc++ 14 refuses). `core/detail/numeric.hpp` works on the bit pattern. `constexpr std::isfinite` is detected by the probe, not required (MSVC STL). |
| `constexpr` `variant` holding a `std::string` (`Unit`, `QuantityId`) | Production code never constant-evaluates one. Tests that do are gated by `MOV_TEST_CONSTEXPR_VARIANT` (`__cpp_lib_variant >= 202106L`; `tests/support/include/mov/test/toolchain.hpp`) and run the same check at run time on libc++ 18 and 19. |
| `std::function_ref` (C++26) | Not used; constrained template parameters instead. |
| FMA contraction | `-ffp-contract=off` on every first-party target on GCC and Clang (`cmake/ProjectOptions.cmake`): otherwise `a * x + b` fuses on arm64 and `-march` builds, and `Affine` results differ in the last bit between compile time, x86-64 and macOS arm64. MSVC's `/fp:precise` does not contract across statements. The test "Affine at run time equals the compile-time result" pins it. |
| Non-ASCII literals | None in sources (degree signs are UTF-8 byte escapes), so MSVC needs no `/utf-8`. |
| `long double` | Is `double` on MSVC; the `checked_time` tests pass with that substitution. |

### 1.2 Probe results

`tests/core/test_toolchain_probe{,_constexpr}.cpp` use each gated feature once; every
test named `probe: ...` SKIPs where the standard library lacks the feature and must be
read on a new platform before anything is unguarded.

GCC 14.2 and Clang 20.1, both with libstdc++ 14 (the Linux dev image):

| Feature | GCC 14.2 | Clang 20.1 |
|---|---|---|
| `ranges::fold_left`, `views::zip`, `views::chunk_by`, `views::enumerate` | yes | yes |
| constexpr integer `from_chars`, constexpr `<chrono>` calendar, `expected` monadic ops | yes | yes |
| constexpr `std::isfinite` | yes | yes |
| constexpr `std::fabs`, `std::llround` | yes | **no** |
| `std::format` of `sys_time<ms>` (`{:%FT%T}Z`), `stop_token`, floating `from_chars` | yes | yes |

libc++ (probed with `<version>` macros, `-std=c++23`; the `dev-libcxx` preset builds
with 18):

| Feature | libc++ 18 | 19 | 20 | Consequence |
|---|---|---|---|---|
| `__cpp_lib_variant` | 202102 | 202102 | 202106 | the gate for constexpr `variant<string>` |
| constexpr `variant` holding `std::string` (tested) | no | yes | yes | 19 works despite the macro; the gate stays on the macro |
| `__cpp_lib_jthread` / `std::stop_token` | experimental only | experimental only | yes | probes SKIP |
| `__cpp_lib_ranges_fold`, `_enumerate`, `_join_with`, `_slide` | no | no | no | not used |
| `views::zip`, `views::chunk_by` | yes | yes | yes | `zip` has no macro in libc++; not used |
| `__cpp_lib_to_chars` (floating) | no | no | no | `strtod_l` path |
| `__cpp_lib_format` | no | 202110 | 202110 | `{:%FT%T}` of `sys_time` compiles in 18 anyway |
| `__cpp_lib_constexpr_charconv`, `__cpp_lib_expected` | yes | yes | yes | |

Both GCC and Clang reject a floating-point operation that overflows or produces NaN in a
constant expression (GCC for `inf - inf` and `1e308 * 1000`, Clang also for `NaN * 1.0`).
Hence `detail::is_finite`, `is_nan` and `magnitude` use `std::bit_cast` (exact,
branchless; `magnitude(-0.0)` is `+0.0`), `round_half_away` truncates and then looks at
the fraction (precondition: finite and `|v| < 2^62`), and `checked_time` bounds the value
*before* multiplying. Constexpr tests cannot build a NaN `Sample`; NaN tests are runtime
tests, and a negative NaN is built from bits.

### 1.3 Platforms and stand-ins

- CI (`.github/workflows/ci.yml`): `ubuntu-24.04` with GCC 14 (build-test, sanitizers,
  coverage, format-compliance), Clang 20 (fuzz, clang-tidy) and libc++ 18 (`libcxx`);
  `macos-15` with Xcode 16.2 pinned (Apple Clang 16, whose libc++ tracks LLVM 17–18: the
  oldest toolchain supported; `macos-14` is retired on 2026-11-02); `windows-2022` with
  MSVC. The macOS deployment target is 14.0: every gap above is header-only, so no
  feature in use needs a newer OS.
- `dev-libcxx` (Clang + libc++ 18 on Linux) is the stand-in for Apple Clang and is a
  blocking CI job; it is the only place the libc++ gaps show before the macOS job.
- `dev-msvc-xwin` (clang-cl against the real MSVC STL, UCRT and SDK, tests under Wine) is
  the stand-in for Windows; what it does and does not catch is in `tools/dev/README.md`.
- macOS CI findings: Apple Clang 16 lacks `-Wmissing-designated-field-initializers`, so a
  compile-fail case that needs a diagnostic carries `// requires-diagnostic: <name>` and
  `tests/cmake/compile_fail/run.cmake` first compiles a probe of that kind (Apple Clang
  skips the case, printed `SKIPPED`; any other compiler that compiles the probe fails the
  test). APFS refuses file names that are not valid UTF-8 (`EILSEQ`), so the `nc_path`
  POSIX round trip accepts a clean refusal there.

### 1.4 Not verified

- **Windows (never run; only the clang-cl cross-check):** every `_WIN32` branch in io
  (`CreateFileW` reads, `FlushFileBuffers`, `MoveFileExW` and its retry, `_wfopen`,
  `_create_locale` / `_strtod_l`, the file identity by volume serial and file index,
  `nc_path` to the active code page, the 8.3 fallback and `unrepresentable_path`,
  `read_file_prefix`); long and non-ASCII paths through netCDF-C (the path tests `WARN`
  instead of failing until a Windows run shows the outcome); `de-DE` as the locale name
  of the `parse_double` locale test; `cl`'s `/W4 /WX` diagnostics that clang-cl lacks
  (C4702, C4458, C4996), `[[msvc::lifetimebound]]`, the `template <double V>` detection
  in the constexpr probe, hex-float literals; the global `operator new` replacement of
  `tests/io/test_adcirc_allocation.cpp`; `core_compile_fail_checks` is skipped on MSVC.
- **macOS:** only the CI run; `F_FULLFSYNC` and its fallback rule, `<xlocale.h>`; ld64
  has no `--wrap`, so the netCDF open/close count, close-failure, sync-failure and
  `remove` tests skip there; `std::jthread` on Xcode 16's libc++ (the thread test uses
  `std::thread`).
- Durability under power loss is untestable (only the sequence and the failure handling
  are); the read races of `read_text_file`; a real `ENOSPC` (only the injected code and
  the stream-failed fallback); `NC3_close`'s sync-error path (no classic writer exists to
  provoke it).
- The measurements of §5.3, §5.4 and §5.9: one shared host, warm page cache, one ext4
  file system; cold cache, network file systems, deflate levels other than 1 and float
  data were not measured.
- The structure fuzzer (§7.5) is a sampler, not a proof.

---

## 2. `mov::core`

Headers are in `src/core/include/mov/core/`, namespace `mov::core`. Every value type is
regular and nothrow-movable (§7.1). Views into owned storage are `const&`-qualified with
the `const&&` overload deleted: with only `&& = delete`, a const rvalue still binds to
`const&` and dangles. A `requires` expression outside a template is ill-formed, not
false, when it names a deleted function, so tests wrap such checks in concepts.

**`detail::` is the fence.** Names in `mov::core::detail` are not API even when a public
header must declare them; the `core_detail_fence` test rejects any other layer naming
one. Helpers other layers need are public (`ascii.hpp`, `utf8.hpp`, `overloaded.hpp`,
`cancelled.hpp`). **One passkey:** `detail::CoreKey` (`detail/core_key.hpp`). Only
`detail::CoreAccess` can make one, and `CoreAccess` is defined in
`src/core/core_access.hpp`, which is on no other layer's include path. Every member that
bypasses a class's own checks takes a `const CoreKey&`: the `TimeSeries` constructor from
parts whose invariant the caller established (asserted in debug builds),
`TimeSeries::transform_samples`, `SeriesMeta::rewrite_unit` / `rewrite_datum`,
`StationTable::rewrite_column_unit` / `transform_column`. The keyed members are inline
and exercised only through the public operations (`series`, the vector series, `slice`,
`shift_time`, `residual`, `convert`, `scale_offset`, `shift`); `transform_samples` is
used by nothing in core and is untested.

### 2.1 Time — `time.hpp`

`Time` is `sys_time<milliseconds>`, UTC; `max_abs_time_ms = 2^53 − 1` is the file bound
(SN §7). `TimeRange::make(begin, end)` is half-open with no default; `ValidRange::make`
takes the aggregate `Bounds{.first, .last}` so the two optionals cannot be swapped
(`first == last` is one valid day; the default is unknown and ongoing).

`checked_time(value, std::chrono::milliseconds unit, Time epoch)` is two constrained
templates (`std::floating_point`, and `std::integral` but not `bool`), so no argument
type is ambiguous (tested with `int`, `long long`, `int32_t`, `uint32_t`, `uint64_t`,
`size_t`, `float`, `long double`). Integers use `cmp_less` / `cmp_greater`, so an unsigned
value above `INT64_MAX` is rejected, not wrapped; `float` and `long double` are bounded in
their own type before narrowing; `unit < 1 ms` is `nullopt`; the epoch itself is not
bounded, only the result. For an integral double with `|v| < 2^53` both paths agree (a
constexpr grid test and the `fuzz_checked_time` reference law).

`parse_utc_datetime` is strict (four-digit year, two-digit fields, a 1–3 digit fraction
in milliseconds, no leap second, no whitespace, no offsets); `DateTimeError::column` is
the 0-based offset of the first bad character, the text size when truncated, or the start
of the field whose value is out of range (`2005-02-30` gives 8). It replaces v4's
local-time parse of the ADCIRC cold start (N5).

### 2.2 Units — `units.hpp` (D13)

Factors to SI (`detail::*_factors`, indexed by enumerator): ft 0.3048, in 0.0254,
mi 1609.344, nmi 1852, kt 1852/3600, mph 1609.344/3600, km/h 1000/3600, hPa = mb = 100,
mH2O 9806.65 (v4 used 9806.38: a documented non-goal), ft3/s 0.028316846592.
`Measure<U>` stores SI, has no raw-number constructor, orders by `std::partial_ordering`
and offers exactly `+ − * /`, unary `+ −`, `Measure/Measure -> double`. Temperatures are a
`Unit` alternative only (C6); their conversion is affine through a Celsius hub with exact
constants.

`parse_unit` spelling table (pinned by test; symbols are case-sensitive, words match any
case):

| Unit | Accepted spellings |
|---|---|
| metre | `m`, `meter(s)`, `metre(s)` |
| foot, inch | `ft`, `foot`, `feet`; `in`, `inch(es)` |
| other lengths | `km`, `kilometer(s)`, `kilometre(s)`; `mi`, `mile(s)`; `nmi`, `nautical_mile(s)` |
| m/s, ft/s | `m/s`, `m s-1`; `ft/s`, `ft s-1` |
| knot | `kn`, `kt`, `knot(s)` |
| other speeds | `mph`, `mile hour-1`; `km/h`, `kph`, `km h-1`, `km hour-1` |
| pressure | `Pa`; `hPa`; `mb`, `mbar`, `millibar(s)`; `mH2O`, `m H2O` |
| discharge | `m3/s`, `m3 s-1`; `ft3/s`, `ft3 s-1`, `cfs` |
| °C | `degC`, `deg C`, `C`, `celsius`, `degree_C`, `degree_celsius`, `°C` |
| °F | `degF`, `deg F`, `F`, `fahrenheit`, `degree_F`, `degree_fahrenheit`, `°F` |
| K | `K`, `kelvin`, `degK`, `degree_K` |

Laws (tested and fuzzed): `parse_unit(symbol(u)) == u` and `parse_unit(udunits(u)) == u`
for every family enumerator. Text is trimmed and inner white-space runs collapse to one
space; `Mb`, `PA` and `c` are therefore `OtherUnit`s. The display symbol for knots is
`kt`; temperatures print as `°C` / `°F` (UTF-8 bytes). `udunits(meter_of_water)` is
`m H2O`, which was not checked against a UDUNITS table (plan §6, open items).

`OtherUnit` is built only by `parse_unit` through the passkey `detail::UnitKey` (its only
friend is `detail::UnitFactory` in `units.cpp`). Aliases canonicalize: `%` → `percent`;
`deg`, `degT`, `degrees`, `degrees_true` → `degree`; `sec` → `s`. `degree()` is a function
because an `OtherUnit` holds a `std::string`. `symbol` and `udunits` have a constexpr
overload per enumerator and for `const Unit&` (deleted for `Unit&&`: the view points into
the variant); the name tables are `static_assert`ed against the enums and
`variant_size_v<Unit>`, and the noexcept lookups use `get_if`, not `visit`.
`conversion(A, B)` for enumerators of different families is a deleted overload (a compile
error); the runtime `conversion(const Unit&, const Unit&)` is identity for equal
`OtherUnit`s and `IncompatibleUnits` otherwise. Whether an `OtherUnit` is one the
registry itself uses is `is_canonical_other` (§2.5); readers warn `unrecognized_unit`
for any other.

### 2.3 Geography — `geo.hpp`

`LatLon` and `Xy` have **no default member values**: a designated initializer that leaves
a field out is a `-Werror` compile error on GCC and Clang. `tests/cmake/compile_fail/`
(`core_compile_fail_checks`) asserts that, plus swapped designators and unit-family
mixing, each with an accepted twin. `Location::make` checks non-finite, then latitude
(`[-90, 90]`), then longitude (`[-180, 360]` → `(-180, 180]`). `Epsg::make` requires
`code > 0` and there is no default (B10). `NativePoint::make(Xy, Epsg)` rejects
non-finite coordinates.

### 2.4 Samples — `sample.hpp`

`Sample` is 16 B (`static_assert`); with its time a sample costs 24 B, the same as
`optional<double>`, less than a separate dry mask (25 B), and with no illegal state.
Model files are read by selection (§5.4–5.5), so the overhead stays bounded. `combine`
requires `op` to return exactly `double` (a `float` or `int` result is a compile error)
and is `noexcept` iff `op` is.

### 2.5 Quantities — `quantity.hpp` (SN §6)

The registry is SN §6 plus the last enumerator **`difference`**: token `difference`, long
name "Difference", no CF `standard_name`, no fixed unit (it takes its operands') and not
`datum_applicable`. `QuantityInfo::standard_name` and `canonical_unit` are therefore
empty for it; `canonical_unit(Quantity)` returns `optional<Unit>` (nullopt for
`difference`); `is_canonical_other` is derived from the registry (no second list) and
ignores empty units. Readers and writers must not assume a registry quantity has a
standard name or a canonical unit.

`GenericQuantity::parse({.token, .standard_name})` takes the aggregate `Spec`. `==`
compares both members, so two quantities with one token and different standard names are
different values that name the same variable; anything that must be unique per variable
(the `StationTable` schema) keys on `token()`. There is no length cap.
`quantity_for_standard_name(name, after)` finds the two water levels in registry order
(observed first). `token(const QuantityId&)` has a deleted `&&` overload (the view points
into a `GenericQuantity`).

### 2.6 Series metadata — `meta.hpp` (C3)

`SeriesMeta::Fields` is `{quantity, label, unit}` and `make` is total; the datum has one
checked entry point, `assume_datum` (`already_set`, then `not_applicable`). `assume_unit`
is `already_set` whenever a unit is engaged, even the same one. `rewrite_unit` and
`rewrite_datum` (keyed) serve `convert` and `shift`; `rewrite_datum` returns
`optional<DatumRewrite{from, meta}>` so the old datum and the new metadata come as one
fact.

### 2.7 Time series — `timeseries.hpp`

`make` reports `LengthMismatch{times, samples}`, then the first `TimeNotIncreasing{index}`;
`detail::first_not_increasing` is behind every strictly-increasing check of the core.
`make` does not bound times to ±2^53 ms; `StationTable::make` does (the file bound). A
series offers `with_samples` (same axis, O(1) check, for derived series outside core),
`into_parts` (to move the vectors into a table) and `assume_*` in `const&` and `&&`
forms (the `const&` copies only on success).

`normalize(rows, meta)` is total: the fast path (`adjacent_find` of a non-increase) takes
the rows as they are; otherwise it counts descents, `stable_sort`s only if there is one,
keeps the first of each run of equal times and counts `duplicates_dropped` and
`conflicting_duplicates` (dropped rows whose sample differs). `normalizing_order(times,
differs)` is the same rule on indices, for records of several samples (the ADCIRC ASCII
hot-start overlap, §5.3), and `normalize` is written on top of it.

### 2.8 Stations and the station table — `station.hpp`, `station_table.hpp`

- `DataSource` tokens are `{source, token}` rows, `static_assert`ed in enumerator
  order; the seven tokens are pinned by a test.
- `StationText` (well-formed UTF-8 without NUL, may be empty) and `StationKey`
  (non-empty `StationText`) are constexpr, ordered, built only by `make`, and used by
  `FileStation{StationKey id; StationText name; ...}`. The error enumerators name the
  violated property; the field is known to the caller. Lenient sources clean the bytes
  first (`io::detail::replace_invalid_utf8`). Both `make`s use explicit branches: GCC 14
  cannot constant-evaluate `expected::transform` on a small `std::string`.
- `Provider` requires `source`, `canonical(string_view) -> string` and `valid_id`.
  `StationId<P>::make` trims, canonicalizes, validates the grammar, then builds the
  `StationKey` it holds (`key()`). Grammars: CO-OPS `^[0-9]{7}$`; USGS
  `^[A-Z0-9]+-[A-Za-z0-9]+$` with the agency prefix upper-cased (`usgs-07374000` →
  `USGS-07374000`); NDBC `^[A-Z0-9]{5}$` upper-cased; harmonics stations non-empty UTF-8
  of at most 255 bytes without control characters (the harmonics reader narrows it).
  `StationId` keeps `<=>`: ids are ordered map keys and sorted lists.
- `StationSelection` stores its `station_count()`, part of `==`; `applies_to(n)` is an
  O(1) `selection_mismatch` check; an empty selection is valid; `out_of_range` is
  reported before `duplicate_index`.
- `StationTable::make(variables, axes, stations)` takes variable-major input
  (`Variable{meta, per_station}`, `StationRow{station, axis}`); storage is
  `[column][station]`. `TableError = variant<SchemaError{code, column},
  StationError{station, fault}>` with `SchemaErrc {duplicate_quantity,
  station_count_mismatch}` (keyed on token) and `StationFault = variant<
  DuplicateStationId, AxisOutOfRange, TimeOutOfRange{index}, TimeNotIncreasing{index},
  ColumnLengthMismatch{column}, SchemaMismatch>`. The text errors of earlier drafts are
  gone: the types rule them out. Check order: each variable (duplicate token, then
  station count), then each station (duplicate id, axis index, the first axis element out
  of range or not increasing, column lengths); an axis error names the first station
  using the axis; unused axes are dropped and the pool renumbered. Equality is by value,
  however the axes are pooled.
- `from_series(schema, series)` moves the series in through `into_parts`; a meta that
  differs from the schema is `SchemaMismatch`; the conversion is partial, because a
  `TimeSeries` may hold times beyond ±2^53 ms (`TimeOutOfRange`). `single_axis`
  short-circuits on a shared pooled axis: 1000 stations on one pooled axis of 100000
  times took 41 ms per call before, about 1 µs after (release build).
- `detail::is_valid_utf8` (`utf8.hpp`) is strict per Unicode table 3-7: no overlongs,
  surrogates or code points above U+10FFFF.

### 2.9 Vector series — `vector_series.hpp`

`VectorSeries::make` accepts the registered pairs only, `(current_u, current_v)` and
`(wind_u, wind_v)` in that order; `assume_components(u, v)` is the visibly named way to
pair two generic series and refuses registry quantities. Both check the pair, then the
times, then the unit (`unit_unknown`, then `units_differ`); datums are not compared (no
derived series carries one). The series stores what `make` proved: `kind()` and
`unit()`. `vector_series(table, StationIndex, ColumnIndex, ColumnIndex)` goes through
`make`; indices out of range are a precondition, as for the table accessors.

Derived metadata (the datum is always `nullopt`; the "stem" is `wind` or `current` for
the registered pairs, otherwise the trimmed `u` label without a trailing `u`/`U`/`x`/`X`
and separator, or `vector` when nothing is left):

| Result | Quantity | Unit | Label |
|---|---|---|---|
| `magnitude` of a wind pair | `wind_speed` | component unit | `"wind speed"` |
| `magnitude` of other pairs | `GenericQuantity::value()` | component unit | `"<stem> speed"` |
| `cartesian_direction` | `GenericQuantity::value()` (**never** `wind_direction`, which is meteorological "from") | `degree()` | `"<stem> direction (cartesian: degrees counter-clockwise from east, toward)"` |
| `magnitude3` | `GenericQuantity::value()` | horizontal unit | `"3D <stem> speed"` |

`cartesian_direction` is `atan2(v, u)` with an exact `-π` made `π` before the conversion
to degrees; a zero vector is `Missing`. `magnitude3(horizontal, w)` is `std::hypot` of
three (pinned with 2, 3, 6 → 7) with its own `VerticalErrc`.

### 2.10 Series operations — `series_ops.hpp`

- **`Bucket` / `ValueSummary` (one fact, one optional).** A `Bucket` is `{missing, dry,
  optional<ValueSummary>}`; the summary exists only once there is a value and holds
  `count`, `min`, `max` (first of equal), `first`, `last` and the sum. `+` joins the
  optionals with `detail::join_optional`; ties keep the left operand (`ranges::min` /
  `max` return their first argument), `first` is the left operand's and `last` the
  right's, so identity and associativity are exact on dyadic data. **The sum is carried
  as Σ v·2^-64** (an exact scaling): a merge can neither overflow nor make a NaN
  (`1e308 + 1e308 − 1e308 − 1e308` is exactly 0 in every grouping), `mean()` is finite
  for any finite values and bit-identical to `sum / n` wherever the plain left-fold sum
  is finite; `sum()` is infinite when the true sum is out of range; values below about
  1e-288 in magnitude lose bits. `Bucket::of` is not `noexcept` (`Sample::visit` is
  `std::visit`); `operator+` is.
- `summarize(span<const Time>, span<const Sample>)` is the one ordered left fold, and
  `index_window(times, range)` the two `lower_bound`s: a pixel column summarizes a
  sub-span with no copy. `slice` is `index_window` plus a copy (the rvalue overload
  erases in place).
- `QuickStats` is `{missing, dry, optional<ValueStats{count, min, max, mean}>}`;
  `values()` is `stats ? count : 0`, so "no stats but some values" cannot be written.
  `extent` is nullopt iff the series is empty; `join` of extents is an exact semilattice
  (any grouping), so the many-series overload may use `transform_reduce`.
- **`residual`** (EX #3) has its own `ResidualErrc`, in check order: `quantities_differ`
  (allowed: equal quantities, a generic pair with one token, or `water_level` observed
  with `water_level_prediction` predicted, in that order; the registry has no other such
  pair), `unit_unknown`, `units_differ` (no silent conversion), `temperature_difference`,
  then, when `datum_applicable`, `datum_unknown` and `datums_differ`. The output is the
  `difference` quantity, the common unit, no datum, label `<obs> U+2212 <pred>`; a
  residual can feed a residual (two differences are the same quantity), cannot be given a
  datum and cannot be shifted. Equal times only; no common time gives an empty series,
  not an error.
- `scale_offset` takes a `Calibration` (total `make({.scale, .offset})` rejecting NaN and
  infinities; the default is the identity), not an `Affine`: the metadata is unchanged
  because a calibration is not a unit conversion (recording it in the label was
  considered and left out; the caller can `with_label`).
- `shift_time` is limited only by 64-bit overflow; `shift_time(s, 0ms) == s` for every
  legal series. The first overflowing index is found without a scan: for `dt < 0` only a
  prefix can underflow (the front decides) and for `dt > 0` only a suffix can overflow
  (`partition_point`). Nothing changes on error.
- **One mapping path.** `detail::mapped(TimeSeries, Affine)` (`src/core/series_rebuild.hpp`,
  private) maps the values and recognizes the identity `Affine` once (the samples are
  untouched, so `-0.0` keeps its sign); `convert`, `scale_offset` and `shift` all go
  through it and the keyed constructor. A result that is not finite becomes `Missing`
  with **no warning** (decision: Missing is the answer). `convert` to an equal unit
  returns the series unchanged (equal `OtherUnit`s too); the table overload takes a
  `ColumnIndex` and skips `transform_column` for the identity.

**Phase 5 decimation recipe (not implemented here).** What the above provides for M4
decimation (first, min, max, last per pixel column) of a long series to W columns over
`[t0, t1)`:

1. Column edges: edge k is `t0 + q·k + (r·k)/W` with `span = t1 − t0`, `q = span / W`,
   `r = span % W` (integer division, k from 0 to W); every product stays below `span` or
   `W²`, so nothing overflows (`span·k` would).
2. Edge indices: one pass of `lower_bound` per edge (the `lo` of `index_window`), since
   the edges increase; column k is `[edge_k, edge_{k+1})`.
3. Segmented `summarize` of the sub-spans, no copy; an empty column is the identity
   `Bucket{}`.
4. Emit per column from `summary()`: `first`, then `min` and `max` ordered by time (one
   point if they are the same sample), then `last`; drop consecutive duplicates. A column
   with `has_gap()` (or an empty column between two non-empty ones) ends the polyline.
5. Autoscale the y axis with one `std::accumulate` of the buckets (or `extent` of the
   visible window); no second pass over the samples.

### 2.11 Datums — `datum.hpp`, `datum_shift.hpp` (D18)

`VerticalDatum` has ten enumerators: the SN §10.2 tokens plus `mtl`, `igld85` and
`stnd`; the core enum is authoritative. `parse_vertical_datum` returns
`expected<optional<VerticalDatum>, UnknownDatum>`: `""`, white space and `none` (any
case) are an engaged nullopt (no datum); the tokens and the aliases `NAVD`, `NGVD`,
`IGLD` match any case with surrounding ASCII white space ignored; anything else is
`UnknownDatum{.text}`, the trimmed text as a **view of the argument**.

`DatumTable::from_heights(reference, rows)` takes heights **above the reference**; the
reference is preset to 0, so a row restating it must be 0 (else
`ConflictingHeight{reference}`); the first conflicting or non-finite row in input order is
reported; unless the reference is MSL an MSL row is required (`MissingMsl`, also for empty
rows); rebasing on MSL that overflows is `NonFiniteHeight{datum}`. Known gap: `offset(a,
b)` is a plain subtraction, so heights near ±1e308 m could give an infinite `Length`; not
worth an error type. F12 (`tests/fixtures/core/datum/offsets.csv`): the legacy CSV
columns are *offsets* (the value to add to the pivot-datum series), which are the negated
heights above the pivot; the fixture carries heights and `test_datum.cpp` checks the
arithmetic.

`shift(series, to, table)`: `ShiftError = variant<UnknownUnit, NotALengthSeries,
UnknownSourceDatum, MissingOffset>`, in the pinned check order of the header; the offset
is `h[from] − h[to]` in metres, converted to the series' unit with `Length::as` and
added (a level at MSL expressed in MLLW at The Battery gains +0.77527 m, pinned against
the F12 rows). The rewritten metadata is computed before the `from == to` test: one
metadata copy on the identity path, in exchange for reading `from` and the new metadata
from one fact. A `DatumTable` that carries its station is deferred (§9.3).

### 2.12 Core error inventory

| Function family | Error (narrow) |
|---|---|
| `TimeRange`, `ValidRange`, `Location`, `Epsg`, `NativePoint`, `StationSelection`, `parse_utc_datetime` | own `enum class` / `DateTimeError` |
| `parse_vertical_datum` | `UnknownDatum` (nullopt in the value means no datum) |
| `StationText`, `StationKey`, `StationId` | `StationTextError`, `StationKeyError`, `StationIdError` |
| `SeriesMeta::assume_unit` / `assume_datum` | `AssumeUnitError`, `AssumeDatumError` |
| `TimeSeries::make` / `with_samples` | `ConstructionError` / `LengthMismatch` |
| `VectorSeries`, `vector_series` | `VectorErrc` |
| `magnitude3` | `VerticalErrc` |
| `residual` | `ResidualErrc` |
| `conversion` | `IncompatibleUnits` |
| `convert` | `UnitError` |
| `shift_time` | `TimeOverflow` |
| `DatumTable::from_heights` / `offset` | `DatumTableError` / `MissingOffset` |
| `shift` | `ShiftError` |
| `StationTable::make` / `from_series` | `TableError` |
| `checked_elevation`, `model_value` | `ElevationError` |
| `ErrorClasses::make` | `ClassBreaksError` |
| `hwm_stats` | `HwmStatsError` |

Errors carry no text. `describe(const X&) -> std::string` for each of them belongs to
the edge (`src/app`, `src/cli`) and is written with the first of those; `core` and `io`
have none.

---

## 3. HWM statistics — `hwm.hpp`, `hwm_stats.hpp` (D16, D17, D25)

**Boundary and dry rule.** `model_value(raw, LengthUnit) -> expected<WetDry,
ElevationError>` applies the dry rule first (`raw <= -999`, so `-inf`, `-99999` and
`-DBL_MAX` are `Dry`); anything else must pass `checked_elevation`: finite and
`|value| <= max_elevation_m = 1e4 m` (a prefilter of `|raw| > 1e300` keeps the unit
conversion finite for any double). The HWM reader (§5.7) builds `ground` and `observed`
through `checked_elevation` and `modeled` through `model_value`; non-finite and
out-of-range are `ParseError`s, never `Missing` or 0. Elevations are therefore bounded
by 1e4 m, so no square, product or sum of marks overflows; `NonFiniteMoments` is a
backstop for marks built some other way.

**Classes.** Break and error comparison is on a 1 nm grid: `detail::grid_nm` is the
metres value times 1e9, `round_half_away`, after saturating at ±1e6 m (monotone, and it
keeps `round_half_away`'s `|v| < 2^62`). `classify` is `upper_bound` on the grid and
`ErrorClasses::make` is `adjacent_find(greater_equal)` on it, so breaks closer than about
1 nm (or both beyond 1e6 m) are rejected. The contract: a decimal tie goes to the upper
class, exactly, for inputs with at most 9 decimal places in metres or 5 in feet or inches
(what a survey or a model writes). The raw-double comparison is wrong for many decimal
ties in metres as well as feet (`2.3 − 1.8` is `0.4999999999999998` against a break of
0.5), and v4 was not "always upper": it compared raw doubles in the file's unit, so
observed −9.94 ft and modeled −6.44 ft (error `3.499999999999999`) fell *below* the
3.5 ft break; the feet path is worse because `Length` stores SI, so each side is first
multiplied by 0.3048 (28 of 77 decimal (break, observed) pairs miss the break by an ulp).
Only the comparison is quantized; no statistic is. `ClassBreaksError::not_finite` is
checked before the order; `breaks() const&&` is deleted.

**Moments.** Private state in one aggregate `Fields` (no default member values) behind a
private constructor: `seen`, `n`, three means, four M2 sums. `of(Dry)` is
`{seen = 1, n = 0}`; an operand with `n == 0` leaves the other's wet fields bit-identical
and adds to `seen`, so `HwmStats::total` (= `seen`) `>= wet` cannot be violated.
`operator==` is bit-for-bit (`bit_cast` of the fields). Welford is `operator+` with a
one-observation right operand; there is no separate function. `wet_moments` is a
constexpr, header-inline `views::transform(Moments::of)` into `std::accumulate`: an
ordered left fold, not `ranges::fold_left` (libc++ lacks it) and not `reduce` /
`transform_reduce` (they may regroup; the join is only approximately associative).
`hwm_stats` lives in `hwm_stats.cpp` (`std::sqrt`).

**Statistics.** `HwmStatsError` order: no wet marks, non-finite moments, too few for a
free fit, degenerate observed. After the fold every moment is checked for finiteness, and
so are the slope, intercept and R². The clamps are `v > hi ? hi : v` (not
`std::min` / `max`, which turn a NaN into the bound). R² is `nullopt` with no degrees of
freedom (free fit with at most 2 wet marks, origin fit with 1) and when its denominator
is zero (free: `m2y == 0`; origin: `Σy² == 0`). The free R² is `(cxy/m2x) · (cxy/m2y)`,
asserted in debug builds to be at most `1 + 4 n eps` before it is clamped to 1 (the
overshoot is accumulated rounding in the moments, a few n ulps, not one ulp); the origin
R² is in `[0, 1]` by construction. `fit_of` switches over `Intercept` with no default.

Derived quantities. Raw sums are reconstructed as `Σx² = m2x + n·x̄²` and
`Σxy = cxy + n·x̄·ȳ`; likewise `Σy²`.

| | free | through_origin |
|---|---|---|
| slope | `cxy/m2x` | `Σxy/Σx²` |
| intercept | `ȳ − slope·x̄` | none (`ThroughOrigin`) |
| R² | `cxy²/(m2x·m2y)` | `1 − SSres/Σy²`, with `SSres = max(0, Σy² − slope·Σxy)` (uncentred, D25; differs from v4) |
| σ | `√(m2e/(n−1))` | same |

Golden numbers come from `tests/fixtures/core/hwm/golden.py` (standard library only,
Python ≥ 3.11): exact `Fraction` sums and breaks, cross-checked against
`statistics.linear_regression` (proportional and free), `correlation` (r²) and `stdev`
(n − 1). `golden.txt` is committed; the CTest `hwm_golden_is_current` runs
`golden.py --check`, and `MOV_REQUIRE_GOLDEN_CHECKS` (on in the `ci-*` presets) makes a
missing interpreter a configure error.

---

## 4. netCDF wrapper — `mov::io::nc` (`netcdf/file.hpp`)

### 4.1 Rules

| Rule | Spec |
|---|---|
| Threading (C11) | **Not thread-safe.** `file.hpp` says it once for every member of `File` and `NewFile`, `write_netcdf_atomic` and every reader built on them: the caller serializes all calls into mov::io netCDF functions, across all files. `const` means the member does not change the `File`, not that it may run concurrently. |
| Choke point | `nc_status(fn) -> int` (`src/io/netcdf/nc_call.hpp`, private) is the single entry to netCDF-C; `nc_call` passes the status to the non-template `status_to_expected`. `nc_inq_libvers`, `nc_free_string`, `nc_abort` and the destructors' `nc_close` go through it too. In debug builds an `inline std::atomic_flag` (relaxed) detects concurrent and re-entrant entry and asserts: a detector, not a lock. |
| One handle per file | `File::open` documents it (the library defect in §4.6). Debug builds list the open files **by identity** (`st_dev` / `st_ino`; Windows: volume serial number and file index, written but not compiled), taken when the file is opened, and assert on a second open of the same file whatever the spelling of the path, the working directory, a symbolic or a hard link; a file that cannot be examined (anything but "not there") asserts too. The list is touched only inside the choke point's debug entry check, so a second thread is caught as well; release builds keep no list. Every reader opens its file once and closes it before it returns (`tests/io/test_model_nc_handles.cpp`, `test_station_nc_handles.cpp` count the opens and closes). |
| Include gate | `#include <netcdf.h>` appears only under `src/io/netcdf/` (CTest `netcdf_include_gate` and `netcdf_include_gate_rejects_violations`, `cmake/CheckNetcdfInclude.cmake`). `nc_max_name = 256` is mirrored in `name.hpp` and `static_assert`ed equal to `NC_MAX_NAME` in `file.cpp`. |
| Sizes (C12) | `check_slab` runs before any allocation: the rank (`rank_mismatch`), each range against its dimension (`NC_EINVALCOORDS`, `NC_EEDGE`), `checked_product` (`overflow`), then `max_elements` and the result bytes against `max_result_bytes` (`too_large`). `read` and `read_samples` charge the whole slab; `read_blocks` charges each block, so a reader walks a slab of any size with one call. Charges: `read_samples` 16 B per `Sample` plus one block of the raw type; `read_char_rows` 2 B per char (read, then copied) plus `rows × sizeof(std::string)`, and `rows <= max_elements`; `read_strings` `n × sizeof(std::string)` up front, then each string's bytes. The zero-fill of `read<T>`'s result vector is not charged (not measured). |
| Cancellation | Bulk reads go in blocks of `rows_per_block(slab, limits.slab_elements)` outer indices (a block is larger only when one outer index alone is) and poll the `StopToken` before each (`Cancelled`). The data reads therefore return `io::Error`; everything else `NcError`. |
| Paths (Windows) | netCDF-C 4.9.3 reads its `char*` path in the **active code page** (`libdispatch/dpathmgr.c`, `NCpath2utf8` in `nc4_H5Fopen` / `nc4_H5Fcreate`), unless the process runs with the UTF-8 code page. `detail::nc_path` (`netcdf/path.cpp`) returns the native bytes on POSIX (tested with a Latin-1 name); on Windows it converts to the ACP with `WC_NO_BEST_FIT_CHARS`, falls back to the 8.3 short name, and otherwise reports `WrapperFault::unrepresentable_path`. The application manifest therefore sets `activeCodePage = UTF-8` and `longPathAware = true` (`docs/packaging.md`, Windows). None of this has run on Windows (§1.4). |
| Close policy | A failed `nc_close` is **never followed by `nc_abort`**: the NC4 close path frees state in its release phase without nulling it (`libhdf5/hdf5internal.c`), and `NC3_close` frees before it returns a sync error (`libsrc/nc3internal.c`), so an abort after a failed close frees twice (a debug HDF5 asserts, a release one segfaults; the test "a close that fails while netCDF-C releases the file does not crash" reproduces it). The id is given up instead: `close()` reports `NcError{close}` and netCDF-C keeps that file's entry, **one leaked id per failed close**, the accepted cost. Write handles sync first: `NewFile::finish` calls `nc_sync` (in define mode that runs enddef first); a failed sync has released nothing, so the file is abandoned and `NcError{sync}` reported; then `nc_close` with the rule above. |
| Abandoning a write | In define mode `NC4_abort` copies the path into `char[NC_MAX_NAME + 1]` with `strncpy` (no terminator for paths of 256 bytes or more) and `remove()`s that copy, so a long path can delete a different file. `NewFile::abandon` calls `nc_enddef` first, ignoring its status (netCDF-C clears the define-mode flag before anything that can fail), and only then `nc_abort`, which then deletes nothing; `TempFileGuard` removes the temporary file. The test wraps `remove` and asserts netCDF-C calls none under a 240-byte directory. |
| Opening | `File::open` opens only a regular file: a directory is `LibraryStatus{EISDIR}`, anything else `LibraryStatus{EINVAL}` (a FIFO would block `open()`). The path is copied before the call. |

### 4.2 API

Public headers in `src/io/include/mov/io/netcdf/`:

| Header | Provides |
|---|---|
| `name.hpp` | `NcName` (owning, validated; a moved-from one is empty, assign to it or destroy it), `NcNameRef` (non-owning, NUL-terminated; a literal is checked `consteval`, an `NcName&&` is rejected). `NcName == std::string_view`. |
| `types.hpp` | `Type` (with **`other`** for compound, enum, opaque and vlen types; `to_type` / `to_nc_type` are one constexpr table `static_assert`ed inverse), `Numeric`, `type_of`, `readable_as` (§4.3), `sample_readable` (byte, short, int, float, double: what `read_samples` and the readers built on it read), the total `dispatch_numeric(Type, on_numeric, on_other)`, `DimInfo` (with `unlimited`), `VarInfo`, `DimRange`, `Slab`, `whole`, the public constexpr `rows_per_block`, `Global` / `nc::global`, `AttTarget` (a class, not a variant: a variant cannot forward a literal to `NcNameRef`'s `consteval` constructor), `VarOptions`. |
| `masking.hpp` | `Masking<T>` (§4.3) and `detail::exact_from<T>`. |
| `file.hpp` | `File` (read-only, from `File::open(path, limits)`; the limits are stored, so the data reads take only a `StopToken` and `limits()` returns what open got), **`NewFile`** (the write capability: neither copyable nor movable, constructed only by `write_netcdf_atomic`, handed to the body by reference; `define_dim`, `define_var<T>`, `define_char_var`, `put_att`, `put<T>`, `put_char_rows`, `end_define`; `put` and `put_att` deduce `T` from a contiguous container, `put_char_rows` takes any range convertible to `string_view`), and `write_netcdf_atomic(target, limits, body)` (§4.4). A compile-time concept test pins that `File` cannot define. |

`File` queries: `find_dim` / `find_var` (nullopt when absent, never id 0: B12),
`variables`, `chunk_shape` (nullopt for contiguous, compact and classic storage),
`reserve_chunk_cache` (grows, never shrinks, §5.4), `att_type` (one inquiry),
`text_att` (attlen-sized, B8; NC_CHAR or a one-element NC_STRING), `numeric_att<T>` (exact
type), `read<T>`, `read_blocks<T>` (one partition of a bulk read: the variable resolved
once, one reused buffer, `visit(span<const T>, DimRange outer)` per block), `masking<T>`,
`read_samples` (masks block by block straight into the `Sample` result), `read_char_rows`
(rank ≥ 1: the last dimension is the row length, every other index a row), `read_strings`
(freed through the choke point, B21). `NcError::object` names an attribute in ncdump's
notation (`var:att`, `:att`), formatted only on an error path. A moved-from `File` keeps
its path, so its `closed` errors name the file. The typed members are thin typed calls on
non-template private halves (`plan_read`, `plan_numeric_att`, `plan_put_att`, `plan_put`,
`define_numeric_var`, `plan_masking`; `for_each_block` and `status_to_expected` are not
templates either); `tests/io/test_nc_typed.cpp` runs every typed call, and its errors, for
all six types. `NcError`, `NcOp`, `WrapperFault` and `LibraryStatus` are in `error.hpp`.

### 4.3 Type rules

| `read<T>` from var type | double | float | int8 | int16 | int32 | int64 |
|---|---|---|---|---|---|---|
| double | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ |
| float | ✓ | ✓ | ✗ | ✗ | ✗ | ✗ |
| byte | ✓ | ✗ | ✓ | ✓ | ✓ | ✓ |
| short | ✓ | ✗ | ✗ | ✓ | ✓ | ✓ |
| int | ✓ | ✗ | ✗ | ✗ | ✓ | ✓ |
| int64 | ✓ (time only; exact below 2^53, which `checked_time` enforces; other callers read `int64_t`) | ✗ | ✗ | ✗ | ✗ | ✓ |
| unsigned, char, string, other | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |

- `read_samples` masks in the native type and only then widens and unpacks (B4, B9).
  64-bit integers are `type_mismatch` for it.
- `put<T>` requires the variable type to equal `T` exactly.
- Masking attributes (`plan_masking`, not a template, reads each once): `missing_value`,
  `valid_range` (2 values), `valid_min`, `valid_max` (1 each) are read as int64 when the
  attribute is an integer type and as double when floating, both exact; `scale_factor`
  and `add_offset` (1 each) are converted to double, exactly or `type_mismatch`;
  `_Unsigned = "true"` is `unsupported_unsigned` (callers turn it into
  `skipped_variable`, foreign files only). **`_FillValue` is strict**: the variable's own
  type and one value (`type_mismatch`, `count_mismatch`), read with its type checked and
  never through `nc_inq_var_fill`, which copies an attribute of another type into a
  buffer sized for the variable's (`libsrc/var.c`, B4). `missing_value` and `valid_*` of
  **another numeric type are accepted when every value is exactly a T**
  (`detail::exact_from<T>`, tested at 2^53 + 1, 2^63, NaN, ±∞, 0.1 to float, 0.5 to int),
  else `type_mismatch`; an unsigned or text attribute is `type_mismatch`. `valid_range`
  wins over `valid_min` / `valid_max`; the NUG's implied valid range is not applied (as in
  xarray). Byte variables get no default fill (NUG: "if the data type is byte and
  _FillValue is not explicitly defined, then the valid range should include all possible
  values"); `NC_NOFILL` means no fill.

### 4.4 Atomic write — `write_netcdf_atomic` (C16)

The mechanism is `detail/atomic_file.hpp`, shared with the text writer
(`write_file_atomic`): stage 0 `check_target_replaceable`, 1 create, 2 body, 3 close,
4 `fsync_file`, 5 rename (with the permission copy), 6 `fsync_dir`; stages 4–6 are
`commit_temp`, and a `FaultInjector{.fail_at, .code, .temp_suffix}` can fail any stage
(a `body` or `close` fault happens after the real stage ran; the `fsync_dir` exemption
below applies to injected codes too, which is how it is tested). The netCDF writer does
stages 1–3 with `nc_create(NC_NETCDF4 | NC_NOCLOBBER)` at the temporary path and
`NewFile::finish`; errors always name the *target*.

- **Temporary name** `.mov-<16 hex>.tmp` in the target's directory (fixed length, so a
  255-byte target name cannot make it too long); 64 random bits from
  `std::random_device`, no retry on collision. The text writer creates exclusively in one
  step (`fopen(temp, "wbx")`; the body writes through a small `std::streambuf` over that
  `FILE*`), so there is no reopen by name; a failed write, flush or `fclose` keeps the C
  library's `errno` (`ENOSPC` and friends), cleared before each call so a stale value
  cannot leak in. `TempFileGuard` is armed after a successful create, so a name
  collision never deletes someone else's file.
- **Target checks:** an existing regular file without owner-write permission is
  `permission_denied` (stage 0, `FileOp::open`) and nothing is created; otherwise the new
  file gets the existing file's permission bits before the rename (`FileOp::permissions`
  on failure). A symbolic link at the target is replaced, not followed.
- **Directory fsync:** `EINVAL`, `ENOTSUP` and `EOPNOTSUPP` mean the file system has no
  such operation and are **success** (no durability flag; plan §6, open items). Any other
  failure comes *after* the rename: the target is the complete new file, the temporary
  file is gone, the error says the rename may not survive a crash.
- **macOS:** `fcntl(F_FULLFSYNC)` first; `fsync` only if that fails with `ENOTSUP` /
  `EINVAL` / `ENOTTY`. **Windows:** `FlushFileBuffers` on the temporary file;
  `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)` with up to 5 attempts 20 ms apart on
  `ACCESS_DENIED` / `SHARING_VIOLATION` (scanners and indexers); no directory step. Not
  verified (§1.4).
- The body returns `std::expected<void, E>` with `E` convertible to `io::Error`
  (concepts `AtomicNcBody`, `AtomicTextBody`); a `void`, `bool` or `int` body is a
  compile error. A body's error is returned unchanged and leaves the target
  byte-identical. A body may throw: the guard removes the temporary file and the
  exception propagates.

### 4.5 Bug classes 4–15

| Bug | Fix |
|---|---|
| B4 | No untyped API; typed reads; fill compared in the native type; narrowing reads refused; exact-type `put`. |
| B5 | RAII `File`; one choke point; `--wrap` open/close count tests (§7.2); every reader opens once and closes before returning. |
| B6 | `open` returns `expected`; on failure no `File` exists to close. |
| B7, B11 | `read_char_rows` stride comes from the dimension. |
| B8 | `text_att` sizes from `nc_inq_attlen`, capped by `max_att_bytes`. |
| B9 | `Masking` covers fill, default fill, `missing_value`, valid range and NaN. |
| B10 | `att_type`, then a read of any signed integer type, then `Epsg::make`; text, floating or unsigned is `type_mismatch`, never a code. |
| B11 | `parse_cf_time_units` + `CfClock` + `checked_time`. |
| B12 | `find_*` return `optional`; dimensions are found by name and variables matched to them by id, never by position; there are no name→id maps. |
| B13, B14 | `extent` → `optional`. |
| B15 | `put_att` byte length, exact `put` counts, `put_char_rows` padding. |

### 4.6 Library defects found (netCDF-C 4.9.3 with HDF5 2.1.1, the vcpkg pins)

Neither is ours; neither has been reported upstream (plan §6, open items).

1. **Segfault inside HDF5** when a file is open through one handle, the same file is
   opened through a second handle whose `NC_STRING` data is read and that handle is
   closed, and the file is then opened again and `nc_inq_var` of the string variable is
   called: the stack ends in `H5F_addr_decode` from `H5T__vlen_disk_isnull`, from
   `H5D_get_create_plist`, from `nc4_get_var_meta` (the vlen fill value is converted with
   a null file pointer). A short program using only netCDF-C reproduces it. It is not in
   the tests because it crashes; the tests never hold two handles on a file whose strings
   are read. v5 files have no `NC_STRING`, foreign files may (SN §12.5), hence the
   one-handle rule of §4.1.
2. **Renaming a variable breaks the file:** `nc_open(NC_WRITE)`, `nc_redef`,
   `nc_rename_var(obs_count -> count)`, `nc_close` all return 0, but after reopening
   `nc_inq_var` gives `NC_EHDFERR` for every variable with dimensions. The same rename
   through netCDF4-python (netCDF-C 4.9.3 with HDF5 1.14.6) works, and our library reads
   that file. Renaming the coordinate variable `time` fails at once ("Problem with HDF5
   dimscales"). v5 never renames, so it matters only to tests: the validation tests
   build what editing cannot make from scratch (`make_skeleton`, raw netCDF-C). A
   `_FillValue` must also be set in the same define session as its variable.

---

## 5. `mov::io` readers and writers

Rules for this section:
- **Every text `parse_*` takes a `ReadContext`** and enforces it itself:
  `parse_x(text, ..., ctx) -> expected<Read<T>, Error>`. The error is `io::Error`, not
  `ParseError`, because a parse can be `Cancelled` and `lift<Error>` does not convert a
  narrower variant. The one exception is `parse_adcirc_ascii_header(text) ->
  expected<AdcircAsciiHeader, ParseError>`: a two-line probe with no limits to enforce,
  no cancellation and no warnings. `read_x` is `read_text_file` plus the same driver.
- **`ParseErrc::too_large`** is "a `ReadContext` limit was exceeded": `line` is where the
  reader noticed, `context` is `"<used> <unit>, limit <n>"` (units `elements`, `bytes`).
  `FileError{size, file_too_large}` is only the file-size check in `read_text_file`.
  `max_elements` counts samples plus **16 per station** (a station costs a few hundred
  bytes, as much as sixteen samples, and a file of nothing but station lines would
  otherwise be unbounded).
- Every reader returns `Read<T>` (C8), projects to `Location` at the boundary (C5) and,
  for model netCDF, requires a `StationSelection` and an `Epsg` (no defaults).
- Every reader fails through `detail::fail(detail::format_error(...))`
  (`detail/reporting.hpp`); the failures of building core values map through
  `detail::to_format_error` (`detail/table_error.hpp`: exhaustive over `TableError`,
  `StationKeyError`, `StationTextError` and `ProjectionError`, no generic arm), so no
  reader invents a code; subjects taken from a file go through `detail::subject_of`
  (120 bytes on a UTF-8 boundary); units through `detail::parsed_unit`, the one unit rule
  (`parse_unit`, plus `unrecognized_unit` whose subject is the unit text *as found*).

### 5.0 Shared — `error.hpp`, `warning.hpp`, `read.hpp`, `read_limits.hpp`, `text_file.hpp`

- `Error = variant<FileError, ParseError, NcError, FormatError, Cancelled>`, with
  `Cancelled` the core's (`core/cancelled.hpp`), one alternative for io and the core's
  long computations. `ParseError` is a class, not an aggregate, so the 120-byte,
  UTF-8-boundary bound on `context` cannot be bypassed (`ParseError::make(code, {.line,
  .column}, text)`); `column` refers to the text as the parser was given it; `context` is
  raw input that the UI must escape. `FormatError` has `station` and `index` defaulted
  (designated initializers may omit them). `lift<V>` is constrained on
  `constructible_from<V, E>`, so a lift that cannot work is a compile error at the call.
- `Read<T>`: `transform` requires an object result, `and_then` a `Read<U>` result, plus
  `pure(x)`. `expected<Read<T>, E>` is `WriterT Warnings (Either E)`: an error discards
  the warnings so far (a reader that failed has no value to qualify). The monad laws are
  tested for `and_then` and `and_then_read`. `collect(thunks...)` is the applicative
  counterpart (runs in order, stops at the first error, yields a tuple) and
  `collect_read` the same over `Read` with the warnings in stage order; readers use them
  so no `operator*` on an `expected` is taken before every independent stage has
  succeeded.
- `ReadLimits` and `ReadContext` are in `read_limits.hpp` (namespace `mov::io`, not
  `nc::`: the text readers take them too). `ReadLimits` lists the real peak memory of
  each netCDF read beside its fields.
- `read_text_file` returns the bytes as stored (BOM, CRs and NULs kept; `LineCursor`
  strips exactly one BOM). The file is **opened first** (`O_RDONLY | O_NONBLOCK |
  O_CLOEXEC`, so a FIFO cannot hang the reader; Windows `CreateFileW` with
  `FILE_FLAG_BACKUP_SEMANTICS`), then `fstat`ed, then read from the same handle: what is
  checked is what is read. Errors compare as `ec == std::errc::x`: not found →
  `open` / `no_such_file_or_directory`; a directory → `open` / `is_a_directory`; any other
  non-regular file → `open` / `invalid_argument`; over `max_text_bytes` → `size` /
  `file_too_large` (before allocating); otherwise the real `errno`, never a zero code.
  `read_file_prefix(path, max_bytes)` is the same open and checks for a sniff, with no
  size limit. B3 test: copy the fixture, chmod 0444, assert opening for write *fails*
  (SKIP as root), then read.
- Text helpers live in `src/io/include/mov/io/detail/` (not `src/io/detail/`: the tests
  and fuzz targets include them; they are not API): `text.hpp` (`next_word` and
  `skip_space` are the one white-space primitive behind `split_ws`, `split_ws_into`,
  `split_ws<N>`, `split_on`, `split_on_into`; `parse_double`, `parse_int<I>`,
  `strip_sign`; `simplified`, `to_lower_ascii`, `to_upper_ascii`, `cut_at_nul`,
  `reserve_capped`; `MOV_LIFETIMEBOUND` plus deleted `std::string&&` overloads for every
  function that returns views), `line_cursor.hpp` (1-based; one CR per line, also on a
  last line without `\n`; `"\n"` is one empty line, `""` none; `next_nonblank`,
  `peek_blank`, `peek_nonblank`, `remaining_bytes`), `parse_at.hpp` (`at`, `position_at`,
  `double_at`, `int_at<I>`: the `NumberError` → `ParseErrc` mapping once), `model_number.hpp`
  (§5.3), `station_names.hpp` (`replace_invalid_utf8`, `uniquify_ids`: the k-th later
  `A` is `A#k+1`, skipping ids taken, so `A, A, A#2` gives `A, A#2, A#2#2`),
  `civil_time.hpp` (Hinnant's `days_from_civil` / `civil_from_days` on `int64`:
  `year_month_day` has a 16-bit year while a `Time` reaches about ±285,000 years, and
  neither the text readers nor the writers may build one out of range; goldens: `+max` is
  `287396-10-12T08:59:00.991Z`, `-max` is `-283457-03-21T15:00:59.009Z`), `reporting.hpp`,
  `table_error.hpp`, `atomic_file.hpp`, `checked_product.hpp`, `traverse.hpp`,
  `adcirc_schema.hpp`, `station_groups.hpp`, `legacy_station_names.hpp`.
- `parse_double`: the grammar `[+-]? (digits [. digits?] | . digits) ([eE] [+-]? digits)?`
  is checked by hand first, and only then does a converter run, so neither
  implementation can accept hex, `nan`, `inf`, white space or a trailing character.
  `1e400` and a nonzero value that rounds to zero (`1e-400`, `2e-324`) are
  `out_of_range`; denormals are values; `-0` keeps its sign. Both implementations are
  always compiled (`parse_double_from_chars` only when `__cpp_lib_to_chars >= 201611L`,
  `parse_double_strtod` with an owned C locale object, `<xlocale.h>` on Apple) and
  `parse_double` calls the first when present. The locale test sets `de_DE.UTF-8` (also
  `de_DE.utf8`, `de_DE`, `de-DE`), asserts the precondition (`strtod("1,5") == 1.5`
  there) and checks every path; when the locale is missing it **fails** if
  `MOV_REQUIRE_LOCALES` is on (CMake option, on in the `base` preset; the dev image and
  the Linux CI jobs generate the locale) or the environment has
  `MOV_REQUIRE_LOCALES=1`, and skips otherwise.
- The read chain `tests/io/test_read_chain.cpp` pins, at compile time, that the CLI
  chain `read_imeds` → `convert(table, ColumnIndex{0}, foot)` → `format_csv`, composed
  with `and_then_read` and `lift<CliError>` over `CliError = variant<io::Error,
  core::UnitError>`, has exactly that error type, and at run time that the reader's
  warnings survive to the result and a non-length column yields the `UnitError`.

### 5.1 CF time — `cf_time.hpp` (C13)

`parse_cf_time_units(text) -> expected<Read<CfTimeUnits>, ParseError>` is lenient for
foreign files (CF §4.4; `core::parse_utc_datetime`, for ADCIRC's own text, stays strict):
the grammar is in the header. A fraction of any length is rounded half up to the
millisecond, and a nonzero digit dropped beyond it gives one `time_precision_dropped`
warning (`.000000` loses nothing and warns nothing). The zone offset is **applied**
(`00:00 -06:00` is 06:00 UTC); the offset digits are read with core's `DateTimeCursor`,
errors at the sign. Probes in the tests: `hours since 1800-1-1 00:00:0.0`,
`seconds since 1970-01-01 00:00:00.000000`. Not `constexpr` (a `ParseError` holds a
`std::string`); `unit_ms`, `parse_cf_calendar` and `gregorian_reform` are.

`CfClock::make(units, calendar)` is the reproducibility check, once per variable: the
`standard` calendar with an epoch before 1582-10-15 is `epoch_before_gregorian_reform`,
which a reader reports as `unsupported_calendar`. `clock.at(value)` is then only the
per-value range check (`checked_time`; under `standard` a result before the reform is
`nullopt` → `time_out_of_range` with the index). A clock cannot be made for a pair that
is wrong in every value, so a reader cannot forget the check. Time variables of model
files are read in their own type (an int64 time masked by `masking<int64_t>`, so a fill is
`time_missing`; others through `read_samples`) by one helper shared by the ADCIRC,
D-Flow and station readers (`src/io/model_netcdf.hpp`, private).

### 5.2 IMEDS — `imeds.hpp`

The header carries the full read and write contract. The v5 rules it applies, and why:

- Fields split on any run of ASCII white space (v4 split on single spaces, so tabs were
  not separators); rows are exactly 6 or 7 words (N3: v4's grammar read the 2 of
  `2015 07 01 00 00 2.605` as the second and `.605` as the value); blank lines are
  skipped (N13); a 3-word line of three integers right after a row is a cut row, not a
  station (a station with an integer name and integer coordinates can only start a
  block or follow a station).
- Coordinates are latitude first (the one format in the tree with that order) and go
  through `Location::make`.
- Only the exact legacy sentinels are masked (C9, the owner's veto of a `<= -999` rule
  here: `-99998.9` and `-999` are values). A v4 header may carry a unit where the datum
  goes (`NOAA UTC ft`): a lone third word that is not a datum but is a unit of a family
  is the unit.
- Each station's rows go through `normalize`; equal names get `#2`, `#3` ids (IMEDS has
  no id column, so the id is the name made unique); a NUL in a name becomes U+FFFD
  rather than cutting the name (C14's "cut at the first NUL" is for fixed-width char
  arrays; an IMEDS name is a token and cutting could leave it empty).
- The driver polls `ctx.stop` every 4096 lines and 4096 stations; the header lines are
  neither validated nor kept (lines 1 and 2 are free text).

Write format (D15), byte-pinned by `tests/io/test_imeds_format.cpp`:

```text
% IMEDS generic format
% year month day hour min sec value
{source}    UTC    {datum | none}    {symbol(unit) | unknown}
{name}    {lat:.6f}    {lon:.6f}
{yyyy:04} {mm:02} {dd:02} {hh:02} {mi:02} {ss:02} {value:14.6f}
```

Times are floored to whole seconds; non-values are omitted (N18: v4 printed −DBL_MAX);
each run of white space or `,` in a name becomes `_` (an empty name becomes
`station_{k}`); lines end in `\n`. Findings behind the rules:

- LF §12 counts 15,363 non-header data lines for `mllw.imeds`; the file has 15,362 rows,
  7,681 per station (the count included header line 1, which has 7 words).
- A `{:.6f}` value of −1e-7 prints `-0.000000` and reads back as −0.0, equal to 0.0 under
  `Sample`'s `==`.
- The `{:14.6f}` width is a minimum: 17-digit and 1e300 values widen the row (pinned).
- The writer's `duplicate_times_dropped` and `time_precision_dropped` counts are a
  partition: a dropped row is not also a cut row. `value_reads_as_missing` values are
  written anyway, because a reader masks exactly those texts.
- Round trip: every fixture reads, writes and reads to an equal table; the second read
  warns only for what the file still says (`duplicate_station_id_renamed`,
  `empty_station`).
- **Cost, measured** (release build, `getrusage` peak, including the generated text; a
  throw-away test): 9M station lines (151 MB) stop at the 8.4M-station limit after 2.4 s
  with peak RSS 876 MB; 2M stations all named `A` (20 MB) parse and assemble in 4.0 s with
  peak 962 MB (about 450 bytes per station once assembled, the `uniquify_ids` map and set
  included); 10M sorted rows of one station (250 MB) 2.8 s, peak 751 MB; the same
  unsorted (sort and dedupe path) 4.1 s, peak 675 MB. So a row is about 40–50 bytes at
  peak, and the default `max_elements` (2^27, "about 1 GiB of doubles") allows several GB
  for a text file.
- The "stream failed, stop the export" path of `write_imeds` / `write_csv` (a full disk)
  has no test: the flush callback returning false is not reachable without a failing
  stream.

### 5.3 ADCIRC ASCII — `adcirc_ascii.hpp`

The contract (request, record grammar, partial runs, values, hot-start overlap, errors)
is in the header; `detail::adcirc_schema(kind, grid)` (`detail/adcirc_schema.hpp`) is the
schema both ADCIRC readers share, so the same run read from either file gives equal
tables (a test compares them, and the ASCII and netCDF legacy fixtures agree to 1e-9):
61 → `water_level` (m); 62 → `current_u`, `current_v` (m s-1); 71 → `air_pressure`
(mH2O; the station netCDF writer converts to hPa); 72 → `wind_u`, `wind_v` (m s-1);
labels are the registry long names; no datum is stated and no `datum_unknown` warning
raised (that warning is a station-netCDF concept). On a projected grid the vector
components are the grid-relative generics of D28. Decisions behind the behavior:

- **The kind is the caller's**; nothing is inferred from the file name (v4 went by
  suffix). NCOLS must equal `column_count(kind)` (`wrong_column_count`).
- **NSnaps is a hint.** Records are read to the end of the text; more than NSnaps gives
  `more_snapshots_than_header`, then the hot-start overlap is normalized with
  `core::normalizing_order` over the shared axis. There is no `trailing_text`: garbage
  after the last record cannot be told from a damaged record, so it is `corrupt_record`
  if more text follows and a dropped cut-off record (`partial_record_dropped`) if it is
  the last thing. Any last line without a newline is cut off (a finished ADCIRC file
  always ends with one; a cut file's last number can be a truncated one that still
  parses); a blank unterminated tail is white space, not a cut. Fixture
  `elevation_restart_past_header.txt`: 144 planned, 100 written, restarted from record 90
  (155 records, the restart disagreeing at record 100) gives 144 times,
  `more_snapshots_than_header` 11, `times_reordered` 1, `duplicate_times_dropped` 11,
  `conflicting_duplicate_times` 1, and the first run's record 100 is kept.
- On a **selected** station the index must be `i + 1`: a line out of step is a record
  with lines lost or doubled. The lines of unselected stations are not read, except that
  a blank line is never a station; damage in them goes unseen. The time field goes
  through `parse_model_number`, so `D` exponents work.
- The N7 partner rule applies to fill (`<= -999`) only: a fill in either component makes
  both `Missing`; a `NaN` / `****` token makes only its own component `Missing` (the
  magnitude is `Missing` through `combine` either way). `nonfinite_masked` counts tokens
  of complete records only, subject `first at line <n>`.
- **Reservation (review blocker).** Nothing is reserved from the header. After the first
  complete record the reader reserves for `remaining bytes / that record's bytes + 1`
  records, capped by `min(NSnaps, estimate, max_elements / cells)` through
  `detail::reserve_capped`, so a header claiming 2^60 records costs about twice the
  text at most. `tests/io/test_adcirc_allocation.cpp` (its own executable,
  `mov_io_alloc_tests`, since it replaces the global `operator new`) counts the bytes:
  2.5 kB for a tiny text with a 2^60 header, 177 kB for 100 kB of text after the first
  record (an honest header: 1.7 kB), and under 16 kB when `max_elements` is small or a
  station file claims 10^8 stations.
- Structure: a record is a `variant<Complete, CleanEnd, CutOff, Malformed>`; the scratch
  row and the columns are shaped `[variable][selected position]` like
  `core::Variable::per_station`; the reader is built once and consumed (`read() &&`).
- `detail::parse_model_number` (`detail/model_number.hpp`): `parse_double` plus what
  Fortran writes. `NonFinite` for `NaN` / `Inf` / `Infinity` (signed, any case),
  all-asterisk fields and a number that overflows or underflows a double; the `D`
  exponent (`1.5D+02`) and a **letterless exponent only as a sign and exactly three
  digits at the end of the token** (`1.5-100`; `1-5` and `1.5-10` are not numbers). The
  rewrite is in a 64-byte buffer, only for tokens with no `E`; `parse_double` still judges
  the result.

**Station file.** `parse_adcirc_station_file(text, crs, ctx)`: one `Projector` per file,
its `approximation_warning()` appended. Projection failures are told apart: an unknown CRS
is `FormatError{unsupported_crs, "EPSG:n"}`, an unopenable database
`projection_unavailable` (tested with `MOV_PROJ_DATA` naming a garbage `proj.db`), a point
PROJ cannot transform `bad_coordinates` with the station; a position that is not a
`Location` stays `ParseError{out_of_range}` at the latitude field or the first coordinate
(`detail::position_at`). The count line is split like the others (`2,stations` is 2).
Order matters: the name is cut at the first NUL (C14) by the word join *before*
`replace_invalid_utf8`, which would turn a NUL into U+FFFD.

**Throughput** (release build, GCC 14 `-O3`, `mov_io_model_text_tests "[.throughput]"`,
one thread, warm cache, one host):

| Input | Size | Speed |
|---|---|---|
| fort.61, 3000 stations × 400 records, 3 stations selected | 37 MB | about 2500 MB/s (14 ms): an unselected line costs a `memchr` and a blank test |
| the same, all 3000 selected | 37 MB | about 170 MB/s (216 ms) |
| HWM file, 300000 marks | 11 MB | about 120 MB/s (98 ms) |

A 1 GB output with a handful of stations selected reads in under a second; with every
station it is a matter of seconds and 16 bytes per sample of memory.

### 5.4 ADCIRC netCDF — `adcirc_netcdf.hpp`

The contract is in the header. Decisions behind it:

- `AdcircNcRequest{kind, cold_start, crs, stations}`: the caller names the kind (as the
  ASCII request does) and the catalog still reports the one the file has. A kind the
  file lacks is `missing_variable` (`partner_variable_missing` for a missing `v-vel` /
  `windy`). Detection is a global `model` of `ADCIRC` (cut at a NUL, trimmed); anything
  else, **including an attribute that is not text**, is `not_this_format`.
- **The file's CRS is not read** beyond `ics` (D28): `x` / `y` are in the `crs` the caller
  states; `HorizontalProjectionEPSG` and `EPSG` belong to the station dialects. `ics`
  (global, int) 1 is Cartesian and 2 spherical; `ics` 2 with a projected `crs`, or 1 with a
  geographic one, is a `crs_mismatch` warning from `inspect` and `read`. The legacy
  fixtures have `ics = 2` and store `x` and `y` as **floats**: the stations are the
  floats' values.
- **Clock.** With `cold_start`: seconds after it, calendar proleptic Gregorian, and
  `time:units` is only looked at (if it parses and its epoch is **more than a second**
  from the cold start, `cold_start_differs`). Without: `units` and `calendar` through
  `parse_cf_time_units` with an `epoch_used` warning; units that are absent or do not
  parse are the NCDATE placeholder, `cold_start_required` (never a `ParseError`); a
  placeholder that parses (`1970-01-01`) is taken at its word and `epoch_used` is what
  tells the user.
- **Values.** Dry is judged on the *unpacked* value before the attribute masking, so the
  file's `_FillValue = -99999` gives `Dry`, not `Missing`, while the library's default
  fill (9.97e36) and `missing_value` are `Missing`. Each component is read into
  `Gathered{columns, flat fill bitmap}` and one `combine_partner_fill` pass makes both
  components `Missing` wherever either was fill (N7). `dispatch_model_numeric` (shared
  with D-Flow) instantiates double, float, int8, int16 and int32 only and fails
  `type_mismatch` for anything else (a first version let the "other" arm succeed and
  produced an all-`Missing` column; the test over `int64` and `ubyte` data catches it).
- Limits: `selected × times × columns` over `max_elements`, or its `Sample` bytes over
  `max_result_bytes`, is `NcError{too_large}` before any value is read; a catalog with
  more stations than `max_elements` is `too_large` before a list is made. The whole
  file's coordinates and names are read however few stations are selected (they are
  small next to the data). Station names: bytes before the first NUL, white space
  simplified, invalid UTF-8 replaced; the row length is the file's (tested for 10, 50 and
  300).

**Chunk-aware reads.** What the readers decide is which stations share a `read_blocks`
call and what the library keeps in memory between blocks (`detail/station_groups.hpp`):
`File::chunk_shape(name)` gives the chunk sizes; `grouping_for(chunk_shape, station_axis)`
groups selected stations in the same chunk column (index divided by the chunk size along
the stations, found by the id of the station dimension, not a position) into one read of
the span from the first to the last; without chunks, stations within `contiguous_stride`
(1024) of each other share a read. `reserve_chunk_cache`: when the chunk of a variable is
bigger than netCDF-C's default cache (16 MiB) and fits `max_result_bytes`, the cache is
made one chunk big.

Method: `tests/io/measure_adcirc_netcdf.cpp` (hidden `[.measure]` cases), release preset,
GCC 14, Xeon E5-2696 v4 (shared host), ext4; the median of seven timed reads (five for
the deflated files; three, marked `n=3`, when the warm-up took more than 5 s) after one
untimed read, in milliseconds (the hidden test prints the ranges, which are not
reproduced here). "per column" reads each
station alone (v4's strided reads), "one block" reads the whole span of the selection,
"readers'" is `grouping_for`. **Read the figures as orders of magnitude.**

1000 stations × 10000 steps of doubles (80 MB), chunked:

| chunks \ selection | 1 station | 10 neighbours | 10 spread (every 100th) | 100 neighbours | 100 spread (every 10th) | all 1000 |
|---|---:|---:|---:|---:|---:|---:|
| **1 step × 1000 stations** (netCDF-C default; what ADCIRC writes) per column | 115.2 | 1072.6 | 1003.4 | 9074.6 n=3 | 8386.2 n=3 | 85079.3 n=3 |
| … one block | 115.4 | 130.6 | 100.5 | 136.9 | 127.1 | 534.0 |
| … readers' | 115.7 | 131.5 | 100.5 | 140.2 | 126.2 | 495.9 |
| **100 steps × 100 stations** per column | 6.2 | 14.1 | 41.2 | 73.2 | 96.6 | 736.9 |
| … one block | 7.4 | 9.7 | 55.0 | 46.2 | 104.2 | 494.0 |
| … readers' | 6.7 | 10.0 | 39.7 | 35.1 | 88.1 | 496.9 |
| **1000 steps × 10 stations** per column | 4.0 | 7.8 | 11.8 | 49.2 | 79.2 | 481.1 |
| … one block | 3.8 | 7.0 | 167.2 | 51.8 | 259.8 | 618.7 |
| … readers' | 3.9 | 6.8 | 11.3 | 36.9 | 76.8 | 352.5 |
| **10000 steps × 1 station** per column | 4.1 | 6.5 | 7.7 | 43.4 | 41.6 | 384.6 |
| … one block | 3.6 | 8.2 | 718.8 | 81.2 | 928.4 | 1190.7 |
| … readers' | 3.4 | 7.6 | 6.7 | 41.5 | 37.4 | 383.0 |

- On the layout netCDF-C gives a variable with an unlimited time dimension (one chunk per
  step, all stations in it; ADCIRC writes it so) a read per station touches every chunk
  of the file each time: 1000 stations take about 85 s, a block read 0.5 s, and ten
  stations spread across the file still take ten times longer.
- On a layout chunked by station a block read over a spread selection reads every chunk
  column in between: two orders of magnitude slower for ten stations a hundred apart
  (0.7 s against 7 ms). Neither fixed strategy is right for both, so the choice follows
  the chunk columns; the readers' column is within noise of the better one, or better
  where the groups are narrower than the span, in every row.

10000 stations × 2000 steps of doubles (160 MB), not chunked:

| storage \ selection | 2 far apart (0, 9999) | 2 apart by 1000 | 2 apart by 100 | 2 neighbours | 10 neighbours | 10 spread (every 1000th) | 100 neighbours |
|---|---:|---:|---:|---:|---:|---:|---:|
| **netCDF-4 contiguous** per column | 58.2 | 58.2 | 59.1 | 58.4 | 274.2 | 270.2 | 2651.8 |
| … one block | 37.8 | 34.3 | 31.7 | 31.3 | 31.7 | 37.2 | 37.8 |
| … readers' | 58.2 | 34.3 | 32.0 | 31.3 | 31.7 | 37.2 | 37.5 |
| **classic CDF-1 record variable** per column | 24.4 | 25.7 | 25.8 | 25.2 | 79.9 | 85.9 | 696.0 |
| … one block | 91.9 | 23.9 | 19.4 | 18.3 | 18.9 | 88.4 | 25.8 |
| … readers' | 23.6 | 23.9 | 19.4 | 18.3 | 19.0 | 87.8 | 25.8 |
| **CDF-2, fixed dimensions** per column | 18.2 | 18.5 | 17.4 | 17.8 | 70.6 | 78.3 | 646.5 |
| … one block | 83.2 | 16.0 | 11.3 | 10.6 | 10.6 | 78.6 | 16.6 |
| … readers' | 17.5 | 15.9 | 11.2 | 10.7 | 10.6 | 79.7 | 16.7 |

- A classic file shows the gap `contiguous_stride` exists for: two stations 9999 apart
  read about four times faster separately (24 ms against 92 ms), two stations 1000 apart
  the same either way, and neighbours about 1.4 times faster as a block.
- Contiguous netCDF-4 shows the reverse, by less: a block read is 1.5 times faster even
  for the far-apart pair (38 ms against 58 ms), because HDF5 reads a contiguous hyperslab
  differently from the classic library. The finite stride therefore costs 1.5 times
  there and gains four times in classic files; neither is an order of magnitude, and the
  stride is one constant.

Deflated chunks longer than a block (1000 × 10000, deflate level 1, readers' grouping;
`slab_elements` is the block size, the default 2^20 being 1048 steps of 1000 stations):

| chunks | `slab_elements` | all 1000, default cache | all 1000, `reserve_chunk_cache` |
|---|---|---:|---:|
| 10000 steps × 100 stations (1.0 M elements) | 2^20 / 2^23 / 2^25 | 628 / 632 / 642 | 531 / 541 / 520 |
| 2000 steps × 1000 stations (2 M elements) | 2^20 / 2^23 / 2^25 | 616 / 832 / 879 | 558 / 771 / 839 |
| 10000 steps × 1000 stations (10 M elements, 80 MB) | 2^20 / 2^23 / 2^25 | 2390 / 1041 / 881 | 704 / 956 / 927 |

The 80 MB chunk is ten blocks long and larger than the default cache, so each block
decompressed it again: 2.4 s for all stations; with the cache one chunk big, 0.7 s (3.4
times faster) at the price of 80 MB of cache while the file is open, and larger blocks do
no better (0.9 s): **the cache is the fix, alignment of the blocks to `chunk[0]` is not
needed**. Chunks of 1 M and 2 M elements are cached by default and unchanged. One station
of a 10000 × 1000 chunk still costs the whole chunk (about 200 ms): the price of the
layout, not of the reader.

### 5.5 D-Flow FM his — `dflow.hpp` (D26: synthetic fixtures only)

The contract is in the header. Decisions behind it:

- Dimensions are found by name (`time`, `stations`, `name_len`, and `laydim` if present)
  and every variable is matched to them by id equality, never by position or a name-to-id
  map (B12). The layer count is `laydim`'s length; `laydimw` is only ever a reason to
  leave a variable out (v4 asked `laydimw` and counted `laydim`).
- A request carries only what selects data: `FlatChoice{source}` or `AtLayer`, whose only
  constructor is `AtLayer::make(const Layered&, one_based)`, so the layer is one that
  catalog entry has; `long_name` and the layer count are display data of the catalog.
  `read_dflow` also checks the layer against the file's own `laydim` (a stale catalog is
  `layer_out_of_range`); a layer for a flat variable, or none for a layered one, is
  `dimension_mismatch`.
- The derived variables come after the file's, in v4's order, when their inputs are all
  there with one shape (the 3-D speed needs layered inputs); the inputs of each are a
  span over a constexpr table indexed by the enumerator. The tokens are what v4 sessions
  stored.
- **Derived series** move the components' columns into one station's series at a time
  and move the derived samples out, so the peak is the input columns plus one station's
  series and one copy of the axis (it was both columns and the axis for every station at
  once). `VectorErrc` and `VerticalErrc` map by case (units to `noncanonical_unit`, the
  rest to `dimension_mismatch`). A column-wise derivation in core (`magnitude(span u,
  span v)` plus a derived-meta function) would also remove the axis copy; it is a change
  to `vector_series` left for the owner (plan §6, open items).
- **The file's CRS is not read**: D-Flow FM writes `station_lon` / `station_lat` for
  spherical models and a `projected_coordinate_system` variable, and neither is
  consulted, because no real his file was available to check them against. Unverified
  against a real file: that `station_x_coordinate` is present in a spherical model, the
  `time:units` strings (LF A6), and the `(time, stations)` coordinates (step 0 is used
  with `coordinates_from_first_step`).

### 5.6 Station netCDF — `station_netcdf.hpp`

The writer's contract (`validate_station_netcdf`, `write_station_netcdf`) and the readers'
(`inspect_station_netcdf`, `read_station_netcdf`) are in the header; SN §11, §12 and
§12.9 are the rules, errors and warning order of each kind. Files: `station_netcdf_dispatch.cpp`
(entry points, version parsers), `station_netcdf_write.cpp`, the v5 reader
(`station_netcdf_v5.hpp`: `station_netcdf_open.cpp`, `station_netcdf_samples.cpp`, whose
row-wise reader the foreign reader shares), the foreign reader
(`station_netcdf_foreign_{facts,open,schema,read}.cpp`), the legacy reader
(`station_netcdf_legacy.cpp`), what the three share (`station_netcdf_dialects.hpp`,
`station_netcdf_shared.{hpp,cpp}`), the names, attribute values, WKT and chunk rule
(`station_netcdf_format.hpp`, including the one `writable_token` predicate the writer and
every reader use for column tokens), and the one classification rule `detect_file` and
the station reader share (`netcdf_kind.{hpp,cpp}`). All private.

Writer decisions:

- Everything that can be refused is decided first (`plan_write`, also behind
  `validate_station_netcdf`), so no file is created for a refused table; the body of
  `write_netcdf_atomic` only defines and puts. The verdict depends on the schema, the
  stations and the options, never on whether a sample is Dry; the warnings are computed
  from the plan, not pushed while planning. Samples are written one station row at a
  time (a model table is never copied whole); the unit conversion is the `Affine` applied
  per value while a row is written, not `core::convert(StationTable, ...)`, which takes
  the table by value. The identity is skipped, so `-0.0` keeps its sign.
- Reserved names are checked on the schema only: a generic token may not be a name the
  format uses (built from the `NcNameRef` constants), nor `<token>_status` of **any**
  column (every column reserves its status name), nor exceed 249 bytes (`NC_MAX_NAME`
  with `_status` appended). No escaping.
- `units_metadata` on every temperature unit (CF 1.11 §3.1.2): `temperature: on_scale`
  for registry quantities, `temperature: difference` for `difference`,
  `temperature: unknown` for generic ones (the IOOS checker accepts it).
- Determinism: two writes of the same table at the same `now` are byte-identical.
  Idempotence: writing a table read back from a v5 file gives no warnings, and the file
  reads back equal (tested with a table that needed every normalization).

Reader decisions (v5):

- No variable may use an unlimited dimension (`unsupported_layout`). With that, a v5 file
  cannot have zero stations or a zero-length sample dimension (netCDF has no fixed
  dimension of length 0), so there is no separate zero check.
- Variables not over the sample dimension are ignored (what a newer minor version needs,
  SN §13); other ancillary variables than `<data>_status` are ignored too. Flags are
  read raw: a flag outside `valid_range` is `bad_flag`, not masked.
- **Padding.** In the incomplete layout each group of selected stations reads
  `min(obs, max n_s + 1)` elements of each row: its longest station's samples and the
  first padding element, which must be fill (SN §12.4); whatever else of a station's
  padding that read covers is checked too, and `check_result_size` charges exactly those
  elements. `PaddingCheck::whole` reads and checks every padding element and is charged
  selected stations × `obs`. Only the samples kept become `core::Sample`s; the padding is
  checked in the raw type. `obs_count` is read as integers without masking: a fill value
  is negative and so `bad_obs_count`.

Reader decisions (foreign CF and legacy; the owner's decision 30 settled the quality
flags, datums, time order, the water-level pair, Kelvin, the name rules and id
substitution):

- Classification order: v5 marker, ADCIRC (`model`), D-Flow (station coordinate
  variables), foreign CF, legacy. ADCIRC and D-Flow come before CF because their files
  can carry CF attributes. A file that names another `metoceanviewer_format` is
  `other_format_netcdf` / `not_this_format`, not foreign CF; a v5 file *without* the
  attribute reads as foreign CF. The foreign file's CF version is the one the rule read
  from `Conventions`; there is no invented default.
- No dialect label: v4's writer (A) and reader (B) files read the same way, so
  `LegacyOrigin` records the facts (`fileformat`, a `stationId` variable, the width of
  the station numbers). The six-digit spelling is accepted when the file has the station
  coordinates; CRMS (C) is `unsupported_netcdf` / `not_this_format`.
- Foreign variables are found by what CF says they are (`cf_role`, `standard_name`, the
  CF §4.1 / §4.2 unit spellings, `axis`, `coordinates`, `sample_dimension`,
  `instance_dimension`); a variable named by any `coordinates`, `bounds` or
  `grid_mapping` attribute is not data. **Time** is the best-ranked candidate: (1) the
  coordinate variable of its dimension, (2) listed in a `coordinates`, (3)
  `standard_name` / `axis`, (4) units alone; a tie is `unsupported_layout` naming both. The
  layout is a `ForeignSampling` variant (each alternative holds its own station dimension
  and helper variable) and where the samples are is a `Placement` variant (matrix, run
  starts, sample owners); no `-1` sentinels.
- **Bounded reads:** a `(time, station)` matrix is read as many time steps as the
  selected stations' samples reach (plus the first padding element), never the whole
  `obs`. The incomplete scan without `obs_count` reads the whole time variable and is
  charged `stations × obs` against `max_elements` (a 2^40 `obs` without `obs_count` is
  `too_large`, from `inspect` too).
- Generic tokens are made in two passes with `writable_token`: every variable that can
  keep its name or its registry token claims it first, then the others get substitutes
  (every byte outside `[A-Za-z0-9_]` becomes `_`, a leading non-letter gets `v`, at most
  64 bytes, then `_2`, `_3`, ...) with `variable_renamed`. Generic quantities v5 wrote read
  back with no warning.
- Legacy: `referenceDate` is the first 19 characters after the first NUL cut (`T`
  accepted); text after them (`Z`, `+02:00`) is `tz_assumed_utc` with that text (blanks,
  UTC and GMT say nothing); absent is 1970-01-01 with `epoch_used`. Seconds come from
  int64, int or double variables, masked in their own type. Data are float or double,
  masked in the variable's type (the default fill is masked, `-99999` is a value, B9).
  Stations must agree on `units` and `datum` (`inconsistent_metadata`).

Findings:

- **ncdump prints double attributes with 15 significant digits** (`_FillValue =
  9.96920996838687e+36`), and prints a newline in a text attribute as `\n` on the same
  line. The committed CDL (`tests/fixtures/io/station_netcdf/*.cdl`) is the real
  `ncdump -h` (netCDF-C 4.9.2, Ubuntu 24.04); the golden check runs twice, in every preset
  with the C++ header dumper (`tests/io/support/nc_header_dump.cpp`, raw netCDF-C) and in
  the compliance job with the real ncdump, with `ncdump_probe.nc` keeping the two in step.
- cfchecker 4.1.0 reports 0 errors and 0 warnings on all three canonical files (a CF-1.8
  copy) with INFO only (`crs_wkt` syntax not verified; `obs_count` has no `units`). IOOS
  compliance-checker 6.1.0 `cf:1.11`: 0 errors and exactly the one waived warning (time
  `units_metadata`) on each file, `temperature: unknown`, `difference` and `value` (no
  `standard_name`) included.

**Compliance job** (`format-compliance` in `.github/workflows/ci.yml`; locally
`tools/check_station_netcdf.sh`, which builds `station_nc_fixtures` in the dev container,
writes `build/format-compliance/`, builds the checker image `tools/compliance/` tagged
by a hash of its inputs and runs it offline with `--network none`). `tools/check_cf.py DIR
--cdl CDL_DIR [--tables DIR] [--waivers FILE]` checks every `station_timeseries_*.nc`:
IOOS `cf:1.11` (fails on an error, or on a warning no waiver in `tools/cf_waivers.txt`
matches **from its start**; each waiver names its section); `cfchecks -v 1.8 -s -a -r`
with the pinned tables on a copy with `Conventions = CF-1.8` (fails on ERROR/WARN lines,
a nonzero exit status or a missing summary); xarray `open_dataset` (the decoded ids,
names, times, values, fills and padding of the two SN §9 files compared with the C++
tables); `ncdump -h` against the golden CDL (application version masked); a waiver that
matched nothing fails the run. Pins: `tools/compliance/setup.sh` downloads the CF
standard name table v95, the area type table v13 (versioned URLs) and the standardized
region list v5 (the cfconventions.org repository at commit `83a9da12`), each checked by
SHA-256 (the name table is 4.5 MB, over the 1 MB `check-added-large-files` limit, so none is
committed); the Python set is installed with `pip --require-hashes` (CPython 3.12
x86-64); `netcdf-bin`, `libudunits2-0` and `udunits-bin` are the Ubuntu 24.04 release
versions. Negative controls, by hand: a copy with an invalid `standard_name` and a wrong
temperature unit fails both checkers and the golden CDL. The canonical files are SN §9.2
(orthogonal), §9.1 (incomplete), both with a dry sample, and a registry file with every
registry quantity, a `value` column with a datum, a grid-relative generic, a generic
temperature and a `difference`.

### 5.7 HWM file — `hwm_file.hpp`

The contract is in the header; the elevation boundary is §3's. The header rule: the first
non-blank line is a header iff it has **five or six comma fields**, something in them, and
**no field looks numeric**, where "looks numeric" is wider than "parses" (`1e999`, `nan`,
`inf`, `Infinity`, `****` count), so a first line of `nan,nan,nan,nan,nan` is a
`bad_number` error; a words-only line with another number of fields (`# lon lat`) is a
`wrong_field_count` error, not a header. The limit counts numbers: marks × 5.
`tests/io/test_hwm_golden.cpp` reads §3's fixtures through `parse_hwm_csv` and checks the
parser against the test-only loader mark for mark and against `golden.txt`.

### 5.8 CSV export — `csv_export.hpp` (D27, §9.1)

The format is in the header: `station_id,station_name,time_utc,quantity,value,status,unit,datum`,
CRLF, UTF-8 without a byte order mark, rows ordered station, then schema column, then
time; `time_utc` is `yyyy-mm-ddThh:mm:ss.mmmZ` with a minus sign and as many digits as
needed outside 0000–9999. Cells with `,`, **`;`** (the field separator of decimal-comma
locales), `"`, CR or LF are quoted; the `'` guard covers id, name, quantity, unit and
datum. `write_csv` streams in 64 KiB chunks; `format_csv` moves the buffer out.

### 5.9 Detection and projection — `file_type.hpp`, `projection.hpp`

**Detection** is by content only, never by file name (v4 went by suffix, and a renamed
file was read as the wrong kind): the netCDF order and the text order are in the header.
Text detection tries ADCIRC ASCII first because the two-column rows of fort.62/72 look
like IMEDS station lines; IMEDS by a first line that is a `%` comment naming IMEDS, else
by structure; netCDF-4 behind an HDF5 user block (the signature at 512 · 2^k) is netCDF.
`detect_file` returns an `Error`, not a "none" kind, for a file with netCDF magic bytes
that netCDF-C cannot open (truncated, plain HDF5) and for an attribute over
`max_att_bytes`. `file_type_of` is total over both `FileDetection` and
`StationFileOrigin`, so a reader that gains an origin does not compile until its kind is
named.

**Projection** (C19). PROJ comes from vcpkg with `default-features: false` (no network,
no TIFF grids). What PROJ does without grids: it still plans a real operation where it
knows one and states its accuracy (NAD83 to WGS 84, EPSG:26915 → 4326, is a datum shift
stated as **4 m**, so it warns; WGS 84-based CRSs such as UTM zones and Web Mercator state
nothing and are exact), and falls back to a **ballpark** transformation (a null shift,
metres to kilometres off) only where it knows no operation for the point, e.g. a NAD83
point outside North America. `Projector` builds one PROJ context and transformation for
a reader with many points (not thread-safe; EPSG:4326 holds no PROJ objects);
`to_location(NativePoint)` builds one per call (a few milliseconds) with a fast path for
4326. Coordinates are x then y in and out whatever the EPSG axis order
(`proj_normalize_for_visualization`). `crs_kind` / `Projector::kind()` is `geographic`
for any PROJ geographic 2D or 3D CRS (4326, 4269, 4979, ...), `projected` for a projected
one, and `unknown_crs` otherwise (a vertical `5703` or geocentric `4978` would give
meaningless angles); `transform_failed` covers no pipeline or a non-finite result
(`1e15 m` in UTM). **Approximation warning, cost measured** (100000 points of EPSG:26915,
debug build, release PROJ): 0.07 s without asking, **21 s** asking for the operation of
every point (about 200 µs each: PROJ builds an operation object per call and there is no
cheap identity to cache on), so the operation is asked for a sample, each of the first
256 points then one in 64 (0.48 s for 100000); a file of up to 256 stations is checked
entirely. `set_projection_data_dir` stores the directory once (`std::call_once` plus an
atomic pointer; no mutex in io, C11); `MOV_PROJ_DATA` overrides it; a configured directory
means exactly `<dir>/proj.db`, and `projection_database_path()` reports it. No build-tree
path is compiled in; the tests find the vcpkg copy through a compile definition of
`test_projection.cpp` (`tests/io/CMakeLists.txt` warns at configure when `proj.db` is not
found) and provoke `database_unavailable` with a file named `proj.db` that is not a
database, because a search path *adds* nothing to what PROJ also tries. PROJ logging is
off (`PJ_LOG_NONE`). Goldens: `tests/io/utm_reference.py` computes UTM 15N (Krueger
series, GRS80) and Web Mercator without PROJ; the tests agree to 1e-7 degrees.

---

## 6. Layout and dependencies

```
src/core/include/mov/core/
  time.hpp units.hpp geo.hpp sample.hpp quantity.hpp datum.hpp        vocabulary I
  meta.hpp timeseries.hpp station.hpp station_table.hpp vector_series.hpp  vocabulary II
  series_ops.hpp datum_shift.hpp                                       operations
  hwm.hpp hwm_stats.hpp                                                HWM
  ascii.hpp utf8.hpp overloaded.hpp cancelled.hpp version.hpp          helpers other layers use
  detail/core_key.hpp detail/numeric.hpp                              not API (core_detail_fence)
src/core/core_access.hpp series_rebuild.hpp                            private to the core sources
src/io/include/mov/io/
  error.hpp warning.hpp read.hpp read_limits.hpp text_file.hpp cf_time.hpp projection.hpp
  netcdf/{name,types,masking,file}.hpp netcdf_library.hpp
  imeds.hpp csv_export.hpp | adcirc_ascii.hpp hwm_file.hpp | adcirc_netcdf.hpp dflow.hpp
  station_netcdf.hpp file_type.hpp
  detail/                 text helpers, reporting, atomic file, station groups (private; public only for the tests)
src/io/*.hpp              the netCDF readers' shared parts (model_netcdf.hpp, station_netcdf_*.hpp, netcdf_kind.hpp; private)
src/io/netcdf/            file.cpp, nc_call.hpp, ... — the ONLY place that includes <netcdf.h>
```

Dependencies point one way (`core ← io`); `cmake/Layering.cmake` enforces it and
`qt_free_sources` fails on a Qt `#include` in either.

---

## 7. Test plan

Conventions:
- Catch2 v3. Tags `[core]` / `[io]` + component; `[regression][B#|N#]` marks bug
  regressions; `[legacy]` tests `SKIP()` when `MetOceanViewer/function_tests` is absent;
  hidden tags `[.throughput]`, `[.measure]`.
- `CONSTEXPR_SOURCES` hold the `STATIC_REQUIRE` tests (built at compile time, plus a
  `_relaxed_constexpr` runtime twin), gated as §1.1 says for `variant<string>`.
- **Every value type** gets `STATIC_REQUIRE(std::regular<T> and
  std::is_nothrow_move_constructible_v<T>)`; types without a default constructor use
  `std::copyable<T> and std::equality_comparable<T>`.
- Executables: `mov_core_tests`, `mov_core_hwm_tests`, `mov_core_ops_tests` (core);
  `mov_io_tests`, `mov_io_model_text_tests`, `mov_io_alloc_tests`,
  `mov_io_text_formats_tests`, `mov_io_netcdf_tests` (with the `--wrap` shims),
  `mov_io_model_netcdf_tests`, `mov_io_station_netcdf_tests` (io), each with its
  constexpr pair. CTest gates: `qt_free_sources`, `core_detail_fence` (and
  `_rejects_violations`), `core_compile_fail_checks` (`tests/cmake/compile_fail/`,
  `compile_fail_io/`), `netcdf_include_gate` (and `_rejects_violations`),
  `hwm_golden_is_current`.
- Tooling findings worth knowing before writing a test: Catch2 treats any type with
  `begin()` / `end()` as a range, so `TimeRange` needs a `Catch::StringMaker`
  (`tests/core/test_helpers.hpp`); `bugprone-unchecked-optional-access` flags `.value()`
  even after `REQUIRE(opt.has_value())`, so tests use `value_or` or an `if`;
  `clang-analyzer-optin.core.EnumCastOutOfRange` fires on Catch2's `CHECK_FALSE` /
  `REQUIRE_FALSE` after a range-for loop, so tests write `CHECK(not (...))`;
  `CHECK_THROWS_AS` of a `[[nodiscard]]` `expected` trips `bugprone-unused-return-value`
  (cast to `void`); Clang's `-Wshadow` flags a local named `last`, `day` or `month` under
  `using namespace std::chrono`; `TEMPLATE_TEST_CASE` rejects duplicate types, and
  `int64_t` and `long` are one type on Linux and two on macOS, so lists use the
  fundamental types; GCC 14 `-O3 -Wnull-dereference` reports false positives in
  `std::string(istreambuf_iterator, istreambuf_iterator)`, on `front()` of a
  `string_view` it cannot see is non-empty, and in `vector::resize` of a
  `vector<size_t>` (`normalizing_order` builds with `reserve` and `back_inserter`).
- `tools/dev/run.sh pre-commit run --all-files` is the form that works in a worktree
  (the staged-files form needs to write git objects, and the main repository's git
  directory is mounted read-only there).

### 7.1 Core

| Component | Test cases | constexpr |
|---|---|---|
| probe | each §1 feature, SKIPping where absent; constexpr `llround` / `isfinite` / `fabs` detected, not required | ✓ |
| time | `TimeRange` make/contains; `ValidRange`; `checked_time`: NaN, ±Inf, 2^53 edge exact, 2^53+1 rejected, epoch+offset overflow, integer bound, negative values, every argument type, the double-versus-integer law on a grid; `parse_utc_datetime` forms + column of the first bad character; TZ-independent (sets `TZ=America/Chicago`, N5) | all but TZ |
| units | exact factors; in/as round trip every enumerator (1e-12 rel); `conversion` same family / incompatible / deleted cross-family; temperature affine incl. Kelvin; `parse_unit` table; `"" → nullopt`; aliases canonicalized; symbol and udunits round trips; `symbol(Unit&&)` deleted (concept check) | factors, conversion |
| geo | bounds; lon normalization; NaN; `Epsg` ≤ 0; `Epsg` not default-constructible; `NativePoint`; compile-fail cases | all |
| sample | `of(NaN) → nullopt`; `finite_or_missing`; `combine` precedence and result-type constraint; `value()` | all |
| quantity | token round trip; `info` vs the SN §6 table (data-driven, `difference` row with empty standard name and unit); `GenericQuantity::parse` rejects registry tokens and bad names; `datum_applicable` truth table; `quantity_for_standard_name` order | token, predicate |
| meta | datum on `wind_speed` rejected; `assume_*` when set → `already_set`; `with_label` total | — |
| timeseries | `make` length/order errors with index; `with_samples`; `into_parts`; `normalize`: clean fast path untouched, descents counted, first duplicate kept, conflicting count, stable order; `normalizing_order` identity, order, call arguments, agreement with `normalize` | — |
| station, station_table | `StationText` / `StationKey` / `StationId` grammars per provider; seven `DataSource` tokens; each `TableError`; check order; `single_axis` (one axis, equal axes, 1 ms difference, pooled); `series(i, k)` round trip; `from_series` schema mismatch and `TimeOutOfRange`; `StationSelection` duplicates / out of range / `applies_to`; `is_valid_utf8` table 3-7 | key, text, utf8 |
| vector_series | non-pair rejected; `assume_components`; B1 magnitude 0.0507918… (u = 0.049298094833, v = 0.012227184139); N7; direction cases incl. (−1, −0.0) → 180; zero → Missing; derived meta table (§2.9); `magnitude3` 2, 3, 6 → 7 | — |
| series_ops | slice (both overloads); `shift_time` overflow at the front and the back, off-by-one; `scale_offset`; `convert` (table and series, identity, equal `OtherUnit`); `extent` (B13, B14) + `join` associativity, commutativity, idempotence and identity over 13 extents; `Bucket` identity exact, associativity exact on dyadic data (all fields, `first` / `last` included) and within 1e-9 absolute on arbitrary doubles in [−1000, 1000], ties keep the left operand, peak equals `max_element` on 200 random series, `mean()` bit-identical to the plain sum over n, the ±1.7e308 groupings equal, finite and zero; `residual` every `ResidualErrc`, join, combine rule; `tests/core/series_ops_helpers.hpp` (seeded generators, dyadic rows, optional accessors) | `Bucket` small cases, the ±1.7e308 groupings |
| datum | strings and aliases (N8); `from_heights` normalizes, MSL missing, equal duplicate ok, conflicting → error, NaN → error; `offset(d, d) = 0` even when unknown; antisymmetry; transitivity on dyadic values (exact) and random values (1e-12); F12 rows (`tests/core/datum_fixture.hpp`) | all but F12 |
| datum_shift | pinned check order; nulls / Dry kept; result datum; input unchanged on error; The Battery +0.77527 m | — |
| hwm | `is_dry` edges; `checked_elevation` / `model_value` bounds; `classify` at every break, decimal ties in metres and feet (`hwm_ties.csv`, `hwm_ties_ft.csv`: `1.8,2.3`, `11.00,12.50`, `-9.94,-6.44`, `12.49999` / `12.50001`); defaults; invalid breaks (NaN first) | all |
| hwm_stats | `Moments` identity exact, `of(Dry)` = identity, fold halves ≈ whole; `wet_moments` bit-identical to the explicit left fold and different from the reversed one; golden free and origin (F8, `golden.py`); `fit` alternative matches mode (N1); σ n−1 (D17); threshold rows (N2); legacy closed forms agree (free); every `HwmStatsError`; R² nullopt rules; 1e-12 relative | `Moments` small |

Mutations tried by hand and caught (series_ops): tie rule `<` to `<=`, `slice` end
inclusive, datum offset sign flipped, `units_differ` check removed, `first` taking the
right operand's, the `shift_time` overflow limit off by one.

### 7.2 io

| Component | Test cases |
|---|---|
| detail | `parse_double` token table (`+1.5`, `.5`, `5.`, `1e3`, `-9.9999E+004`, `0x1p3` reject, `1,5` reject, ` 1` reject, `1.5x`, `nan`, `inf`, `""`, 400-digit mantissa, `1e400` reject, `1e-400` reject, denormals) on both implementations; the locale test with the asserted precondition (`MOV_REQUIRE_LOCALES`); `parse_model_number` NaN/Inf/`****`/`D`/letterless exponents; `LineCursor` BOM/CRLF/no final newline; `split_*`; `cut_at_nul` with junk after the NUL; `uniquify_ids`; `reserve_capped`; `civil_time` goldens; concept checks for the deleted rvalue overloads |
| text_file | B3 ordering (0444 copy, opening for write fails, then read); missing file; directory; FIFO; `max_text_bytes`; `read_file_prefix`; atomic: a fault at every stage leaves the target byte-identical and no temp file; permission copy (skipped on `_WIN32`, where Wine reports 0777); read-only target refused; symlink replaced; `fsync_dir` exemption; body throws |
| cf_time | grammar cases incl. abbreviations, one-digit fields, long fractions and rounding, zones applied; calendars; `CfClock::make` pre-1582; `at` out of range; the two probes |
| nc::File | open nonexistent (B6), non-netCDF, a directory, a FIFO; moved-from → `closed`; **`--wrap` of `nc_open`, `nc_create`, `nc_close`, `nc_abort`, `nc_sync`, `remove` [linux]**: every open has exactly one close across 1000 open-then-fail paths (B5), a Catch2 listener `_Exit`s when a test case ends with `opened != closed`, `fail_next_closes` (the close really happens, `NC_EHDFERR` is reported), `fail_next_syncs`, `remove_calls` (netCDF-C's own deletions under a 240-byte directory), the B1 double-free reproduced with `sabotage_hdf5_dataset` (an HDF5 dataset closed behind netCDF-C's back; the one accepted leak settled by hand); typed reads per §4.3 incl. `put` exact type (B4) for all six types; masking: double/float fill, default fill (B9), NaN, `missing_value` vector and of another exact type, valid range, `NC_NOFILL`, `_Unsigned`, unpack after mask, the wrong-type and two-value `_FillValue` (hand-made CDF-1 files); `text_att` 120 B (B8), above `max_att_bytes`, NC_STRING of one and of several; `read_char_rows` 20/50/64/300 (B7, B11) and rank 1; `find_*` absent (B12); `define_dim(0)`; `put_att` UTF-8 bytes (B15); count mismatch; `checked_product` overflow; `max_elements` and `max_result_bytes` → `too_large`, per block for `read_blocks`; stop token honored between blocks; `NcName` empty/257 B/embedded NUL; `NcNameRef` and `AttTarget` literals (`tests/cmake/compile_fail_io/`); `read_strings` LSan (B21); `chunk_shape`, `reserve_chunk_cache`, `DimInfo::unlimited`; non-ASCII path (POSIX; `WARN` on Windows); debug choke-point re-entry and one-handle death tests via `fork()` (the child's stderr piped and matched against glibc's `assert` text; same path, other spelling, symlink, hard link, moved handle, two working directories) |
| imeds | goldens (`mllw_small`, `msl_small`); header tokens, lone unit, unknown datum, CST; N3; CRLF; tabs; BOM; blank lines (N13); empty; short header; bad date / number / coordinates; exact sentinels → Missing + count, −99998.9 and −999 kept (C9); `nan` / `inf` / `****`; unsorted → warning; duplicate names → `#2` + warning; invalid UTF-8 and NUL in names; truncated rows; shape per station; empty station; longitude 360; read-only (B3); format golden bytes (D15); non-values omitted (N18); sanitize; floor seconds; `wrong_column_count`; year 10000; `too_large`; every writer warning; round trip |
| adcirc_ascii | header probe; station file (names, CRLF, count mismatch, UTM 15N, bad latitude, BOM and blank lines; no name stays empty, C14); fort.61 −99999 → Dry; fort.62 B1; partner −99999 → Missing (N7); fort.71/72; −950 kept as value (vs v4 −900); NaN/`****` → Missing + count; partial record and partial header dropped + warning; cut mid-line; fewer and more snapshots than the header; mid-file corruption → `corrupt_record`; time beyond 2^53 → `time_out_of_range`; hot-start overlap and restart past the header → warnings; selection required / mismatch; `too_large`; allocation bounds (`mov_io_alloc_tests`); legacy fixtures byte-exact (`tests/fixtures/io/adcirc/legacy/`); throughput (hidden) |
| adcirc_netcdf | `make_adcirc_nc`: float, double, packed int8/16/32, int64 and ubyte data (`type_mismatch`), `_FillValue` / `missing_value` / `scale_factor` / `add_offset`, `station_name` with junk after the NUL and any row length, `time` not dimension 0, transposed data, int64 time with a fill (`time_missing`), any `time:units` and `calendar`, a missing, other or non-text `model`, `ics`, explicit chunking, deflate, CDF-1/CDF-2/netCDF-4; zeta fill → Dry, u-vel fill → Missing (N7); cold start explicit vs `epoch_used` vs `cold_start_required`, `cold_start_differs`; selection required / out of range; non-increasing → error; block read equals column read for every grouping; D28 (geographic non-4326, projected velocity / wind / scalar, `crs_mismatch`); `[legacy]` originals: catalog (`Station One...`, float coordinates, the `seconds since Met` placeholder, `ics = 2`), first and last values to 17 digits (fort.61's first station dry throughout), ASCII == netCDF to 1e-9; open/close counts (B5) |
| dflow | `make_dflow_nc`: `laydim` and `laydimw` of different lengths, flat / layered / interface variables, float, double, integer or 64-bit, per-variable attributes, blank or NUL name padding at any `name_len`, `stations` before `time`, coordinates over `(time, stations)`, each dimension and variable left out (B2, B11, B12); `AtLayer::make` 0 and n+1 (N16); `Flat` vs `AtLayer` typing and `dimension_mismatch`; derived values and meta, flat and projected; `_FillValue`; NaN; catalog excludes `laydimw`; open/close counts |
| station_netcdf | writer: SN §14.1 rows; `choose_layout`; status iff Dry; canonical conversion (ft → m, mH2O → hPa, ft3/s → m3 s-1), `noncanonical_unit`; every writer error and warning; reserved names; options; `too_many_samples` through `detail::StationNcWriteLimits`; injected `now` determinism; idempotence; a fault at every atomic stage; golden CDL; the canonical files (`mov_station_nc_canonical`). v5 reader: every §12.9 error and the warning order; the padding boundary read (a 50-station file with one 10000-sample station bounded by `max_elements = 1000`: boundary passes, `whole` is `too_large`; and `whole` sees padding the boundary read does not). Foreign: the five layouts in every variant the tests need, transposed matrices, `NC_STRING` and integer ids, packing, quality flags, datums, water-level hints, time ranking and the decoy time variable, the order of the warnings. Legacy: name_len 7/50/300 and 150-byte names (B7), junk after the NUL, `referenceDate` 19/20/120 bytes (B8) and text after it, default and explicit fill, NaN, `-99999` as a value (B9), EPSG missing / text / unknown / unsigned (B10), MHW (N8), white space (A8), duplicate ids, `inconsistent_metadata`. The hostile set (§7.3). `[legacy]` files are built at test time: no real foreign or v4 file exists in the tree. |
| hwm_file | header only if all fields non-numeric (+ warning); `1,2,abc,…` first line → `ParseError`, not a header; blank lines (N20); BOM and CRLF; 5/6/4 columns; range; Dry; header-only → `empty_input`; the §3 fixtures through the parser against `golden.txt` |
| csv_export | golden; quoting incl. `;`; `=SUM(A1)` station name → `'=SUM(A1)`; `-1.5` value **not** prefixed; Missing/Dry status; ms timestamps and years outside 0000–9999; multi-column table |
| file_type | each fixture, by content only (a renamed file is still found); v5 / foreign / legacy / other-format; CRMS-like; HDF5 user block; truncated netCDF → `Error`; `to_token` |
| projection | 4326 passthrough; 26915 point (1e-7°) against `utm_reference.py`; Web Mercator; unknown, vertical and geocentric codes; `database_unavailable`; the approximation warning (NAD83 4 m, WGS 84 exact, a ballpark point); `MOV_PROJ_DATA`; a `std::thread` per `Projector` |
| read chain | `test_read_chain.cpp` compile-time type check plus the runtime runs (§5.0) |

### 7.3 Fixtures

- Copied byte-exact from `MetOceanViewer/function_tests`: `tests/fixtures/io/adcirc/legacy/`
  (`fort.61`, `fort.62`, `fort.71`, `fort.72`, `stations.csv`, all under 20 kB) and the
  excerpts `tests/fixtures/io/imeds/{mllw_small,msl_small}.imeds` (8 rows per station).
  The 4.2 MB ADCIRC netCDF files are read in place under `[legacy]` and skipped when the
  legacy tree is absent.
- Generated at test time with raw netCDF-C, each test into its own `ScratchDir`
  (`tests/support/include/mov/test/scratch_dir.hpp`): the wrapper's fixtures
  (`tests/io/support/nc_fixtures.cpp`, library `mov_nc_fixtures`, with the `--wrap`
  shims `nc_wrap.cpp` and the counts `nc_counts.cpp`), ADCIRC and D-Flow
  (`model_fixtures.cpp`: `make_adcirc_nc`, `make_dflow_nc`, D26), the legacy station
  files (`legacy_fixtures.cpp`: `Hmdf::writeNetcdf`'s layout, junk after the NUL, the
  20-byte `referenceDate`, CRMS), the foreign CF files (`foreign_fixtures.cpp`: CF H.2.1
  to H.2.5 and 9.2) through the builder `nc_build.hpp`, raw edits through `nc_edit.hpp`,
  and the canonical v5 files (`station_nc_canonical.cpp`). No `ncgen` exists in the dev
  image, so no foreign or legacy CDL is committed; the golden CDL of the v5 files is the
  real `ncdump -h`.
- Hand-written text under `tests/fixtures/{core,io}/`: `core/hwm/*.csv` (F8, with
  `golden.py` / `golden.txt`), `core/datum/offsets.csv` (F12), `io/imeds/*`, `io/adcirc/*`
  (built by a throw-away generator, not committed; the files are the source), `io/hwm/*`,
  and the fuzz seed corpora (§7.5).
- **Hostile-structure set** (`tests/io/test_station_netcdf_hostile.cpp`,
  `test_nc_hostile.cpp`), each file giving a specific error and no crash or oversized
  allocation:

| File | Expected |
|---|---|
| 2^31 stations (v5, foreign, legacy `numStations`) on chunked variables | `NcError too_large` (the dimension), also from `inspect` |
| time 2^40 (v5 / foreign orthogonal) | `read`: `too_large`; `inspect`: ok |
| obs 2^40, few samples per station (v5, foreign with `obs_count`, both orientations) | ok (boundary read); `PaddingCheck::whole`: `too_large` |
| obs 2^40, foreign without `obs_count` | `too_large` from `read` and `inspect` |
| `stationLength_0001` 2^40 | `read`: `too_large`; `inspect`: ok (2^40 samples) |
| a global or variable attribute of 1 MiB + 1 | `NcError too_large`; an attribute never read costs nothing |
| `time:units` of 10 kB | `ParseError bad_time_units`, context at most 120 bytes |
| NC_STRING `Conventions`: one / two | read / `missing_attribute :Conventions` |
| `_FillValue` of another type, with two values, or text, on a foreign data variable | the variable is skipped (`bad:_FillValue`); in a v5 file, or on `time` / `lat`: `NcError type_mismatch` / `count_mismatch` |
| `valid_range` of one value, `add_offset` of two, text `scale_factor`, an unrepresentable `missing_value`, a float `valid_range` on a short | foreign data variable skipped (`no_data_variables` if none is left); v5 `NcError` |
| NC_STRING id with a NULL element; id dimension of length 0 | foreign: the index, `station_id_substituted`; v5 `unsupported_layout` for the dimension |
| time all fill | `time_missing`, index 0 |
| time dimension not dimension 0 | reads |
| `HorizontalProjectionEPSG` as NC_CHAR | `NcError type_mismatch` |
| `obs_count` −1 or past `obs` | `bad_obs_count` (the station) |
| 1-D id variable | v5 `dimension_mismatch`; foreign `missing_variable latitude` |
| 3-D `time` | `unsupported_layout` (`time`) |
| `(time, station)` data in a v5 file | `dimension_mismatch` (the variable) |
| 5000 data variables | reads; over the limit `too_large` |
| `coordinates` / `ancillary_variables` / `bounds` / `grid_mapping` naming nothing | ignored (`crs_assumed` for the mapping) |
| ragged index out of range; row sizes not adding up; negative; a `rowSize` of doubles | `bad_ragged_index`; `bad_row_size`; `bad_row_size`; `NcError type_mismatch` |
| `instance_dimension` naming no dimension; both helpers for one dimension | `missing_dimension`; `unsupported_layout` |
| `referenceDate` of 1 MiB / 1 MiB + 1 | `ParseError bad_date` / `too_large` |
| a text file, an empty file, a directory, a truncated netCDF-4 | `NcError` (`open`) |
| a decoy time variable, a time-major 2^40 matrix, a scalar `platform_name`, a packed `valid_range`, `foo` with `foo_status` | `test_station_netcdf_foreign_review.cpp` |

### 7.4 Regression map

| Bug | Where | Bug | Where |
|---|---|---|---|
| B1 | vector_series, adcirc_ascii (netCDF: parity test) | N1 | hwm_stats fit alternative |
| B2 | dflow | N2 | hwm `is_dry`, threshold rows |
| B3 | text_file (ordered precondition), imeds | N3 | imeds |
| B4 | nc types; adcirc_nc, dflow, station_nc float | N5 | time `parse_utc_datetime` TZ |
| B5 | nc `--wrap` counts; every reader's handle tests | N6 | adcirc station file |
| B6 | nc open; atomic stages; station_nc handles | N7 | sample combine; adcirc partner |
| B7 | nc char rows; station_nc name_len 7/50/300 | N8 | datum strings |
| B8 | nc `text_att`; `referenceDate` 120 B | N13 | imeds |
| B9 | nc masking; legacy default fill, `-99999` a value | N16 | dflow `AtLayer` |
| B10 | geo `Epsg`; station_nc EPSG types | N17 | `empty()` |
| B11 | dflow, cf_time | N18 | imeds writer |
| B12 | nc `find_*`, dflow | N19 | spans + hardening; `StationTable` |
| B13, B14 | series_ops `extent` | N20 | hwm_file |
| B15 | nc `put_*`; station_nc | B19 | atomic writes (session test in Phase 6) |
| B16, B26, N15, N21, N22 | retired with CRMS (D7) | B17, B18, B20–B25 | later phases |

### 7.5 Fuzzing (D21)

Targets are built with Clang `-fsanitize=fuzzer,address,undefined` (the `fuzz` preset).
Clang's `-fsanitize=undefined` includes `float-cast-overflow`, so the fuzz preset covers
the double → integer casts that the GCC UBSan builds leave out (D21); `checked_time`
makes those casts unreachable anyway. CI and the `fuzz` workflow run each target for
`MOV_FUZZ_SECONDS` (default 10) seeded from `tests/fixtures/{core,io}/<target>/`; there
is no nightly run.

| Target | Reference law |
|---|---|
| `fuzz_parse_unit` | symbol and udunits round trip; identity conversion; an `OtherUnit` symbol has no leading, trailing or doubled white space |
| `fuzz_parse_utc_datetime` | the error column is inside the text; the canonical form re-parses |
| `fuzz_parse_vertical_datum` | token round trip; unknown text is reported trimmed; case and padding do not change the answer |
| `fuzz_parse_quantity_token` | registry token xor generic |
| `fuzz_parse_version` | round trip / no UB |
| `fuzz_checked_time` | result within ±2^53 ms for every value type; a separate epoch field; the double-versus-integer law; under UBSan also the double-to-int casts (16-byte header: a double, an int64) |
| `fuzz_normalize` (3 bytes per row) | the output passes `make` and equals "first sample per time"; all three counts exact; `clean()` iff nothing changed, and then output == input; re-normalizing is clean and identical |
| `fuzz_parse_double` | both implementations agree bit for bit; the verdict equals a `std::regex` of the grammar (tokens up to 128 bytes); an integer token of up to 15 digits has exactly the integer's value; the shortest `to_chars` text of the result parses back to the same bits |
| `fuzz_line_cursor` | `LineCursor` against a model (`split_on('\n')`, BOM skipped, empty last field dropped, one CR chomped); `split_ws` complete, maximal and equal to `split_ws_into`; `split_on` rejoins; `truncate_utf8` never cuts inside a sequence |
| `fuzz_parse_cf_time_units` | errors lie inside the text; accepted units round-trip through the canonical text without a warning; the same instant as local time + `+05:30` with an abbreviated unit parses equal; `CfClock::at` stays within ±2^53 ms for a double and an integer that agree on an integral value |
| `fuzz_parse_imeds` | a parse error names an existing line (or the one after the last), never the internal `corrupt_record`; non-value samples are bounded by the masked counts; an accepted file formats and re-parses to the same stations (ids from `uniquify_ids(imeds_name)`, values to 6 decimals), with value rows conserved (`values(T2) + value_reads_as_missing == values(T)`) and only the warnings the text still earns; when nothing is masked and every value is under 1e9, `format(parse(format(T))) == format(T)` byte for byte. Mutation `{:14.6f}` → `{:14.3f}` is caught in 30 s |
| `fuzz_parse_adcirc_ascii` (3-byte prefix: kind, station count, selection mask) | column lengths == axis length; times strictly increasing; a subset parse is a view of the full parse, or holds the same records plus at most one more when the whole dropped a cut-off record (a damaged line of an unselected station ends the whole's last record, not the subset's); only a reordered axis is skipped |
| `fuzz_parse_adcirc_station_file` | `size() == count`; valid locations |
| `fuzz_parse_hwm_csv` (1-byte unit prefix) | valid locations; finite `Wet` within bounds |
| **`fuzz_station_netcdf_structure`** | the first byte picks a template (free schema, v5 orthogonal or incomplete, foreign orthogonal / incomplete / contiguous / indexed / single, legacy, ADCIRC, D-Flow); the rest spoil it (a missing variable, another type, a vocabulary attribute, a huge dimension on a chunked variable, a classic file, a decoy time-units variable, a data variable labelled `time`). The schema is written with netCDF-C, then `detect_file`, `inspect_station_netcdf`, `read_station_netcdf` (boundary and whole padding), and the ADCIRC and D-Flow inspect and read run with small `ReadLimits` (16384 elements, 4 KiB attributes, 4 MiB results). Law: no crash, UB or hang; no allocation over 256 MB (`__asan_default_options` sets `max_allocation_size_mb`); the tables are consistent (columns as long as times, times increasing, inspect equals read in origin, stations and schema, the file holds at least what a read returns); `detect_file` agrees with the origin the reader reports and never names a station kind the reader refuses; **a template no byte spoiled is a valid file** that must read back as the ids, times and values written. `MOV_FUZZ_TRACE=1` prints whether an input is exact; `MOV_FUZZ_SCRATCH` moves the file to a faster directory (on NFS an execution took about 60 ms instead of 15). Runs (about 16 executions per second): 180 s clean (2981 runs); 300 s found the legacy template's per-station units and datum, now counted as spoiled; 300 s clean (4984 runs, peak RSS 264 MB); 600 s clean (9119 runs, peak RSS 279 MB). |

---

## 8. Work packages

How Phase 2 was cut (the dependency graph of §6 follows it):

| WP | Scope | Depends on |
|---|---|---|
| WP1 | Vocabulary I: `time`, `units`, `geo`, `sample`, `quantity`, `datum`, the probe | — |
| WP2 | Vocabulary II: `meta`, `timeseries` + `normalize`, `station`, `station_table`, `vector_series` | WP1 |
| WP3 | `series_ops`, `datum_shift` | WP2 |
| WP4 | `hwm`, `hwm_stats`, `golden.py` + F8 | WP1 |
| WP5 | io `error` / `warning` / `read`, `text_file`, `detail` helpers, `cf_time`, `projection`, text fuzz targets | WP1 |
| WP6 | the netCDF wrapper, `write_netcdf_atomic`, `--wrap` tests, fixtures | WP5 |
| WP7 | IMEDS, CSV, the read-chain test | WP2, WP3, WP5 |
| WP8 | ADCIRC ASCII + station file, HWM file | WP2, WP4, WP5 |
| WP9 | ADCIRC netCDF, D-Flow, the block-read measurement | WP2, WP6 |
| WP10a | SN writer and v5 reader, the `format-compliance` job | WP3, WP6 |
| WP10b | foreign CF + legacy readers, `detect_file`, the hostile set, the structure fuzzer | WP10a |

---

## 9. Open items

### 9.1 Resolved: Dry samples in CSV export

Resolved by the maintainer on 2026-10-07: the CSV gets a `status` column (`value`,
`missing`, `dry`) after `value`. `value` is empty unless `status == value`, so the value
column stays numeric for pandas and spreadsheets. This extends D27's column list by one.

### 9.2 Resolved: `docs/station-netcdf.md` alignment

SN now says what the code does: the dry rule exempts D-Flow (§8.2); `vertical_datum`
applies to `water_level*` and generic quantities (§4.3, §10.2); PROJ lives in io (§10.1);
the readers return `expected<Read<...>, io::Error>` with the `FormatErrc` names of §12.9;
the writer's errors are those of `validate_station_netcdf` (§12.8); the `<q>_status`
trigger is "iff any sample of the column is Dry" (§8.2); `VerticalDatum` includes
`IGLD85` (§10.2); the legacy width rule is in §11.

### 9.3 Deferred

- The affine `Temperature` value type (no single-temperature consumer in Phase 3 either,
  `docs/providers-design.md` §3.6).
- A `DatumTable` that carries its station (plan OI 12; Phase 6, where model-vs-observed
  may need it; Phase 3 shifts no provider series client-side).
- Rotating grid-relative vector components by the meridian convergence (D28).
- A column-wise vector derivation in core (§5.5).
- The `.mvs` importer (Phase 6).
- Fuzzing netCDF from memory bytes (v5.x; the structure fuzzer covers schemas).

Resolved by the Phase 3 design (`docs/providers-design.md`, built from WP P1 on):
- `TimeRange::split` and chunk merging: `core::split` and `aligned` (§3.1), and `fetch`'s
  `Part` with its left-biased `combine` (§5.4).
- The USGS daily cadence: not a `SeriesMeta` field but its own type, `core::DailySeries`
  (decision 34, §3.4).
- `GaugeStation<P>::datums` is removed: the CO-OPS datum table moves to
  `Capabilities<Coops>` as an `optional` (§3.3).

Owner-facing questions Phase 2 left open are collected in `docs/rearchitecture-plan.md`
§6, "Open items from Phase 2".

---

## Appendix A. Design review resolution (2026-10-07)

Findings of the design review (ben-deane, sean-parent, neckbeard-nate) as triaged by the
coordinator. **R** = resolved as asked; **P** = partially adopted (reason given); **X** =
rejected.

| ID | Finding | Status | Where |
|---|---|---|---|
| Ben Q1 | error composition | R: narrow in core, one variant per layer; `lift<V>`; `Read` writer monad + chain compile test | C8, §2.12, §5.0 |
| Ben Q2 | Dry on any quantity | R (accepted) | C1 |
| Ben B1, S1, S2; Sean S3 | datum/meta consistency | R: `datum_applicable` at construction; `assume_*`; derived-meta table; datum equality known-and-equal | C3, §2.6, §2.9, §2.10 |
| Ben S3; Sean S2 | finite by construction | R: `Sample::of → optional`; `finite_or_missing` | C1, §2.4 |
| Ben S4; Sean S4 | `normalize` total over rows | R: `vector<Point>`, `adjacent_find` fast path, `descents`, conflicting warning; builder dropped | §2.7 |
| Ben S5 | narrow errors | R: one error type per function | §2.12 |
| Ben S6 | project at read boundary | R: `FileStation.location` (+ optional native) | C5 |
| Ben S7; Sean S8 | total QuickStats; NoWetMarks keeps total | R | §2.10, §3 |
| Ben S8; Sean N5 | `LinearFit` variant | R | §3 |
| Ben S9 | generic quantity identity | R: `GenericQuantity` | §2.5 |
| Ben S10; Sean S5 | folds | R: ordered left fold for `Moments` / `Bucket`; `transform_reduce` only for `Extent`; tolerance law tests | §2.10, §3, §7.1 |
| Ben N1; Sean S6 | units | R: `OtherUnit` only via `parse_unit`; `""` → nullopt; warning; deleted rvalue views | §2.2 |
| Ben N6; Sean S7 | DatumTable | R: no MSL slot; conflicts/NaN errors; total `offset(d,d)`; pinned shift order | §2.11 |
| Ben N15 | defer Temperature | P: value type deferred; `TemperatureUnit` kept, because `degC`/`degF` must classify as temperature for SN `units_metadata` and for the residual rule | C6 |
| Ben N16 | drop writer codes types rule out | R | §5.6 |
| Sean B1; Nate S6 | thread safety | R: no mutex; caller-serialized; choke point + debug assert; include gate; blocks + `StopToken`; 8-thread test dropped | C11, §4.1 |
| Sean B2 | WP graph | R: vocabulary split WP1/WP2; `DatumTable` → WP1 | §8 |
| Sean S1 | station record | R: `StationTable` (schema + axis pool + columns); `choose_layout` = `single_axis`; `ModelOutput` removed | C4, §2.8 |
| Sean N7 | transitivity tolerance | R: dyadic exact + random 1e-12 | §7.1 |
| Nate B1, B2 | time arithmetic | R: `checked_time` (both overloads), integer `CfTimeUnit`, masked time → error, fuzz targets, UBSan note | C13, §2.1, §5.1, §7.5 |
| Nate B3 | sizes | R: `checked_product`, `ReadLimits`, `too_large`, `reserve_capped`, required selection, text size check | C12, §4 |
| Nate B4 | names | R: `NcName` / `NcNameRef` (`consteval` literals) | §4.2 |
| Nate S1 | legacy names | R: `cut_at_nul` + junk fixture | C14, §7.3 |
| Nate S2 | masking | R: native-type `Masking<T>`, vector `missing_value`, `_Unsigned` → skip with warning (honoring it was rejected: no known source uses it), 64-bit rejected, exact `put` | §4.2–4.3 |
| Nate S3 | Windows paths | R: `nc_path` helper flagged for verification | §4.1 |
| Nate S4 | atomic write | R: the stages, `NC_NOCLOBBER`, abandon, `NewFile`, fault injection | §4.4 |
| Nate S5 | close policy, `--wrap` tests | R | §4.1, §7.2 |
| Nate S7 | chunk-aware reads | R + measured | §5.4 |
| Nate S10 | `Read<>` everywhere; `epoch_used` | R | §5 |
| Nate S11 | hostile set + structure fuzzer | R | §7.3, §7.5 |
| Nate S12 | non-vacuous tests | R: asserted locale precondition, B3 ordering, public N5 parser | §7.2 |
| Nate S13, S14 | text boundary | R: token table, BOM, all-non-numeric header rule, IMEDS id uniquify, `Location::make` with designated init | §5 |
| Owner | R² uncentred (D25), CSV long (D27), synthetic DFlow (D26), C9 veto (IMEDS exact sentinels; D16 model-only), formula injection, ADCIRC partial runs, Fortran NaN | R | C9, C10, C18, §5.3, §5.8 |
| — | `StationId` keeps `<=>` | X: ids are used as ordered map keys and sorted lists | §2.8 |
