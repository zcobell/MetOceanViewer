# WP1 notes (Vocabulary I)

Fold into `docs/core-design.md`, then delete this file. Everything here is a
choice the design left open or got wrong, or a change from the post-WP1 review
(ben-deane, jason-turner). The declarations otherwise match §2.1-2.5, §2.11.

## Toolchain probe (§1)

Probe tests: `tests/core/test_toolchain_probe{,_constexpr}.cpp`. Checked on GCC 14.2
and Clang 20.1, both with libstdc++ 14 (Linux dev image).

| Feature | GCC 14.2 | Clang 20.1 | Consequence |
|---|---|---|---|
| `ranges::fold_left` (`__cpp_lib_ranges_fold`) | yes | yes | §1 may allow it (still gated by the macro in the probe) |
| `views::zip`, `views::chunk_by` | yes | yes | allowed |
| `views::enumerate` (`__cpp_lib_ranges_enumerate` 202302) | yes | yes | allowed here; libc++ not checked |
| constexpr integer `from_chars` (`__cpp_lib_constexpr_charconv`) | yes | yes | allowed |
| constexpr `std::chrono` calendar, `expected` monadic ops | yes | yes | allowed |
| constexpr `std::isfinite` | yes | yes | |
| constexpr `std::fabs`, `std::llround` | **yes** | **no** | the core cannot use them |
| `std::format` of `sys_time<ms>` (`{:%FT%T}Z`), `stop_token` | yes | yes | |
| floating `from_chars` (`__cpp_lib_to_chars` 201611) | yes | yes | |

Further facts the probe found:

- Both compilers reject a floating-point operation that **overflows or produces NaN** in a
  constant expression: GCC for `inf - inf` and `1e308 * 1000`, Clang also for `NaN * 1.0`.
  `core/detail/numeric.hpp` therefore works on the bit pattern: `is_finite` and `magnitude` use
  `std::bit_cast` (exact, branchless, constexpr; `magnitude(-0.0)` is `+0.0`), and
  `round_half_away` truncates and then looks at the fraction (precondition: finite and
  `|v| < 2^62`). `checked_time` bounds the value *before* multiplying. Constexpr tests cannot
  build a NaN `Measure`; NaN-order and NaN-height tests are runtime tests, and a negative NaN is
  built from bits.
- The design's "constexpr `<cmath>` ... STATIC_REQUIREs become runtime behind a feature
  macro" is not needed: nothing in core calls `<cmath>` in a constant expression.
- `-ffp-contract=off` is passed to every first-party target on GCC and Clang
  (`cmake/ProjectOptions.cmake`, via `mov::options`). GCC (non-ISO mode) and Clang (`on`) and arm64
  compilers otherwise fuse `a * x + b`, so `Affine` results would differ in the last bit between
  compile time, x86-64 and macOS arm64. MSVC's default `/fp:precise` does not contract across
  statements; it needs no flag. `Affine at run time equals the compile-time result` pins it (the
  x86-64 baseline has no FMA, so the regression is only visible on `-march` builds or arm64).

To confirm on the macOS (Apple Clang + libc++) and Windows (MSVC STL) runners: every
test named `probe: ...`. In particular libc++ for `views::enumerate`, `views::zip`,
`ranges::fold_left`, constexpr `from_chars`; MSVC for the `template <double V>` detection
in the constexpr probe, for `long double` (= `double` there) in the `checked_time` tests, and
for the compile-fail tests (skipped on MSVC; the harness uses GCC/Clang flag syntax). Source
files carry no non-ASCII literals (degree signs are UTF-8 byte escapes), so `/utf-8` is not needed.

## Declarations that differ from §2 (additions marked +)

Time and geo
- `checked_time(value, std::chrono::milliseconds unit, Time epoch)`: two constrained templates,
  `std::floating_point` and `std::integral` (not `bool`), replace the `double` / `int64_t`
  overloads, so no argument type is ambiguous (tested: `int`, `long long`, `int32_t`, `uint32_t`,
  `uint64_t`, `size_t`, `float`, `long double`). Integers use `cmp_less`/`cmp_greater`, so an
  unsigned value above `INT64_MAX` is rejected, not wrapped. `float` and `long double` are
  bounded in their own type before narrowing. `unit < 1 ms` gives `nullopt` (defensive: the
  design never said). The epoch itself is not bounded, only the result. For an integral double
  with `|v| < 2^53` both paths agree (constexpr grid test and the fuzz oracle).
- `parse_utc_datetime`: the fraction is 1 to 3 digits (`.5` is 500 ms); year is exactly 4
  digits; no leap second (`:60`), no whitespace, no offsets. `DateTimeError::column` is the
  0-based offset of the first bad character, the text size when truncated, and the start of
  the field when its value is out of range (`2005-02-30` gives 8).
- `TimeRange`: no default constructor. `ValidRange` has a public default (unknown, ongoing);
  `ValidRange::make({.first = ..., .last = ...})` takes the aggregate `ValidRange::Bounds` so the
  two optionals cannot be swapped; `first == last` is allowed (one valid day).
  Error enums: `TimeRangeError::empty_or_inverted`, `ValidRangeError::inverted`,
  `LocationError::{not_finite, latitude_out_of_range, longitude_out_of_range}`,
  `EpsgError::not_positive`, `NativePointError::not_finite`. `Location::make` checks non-finite,
  then latitude, then longitude.
- `LatLon` has **no default member values** (`==` defaulted +): a designated initializer that
  leaves a field out is a `-Werror` compile error on GCC (`-Wmissing-field-initializers`) and
  Clang. `tests/cmake/compile_fail/` asserts that, and also `NativePoint`, unit-family mixing
  and swapped designators, each with an accepted twin (`core_compile_fail_checks`).
- `NativePoint` is a class with `make(Xy{.x, .y}, Epsg)` rejecting non-finite coordinates, and
  accessors `x()`, `y()`, `crs()` (was an aggregate). `Xy` + is the planar coordinate pair.

Samples, units
- `combine` requires `std::same_as<std::invoke_result_t<Op&, double, double>, double>` (a `float`
  or `int` result is a compile error); `noexcept` is conditional on `op`.
- `Measure`: `operator<=>` is `std::partial_ordering`; the design's operator list exactly (no `+=`).
- `symbol` and `udunits`: one constrained template per enumerator (`symbol(LengthUnit::foot)`
  works and is constexpr), `constexpr` overloads for `const Unit&`, and deleted `Unit&&`
  overloads (an `OtherUnit`'s view points into the `Unit`). The name tables are
  `detail::symbol_names` / `udunits_names` in `units.hpp`; each array size is `static_assert`ed
  against its enum, and `variant_size_v<Unit>` next to the visitor. `OtherUnit::symbol()` and `==` are
  constexpr. The noexcept lookups use `std::get_if`, not `std::visit` (`bugprone-exception-escape`).
- `conversion(A, B)` for two enumerators of different families is a deleted overload
  (compile error); `conversion(const Unit&, const Unit&)` stays for runtime units.
- + `UnknownUnit::operator==`.
- `OtherUnit`'s passkey is `detail::UnitKey`, whose only friend is `detail::UnitFactory`
  (defined in `units.cpp`).
- `parse_unit` accepts spellings beyond the §2.2 table, so that `parse_unit(symbol(u)) == u` and
  `parse_unit(udunits(u)) == u` hold for every non-`Other` unit (tested, fuzzed): `in/inch/inches`,
  `mile(s)`, `nautical_mile(s)`, `kilometer(s)/kilometre(s)`, `knot`, `kph`, `km h-1`,
  `km hour-1`, `mile hour-1`, `ft s-1`, `millibar(s)`, `m H2O`, `celsius`, `fahrenheit`, `deg F`,
  `degree_C`, `degree_Celsius`, `degree_F`, `degree_Fahrenheit`. `degrees` and `degrees_true` join
  `deg` and `degT` as aliases of the `degree` OtherUnit (so `%`, `deg`, `degT`, `degrees`,
  `degrees_true`, `sec` canonicalize). Whitespace is trimmed and inner runs collapse to one space;
  symbols are case-sensitive (`Mb`, `PA`, `c` are `OtherUnit`); words match any case. The
  display symbol for knots is `kt`; temperatures print as `°C`/`°F` (UTF-8 bytes).
  `udunits(meter_of_water)` is `m H2O` (I could not verify `mH2O` against a udunits table).

Quantities
- `QuantityId = std::variant<GenericQuantity, Quantity>` and `GenericQuantity` has a public
  default constructor equal to `value()`, so `QuantityId{}` is the generic `value` quantity
  and both stay `std::regular`. The raw-string constructor is still private.
- `GenericQuantity::parse({.token, .standard_name})`: aggregate `GenericQuantity::Spec`.
  Identity: `==` compares both members; two quantities with one token and different standard
  names are different values that name the same variable, so anything that must be unique per
  variable (WP2 `StationTable` schema) keys on `token()`. Documented in the header. No length cap.
  `parse({.token = "value", .standard_name = ""})` equals `GenericQuantity::value()`.
- `token(Quantity)` is constexpr; `token(const QuantityId&)` is constexpr inline with a
  deleted `&&` overload; `variant_size_v<QuantityId>` is `static_assert`ed beside it.
- + `Unit canonical_unit(Quantity)` (parse of the registry's unit) and a constexpr
  `is_canonical_other(const OtherUnit&)` next to the registry in `quantity.hpp`, derived from
  it (no second list). Readers warn `unrecognized_unit` for any other `OtherUnit`.
- `QuantityInfo` gets `==` (+). The datum predicate is `datum_applicable` (design name); there
  is no `carries_datum`.

Datums
- `parse_vertical_datum` returns `std::expected<std::optional<VerticalDatum>, UnknownDatum>`:
  `""`, whitespace and `none` (any case) are an engaged `nullopt` (no datum); text that is
  neither is `UnknownDatum{.text}`, the trimmed text as a **view of the argument**. Tokens and
  the `NAVD`/`NGVD`/`IGLD` aliases (namespace-scope `detail::datum_aliases`) match any case,
  with surrounding ASCII whitespace ignored.
- `DatumTable::from_heights(reference, rows)`: rows are heights **above the reference**. The
  reference is preset to 0, so a row restating it must be 0 (else `ConflictingHeight{reference}`);
  the first conflicting or non-finite row in input order is reported; unless the reference is MSL
  an MSL row is required (`MissingMsl`, also for empty `rows`); rebasing on MSL that overflows
  gives `NonFiniteHeight{datum}`. `DatumHeight` gets `==` (+).
  Known gap: `offset(a, b)` is a plain subtraction, so heights near ±1e308 m could still give
  an infinite `Length`. Not worth an error type.
- F12 (`tests/fixtures/core/datum/offsets.csv`): the legacy CSV columns are *offsets* (value to add
  to the pivot-datum series), which are the negated heights above the pivot. The fixture
  carries heights and the arithmetic is checked in `test_datum.cpp`.

## Test and tooling findings

- Catch2 treats any type with `begin()` and `end()` as a range, so `TimeRange` needs a
  `Catch::StringMaker` specialization (`tests/core/test_helpers.hpp`).
- `bugprone-unchecked-optional-access` also flags `.value()`; tests use `value_or`, or
  `opt ? ... *opt : ...`.
- `clang-analyzer-optin.core.EnumCastOutOfRange` fires on Catch2's `operator|` for result
  flags in `CHECK_FALSE`/`REQUIRE_FALSE` that follow a range-for loop. Inside and after loops the
  tests use `CHECK(not (...))`.
- `tools/dev/run.sh` now mounts the main repository's git directory read-only and the
  worktree's gitdir read-write when `.git` is a file, so `git` and `pre-commit` work in a
  worktree (`tools/dev/README.md`).
- Fuzz oracles: `fuzz_parse_unit` (symbol and udunits round trip, identity conversion, an
  `OtherUnit` symbol has no leading, trailing or doubled whitespace), `fuzz_parse_utc_datetime`
  (error column inside the text; canonical form re-parses), `fuzz_parse_vertical_datum` (token
  round trip; unknown text is reported trimmed; case and padding do not change the answer),
  `fuzz_parse_quantity_token` (registry token xor generic), `fuzz_checked_time` (result within
  +-2^53 ms for every value type, a separate epoch field, and the double-versus-integer law; under
  the fuzz preset's UBSan also the double-to-int casts). Seeds are in
  `tests/fixtures/core/<target>/`.
