# WP2 notes (Vocabulary II)

Fold into `docs/core-design.md`, then delete this file. Everything here is a
choice the design left open or got wrong, including the changes from the
post-WP2 review (ben-deane, sean-parent) and the maintainer's answers to it
(Q1-Q6). Headers: `meta.hpp`, `timeseries.hpp`, `station.hpp`,
`station_table.hpp`, `vector_series.hpp`, `detail/core_key.hpp`,
`detail/utf8.hpp`; private `src/core/core_access.hpp`. `detail/ascii.hpp`
gained `to_upper`; `units.hpp` gained `degree()` and
`detail::unit_of_nonblank`.

## Cross-cutting

- **`detail::` is the fence.** Names in `mov::core::detail` are not API, even
  when a public header must declare them.
- **One passkey:** `detail::CoreKey` (`detail/core_key.hpp`). Only
  `detail::CoreAccess` can make one, and `CoreAccess` is defined in
  `src/core/core_access.hpp`, which is not on any other layer's include path. Every
  bypass of a class's own checks takes a `const CoreKey&`:
  - `TimeSeries(CoreKey, times, samples, meta)`: parts whose invariant the caller
    established, asserted in debug builds. `StationTable::series` and the vector
    series use it; WP3's `slice`/`shift_time` should too.
  - `TimeSeries::transform_samples(CoreKey, f)` (Q6): the public sample-changing
    operations arrive in WP3 as `convert`/`scale_offset`, which keep the metadata
    coherent with the values.
  - `SeriesMeta::rewrite_unit(CoreKey, Unit)` and
    `rewrite_datum(CoreKey, VerticalDatum) -> optional<SeriesMeta>` (nullopt if
    no datum is engaged) for WP3's `convert`/`shift`.

  The keyed members are inline and **not tested in WP2**; tests cannot include the
  private header. The keyed constructor is covered through `series()` and the
  vector series. WP3 adds `#include "core_access.hpp"` in its `.cpp` files.
- **Rvalue views:** views into owned storage delete the `const&&` overload, not
  `&&`. With only `&& = delete`, a const rvalue still binds to `const&` and
  dangles. WP1's `GenericQuantity::token`/`standard_name` and `OtherUnit::symbol`
  were aligned (Q14).
- A `requires { ... }` expression outside a template is ill-formed, not false,
  when it names a deleted function. The tests wrap such checks in concepts.
- **Portability (macOS/Windows CI):** WP2 uses no `views::zip`, `chunk_by`,
  `pairwise`, `enumerate`, `fold_left`, `ranges::to` or `ranges::iota`.
  - `points()` is `views::iota | views::transform(detail::PointAt)` yielding
    `Point`s.
  - Descents are a `transform_reduce` over the shifted range.
  - Runs of equal times are found with `find_if` (a hand-rolled `chunk_by`).
  - `StationSelection::all` uses `ranges::copy(views::iota(...))`.
  - Pairwise and `chunk_by` can replace these once the probe confirms them on
    libc++ and MSVC.
- **Narrow errors:** each function has its own error type:
  - `AssumeUnitError {already_set}`, `AssumeDatumError {already_set, not_applicable}`;
  - `ConstructionError = variant<LengthMismatch{times, samples}, TimeNotIncreasing{index}>`;
  - `TableError` (below);
  - `VectorErrc`, `VerticalErrc`;
  - `SelectionError`;
  - `StationTextError`, `StationKeyError`, `StationIdError`.

  The design's `MetaError` and `AlignmentErrc` are gone. `residual` (WP3)
  defines its own.
- **No unwraps:** WP2 sources contain no `value_or` fallbacks. `canonical_unit`
  (quantity.cpp) is total: it uses `detail::unit_of_nonblank`, and a
  `static_assert` proves every registry spelling non-blank. The degree unit is the
  one named function `degree()`, pinned by a test against `parse_unit` and its
  aliases. It is a function, not a constant: an `OtherUnit` holds a
  `std::string`.

## meta

- **`SeriesMeta::make` is total.** `Fields` is `{quantity, label, unit}` and has no
  datum. A datum enters only through `assume_datum`, so the C3 invariant
  (`datum_applicable`) is checked in one place.
- `assume_unit` returns `already_set` whenever a unit is engaged, even if it is the
  same unit. `assume_datum` checks `already_set` first, then `not_applicable`.

## timeseries

- `using TimeAxis = std::vector<Time>;`.
- `detail::first_not_increasing(span<const Time>)` (constexpr) is behind every
  strictly-increasing check: `make`, the `StationTable` axis check and the debug
  assert.
- `make` does not bound times to ±2^53 ms; `StationTable` does (Q5, below).
- `with_samples(samples, meta) const&/&& -> expected<TimeSeries, LengthMismatch>`:
  same axis, O(1) length check, for derived series outside the core.
- `into_parts() && -> TimeSeriesParts{times, samples, meta}`.
- `assume_*` have `const&` and `&&` overloads; `const&` copies only on success.
- `normalize` is a friend (private constructor). Behaviour:
  - Fast path: `adjacent_find(not (a.time < b.time)) == end` takes the rows as they
    are.
  - Otherwise it counts descents, `stable_sort`s only if there is a descent, and
    sums `DedupCounts{dropped, conflicting}` (a monoid under `+`) over the runs of
    equal times. `ranges::unique` then keeps the first of each run, and the result
    is unzipped into the two vectors.
  - The report keeps the design's names: `descents`, `duplicates_dropped`,
    `conflicting_duplicates`.
- `fuzz_normalize` (seeds `tests/fixtures/core/normalize/`): 3 bytes per row.
  Oracle:
  - the output passes `make` and equals "first sample per time";
  - all three counts are exact;
  - `clean()` iff nothing changed, and then output == input;
  - re-normalizing the output is clean and identical.

## station

- **Station text types (Q1).**
  - `StationText`: well-formed UTF-8 without NUL, may be empty; error
    `StationTextError {embedded_nul, invalid_utf8}`.
  - `StationKey`: non-empty `StationText`; error
    `StationKeyError {empty, embedded_nul, invalid_utf8}`.
  - Both are constexpr, ordered, and built only by `make`.
  - `FileStation{StationKey id; StationText name; ...}` and
    `GaugeStation::name` use them.
  - The error enumerators name the violated property. The field is known to the
    caller: a reader that builds a key from the id column reports the id.
  - Lenient sources clean bytes before `make` (C14).
  - Both `make`s use explicit branches, because GCC 14 cannot constant-evaluate
    `expected::transform` on a small `std::string`.
- **`StationId<P>` holds a `StationKey`** (`key()`). `make` trims, canonicalizes,
  validates the provider grammar, then builds the key. A grammar that admits a
  non-key would give `invalid`; that branch is unreachable for the four providers.
- **`Provider` concept:** requires `source`, `canonical(string_view) -> string` and
  `valid_id`. `canonical` replaces the earlier `upper_case_ids` flag:
  - NDBC upper-cases everything.
  - **USGS upper-cases the agency prefix only** (Q4): `usgs-07374000` →
    `USGS-07374000`, and the grammar now needs `[A-Z0-9]+` before the dash.
  - XTide additionally requires valid UTF-8.
- **DataSource tokens** are `{source, token}` rows. A `static_assert` checks
  enumerator order, and a test pins all seven tokens.

## station_table

- **Strong indices.** `StationIndex` and `ColumnIndex` are explicit, have
  `.value()`, are ordered and default to 0. They index `station`, `times`,
  `column`, `series` and `vector_series`.
  - `column_of(const QuantityId&) -> optional<ColumnIndex>` (keyed on token).
  - `stations()` is an iota range of `StationIndex`.
- **Variable-major input (Q2).** `make(vector<Variable>, vector<TimeAxis>,
  vector<StationRow>)`, where `Variable{SeriesMeta meta; vector<Column>
  per_station}` and `StationRow{FileStation station; size_t axis}`. Storage is
  `[column][station]`.
- **`TableError = variant<SchemaError, StationError>`.**
  - `SchemaError{SchemaErrc code, ColumnIndex column}`: `duplicate_quantity` (keyed
    on token: two `GenericQuantity`s with one token collide; so do entries that
    differ only by label or unit) or `station_count_mismatch`
    (`per_station.size() != stations`).
  - `StationError{StationIndex station, StationFault fault}`, where `StationFault`
    is one of `DuplicateStationId`, `AxisOutOfRange`, `TimeOutOfRange{index}`,
    `TimeNotIncreasing{index}`, `ColumnLengthMismatch{ColumnIndex}` or
    `SchemaMismatch` (from_series).
  - The text errors (`empty_station_id`, `embedded_nul`, `invalid_utf8`) are gone:
    the types rule them out.
- **Check order:**
  - Each variable in order: duplicate token, then station count.
  - Then each station in order: duplicate id, axis index, axis contents, column
    lengths.
  - Axis contents: the first element that is out of range or not above its
    predecessor. The error names the first station using the axis; axes no
    station uses are dropped unchecked and the pool is renumbered.
- **Equality is by value**, independent of how the axes are pooled.
- **`from_series(SeriesMeta schema, vector<AtStation<FileStation, TimeSeries>>)`.**
  - Moves the series in via `into_parts`.
  - A series whose meta differs from the declared schema is
    `StationError{i, SchemaMismatch}`.
  - No series gives the schema with no stations.
  - The conversion is partial (Q5): a `TimeSeries` may hold times beyond the file
    bound ±2^53 ms (SN §7), which is `TimeOutOfRange`.
- **`StationSelection`** stores `station_count()`, which is part of `==`.
  - `applies_to(n)` returns `selection_mismatch` in O(1) when the selection was
    made for another count.
  - An empty selection is valid.
  - `out_of_range` is reported before `duplicate_index`.
- `total_samples()` = Σ over stations of `axis length × schema size`.
- `detail::is_valid_utf8` is strict per Unicode table 3-7: no overlongs,
  surrogates or code points above U+10FFFF.

## vector_series

- **`make` accepts the registered pairs only:** `(current_u, current_v)` and
  `(wind_u, wind_v)`, in that order. **`assume_components(u, v)`** is the visibly
  named way to pair two generic series, and refuses registry quantities. Both
  check the pair, then the times, then the unit (`unit_unknown`, then
  `units_differ`). **Datums are not compared** (Q3).
- **`VectorSeries` stores what make proved:** `kind()` (`VectorKind {current,
  wind, generic}`) and `unit()`.
- **Labels.** The derived-meta table of §2.9 holds. The "stem" is:
  - `wind` or `current` for the registered pairs;
  - for generic pairs, the trimmed `u` label without a trailing separator
    (` `, `_`, `-`) + `u`/`U`/`x`/`X`;
  - `"vector"` when that leaves nothing.
- **`cartesian_direction`:** `atan2(v, u)`, and an exact `-π` becomes `π` before
  conversion to degrees. A zero vector is `Missing`. Missing and Dry follow
  `combine` (N7). The unit is `degree()`.
- **`magnitude3(const VectorSeries& horizontal, const TimeSeries& w)`.**
  - Errors are `VerticalErrc {not_generic, times_differ, unit_unknown,
    units_differ}`.
  - The value is `std::hypot(u, v, w)` (three arguments; pinned with 2, 3, 6 → 7)
    under the combine rule.
  - The label is `"3D <stem> speed"` (`"3D current speed"`, `"3D wind speed"`,
    `"3D flow speed"`), so a generic pair is not called a current.
- `vector_series(table, StationIndex, ColumnIndex, ColumnIndex)` goes through
  `make`. Indices out of range are a precondition, as for the table accessors.
