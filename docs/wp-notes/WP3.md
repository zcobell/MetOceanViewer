# WP3 notes (series operations, datum shift)

Fold into `docs/core-design.md` §2.10-2.12, then delete this file. Everything here
is a choice the design left open, or a change from it. Headers: `series_ops.hpp`,
`datum_shift.hpp`; private `src/core/series_rebuild.hpp`. The declarations
otherwise match §2.10-2.11.

## Declarations that differ from §2.10-2.12

- **`residual` has its own error type**, `ResidualErrc {unit_unknown,
  units_differ, temperature_difference, datum_unknown, datums_differ}` (WP2
  removed `AlignmentErrc`). The values are listed in the order the checks run.
  §2.12's row `residual -> AlignmentErrc` becomes `ResidualErrc`.
- **`convert` on a table takes a `ColumnIndex`**, not a `std::size_t` (WP2's strong
  index). Out-of-range is a precondition, as for the table accessors.
  `StationTable` gained one keyed member, `rewrite_column(CoreKey, ColumnIndex,
  SeriesMeta, f) &&`, because the schema and columns are private; it is the only
  change to a WP2 header. It does not re-check schema uniqueness: `convert` changes
  only the unit, so the token is unchanged.
- **`Bucket`** keeps its state in a private aggregate `Fields` with no default
  member values (the `Moments` pattern, WP4). `Bucket::of` and `+` use designated
  initializers. `detail::lower_extreme` / `upper_extreme` are the tie-keeping
  joins (left operand wins on equal values).
- **`quick_stats` mean.** `sum / values`, unless that is not finite (the sum of
  values near 1e308 overflows, or `inf + -inf`); then the mean is recomputed as
  the sum of `value / n`, which cannot overflow. `Bucket::sum()` itself is
  documented as possibly infinite. A series with no value has `stats ==
  nullopt`, so there is no division by zero.
- **`extent(series)`** is nullopt iff the series is empty. A series of only
  Missing/Dry has `first`/`last` and `values == nullopt` (the times are real; the
  range of values is unknown). `combine` treats a missing range as the identity
  for the value range. `extent(span)` uses `std::transform_reduce`; the join is
  exact, so grouping cannot matter.
- **`summarize`** is `std::accumulate` over `points()`; no `fold_left`.
- **Identity `Affine`.** `detail::apply_affine` returns each sample untouched when
  the `Affine` equals `Affine{}`. `convert` between units with factor 1 (hPa and
  mb) and `convert` to the same unit therefore keep `-0.0`'s sign, and
  `scale_offset(s, Affine{})` is `s`. Any other result that is not finite becomes
  `Missing` (`finite_or_missing`): a value that overflows in `convert`,
  `scale_offset` or `shift` is Missing, never infinite.
- **`scale_offset` leaves the metadata alone**: it is a calibration (v4's
  multiplier and y shift), not a unit conversion.
- **`convert` to an equal unit** (including equal `OtherUnit`s) is the identity and
  returns the series unchanged.
- **`slice`** never fails: a range outside the series gives an empty series with the
  same metadata. Both overloads build through the keyed constructor.
- **`shift_time`** checks every time (`detail::offset_time`, which also catches
  64-bit overflow) before changing anything, so an error leaves nothing half
  shifted. A series whose times already lie beyond +-2^53 ms (legal for
  `TimeSeries`) is reported at its first such sample even for `dt == 0`, and a
  shift that brings them back is accepted.
- **`shift` (datum).**
  - The pinned order is: unit is a `LengthUnit` (a missing unit is
    `NotALengthSeries`), a datum is engaged, `from == to` returns the series, then
    `table.offset(from, to)`. `MissingOffset` names `from` before `to` (that is
    `DatumTable::offset`'s order).
  - The offset is `offset(from, to)` in metres, converted to the series' unit
    with `Length::as(unit)`, and added. Sign: `offset = h[from] - h[to]`, so a level
    at MSL expressed in MLLW at The Battery gains +0.77527 m. Pinned by tests
    against the F12 rows.
  - `from` and the rewritten metadata (`rewrite_datum`) are read together; they are
    engaged together. The rewrite is done before the `from == to` test, which costs
    one metadata copy on the identity path and avoids an unreachable branch.
- **`residual`.**
  - The datum rule applies when either quantity satisfies `datum_applicable`: both
    datums must then be engaged and equal. A water level against a wind speed that
    has equal units therefore gives `datum_unknown`. Two quantities with no datum
    (wind speed, temperature) need none.
  - Units are checked first (`unit_unknown`, `units_differ`), then temperature,
    then datums. A temperature pair with equal units is `temperature_difference`
    even for a generic quantity.
  - No silent conversion: feet against metres is `units_differ`.
  - Equal times only; times in one series are dropped; no common time gives an
    empty series, not an error. The output axis is built through the keyed
    constructor (a subset of a strictly increasing axis is strictly increasing).
  - The label is `<obs> U+2212 <pred>` with the minus sign as UTF-8 bytes
    (`"\xE2\x88\x92"`); sources carry no non-ASCII literals.

## Portability

No `views::zip`, `chunk_by`, `pairwise`, `fold_left`, `enumerate`, `ranges::to` or
`join_with` is used. Used beyond WP2's list: `std::accumulate` and
`std::transform_reduce` over `std::span` and over the `points()` view
(`iota | transform`), `std::ranges::lower_bound` with an iterator pair,
`std::ranges::find_if`/`transform`/`copy_if`/`max_element`/`min_element`.

## Tests

- `mov_core_ops_tests` (+ `_constexpr`, `_relaxed_constexpr`) is appended to
  `tests/core/CMakeLists.txt`; fold it into `mov_core_tests` if one executable per
  module is wanted. `src/core/CMakeLists.txt` gets a trailing `target_sources`.
- `tests/core/series_ops_helpers.hpp`: series builders and the random dyadic row
  generator (multiples of 0.25, so sums are exact and `==` is the right check).
  `tests/core/datum_fixture.hpp`: the F12 reader moved out of `test_datum.cpp` so
  `test_datum_shift.cpp` shares it.
- Exercised through the public operations: the keyed `TimeSeries` constructor
  (`slice`, `shift_time`, `residual`, `convert`, `shift`),
  `TimeSeries::transform_samples &&` (`scale_offset`), `SeriesMeta::rewrite_unit`
  and `rewrite_datum` (`convert`, `shift`), `into_parts`, and
  `StationTable::rewrite_column`. `transform_samples const&` has no caller in
  the core and is still untested.
- Laws: `Bucket` identity exact; associativity exact on dyadic data and within
  1e-9 absolute on arbitrary doubles in [-1000, 1000] (counts and extremes exact);
  ties keep the left operand; the peak equals `ranges::max_element` over the values
  on 200 random series; `Extent` combine is checked for associativity,
  commutativity, idempotence and identity over a pool of 13 extents (including
  time-only and empty).
- Mutations tried by hand and caught: tie rule `<` to `<=`; `slice` end inclusive;
  datum offset sign flipped; `units_differ` check removed.

## Unverified

- macOS (Apple Clang + libc++) and Windows (MSVC): not run.
- No parser here, so no fuzz target.
