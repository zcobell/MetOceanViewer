# WP4 notes (HWM types and statistics)

Fold into `docs/core-design.md` §3, then delete this file. Everything here is a
choice the design left open, or a change from it (including the maintainer
decisions after the Nate/Conor review). The declarations otherwise match §3.

## Declarations that differ from §3 (additions marked +)

Boundary and dry rule
- `model_value(double raw, LengthUnit)` returns
  `std::expected<WetDry, ElevationError>`; `ElevationError` is
  `{not_finite, out_of_range}`. The dry rule comes first (`raw <= -999`, so
  `-inf`, `-99999` and `-DBL_MAX` are `Dry`); anything else must pass
  `checked_elevation`.
- + `checked_elevation(double raw, LengthUnit) -> expected<Length, ElevationError>`:
  finite and `|value| <= max_elevation_m = 1e4` m. The reader builds `ground` and
  `observed` through it, and `modeled` through `model_value`. Elevations are
  therefore bounded by 1e4 m, so no square, product or sum of marks overflows.
  (The maintainer named a single `NonFiniteModelValue`; the bound needs a second
  reason, hence the two-valued enum.)
- A prefilter (`|raw| > 1e300` is `out_of_range`) keeps the unit conversion finite
  for any `double`.

Contract for WP8's HWM parser (`parse_hwm_csv`)
- Every elevation column goes through `checked_elevation` (ground, observed) or
  `model_value` (modeled); non-finite is a `ParseError` (it must not become
  Missing or 0), and so is `out_of_range`. A modeled value at or below -999 is Dry.
- `lon`/`lat` go through `Location::make`.
- With that, `hwm_stats` cannot see a non-finite or overflowing value;
  `NonFiniteMoments` is a backstop for marks built some other way.

Classes and `classify`
- Break and error comparison is on a 1 nm grid: `grid_nm(Length)` is the metres
  value times 1e9, `round_half_away`, after saturating at +-1e6 m (monotone; keeps
  `round_half_away`'s `|v| < 2^62`). `classify` is
  `upper_bound(breaks, grid_nm(error), less, grid_nm)`; `make` is
  `adjacent_find(breaks, greater_equal, grid_nm)`, so breaks on the same grid
  point (closer than about 1 nm, or both beyond 1e6 m) are rejected. Only the
  comparison is quantized, no statistic is.
- Contract: a decimal tie goes to the upper class, exactly, for inputs with at
  most 9 decimal places in metres or 5 in feet or inches (what a survey or model
  writes). The raw-double comparison is wrong for many decimal ties, in metres
  as well as feet: `2.3 - 1.8` is `0.4999999999999998` against a break of 0.5.
  v4 was not "always upper": it compared raw doubles in the file's unit, so
  observed -9.94 ft and modeled -6.44 ft (error `3.499999999999999`) fell
  *below* the 3.5 ft break. The feet path is worse still because `Length` stores
  SI, so each side is first multiplied by 0.3048 (28 of 77 decimal
  (break, observed) pairs miss the break by an ulp). The grid removes both.
- `classify` has a finite-input precondition (asserted in `grid_nm`; a NaN
  saturates high in a release build rather than reaching the integer
  conversion). `breaks() const&&` is deleted (a view into a temporary).
- `ClassBreaksError` has `not_finite` (checked first) and
  `not_strictly_increasing`. `make` takes `const ErrorClasses::Breaks&`.

Moments
- `Moments` has private state in one aggregate `Fields` (no default member
  values) behind an explicit private constructor; `of` and `operator+` build
  results with designated initializers. Fields: `seen`, `n`, three means, four M2
  sums. `of(Dry) = {seen = 1, n = 0}`; the identity `{0, 0}` stays exact; an
  operand with `n == 0` leaves the other's wet fields bit-identical and adds to
  `seen`. `operator==` is bit-for-bit (`bit_cast` of the fields), documented.
- `HwmStats::total` is `seen`; `total >= wet` cannot be violated. The overload is
  `hwm_stats(const Moments&, Intercept)` (no `total` parameter).
- `wet_moments` is `constexpr`, `noexcept`, header-inline:
  `views::transform(Moments::of)` into `std::accumulate(..., std::plus{})`, an
  ordered left fold. Not `ranges::fold_left` (libc++ 17/18 lacks it); not
  `reduce`/`transform_reduce` (they may regroup; the join is only approximately
  associative; commented at the function). `hwm_stats` lives in `hwm_stats.cpp`
  (`std::sqrt`).
- Welford is `operator+` with a one-observation right operand; there is no
  separate function. Accessors return `double` (SI).

Statistics
- + `NonFiniteMoments` in `HwmStatsError`; the order is no wet marks, non-finite
  moments, too few for a free fit, degenerate observed. After the fold every
  moment is checked for finiteness, and so are the slope, intercept and R^2.
  The clamps are `v > hi ? hi : v` (not `std::min`/`max`, which turn a NaN into
  the bound).
- R^2 is `nullopt` with no degrees of freedom: free fit with at most 2 wet marks,
  origin fit with 1; also when its denominator is zero (free: `m2y == 0`; origin:
  `sum(y^2) == 0`). Free R^2 is `(cxy/m2x) * (cxy/m2y)`, asserted (debug) to be at
  most `1 + 4 n eps` before it is clamped to 1; the overshoot comes from
  accumulated rounding in the moments, a few n ulps, not one ulp. Origin R^2 is in
  `[0, 1]` by construction.
- `fit_of` switches over `Intercept` with no default (a value outside the enum is
  the origin fit); the result is built with `expected::and_then/transform`.
- `HwmStats` and the error structs have defaulted `==`; zero checks are `<= 0.0`.

## Fixtures and tests

- F8 in `tests/fixtures/core/hwm/`: `hwm_basic.csv` (metres, 8 rows, 2 dry, one error
  on the 0.5 m break), `hwm_ft.csv` (feet, 7 rows, 1 dry), `hwm_allDry.csv`,
  `hwm_one_wet.csv`, + `hwm_ties.csv` (a decimal tie on every metre break,
  including `1.8,2.3`), + `hwm_ties_ft.csv` (a tie on every foot break, `11.00,12.50`,
  `-9.94,-6.44`, and `12.49999` / `12.50001`). `hwm_header.csv` and
  `hwm_blank_line.csv` are parser fixtures and belong to WP8.
- `golden.py` (standard library only, Python >= 3.11, version-checked at the top)
  uses exact `Fraction` sums and `Fraction` breaks, and cross-checks
  `statistics.linear_regression` (proportional and free), `correlation` (r^2) and
  `stdev` (n - 1). `golden.txt` is committed. CTest `hwm_golden_is_current` runs
  `golden.py --check`. `find_package(Python3 3.11)`; `MOV_REQUIRE_GOLDEN_CHECKS`
  (ON in the `ci-*` presets) makes a missing interpreter a configure error.
- Tests read the CSVs with a test-only loader (`tests/core/hwm_fixture.hpp`) that
  goes through the same boundary functions a real reader must use.
- One executable, `mov_core_hwm_tests` (+ `_constexpr`, `_relaxed_constexpr`), is
  appended to `tests/core/CMakeLists.txt` so this WP's edit does not conflict with
  WP2's; fold it into `mov_core_tests` if one executable per module is wanted.
  `src/core/CMakeLists.txt` gets a trailing `target_sources(mov_core PRIVATE hwm_stats.cpp)`.
- Numeric tests use 1e-12 relative (or absolute for a golden that is exactly 0).
  A runtime test pins `wet_moments` to the explicit left fold bit for bit on
  decimal data, and shows the reversed fold differs.

## Unverified

- macOS (Apple Clang + libc++) and Windows (MSVC): not run. Used beyond WP1's
  probe list: `std::bit_cast` of a struct of doubles in a constant expression,
  `views::transform` with `std::accumulate` in a constant expression,
  `expected::transform/and_then` in a constant expression.
- No parser here, so no fuzz target.
