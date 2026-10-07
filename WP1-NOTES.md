# WP1 notes (Vocabulary I)

Fold into `docs/core-design.md`, then delete this file. Everything here is a
choice the design left open or got wrong; the declarations otherwise match §2.1-2.5, §2.11.

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

Two further facts the probe found, both for tests and constexpr code:

- Both compilers reject a floating-point operation that **overflows or produces NaN** in a
  constant expression: GCC for `inf - inf` and `1e308 * 1000`, Clang also for `NaN * 1.0`.
  `core/detail/numeric.hpp` therefore has constexpr `is_finite` (comparisons only),
  `magnitude` and `round_half_away` (exact: truncate, then look at the fraction).
  `checked_time` bounds the value *before* multiplying. Constexpr tests cannot build a
  NaN `Measure`; the NaN-order and NaN-height tests are runtime tests.
- The design's "constexpr `<cmath>` ... STATIC_REQUIREs become runtime behind a feature
  macro" is not needed: nothing in core calls `<cmath>` in a constant expression.

To confirm on the macOS (Apple Clang + libc++) and Windows (MSVC STL) runners: every
test named `probe: ...`. In particular libc++ for `views::enumerate`, `views::zip`,
`ranges::fold_left`, constexpr `from_chars`; MSVC for the `template <double V>` detection
in the constexpr probe and for `/utf-8`-free source (degree signs are spelled as UTF-8
byte escapes, not literals).

## Declarations that differ from §2 (additions are marked +)

- `LatLon`: members default to 0 and `==` defaulted (+), so it is `std::regular`.
- `TimeRange`: no default constructor (a default range would be empty, which `make` forbids).
  Error enums are named `TimeRangeError::empty_or_inverted`, `ValidRangeError::inverted`,
  `LocationError::{not_finite, latitude_out_of_range, longitude_out_of_range}`,
  `EpsgError::not_positive`. `Location::make` checks non-finite, then latitude, then longitude.
- `ValidRange` has a public default constructor (unknown, ongoing: a valid state). `first == last`
  is allowed (one valid day).
- `checked_time`: `unit_ms < 1` gives `nullopt` (not in the design). The epoch itself is not
  bounded, only the result. The two overloads are ambiguous for an `int` or `long long`
  argument (compile error, not a silent choice); call sites pass `std::int64_t` or `double`.
- `parse_utc_datetime`: the fraction is 1 to 3 digits (`.5` is 500 ms); year is exactly 4
  digits; no leap second (`:60`), no whitespace, no offsets. `DateTimeError::column` is the
  0-based offset of the first bad character, the text size when truncated, and the start of
  the field when its value is out of range (`2005-02-30` gives 8).
- `Sample::combine`: `noexcept` is conditional on `op` (a lambda not marked `noexcept` would
  otherwise be rejected).
- `Measure`: `operator<=>` is `std::partial_ordering`; the design's operator list is
  implemented exactly (no `+=`). `Affine::operator()` is `scale * x + offset`.
- + `UnknownUnit::operator==` (so `UnitError` is comparable).
- + `bool is_canonical_other(const OtherUnit&) noexcept`: true for percent, degree, s,
  `S m-1`. The design says readers warn `unrecognized_unit` for a non-canonical
  `OtherUnit` but gave no predicate.
- `OtherUnit`'s passkey is `detail::UnitKey`, whose only friend is `detail::UnitFactory`
  (defined in `units.cpp`), so `parse_unit` is not a single function.
- `symbol(Unit&&) = delete` also rejects `symbol(LengthUnit::foot)` (the enumerator converts to
  a temporary `Unit`). Callers need an lvalue `Unit`. Consider `symbol(LengthUnit)` etc. overloads
  in WP2 if that bites; none exist now.
- `parse_unit` spellings beyond the §2.2 table, so that `parse_unit(symbol(u)) == u` and
  `parse_unit(udunits(u)) == u` hold for every non-`Other` unit (tested, and fuzzed):
  `in/inch/inches`, `mile(s)`, `nautical_mile(s)`, `kilometer(s)/kilometre(s)`, `knot`, `kph`,
  `km h-1`, `km hour-1`, `mile hour-1`, `ft s-1`, `millibar(s)`, `celsius`, `fahrenheit`, `deg F`.
  Rules: whitespace-trimmed, inner runs collapsed to one space; symbols are case-sensitive
  (`Mb`, `PA`, `c` are `OtherUnit`); words (`Meters`, `FEET`, `Knots`, `celsius`) match any case.
  `symbol()` for knots is `kt`; temperatures are `°C`/`°F` (UTF-8 bytes).
  `udunits(mH2O)` is `mH2O`, which I could not check against a udunits table.
- `GenericQuantity` has no default constructor, so it is `copyable` + `equality_comparable`
  (the §7 rule for types without a default). `QuantityId` is `std::regular`, but its default is
  `Quantity::water_level`: always build a generic one explicitly. `parse` has no length cap.
  `parse("value", "")` equals `GenericQuantity::value()`.
- `QuantityInfo` gets `==` (+). The user-facing predicate is `datum_applicable` (design name);
  there is no `carries_datum`.
- `VerticalDatum` parsing trims ASCII whitespace; `"none"`, `""`, whitespace and unknown text
  all give `nullopt`. `DatumHeight` and `QuantityInfo` get defaulted `==` (+).
- `DatumTable::from_heights(reference, rows)`: rows are heights **above the reference**. The
  reference is preset to 0, so a row restating it must be 0 (else `ConflictingHeight{reference}`);
  the first conflicting or non-finite row in input order is reported; unless the reference is MSL
  an MSL row is required (`MissingMsl`, also for an empty `rows`); rebasing on MSL that overflows
  gives `NonFiniteHeight{datum}`.
  Known gap: `offset(a, b)` is a plain subtraction, so heights near ±1e308 m could still give
  an infinite `Length`. Not worth an error type.
- F12 (`tests/fixtures/core/datum/offsets.csv`): the legacy CSV columns are *offsets* (value to add
  to the pivot-datum series), which are the negated heights above the pivot. The fixture
  carries heights and the arithmetic is checked in `test_datum.cpp`. This is the same sign
  statement `legacy-formats.md` §9.2 makes in prose.

## Test and tooling findings

- Catch2 treats any type with `begin()` and `end()` as a range, so `TimeRange` needs a
  `Catch::StringMaker` specialization (`tests/core/test_helpers.hpp`).
- `bugprone-unchecked-optional-access` also flags `.value()`; tests use `value_or`, or
  `opt ? ... *opt : ...`.
- `clang-analyzer-optin.core.EnumCastOutOfRange` fires on Catch2's `operator|` for result
  flags in `CHECK_FALSE`/`REQUIRE_FALSE` that follow a range-for loop. Inside and after loops the
  tests use `CHECK(not (...))`.
- `bugprone-exception-escape`: `symbol`, `udunits` and `token` are `noexcept`, so they use
  `std::get_if` chains, not `std::visit`.
- A git worktree cannot run `pre-commit` in the dev container: `.git` is a file pointing at the
  main repo's `.git/worktrees/<name>`, which is not mounted. I ran
  `MOV_DOCKER_ARGS="-v <repo>/.git:<repo>/.git:ro -v <repo>/.git/worktrees/<name>:<repo>/.git/worktrees/<name>" tools/dev/run.sh pre-commit run --all-files`.
  `run.sh` could do this itself when `.git` is a file.
- Fuzz oracles: `fuzz_parse_unit` (symbol and udunits round trip, identity conversion),
  `fuzz_parse_utc_datetime` (error column inside the text; canonical form re-parses),
  `fuzz_parse_vertical_datum` (token round trip), `fuzz_parse_quantity_token` (registry
  token xor generic), `fuzz_checked_time` (result within +-2^53 ms; under the fuzz preset's UBSan
  also the double-to-int casts). Seeds are in `tests/fixtures/core/<target>/`. One-off 120 s runs
  of each: 8M to 38M executions, no findings.
