# WP4 notes (HWM types and statistics)

Fold into `docs/core-design.md` §3, then delete this file. Everything here is a
choice the design left open, or a change from it. The declarations otherwise
match §3.

## Declarations that differ from §3 (additions marked +)

- `ClassBreaksError` has two enumerators, `not_finite` and `not_strictly_increasing`
  (the design had only the second). `ErrorClasses::make` checks finiteness of all
  seven breaks first, so a NaN is not reported as "not increasing".
  `make` takes `const ErrorClasses::Breaks&` (+ alias for `std::array<Length, 7>`);
  `breaks() &&` is deleted (a view into a temporary).
- + `hwm_stats(const Moments&, std::size_t total, Intercept)`: the real
  implementation. `hwm_stats(span<const HighWaterMark>, Intercept)` folds and
  calls it. Lets a caller that already holds moments (a cached selection) skip the
  fold, and lets the tests drive the degenerate cases directly.
- `wet_moments` is `constexpr` and defined in the header with `std::accumulate`
  (an ordered left fold, as the design says), not `ranges::fold_left`: libc++ 17/18
  (Apple Clang) has no `fold_left`, and `accumulate` is the same fold. `hwm_stats`
  itself is not `constexpr` (`std::sqrt`), so it lives in `src/core/hwm_stats.cpp`.
- `Moments` accessors return `double` (metres, metres squared), not `Length`: the
  M2 sums are squared lengths. There is no separate Welford function: Welford's
  update is Chan's join with a one-observation right operand, so `operator+` is
  both (documented in the class comment).
- `r_squared` for a free fit is clamped to at most 1 (Cauchy-Schwarz bounds it;
  rounding can give `1 + 2e-16`). Through the origin it is in `[0, 1]` by
  construction (`SSres = max(0, Σy² − slope·Σxy)` and `slope·Σxy ≥ 0`).
  `nullopt` when the denominator is zero: free `m2x·m2y == 0` with `m2x > 0`
  (all modeled values equal); origin `Σy² == 0`.
- `HwmStats`, `ThroughOrigin`, `Free`, `NoWetMarks`, `TooFewForFreeFit` and
  `DegenerateObserved` have defaulted `==` (+). `HwmStats`'s fields have no
  default member values (a designated initializer that omits one is a compile error).
- Zero checks on `m2x` and `Σx²` are `<= 0.0`, not `== 0.0` (no float-equal).
- `model_value` precondition: the raw value is finite or `-inf`. NaN and `+inf`
  come out as `Wet` and poison the sums; `parse_hwm_csv` (WP8) must reject them
  (design: "finite `Wet`"). `classify` of a NaN error is `bin7` (total, no UB).

## Finding: feet data on a break (needs an owner decision)

`Length` stores SI, so `modeled_error` is `y·0.3048 − x·0.3048`, not
`(y − x)·0.3048`. For a file in feet, an error that is exactly on a break in
decimal (say 12.5 ft observed 11 ft, break 1.5 ft) can differ from `1.5·0.3048`
by one ulp and land in the lower class. v4 compared in the file's own unit, so on
such rows v4 always put the value in the upper class. Probe: of 77 (break, observed)
pairs with decimal inputs, 28 miss the break by one ulp. For metres (and any value
exact in binary) the rule is exact; `tests/core/test_hwm.cpp` pins this
("exact only up to rounding of the unit"). Options, not taken because they change
the contract: classify with a relative tolerance of a few ulps; or keep the file
unit in `HighWaterMark` and the classes. The fixtures avoid feet rows on a break.

## Fixtures and tests

- F8 in `tests/fixtures/core/hwm/`: `hwm_basic.csv` (metres, 8 rows, 2 dry,
  one error exactly on the 0.5 m break), `hwm_ft.csv` (feet, 7 rows, 1 dry, no row on
  a break), `hwm_allDry.csv`, `hwm_one_wet.csv`. `hwm_header.csv` and
  `hwm_blank_line.csv` are parser fixtures and belong to WP8 (`hwm_file`).
- `golden.py` (standard library only; exact `fractions.Fraction` sums; cross-checks
  the origin slope against `statistics.linear_regression(proportional=True)`, so
  Python >= 3.11) writes `golden.txt`, which is committed. CTest
  `hwm_golden_is_current` runs `golden.py --check golden.txt` (registered only if
  a Python 3 interpreter is found). Values are in metres; the category codes are
  per row (0 dry, 1..8 = `bin0..bin7`), classified in the file's unit, where the
  breaks are exact.
- The C++ tests read the CSV with a test-only loader (`tests/core/hwm_fixture.hpp`),
  because the real parser is io's.
- One executable, `mov_core_hwm_tests` (+ `_constexpr`, `_relaxed_constexpr`), is
  appended to `tests/core/CMakeLists.txt` instead of extending the `mov_core_tests`
  source lists, so this WP's edit does not conflict with WP2's. Fold it into
  `mov_core_tests` when merging if one executable per module is wanted.
  `src/core/CMakeLists.txt` gets a trailing `target_sources(mov_core PRIVATE hwm_stats.cpp)`.
- Not tested bit-for-bit across platforms: the numeric tests use 1e-12 relative
  (the fold itself is deterministic for a given order and `-ffp-contract=off`).
  `Moments` equality is `==` on bits; associativity and fold-halves use a field-wise
  tolerance (`moments_near`).

## Unverified

- macOS (Apple Clang + libc++) and Windows (MSVC): not run. Nothing new beyond
  WP1's probe list is used (`std::accumulate`, `ranges::adjacent_find`,
  `ranges::upper_bound` on a `span` are constexpr in all three STLs).
- No parser here, so no fuzz target.
