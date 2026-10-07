# WP3 notes (series operations, datum shift)

Fold into `docs/core-design.md` §2.5, §2.6, §2.10-2.12, then delete this file.
Everything here is a choice the design left open, or a change from it, including the
changes from the post-WP3 review (ben-deane, conor-hoekstra) and the maintainer's
decisions on it. Headers: `series_ops.hpp`, `datum_shift.hpp`; private
`src/core/series_rebuild.hpp`. The declarations otherwise match §2.10-2.11.

## Registry: the `difference` quantity

- `Quantity::difference` is the last registry enumerator: token `difference`, long
  name "Difference", **no CF `standard_name`** (empty, as for a `GenericQuantity`),
  **no fixed unit** (it takes the unit of its operands), and **not
  `datum_applicable`**. `docs/station-netcdf.md` §6 has the new row: writers omit
  `standard_name`, and `vertical_datum` is never written for it.
- `QuantityInfo::canonical_unit` is empty for `difference` (documented on the
  field). Consequences: `canonical_unit(Quantity)` now returns
  `std::optional<Unit>` (nullopt for `difference`); the "all registry units are
  non-blank" `static_assert` excludes it; `is_canonical_other` ignores empty
  units. Readers and writers must not assume `info(q).standard_name` or
  `canonical_unit` are non-empty. (Left as `string_view` rather than
  `optional<string_view>` so existing callers of `info` still compile.)
- Registry tests: the data-driven table in `test_quantity_constexpr.cpp` has the
  row (`standard_name` and units empty); `test_quantity.cpp` checks `difference` is
  excluded from the unit and standard-name loops and pinned separately.

## Declarations that differ from §2.10-2.12

- **`residual`** has its own error type, `ResidualErrc {quantities_differ,
  unit_unknown, units_differ, temperature_difference, datum_unknown,
  datums_differ}`, in the order the checks run (WP2 removed `AlignmentErrc`;
  §2.12's row becomes `ResidualErrc`).
  - `quantities_differ` comes first. Allowed pairs: equal quantities (registry
    quantity equal, or generic with the same token; standard names are not
    compared) or the registered observed/prediction pair, **`water_level`
    (observed) with `water_level_prediction` (predicted), in that order**. The
    registry has no other such pair; add one in `compatible_quantities`.
  - Then unit known (`unit_unknown`), units equal (`units_differ`, no silent
    conversion), not a temperature, then datums.
  - Datums: if the observed quantity is `datum_applicable` (compatible quantities
    share the answer), both datums must be engaged and equal.
  - The output quantity is **`difference`**, the common unit, **no datum**, label
    `<obs> U+2212 <pred>` (UTF-8 bytes `"\xE2\x88\x92"`; sources carry no
    non-ASCII literals). So a residual can feed a residual (two differences are
    the same quantity), cannot be given a datum and cannot be shifted
    (`UnknownSourceDatum`).
  - Equal times only; a time in one series is dropped; no common time gives an
    empty series, not an error. The output axis is built through the keyed
    constructor (a subset of a strictly increasing axis is strictly increasing).
- **`scale_offset` takes a `Calibration`**, not an `Affine`: an own type with a total
  factory `Calibration::make({.scale, .offset}) -> optional<Calibration>` that
  rejects NaN and infinities (the `Sample::of` pattern; zero is allowed) and a
  default that is the identity. `affine()` hands the pair to the shared mapping.
  The metadata is unchanged: it is a calibration, not a unit conversion; recording
  it in the label was considered and left out (the caller can `with_label`).
  `timeseries.hpp`'s comment on `transform_samples` no longer says that
  `scale_offset` keeps the metadata coherent.
- **`shift_time` is limited only by 64-bit overflow.** `shift_time(s, 0ms) == s` for
  every legal `TimeSeries`; the ±2^53 ms file bound stays with `StationTable::make`.
  The first overflowing index is found without a scan: the times increase, so for
  `dt < 0` only a prefix can underflow (the front decides) and for `dt > 0` only a
  suffix can overflow (`partition_point`). Nothing is changed on error.
- **`Bucket` / `ValueSummary` (one fact, one optional).**
  - A `Bucket` is `{missing, dry, optional<ValueSummary>}`. A `ValueSummary` exists
    only when there is at least one value and holds `count`, `min`, `max`
    (first of equal), `first`, `last` (earliest and latest value) and the sum.
    `Bucket::values()` is derived from it. It has no default constructor and only
    `Bucket` builds one.
  - `+` joins the optionals with `detail::join_optional`. `min`/`max` use
    `ranges::min`/`ranges::max` with the `&Extreme::value` projection (both return
    their first argument on a tie, so the left operand wins); `first` is the left
    operand's, `last` the right's. Identity and associativity laws are exact on
    dyadic data, including `first`/`last`.
  - **The sum is carried as Σ v·2^-64** (exact scaling by a power of two). `mean()`
    is `(scaled_sum / n)·2^64` and `sum()` is `scaled_sum·2^64`. A merge can then
    neither overflow nor make a NaN (1e308 + 1e308 − 1e308 − 1e308 is exactly 0 in
    every grouping), and `mean()` is finite for any finite values. Where the plain
    left-fold sum is finite, `sum()` and `mean()` are bit-identical to it and to
    `sum / n` (tested on random data). `sum()` is infinite when the true sum is out
    of range. Caveat: values smaller in magnitude than about 1e-288 lose bits in
    the scaling.
  - `Bucket::of` is built with `Sample::visit`, so it is not `noexcept`
    (`std::visit` can throw `bad_variant_access`; a `noexcept` would trip
    `bugprone-exception-escape`). `operator+` is `noexcept`.
- **`QuickStats`** is `{missing, dry, optional<ValueStats>}`; `ValueStats` is
  `{count, min, max, mean}` and `QuickStats::values()` is `stats ? count : 0`. The
  design's `{values=0, stats=…}` cannot be written. There is no second pass for the
  mean (the old overflow fallback is gone).
- **`extent`.** nullopt iff the series is empty. A series of only Missing/Dry has
  `first`/`last` and `values == nullopt`. `combine` is renamed **`join`**; it is
  `detail::join_optional` over `join_extents`, with `ranges::min`/`max`. The
  many-series overload uses `std::transform_reduce` over a span; the join is exact,
  so the grouping cannot matter. `extent` and `quick_stats` use `optional::transform`.
- **Index windows (decimation readiness).**
  - `index_window(span<const Time>, TimeRange) -> Window{lo, hi}` is the two
    `lower_bound`s; `slice` is `index_window` plus a copy (the rvalue overload
    erases in place).
  - `summarize(span<const Time>, span<const Sample>)` is the one ordered left
    fold (`std::accumulate` over `iota` indices; equal lengths are a
    precondition), and `summarize(const TimeSeries&)` calls it. So a pixel column
    summarizes a sub-span with no copy.
- **One mapping path.** `detail::mapped(TimeSeries, Affine)` (in
  `series_rebuild.hpp`) takes the parts and maps the values; **the identity
  `Affine` is recognized there, once**, and leaves the samples untouched (so `-0.0`
  keeps its sign and there is no pass). `convert`, `scale_offset` and `shift` all
  go through it and through `detail::assembled` (the keyed constructor); a result
  that is not finite becomes Missing (`finite_or_missing`), never infinite. There
  is **no warning** for that (decision): Missing is the answer.
- **`convert` to an equal unit** returns the series unchanged (equal `OtherUnit`s
  too). The table overload takes a `ColumnIndex` (WP2's strong index; out of range
  is a precondition). `StationTable` gained two keyed members:
  `rewrite_column_unit(CoreKey, ColumnIndex, Unit) &&` (the unit only; it rebuilds
  the schema entry with `SeriesMeta::rewrite_unit`) and
  `transform_column(CoreKey, ColumnIndex, f) &&`. `convert` skips `transform_column`
  for the identity map.
- **`SeriesMeta::rewrite_datum`** returns `optional<DatumRewrite{from, meta}>`, so
  the old datum and the new metadata come together (defined after `SeriesMeta`,
  `meta.hpp`).
- **`shift` (datum).** `ShiftError = variant<UnknownUnit, NotALengthSeries,
  UnknownSourceDatum, MissingOffset>` (`UnknownUnit` is `units.hpp`'s).
  - Pinned order: the series has a unit (`UnknownUnit`), the unit is a
    `LengthUnit` (`NotALengthSeries`), a datum is engaged (`UnknownSourceDatum`),
    `from == to` returns the series, then `table.offset(from, to)`
    (`MissingOffset` names `from` before `to`). A missing offset is an error, never
    zero.
  - The offset is `h[from] − h[to]` in metres, converted to the series' unit with
    `Length::as(unit)`, and added. A level at MSL expressed in MLLW at The Battery
    gains +0.77527 m. Pinned against the F12 rows.
  - The rewritten metadata is computed before the `from == to` test: one metadata
    copy on the identity path, in exchange for reading `from` and the new metadata
    from one fact.

## Deferred

- **A `DatumTable` that carries its station** (review Q2) is deferred to Phase 3.
  Today the caller pairs a table with the right series.

## Phase 5 decimation recipe (not implemented here)

What WP3 provides for min/max decimation of a long series to W pixel columns over
`[t0, t1)` (M4: first, min, max, last per column):

1. `column_edges`: the edge of column k is `t0 + q·k + (r·k)/W` with
   `span = t1 − t0`, `q = span / W`, `r = span % W` (integer division, `k` from 0
   to W). Every product stays below `span` or `W²`, so nothing overflows
   (`span·k` would).
2. `edge_indices`: for each edge, `lower_bound` in `times` (the `lo` of
   `index_window`), so column k is `[edge_k, edge_{k+1})` of indices. One pass, since
   the edges increase.
3. Segmented `summarize`: `summarize(times.subspan(lo, hi − lo),
   samples.subspan(lo, hi − lo))` per column, with no copy. Each result is a
   `Bucket`; an empty column is the identity `Bucket{}`.
4. Emit per column from `summary()`: `first`, then `min` and `max` ordered by
   their time (one point if they are the same sample), then `last`; consecutive
   duplicates (same time) are dropped. A column with `has_gap()` (or an empty
   column between two non-empty ones) draws a break: end the polyline there and
   start a new one at the next non-empty column.
5. Autoscale the y axis with one `std::accumulate` of the buckets by `join` on
   `{min, max}` (or `extent` of the visible window); no second pass over the
   samples.

## Portability

No `views::zip`, `chunk_by`, `pairwise`, `fold_left`, `enumerate`, `ranges::to` or
`join_with` is used, and no constant evaluation of a variant that holds a
`std::string`. Used beyond WP2's list: `std::accumulate` over an `iota` view of
indices; `std::transform_reduce` over spans; `std::ranges::lower_bound` with an
iterator pair, `partition_point`, `find_if`, `transform`, `copy_if`, `generate`,
`max_element`, `min_element`, `ranges::min`/`max` with a projection;
`std::partial_sum`; `optional::transform` and `expected::transform/and_then/
transform_error` (`extent`, `quick_stats`, `residual`, `convert`); hexadecimal
floating literals (`0x1p64`).

## Tests

- `mov_core_ops_tests` (+ `_constexpr`, `_relaxed_constexpr`) is appended to
  `tests/core/CMakeLists.txt`; fold it into `mov_core_tests` if one executable per
  module is wanted. `src/core/CMakeLists.txt` gets a trailing `target_sources`.
- `tests/core/series_ops_helpers.hpp`: series builders, the seeded generator
  (`std::seed_seq`), the random dyadic row generator (multiples of 0.25, so sums
  are exact and `==` is the right check; built with `generate`, `partial_sum`,
  `transform`), and accessors that return optionals (`min_of`, `max_of`,
  `first_of`, `last_of`, `sum_of`, `mean_of`) so the tests never dereference an
  unchecked optional. `tests/core/datum_fixture.hpp`: the F12 reader, shared with
  `test_datum.cpp`.
- Exercised through the public operations: the keyed `TimeSeries` constructor
  (`slice`, `shift_time`, `residual`, `convert`, `scale_offset`, `shift`),
  `into_parts`, `SeriesMeta::rewrite_unit` and `rewrite_datum`, and
  `StationTable::rewrite_column_unit` and `transform_column`.
  `TimeSeries::transform_samples` is no longer used by the core and is still
  untested.
- Laws: `Bucket` identity exact; associativity exact on dyadic data (all fields,
  `first`/`last` included) and within 1e-9 absolute on arbitrary doubles in
  [-1000, 1000] (counts, extremes, `first`/`last` exact); ties keep the left
  operand; the peak equals `ranges::max_element` over the values on 200 random
  series; `mean()` is bit-identical to the plain sum over n on 200 random series;
  the ±1.7e308 groupings are equal, finite and zero, in a runtime test and as
  `STATIC_REQUIRE`s; `Extent` `join` is checked for associativity, commutativity,
  idempotence and identity over a pool of 13 extents.
- Mutations tried by hand and caught: tie rule `<` to `<=`; `slice` end inclusive;
  datum offset sign flipped; `units_differ` check removed; `first` taking the
  right operand's; the `shift_time` overflow limit off by one.

## Unverified

- macOS (Apple Clang + libc++) and Windows (MSVC): not run. In particular
  `optional::transform` and `expected::transform_error` on libc++ 17, and hex-float
  literals on MSVC.
- No parser here, so no fuzz target.
