# WP2 notes (Vocabulary II)

Fold into `docs/core-design.md`, then delete this file. Everything here is a
choice the design left open or got wrong. The declarations otherwise match
§2.6-2.9. Headers: `meta.hpp`, `timeseries.hpp`, `station.hpp`,
`station_table.hpp`, `vector_series.hpp`, `detail/utf8.hpp` (+), and
`detail/ascii.hpp` gained `to_upper`.

## All WP2 types

- Views into owned storage delete the **`const&&`** overload, not `&&`. With only
  `&& = delete`, a `const` rvalue still binds to the `const&` overload and dangles
  (`std::move(const_series).times()`). `const&& = delete` catches both rvalue kinds;
  `tests/core/test_*_constexpr.cpp` pin it (`SeriesViews<const TimeSeries>` is false).
  The WP1 types (`GenericQuantity::token`, `OtherUnit::symbol`) still use `&&`;
  aligning them is a one-line change each.
- A `requires { ... }` expression outside a template is ill-formed, not false, when
  it names a deleted function (GCC rejects the program). The tests wrap such checks
  in small concepts (`TableViews<T>`, `HasValue<T>`).
- Value-type checks per §7: `std::regular` + nothrow move where a default exists;
  `copyable` + `equality_comparable` for `StationId`, `GaugeStation`, `FileStation`,
  `StationRow`, `StationSelection`, `VectorSeries` (no default, because `Location`
  has none or because an empty value would be invalid). `Point`, `NormalizeReport`,
  `ConstructionError`, `TableError` are also trivially copyable.

## meta

- `SeriesMeta::Fields` members all have default member initializers, so any subset
  can be given by designator; `Fields` has `==` (+). The validating constructor is
  private; `SeriesMeta{}` is the only other way in.
- `assume_unit` returns `already_set` whenever a unit is engaged, even if it is the
  same unit (an engaged unit changes only through `convert`). `assume_datum` checks
  `already_set` first; the order cannot matter, since an engaged datum implies
  `datum_applicable`.
- `with_label` has `const&` and `&&` overloads.
- The C3 passkey is `detail::MetaRewrite`, constructible only by
  `detail::SeriesOpsAccess` and `detail::DatumShiftAccess`, which are declared here
  and **defined by WP3** in `series_ops.cpp` / `datum_shift.cpp`. The members it
  unlocks are `rewrite_unit(key, Unit) -> SeriesMeta` and
  `rewrite_datum(key, VerticalDatum) -> expected<SeriesMeta, MetaError>`
  (`datum_not_applicable` keeps the invariant even for a caller holding the key).
  They are inline and **untested in WP2** (no test can build the key); WP3 tests them.

## timeseries

- `ConstructionError::index`: for `length_mismatch` the shorter length; for
  `time_not_increasing` the first `i` with `not (t[i-1] < t[i])`. The length is
  checked first. `make` does not bound times to ±2^53 ms; `StationTable::make` does
  (`time_out_of_range`), because only the file formats need the bound.
- `assume_unit`, `assume_datum` and `transform_samples` have `&&` overloads (+) as
  well as `const&`, so a reader's temporary is not copied. `transform_samples`
  requires `std::invoke_result_t<F&, Sample>` to be exactly `Sample`.
- `points()` returns the named type `TimeSeries::PointsView` (a `zip_view` of two
  `ref_view`s), so its rvalue overload can be deleted (a deleted function with a
  deduced return type is not usable).
- `detail::trusted_series(times, samples, meta)` (+) is the one core-internal way to
  build a `TimeSeries` without rechecking: `normalize`, `StationTable::series` and
  the derived vector series use it, and WP3's `slice`/`shift_time` can. It asserts
  the invariant in debug builds. It is declared in the public header (it must be
  named as a friend) but is `detail`.
- `NormalizeReport` keeps the design's field names: `descents` (adjacent input pairs
  with `t[i+1] < t[i]`), `duplicates_dropped`, `conflicting_duplicates`. The WP2
  brief called the first one "out_of_order"; the design name was kept. `clean()` is
  `descents == 0 and duplicates_dropped == 0`.
- `normalize`: fast path = `adjacent_find(not (a.time < b.time)) == end`, which
  takes the rows as they are. Otherwise descents are counted, the rows are
  `stable_sort`ed only if there is a descent (non-decreasing input with repeats is
  not sorted), and of each run of equal times the first in input order is kept.
  `Normalized` gets `==` (+).
- Fuzz target `fuzz_normalize` (seeds `tests/fixtures/core/normalize/`): 3 bytes per
  row (signed time byte, tag byte → Missing/Dry/value, signed value byte). Oracle:
  output passes `TimeSeries::make`; it equals the map "time → first sample"; the
  three counts equal independently computed ones; `clean()` iff nothing changed, and
  then output == input; normalizing the output again is clean and identical.

## station

- `Provider` also requires `P::upper_case_ids` (bool): `make` upper-cases before
  validation iff it is set (only `Ndbc`). The design said "NDBC upper-case" without
  a mechanism; a trait keeps the concept open instead of special-casing `Ndbc`.
- `StationIdError { empty, invalid }`: `empty` when nothing is left after trimming
  ASCII whitespace, `invalid` when the provider grammar fails. `make` is
  `constexpr` (tested with `STATIC_REQUIRE`).
- Grammars as §2.8. USGS ids are not case-folded. XTide: non-empty, at most 255
  bytes, no byte below 0x20 and no 0x7F; any other byte (UTF-8 included) passes,
  without a UTF-8 check.
- `parse_data_source` is exact and case-sensitive, like `parse_quantity_token`.
- `GaugeStation` and `FileStation` get `==`; neither is default-constructible.

## station_table

- `StationSelection` lives in `station_table.hpp`. An empty selection is valid
  (it reads no station; the SN writer then reports `empty_collection`).
  `out_of_range` is checked over all indices before `duplicate_index`; the error
  enum carries no index (design).
- `TableError` payloads: `duplicate_quantity` has `index` = the later schema entry;
  station-level codes have `station` = the row (`duplicate_station_id`: the later
  row); axis codes have `station` = the **first row using that axis** and `index` =
  the element; `column_length_mismatch` has `index` = the column. Check order is in
  the header (schema; then per row: id/name text, duplicate id, axis index, axis
  contents, column count, column lengths).
- Schema uniqueness keys on `token(quantity)` (WP1 decision): two
  `GenericQuantity`s with one token and different standard names collide; so do
  two entries that differ only by label or unit.
- Station text: `id` must be non-empty; `id` and `name` must not contain NUL and
  must be well-formed UTF-8 (`detail::is_valid_utf8`, constexpr, Unicode table 3-7:
  no overlongs, surrogates or code points above U+10FFFF). An empty `name` is
  allowed (the SN writer substitutes "Station <id>"). Ids compare bytewise.
- Axes no station references are **dropped without being checked**, and the pool is
  renumbered in order of first use. There is no error code for an unused axis, and
  keeping it would only waste memory.
- **Equality is by value, not defaulted**: schema, then per station the
  `FileStation`, its times and its columns. A table whose stations share one axis
  equals one that stores the same times twice. A defaulted `==` would make pooling
  observable (e.g. `from_series` vs an L1 reader).
- `total_samples()` = Σ over stations of `axis length × schema size` (the number of
  `Sample` cells).
- `from_series`: an empty input gives the empty table. `schema_mismatch` compares
  the whole `SeriesMeta` (labels included) against the first series. Each series
  gets its own axis; times and samples are copied, since `TimeSeries` has no way to
  release its vectors. Then `make` runs its checks.

## vector_series

- Pairs: `(current_u, current_v)`, `(wind_u, wind_v)` and `(generic, generic)`, as in
  §2.9. Swapped components and mixed pairs are `not_a_vector_pair`. The WP2 brief
  says "restricted to registered component pairs"; the generic pair is the only
  non-registry one and is kept because foreign and IMEDS components are generic.
- Check order: pair, times, units (`unit_unknown` if either is unset, then
  `units_differ`), datums. The design gave no datum rule for vectors: both unset or
  both set and equal pass; exactly one set is `datum_unknown`; both set and
  different is `datums_differ`. Only generic components can carry a datum.
- `temperature_difference` is declared but unused here (it is `residual`'s, WP3).
- Derived-meta "stem": `"wind"` for the wind pair, `"current"` for the current pair;
  for a generic pair the trimmed `u` label without a trailing separator
  (` `, `_`, `-`) + component letter (`u`, `U`, `x`, `X`), so `"velocity u"` →
  `"velocity"`, `"flow_X"` → `"flow"`, `"menu"` stays; an empty result (or the
  label `"u"`) becomes `"vector"`. Direction unit: `parse_unit("degree")`.
- `cartesian_direction`: `atan2(v, u)`; a result of exactly `-π` is replaced by `π`
  before converting to degrees, so `(-1, -0.0)` and `(-1, -1e-300)` give the same
  value as `(-1, 0)`. A zero vector (both components ±0) is `Missing`; Missing and
  Dry follow `combine` (N7).
- `magnitude3(x, y, z)`: `(x, y)` must be a current or generic pair (a wind pair is
  `not_a_vector_pair`, since the label is "3D current speed") and `z` generic (no
  registry quantity for vertical velocity); then the alignment checks for `(x, y)`
  and `(x, z)`. Computed as `hypot(hypot(x, y), z)` through `combine`.
- `vector_series(table, i, ku, kv)`: out-of-range indices are a precondition, like
  the table accessors.
- `VectorSeries` gets `==` (+) and has no default constructor.
