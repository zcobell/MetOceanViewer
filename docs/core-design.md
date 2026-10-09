# MetOceanViewer v5 — Phase 2 design: `mov::core` and `mov::io`

Status: **revision 2**, after review by ben-deane, sean-parent and neckbeard-nate
(2026-10-07). It is implemented test-first, in the work packages of §8. Appendix A
maps each review finding to its resolution.

Inputs:
- `docs/rearchitecture-plan.md` (plan; decisions D1–D27)
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
| C2 | Time order | `TimeSeries` times are **strictly increasing** (owner, SN-Q6). Text readers call the total `normalize(vector<Point>)`, which stable-sorts, keeps the first duplicate, and reports through warnings. netCDF readers call `make` and reject disorder. |
| C3 | Meta | `SeriesMeta` is validated at construction: a datum may be engaged only if `datum_applicable(quantity)` holds (a datum-carrying registry quantity or a generic one). An engaged unit or datum changes only through `convert` or `shift`. `with_label`, `assume_unit` and `assume_datum` exist; the `assume_*` functions apply only when the field is unset. |
| C4 | File records | `StationTable` (core) is the record every file reader returns and every file writer takes. It has an ordered unique schema of `SeriesMeta`, an axis pool, and per station one axis index plus one sample column per schema entry. Model output has one shared axis, so no time vector is duplicated per station. |
| C5 | Positions | Projected to WGS84 `Location` at the read boundary (`FileStation.location`). The native point is kept as optional metadata. |
| C6 | Units | `Measure<UnitEnum>` (Length, Speed, Pressure, Discharge) in SI. The runtime `Unit` variant is built only through `parse_unit` (`""` → `nullopt`). The affine `Temperature` value type is deferred to Phase 3; `TemperatureUnit` stays as a `Unit` alternative. |
| C7 | Quantities | `QuantityId = variant<Quantity, GenericQuantity>`. The registry is SN §6. `GenericQuantity` keeps the token and standard name and can only be built by parsing. |
| C8 | Errors | Errors are narrow per function inside core. io and each layer above have **one** error variant (`io::Error`). Provided: a generic `lift<V>` and `Read<T>` (value plus warnings) as a writer monad (`transform`, `and_then`, `and_then_read` over `expected`). `describe()` is called only at the edge. |
| C9 | Dry and sentinel rules | D16 (`raw <= -999` → `Dry`) applies **only to model-output readers**: ADCIRC elevation (ASCII and nc) and HWM modeled values. IMEDS masks only the **exact** legacy sentinels −99999, −9999, −DBL_MAX and −1.7977e+308 (v4's printed −DBL_MAX, N18), giving `Missing` plus a count warning. ADCIRC non-elevation outputs: `raw <= -999` → `Missing` (fill, N7 partner rule). DFlow: no dry rule. This **deviates from SN:252**; see §9.2. Fortran `NaN`/`Inf`/`****` tokens in model text → `Missing` plus a count warning. |
| C10 | HWM | Moments monoid with private state, folded left in order. σ uses `n−1` (D17). R² through the origin is **uncentred**, `1 − SSres/Σy²` (D25). `LinearFit = variant<ThroughOrigin, Free>`. |
| C11 | Threading | **No mutex in io.** io's netCDF functions are documented as not thread-safe. The **caller serializes**: providers and app own one serial queue (`QThreadPool` with `maxThreadCount(1)` plus `QtConcurrent::run(&pool, …)`). Every `nc_*` call goes through `detail::nc_call`, which asserts against concurrent or re-entrant entry in debug builds. Reads use bounded slabs and check a `std::stop_token` between slabs. |
| C12 | Sizes and names | Every count is computed with `checked_product`. `ReadLimits` (elements, attribute bytes, text bytes) is enforced, and exceeding it gives `too_large`. Selection is required (`StationSelection`), never defaulted to all. Every netCDF name parameter is an `NcNameRef` (non-empty, NUL-terminated, at most `NC_MAX_NAME` bytes, no embedded NUL; literals are checked `consteval`). |
| C13 | Time arithmetic | Every file time goes through `checked_time` (double and int64 overloads). It rejects non-finite values, offsets beyond 2^53 ms and overflow. `CfTimeUnits` stores an integer unit in milliseconds. |
| C14 | Ids and names | ADCIRC and DFlow ids are the 0-based index. A station without a name keeps an empty one: no reader makes one up, a display shows the id, and only the station netCDF writer substitutes `"Station <id>"`. ADCIRC netCDF reads `station_name`. Legacy sources cut names at the first NUL, then apply `simplified()`. v5 CF files keep names exactly; an embedded NUL there is `bad_encoding`. Duplicate ids in lenient sources (IMEDS, legacy) get `#2`, `#3` suffixes plus a warning. |
| C15 | Station netCDF | Reads v5 (L1/L2; L3 and scalar-station as SHOULD), foreign CF-DSG and legacy A/B. CRMS dialect C → `not_this_format`. Writes v5 only. The `<q>_status` variable is written iff at least one sample of that column is `Dry` (approved). `vertical_datum` is written whenever the meta datum is engaged (§9.2). |
| C16 | Atomic writes | Unique temp file opened `NC_NOCLOBBER`/`O_EXCL` → body → close → fsync file → rename → fsync dir. A failed body calls `nc_abort` and removes the temp file. `File::create` is reachable only through `write_netcdf_atomic`. A fault can be injected at every stage. |
| C17 | IMEDS write (D15) | Values `{:14.6f}`, coordinates `{:.6f}`, times floored to whole seconds, non-values omitted. |
| C18 | CSV (D27) | Long format, one row per sample. Text cells starting with `= + - @ \t \r` get a `'` prefix (formula injection). Numeric and time cells, which the writer formats itself, are exempt. |
| C19 | Projection | `to_location` lives in **io** with PROJ as a private dependency (core stays standard-library only). Projection runs at the read boundary. |

---

## 1. Toolchain constraints

| Feature | Rule |
|---|---|
| Floating `from_chars` (Apple libc++ may lack it) | Only through `io::detail::parse_double`. Gate on `__cpp_lib_to_chars >= 201611L`; the fallback is `strtod_l`/`_strtod_l` with the C locale. Both paths are tested. |
| `std::chrono::parse` (missing in libc++) | Not used. Dates are parsed by hand with integer `from_chars` and `year_month_day`, all `constexpr`. |
| `views::zip`, `expected` monadic ops, `format`, `stop_token` | Allowed (GCC 14, LLVM ≥ 17 libc++, MSVC 17.10). |
| `ranges::fold_left`, `views::enumerate`, `views::chunk_by` | Allowed only after the WP1 probe passes. Ordered folds use `std::accumulate` meanwhile. |
| `constexpr` `<cmath>` (`llround`, `isfinite`, `fabs`; P0533) | WP1 probe. Where it is missing, the affected `STATIC_REQUIRE`s become runtime `REQUIRE`s behind a feature macro. |
| `constexpr` integer `from_chars` (`__cpp_lib_constexpr_charconv`) | Same fallback. |
| `std::function_ref` (C++26) | Not used; use constrained template parameters instead. |

`tests/core/test_toolchain_probe.cpp` (WP1) uses each gated feature once.

---

## 2. `mov::core`

Headers are in `src/core/include/mov/core/`, namespace `mov::core`. Every value type is
regular and nothrow-movable; §7.1 asserts both for each type with `STATIC_REQUIRE`.
`[[nodiscard]]` on factories and queries is omitted below for brevity. Views into owned
storage are `const&`-qualified, with `&&` overloads deleted.

### 2.1 Time — `time.hpp`

```cpp
using Time = std::chrono::sys_time<std::chrono::milliseconds>;     // UTC
inline constexpr std::int64_t max_abs_time_ms = (std::int64_t{1} << 53) - 1;   // SN §7 exact-double bound

class TimeRange {                                  // half-open [begin, end), begin < end
 public:
  static constexpr std::expected<TimeRange, TimeRangeError> make(Time begin, Time end) noexcept;  // enum: empty_or_inverted
  constexpr Time begin() const noexcept;
  constexpr Time end() const noexcept;
  constexpr bool contains(Time t) const noexcept;
  friend constexpr bool operator==(const TimeRange&, const TimeRange&) = default;   // no ordering
 private: Time begin_, end_;
};

class ValidRange {                                 // first: nullopt = unknown; last: nullopt = ongoing
 public:
  static constexpr std::expected<ValidRange, ValidRangeError>
  make(std::optional<std::chrono::sys_days> first, std::optional<std::chrono::sys_days> last) noexcept;   // enum: inverted
  constexpr std::optional<std::chrono::sys_days> first() const noexcept;
  constexpr std::optional<std::chrono::sys_days> last() const noexcept;
  friend constexpr bool operator==(const ValidRange&, const ValidRange&) = default;
 private: std::optional<std::chrono::sys_days> first_, last_;
};

/// epoch + value·unit_ms. nullopt if value is non-finite, |value·unit_ms| > max_abs_time_ms,
/// or |result| > max_abs_time_ms. The double path rounds with llround; the int64 path uses a checked multiply.
constexpr std::optional<Time> checked_time(double value, std::int64_t unit_ms, Time epoch) noexcept;
constexpr std::optional<Time> checked_time(std::int64_t value, std::int64_t unit_ms, Time epoch) noexcept;

/// "yyyy-mm-dd[( |T)hh:mm[:ss[.fff]]][Z]", always UTC. Used for ADCIRC cold start and session strings (N5).
struct DateTimeError { std::size_t column; friend constexpr bool operator==(DateTimeError, DateTimeError) = default; };
constexpr std::expected<Time, DateTimeError> parse_utc_datetime(std::string_view text) noexcept;
```

### 2.2 Units — `units.hpp` (D13)

```cpp
enum class LengthUnit : std::uint8_t { meter, foot, inch, kilometer, statute_mile, nautical_mile };
enum class SpeedUnit : std::uint8_t { meter_per_second, foot_per_second, knot, mile_per_hour, kilometer_per_hour };
enum class PressureUnit : std::uint8_t { pascal, hectopascal, millibar, meter_of_water };
enum class DischargeUnit : std::uint8_t { cubic_meter_per_second, cubic_foot_per_second };
enum class TemperatureUnit : std::uint8_t { celsius, fahrenheit };
// to_si: ft 0.3048 | in 0.0254 | mi 1609.344 | nmi 1852 | kt 1852/3600 | mph 1609.344/3600 | km/h 1000/3600
//        hPa = mb = 100 | mH2O 9806.65 (v4 9806.38: documented non-goal) | ft3/s 0.028316846592

template <class U>                                   // U ∈ {Length, Speed, Pressure, Discharge}Unit
class Measure {
 public:
  constexpr Measure() noexcept = default;            // zero
  static constexpr Measure in(double value, U unit) noexcept;
  constexpr double as(U unit) const noexcept;
  // + − (binary, unary), scalar * and /, Measure/Measure → double, <=> (partial order, by SI value)
 private:
  explicit constexpr Measure(double si) noexcept;
  double si_{};
};
using Length = Measure<LengthUnit>;
using Speed = Measure<SpeedUnit>;
using Pressure = Measure<PressureUnit>;
using Discharge = Measure<DischargeUnit>;

namespace detail { class UnitKey; }                  // passkey: constructible only inside units.cpp (parse_unit)
class OtherUnit {
 public:
  OtherUnit(const detail::UnitKey&, std::string canonical);
  std::string_view symbol() const& noexcept;
  std::string_view symbol() && = delete;
  friend bool operator==(const OtherUnit&, const OtherUnit&) = default;
 private:
  std::string symbol_;
};
using Unit = std::variant<LengthUnit, SpeedUnit, PressureUnit, DischargeUnit, TemperatureUnit, OtherUnit>;

std::optional<Unit> parse_unit(std::string_view text);   // "" or blank → nullopt; unknown → OtherUnit (aliases canonicalized)
std::string_view symbol(const Unit& u) noexcept;         // display ("m/s")
std::string_view symbol(Unit&&) = delete;
std::string_view udunits(const Unit& u) noexcept;        // netCDF text ("m s-1")
std::string_view udunits(Unit&&) = delete;
constexpr bool is_temperature(const Unit& u) noexcept;

struct Affine { double scale{1.0}; double offset{0.0}; constexpr double operator()(double x) const noexcept; friend constexpr bool operator==(Affine, Affine) = default; };
struct IncompatibleUnits { Unit from; Unit to; friend bool operator==(const IncompatibleUnits&, const IncompatibleUnits&) = default; };
struct UnknownUnit {};                                                             // series unit not set
using UnitError = std::variant<IncompatibleUnits, UnknownUnit>;
template <class U> constexpr Affine conversion(U from, U to) noexcept;                // same family; temperatures are affine
std::expected<Affine, IncompatibleUnits> conversion(const Unit& from, const Unit& to);  // OtherUnit: identity iff equal
```

`parse_unit` spelling table (pinned by test):

| Unit | Accepted spellings |
|---|---|
| metre | `m`, `meter(s)`, `metre(s)` |
| foot | `ft`, `feet`, `foot` |
| m/s | `m/s`, `m s-1` |
| knot | `knots`, `kn`, `kt` |
| other speeds | `mph`, `ft/s`, `km/h` |
| °C | `degC`, `deg C`, `°C`, `C` |
| °F | `degF`, `°F`, `F` |
| pressure | `Pa`, `hPa`, `mb`, `mbar`, `mH2O` |
| other lengths | `km`, `mi`, `nmi` |
| discharge | `m3/s`, `m3 s-1`, `ft3/s`, `ft3 s-1`, `cfs` |

`OtherUnit` aliases are canonicalized: `%` → `percent`, `deg`/`degT` → `degree`,
`sec` → `s`. Readers emit `unrecognized_unit` when the result is an `OtherUnit` that is
not in the canonical alias set.

### 2.3 Geography — `geo.hpp`

```cpp
struct LatLon { double lat; double lon; };               // Location::make({.lat = 29.98, .lon = -90.01})

class Location {                                          // WGS84; lat ∈ [-90,90]; input lon ∈ [-180,360] → (-180,180]
 public:
  static constexpr std::expected<Location, LocationError> make(LatLon p) noexcept;   // enum: latitude/longitude_out_of_range, not_finite
  constexpr double lat() const noexcept;
  constexpr double lon() const noexcept;
  friend constexpr bool operator==(const Location&, const Location&) = default;     // no ordering
 private: double lat_, lon_;
};

class Epsg {                                              // no default constructor: a CRS is always stated
 public:
  static constexpr std::expected<Epsg, EpsgError> make(int code) noexcept;   // code > 0 (B10)
  static constexpr Epsg wgs84() noexcept;
  constexpr int code() const noexcept;
  friend constexpr bool operator==(Epsg, Epsg) = default;
 private:
  explicit constexpr Epsg(int c) noexcept;
  int code_;
};

struct NativePoint { double x; double y; Epsg crs; friend constexpr bool operator==(const NativePoint&, const NativePoint&) = default; };
```

### 2.4 Samples — `sample.hpp`

```cpp
struct Missing { friend constexpr bool operator==(Missing, Missing) = default; };
struct Dry     { friend constexpr bool operator==(Dry, Dry) = default; };

class Sample {                                            // Missing | Dry | finite double
 public:
  constexpr Sample() noexcept = default;                  // Missing
  constexpr Sample(Missing) noexcept;
  constexpr Sample(Dry) noexcept;
  static constexpr std::optional<Sample> of(double v) noexcept;   // nullopt iff non-finite
  constexpr std::optional<double> value() const noexcept;
  constexpr bool is_value() const noexcept;
  constexpr bool is_dry() const noexcept;
  constexpr bool is_missing() const noexcept;
  template <class... F> constexpr decltype(auto) visit(F&&... f) const;
  friend constexpr bool operator==(const Sample&, const Sample&) = default;
 private:
  std::variant<Missing, Dry, double> v_;
};
constexpr Sample finite_or_missing(double v) noexcept;    // the ONE lossy total factory
/// Missing if either operand is Missing; else Dry if either is Dry; else finite_or_missing(op(a, b)).
template <std::invocable<double, double> Op> constexpr Sample combine(Sample a, Sample b, Op op) noexcept;
```

Cost (C1): 24 B per sample including the time. That is the same as
`optional<double>`, beats a separate dry mask (25 B) and has no illegal states. Model
files are read by selection (§5.4–5.5), so the overhead stays bounded.

### 2.5 Quantities — `quantity.hpp` (SN §6)

```cpp
enum class Quantity : std::uint8_t {
  water_level, water_level_prediction, air_temperature, water_temperature, dew_point,
  wind_speed, wind_direction, wind_gust, wind_u, wind_v, air_pressure, relative_humidity,
  conductivity, visibility, current_u, current_v, wave_height, wave_period_dominant,
  wave_period_average, wave_direction, discharge,
};
class GenericQuantity {                     // SN `value` and foreign tokens; identity = token
 public:
  static GenericQuantity value();           // token "value", no standard name
  /// token: CF name ^[A-Za-z][A-Za-z0-9_]*$ and not a registry token (else → nullopt; caller uses the registry entry)
  static std::optional<GenericQuantity> parse(std::string_view token, std::string_view standard_name);
  std::string_view token() const& noexcept;
  std::string_view standard_name() const& noexcept;
  friend bool operator==(const GenericQuantity&, const GenericQuantity&) = default;
 private:
  std::string token_, standard_name_;
};
using QuantityId = std::variant<Quantity, GenericQuantity>;

struct QuantityInfo { std::string_view token, standard_name, long_name; std::string_view canonical_unit; };  // canonical via parse_unit
constexpr QuantityInfo info(Quantity q) noexcept;                                   // SN §6 table
constexpr std::optional<Quantity> parse_quantity_token(std::string_view token) noexcept;
std::string_view token(const QuantityId& q) noexcept;
std::string_view token(QuantityId&&) = delete;
/// THE datum predicate (C3): water_level, water_level_prediction, or any GenericQuantity.
constexpr bool datum_applicable(const QuantityId& q) noexcept;
```

### 2.6 Series metadata — `meta.hpp` (C3)

```cpp
enum class MetaError : std::uint8_t { datum_not_applicable, already_set };

class SeriesMeta {
 public:
  struct Fields {                                    // designated-initializer input
    QuantityId quantity{GenericQuantity::value()};
    std::string label;
    std::optional<Unit> unit;
    std::optional<VerticalDatum> datum;
  };
  SeriesMeta();                                      // generic value, empty label, no unit, no datum
  static std::expected<SeriesMeta, MetaError> make(Fields f);   // datum engaged ∧ ¬datum_applicable → error
  const QuantityId& quantity() const& noexcept;
  std::string_view label() const& noexcept;
  const std::optional<Unit>& unit() const& noexcept;
  std::optional<VerticalDatum> datum() const noexcept;
  // (&& overloads of the reference accessors are deleted)

  SeriesMeta with_label(std::string label) const;                                  // total
  std::expected<SeriesMeta, MetaError> assume_unit(Unit u) const;                  // only when unset
  std::expected<SeriesMeta, MetaError> assume_datum(VerticalDatum d) const;        // only when unset and applicable
  friend bool operator==(const SeriesMeta&, const SeriesMeta&) = default;
 private:
  // convert() and shift() rewrite unit/datum through a passkey (detail::MetaRewrite) that only
  // series_ops.cpp and datum_shift.cpp can construct.
  Fields f_;
};
```

### 2.7 Time series — `timeseries.hpp`

```cpp
enum class ConstructionErrc : std::uint8_t { length_mismatch, time_not_increasing };
struct ConstructionError { ConstructionErrc code; std::size_t index; friend constexpr bool operator==(ConstructionError, ConstructionError) = default; };

class TimeSeries {
 public:
  TimeSeries() = default;
  static std::expected<TimeSeries, ConstructionError>
  make(std::vector<Time> times, std::vector<Sample> samples, SeriesMeta meta);

  std::span<const Time> times() const& noexcept;
  std::span<const Sample> samples() const& noexcept;
  const SeriesMeta& meta() const& noexcept;
  auto points() const& { return std::views::zip(times_, samples_); }
  // && overloads of the four above are deleted
  std::size_t size() const noexcept;
  bool empty() const noexcept;

  TimeSeries with_label(std::string label) const&;
  TimeSeries with_label(std::string label) &&;
  std::expected<TimeSeries, MetaError> assume_unit(Unit u) const;
  std::expected<TimeSeries, MetaError> assume_datum(VerticalDatum d) const;
  template <std::invocable<Sample> F> requires std::same_as<std::invoke_result_t<F, Sample>, Sample>
  TimeSeries transform_samples(F f) const;   // times and meta unchanged; finiteness ensured by Sample
  friend bool operator==(const TimeSeries&, const TimeSeries&) = default;
 private:
  std::vector<Time> times_;
  std::vector<Sample> samples_;
  SeriesMeta meta_;
};

struct Point { Time time; Sample sample; friend constexpr bool operator==(const Point&, const Point&) = default; };
struct NormalizeReport {
  std::size_t descents{};              // i with t[i+1] < t[i] in the input
  std::size_t duplicates_dropped{};
  std::size_t conflicting_duplicates{};// dropped samples whose Sample differs from the kept one
  constexpr bool clean() const noexcept;
  friend constexpr bool operator==(const NormalizeReport&, const NormalizeReport&) = default;
};
struct Normalized { TimeSeries series; NormalizeReport report; };
/// Total. Fast path: adjacent_find(not t[i] < t[i+1]) == end → no sort. Else stable_sort by time, then keep the first of each time.
Normalized normalize(std::vector<Point> rows, SeriesMeta meta);

struct ObsVsPred { TimeSeries observed; TimeSeries predicted; friend bool operator==(const ObsVsPred&, const ObsVsPred&) = default; };
template <class Station, class Data>
struct AtStation { Station station; Data data; friend bool operator==(const AtStation&, const AtStation&) = default; };
```

There is no builder. netCDF readers build the vectors and call `make`.

### 2.8 Stations and the station table — `station.hpp`, `station_table.hpp` (shared vocabulary)

```cpp
enum class DataSource : std::uint8_t { noaa_coops, usgs, ndbc, harmonics, adcirc, dflowfm, user };   // SN tokens
constexpr std::string_view to_token(DataSource s) noexcept;
constexpr std::optional<DataSource> parse_data_source(std::string_view token) noexcept;

namespace provider {   // valid_id is the parser predicate; StationId::make is the only constructor
struct Coops { static constexpr DataSource source = DataSource::noaa_coops; static constexpr bool valid_id(std::string_view) noexcept; }; // ^[0-9]{7}$
struct Usgs  { static constexpr DataSource source = DataSource::usgs;       static constexpr bool valid_id(std::string_view) noexcept; }; // ^[A-Za-z0-9]+-[A-Za-z0-9]+$
struct Ndbc  { static constexpr DataSource source = DataSource::ndbc;       static constexpr bool valid_id(std::string_view) noexcept; }; // ^[A-Z0-9]{5}$ (after upper-casing)
struct Harmonics { static constexpr DataSource source = DataSource::harmonics; static constexpr bool valid_id(std::string_view) noexcept; }; // non-empty, no control chars, ≤ 255 B (HJ narrows it)
}
template <class P> concept Provider = requires(std::string_view s) { { P::source } -> std::convertible_to<DataSource>; { P::valid_id(s) } -> std::same_as<bool>; };

template <Provider P>
class StationId {
 public:
  static std::expected<StationId, StationIdError> make(std::string_view raw);   // parse: trim, NDBC upper-case, validate
  std::string_view value() const& noexcept;
  friend auto operator<=>(const StationId&, const StationId&) = default;       // ordered: map keys, sorting lists
 private:
  explicit StationId(std::string v);
  std::string value_;
};

template <Provider P> struct GaugeStation { StationId<P> id; std::string name; Location location; ValidRange validity; DatumTable datums; };

struct FileStation {                                  // C5, C14
  std::string id;
  std::string name;
  Location location;
  std::optional<NativePoint> native;                  // file CRS point when not 4326
  std::optional<DataSource> source;
  friend bool operator==(const FileStation&, const FileStation&) = default;
};
template <Provider P> FileStation to_file_station(const GaugeStation<P>& g);

/// Distinct 0-based indices in caller order, all < station_count. Required by every netCDF model read (C12).
class StationSelection {
 public:
  static std::expected<StationSelection, SelectionError> make(std::vector<std::size_t> indices, std::size_t station_count);  // enum: duplicate_index, out_of_range
  static StationSelection all(std::size_t station_count);
  std::span<const std::size_t> indices() const& noexcept;
  friend bool operator==(const StationSelection&, const StationSelection&) = default;
};

using Column = std::vector<Sample>;
struct StationRow { FileStation station; std::size_t axis; std::vector<Column> columns; };

enum class TableErrc : std::uint8_t {
  duplicate_quantity,          // schema quantities must be unique (by QuantityId)
  axis_out_of_range, time_not_increasing, time_out_of_range,   // |t| > max_abs_time_ms
  column_count_mismatch, column_length_mismatch,
  empty_station_id, duplicate_station_id, embedded_nul, invalid_utf8,
  schema_mismatch,             // from_series only: metas differ
};
struct TableError { TableErrc code; std::optional<std::size_t> station; std::optional<std::size_t> index; friend bool operator==(const TableError&, const TableError&) = default; };

/// The file record (C4). Its make() reports exactly the structural constraints of SN §12.8 that types do not already rule out.
class StationTable {
 public:
  StationTable() = default;                                     // empty
  static std::expected<StationTable, TableError>
  make(std::vector<SeriesMeta> schema, std::vector<std::vector<Time>> axes, std::vector<StationRow> rows);
  /// One-column table from series with equal meta (provider exports).
  static std::expected<StationTable, TableError> from_series(std::vector<AtStation<FileStation, TimeSeries>> series);

  std::span<const SeriesMeta> schema() const& noexcept;
  std::size_t size() const noexcept;
  const FileStation& station(std::size_t i) const&;
  std::span<const Time> times(std::size_t i) const&;
  std::span<const Sample> column(std::size_t i, std::size_t k) const&;
  TimeSeries series(std::size_t i, std::size_t k) const;        // materialized copy
  bool single_axis() const noexcept;                            // ≥1 station, all time vectors equal and non-empty (SN L1)
  std::size_t total_samples() const noexcept;
  friend bool operator==(const StationTable&, const StationTable&) = default;
};
```

`ModelOutput` is
gone: vector outputs are two schema columns (`current_u`/`current_v` or `wind_u`/`wind_v`),
and `vector_series(table, i, ku, kv)` (§2.9) pairs them.

### 2.9 Vector series — `vector_series.hpp`

```cpp
enum class AlignmentErrc : std::uint8_t {
  times_differ, units_differ, unit_unknown, datums_differ, datum_unknown, not_a_vector_pair, temperature_difference,
};
/// u/v pair: (current_u, current_v), (wind_u, wind_v), or (generic, generic). Same times and units.
class VectorSeries {
 public:
  static std::expected<VectorSeries, AlignmentErrc> make(TimeSeries u, TimeSeries v);
  const TimeSeries& u() const& noexcept;
  const TimeSeries& v() const& noexcept;
  TimeSeries magnitude() const;              // hypot (B1), combine rule (N7)
  TimeSeries cartesian_direction() const;    // atan2(v,u)° in (-180,180]; -180 → 180; zero vector → Missing
 private:
  TimeSeries u_, v_;
};
std::expected<VectorSeries, AlignmentErrc> vector_series(const StationTable& t, std::size_t station, std::size_t ku, std::size_t kv);
std::expected<TimeSeries, AlignmentErrc> magnitude3(const TimeSeries& x, const TimeSeries& y, const TimeSeries& z);
```

Output metadata of the derived series (C3; the datum is always `nullopt`):

| Result | Quantity | Unit | Label |
|---|---|---|---|
| `magnitude` of a wind pair | `wind_speed` | component unit | `"wind speed"` |
| `magnitude` of other pairs | `GenericQuantity::value()` | component unit | `"<u label stem> speed"` (e.g. `"current speed"`) |
| `cartesian_direction` | `GenericQuantity::value()` (**never** `wind_direction`, which is meteorological "from") | `degree` | `"<stem> direction (cartesian: degrees counter-clockwise from east, toward)"` |
| `magnitude3` | `GenericQuantity::value()` | component unit | `"3D current speed"` |

### 2.10 Series operations — `series_ops.hpp`

```cpp
TimeSeries slice(const TimeSeries& s, TimeRange r);                          // two lower_bounds; half-open
TimeSeries slice(TimeSeries&& s, TimeRange r);                               // sink: erases in place
struct TimeOverflow { std::size_t index; };                                  // first sample whose shifted time leaves ±max_abs_time_ms
std::expected<TimeSeries, TimeOverflow> shift_time(TimeSeries s, std::chrono::milliseconds dt);  // checked add
TimeSeries scale_offset(TimeSeries s, Affine a);                             // v4 multiplier + y shift; non-values kept
std::expected<TimeSeries, UnitError> convert(TimeSeries s, const Unit& to);  // UnitError = variant<IncompatibleUnits, UnknownUnit>
std::expected<StationTable, UnitError> convert(StationTable t, std::size_t column, const Unit& to);

// ---- Extent: commutative monoid → transform_reduce is fine ----
struct ValueRange { double min; double max; };
struct Extent { Time first; Time last; std::optional<ValueRange> values; };       // B13/B14; == defaulted
std::optional<Extent> extent(const TimeSeries& s);                               // nullopt iff empty
constexpr std::optional<Extent> combine(const std::optional<Extent>&, const std::optional<Extent>&) noexcept;
std::optional<Extent> extent(std::span<const TimeSeries> all);

// ---- Bucket: the summary/decimation monoid. Associative; NOT commutative (ties keep the left operand). ----
struct Extreme { double value; Time time; friend constexpr bool operator==(Extreme, Extreme) = default; };
class Bucket {
 public:
  constexpr Bucket() noexcept = default;                                         // identity
  static constexpr Bucket of(Time t, Sample s) noexcept;
  friend constexpr Bucket operator+(const Bucket& a, const Bucket& b) noexcept;
  constexpr std::size_t values() const noexcept;
  constexpr std::size_t missing() const noexcept;
  constexpr std::size_t dry() const noexcept;
  constexpr std::optional<Extreme> min() const noexcept;                          // first occurrence
  constexpr std::optional<Extreme> max() const noexcept;
  constexpr double sum() const noexcept;
  constexpr bool has_gap() const noexcept;                                        // missing + dry > 0 (decimation draws a break)
  friend constexpr bool operator==(const Bucket&, const Bucket&) = default;
 private: /* counts, optional<Extreme> min/max, sum */
};
Bucket summarize(const TimeSeries& s);                 // ordered left fold: std::accumulate(points, Bucket{}, …)
struct ValueStats { Extreme min; Extreme max; double mean; };
struct QuickStats { std::size_t values, missing, dry; std::optional<ValueStats> stats; };   // EX #2; total
QuickStats quick_stats(const TimeSeries& s);           // from summarize; peak cross-checked against max_element in tests

/// EX #3: observed − predicted on exactly equal timestamps (merge-join, O(n+m)).
/// Requires units known and equal (not a temperature → temperature_difference). If datum_applicable,
/// datums must be known and equal. Result: GenericQuantity::value(), common unit, datum nullopt,
/// label "<obs> − <pred>", combine rule.
std::expected<TimeSeries, AlignmentErrc> residual(const ObsVsPred& pair);
```

Phase 5 decimation folds `Bucket`s per pixel column; `quick_stats` and the chart share one
monoid.

### 2.11 Datums — `datum.hpp` (vocabulary, WP1), `datum_shift.hpp` (WP3) (D18)

```cpp
enum class VerticalDatum : std::uint8_t { mhhw, mhw, mtl, msl, mlw, mllw, navd88, ngvd29, igld85, stnd };
constexpr std::string_view to_string(VerticalDatum d) noexcept;                       // SN §10.2 tokens + "IGLD85"
constexpr std::optional<VerticalDatum> parse_vertical_datum(std::string_view s) noexcept;   // case-insensitive; NAVD/NGVD/IGLD/Stnd aliases; MHW (N8); "none"/"" → nullopt

struct DatumHeight { VerticalDatum datum; Length height; };
struct MissingMsl {};
struct ConflictingHeight { VerticalDatum datum; };
struct NonFiniteHeight { VerticalDatum datum; };            // (== defaulted on each)
using DatumTableError = std::variant<MissingMsl, ConflictingHeight, NonFiniteHeight>;
struct MissingOffset { VerticalDatum datum; };

class DatumTable {                                          // heights above MSL; MSL is not stored (≡ 0)
 public:
  constexpr DatumTable() noexcept = default;
  /// heights relative to `reference`; equal duplicates accepted, conflicting → error; non-finite → error
  static constexpr std::expected<DatumTable, DatumTableError> from_heights(VerticalDatum reference, std::span<const DatumHeight> h) noexcept;
  constexpr std::optional<Length> height_above_msl(VerticalDatum d) const noexcept;    // msl → 0
  /// h[from] − h[to]; offset(d, d) == 0 for every d, total
  constexpr std::expected<Length, MissingOffset> offset(VerticalDatum from, VerticalDatum to) const noexcept;
  friend constexpr bool operator==(const DatumTable&, const DatumTable&) = default;
 private:
  std::array<std::optional<Length>, 9> heights_{};          // non-MSL slots
};

// datum_shift.hpp
struct NotALengthSeries {};
struct UnknownSourceDatum {};
using ShiftError = std::variant<NotALengthSeries, UnknownSourceDatum, MissingOffset>;
/// Check order (pinned): unit is a LengthUnit → datum engaged → from == to returns s unchanged
/// → offset (MissingOffset{from} is reported before {to}). The result has datum = to; non-values kept.
std::expected<TimeSeries, ShiftError> shift(TimeSeries s, VerticalDatum to, const DatumTable& table);
```

### 2.12 Core error inventory and `describe.hpp`

| Function family | Error (narrow) |
|---|---|
| `TimeRange`, `ValidRange`, `Location`, `Epsg`, `StationId`, `StationSelection`, `parse_utc_datetime` | own `enum class` / `DateTimeError` |
| `SeriesMeta::make` / `assume_*` | `MetaError` |
| `TimeSeries::make` | `ConstructionError` |
| `VectorSeries`, `magnitude3`, `residual`, `vector_series` | `AlignmentErrc` |
| `conversion` | `IncompatibleUnits` |
| `convert` | `UnitError` |
| `shift_time` | `TimeOverflow` |
| `DatumTable::from_heights` | `DatumTableError` |
| `offset` | `MissingOffset` |
| `shift` | `ShiftError` |
| `StationTable::make` | `TableError` |
| HWM | §3 |

`describe(const X&) -> std::string` is defined for each type. A CI grep allows callers
only in `src/app`, `src/cli` and `tests`.

---

## 3. HWM statistics — `hwm.hpp`, `hwm_stats.hpp` (D16, D17, D25)

```cpp
inline constexpr double dry_threshold = -999.0;
constexpr bool is_dry(double raw) noexcept { return raw <= dry_threshold; }   // model-output readers only (C9)

struct Wet { Length elevation; };                                // == defaulted
using WetDry = std::variant<Wet, Dry>;
constexpr WetDry model_value(double raw, LengthUnit unit) noexcept;

struct HighWaterMark { Location location; Length ground; Length observed; WetDry modeled; };   // == defaulted
constexpr std::optional<Length> modeled_error(const HighWaterMark& h) noexcept;             // Dry → nullopt

class Moments {                                    // x = observed, y = modeled, e = y − x (metres)
 public:
  constexpr Moments() noexcept = default;          // identity
  static constexpr Moments of(const HighWaterMark& h) noexcept;   // Dry → Moments{} (null object)
  /// Chan, Golub & LeVeque. n == 0 operands short-circuit, so the identity is exact.
  friend constexpr Moments operator+(const Moments& a, const Moments& b) noexcept;
  constexpr std::size_t n() const noexcept;
  // read-only accessors mean_x/y/e, m2x/m2y/cxy/m2e; friend ==
 private:
  std::size_t n_{};
  double mean_x_{}, mean_y_{}, mean_e_{}, m2x_{}, m2y_{}, cxy_{}, m2e_{};
};
Moments wet_moments(std::span<const HighWaterMark> marks);   // ordered left fold (std::accumulate)

enum class Intercept : std::uint8_t { free, through_origin };   // N1: never a bool
struct ThroughOrigin { double slope; };
struct Free { double slope; Length intercept; };
using LinearFit = std::variant<ThroughOrigin, Free>;

struct HwmStats {
  std::size_t total;
  std::size_t wet;
  LinearFit fit;                                   // alternative matches the requested Intercept
  std::optional<double> r_squared;                 // free: Pearson r²; origin: 1 − SSres/Σy² (D25). nullopt if denominator == 0
  Length mean_error;
  std::optional<Length> error_stddev;              // √(m2e/(n−1)), n ≥ 2 (D17)
};
struct NoWetMarks { std::size_t total; };
struct TooFewForFreeFit { std::size_t wet; };
struct DegenerateObserved {};                      // free: m2x == 0; origin: Σx² == 0
using HwmStatsError = std::variant<NoWetMarks, TooFewForFreeFit, DegenerateObserved>;   // checked in this order
std::expected<HwmStats, HwmStatsError> hwm_stats(std::span<const HighWaterMark> marks, Intercept mode);

enum class ClassBreaksError : std::uint8_t { not_strictly_increasing };
class ErrorClasses {                               // 7 strictly increasing breaks
 public:
  static constexpr std::expected<ErrorClasses, ClassBreaksError> make(std::array<Length, 7> b) noexcept;
  static constexpr ErrorClasses feet_default() noexcept;     // -5,-3.5,-1.5,0,1.5,3.5,5 ft
  static constexpr ErrorClasses meters_default() noexcept;   // -1.5,-1,-0.5,0,0.5,1,1.5 m
  constexpr std::span<const Length, 7> breaks() const& noexcept;
 private:
  std::array<Length, 7> breaks_;
};
enum class HwmCategory : std::uint8_t { dry, bin0, bin1, bin2, bin3, bin4, bin5, bin6, bin7 };
constexpr HwmCategory classify(const HighWaterMark& h, const ErrorClasses& c) noexcept;   // upper_bound: e == c_i → bin i+1
```

Derived quantities. Raw sums are reconstructed as `Σx² = m2x + n·x̄²` and
`Σxy = cxy + n·x̄·ȳ`; likewise `Σy²`.

| | free | through_origin |
|---|---|---|
| slope | `cxy/m2x` | `Σxy/Σx²` |
| intercept | `ȳ − slope·x̄` | none (`ThroughOrigin`) |
| R² | `cxy²/(m2x·m2y)` | `1 − SSres/Σy²`, with `SSres = max(0, Σy² − slope·Σxy)` (uncentred, D25; differs from v4) |
| σ | `√(m2e/(n−1))` | same |

---

## 4. netCDF wrapper — `mov::io::nc` (`netcdf/file.hpp`)

### 4.1 Rules

| Rule | Spec |
|---|---|
| Threading (C11) | **Not thread-safe.** The doc comment on every public function says: "the caller must serialize all calls into mov::io netCDF functions". There is no mutex. |
| Choke point | Every `nc_*` call goes through `detail::nc_call(op, object, file, fn)` (`src/io/netcdf/nc_call.hpp`, private). That includes `nc_free_string`, `nc_abort` and the destructor's `nc_close`. In debug builds `nc_call` sets an `std::atomic_flag` on entry and `assert`s it was clear, which catches concurrent and re-entrant use. |
| Include gate | CI grep: `#include <netcdf.h>` appears only in `src/io/netcdf/*.cpp`. `nc_max_name = 256` is mirrored in the header and `static_assert`ed equal to `NC_MAX_NAME` in `file.cpp`. |
| Sizes (C12) | Every element count is `checked_product(dims)`. Overflow → `WrapperFault::overflow`. Above `ReadLimits::max_elements` → `too_large`. Attribute length above `max_att_bytes` → `too_large`. |
| Cancellation | Bulk reads go in slabs of at most `limits.slab_elements` and check `stop.stop_requested()` between slabs → `Cancelled`. |
| Paths (Windows) | One helper, `detail::nc_path(const std::filesystem::path&) -> std::string` (UTF-8). **Verify against the pinned netCDF-C/HDF5 on the Windows CI runner.** A CI test opens a non-ASCII path and a path longer than 260 characters on every OS. |
| Close policy | A read handle's destructor closes and ignores close errors. A write handle (atomic writer only) is closed explicitly with `close() &&`, which reports errors. A write handle destroyed without closing calls `nc_abort`. |

### 4.2 API

```cpp
namespace mov::io::nc {

class NcName {                                       // owning, validated
 public:
  static std::expected<NcName, NcNameError> make(std::string_view s);    // enum: empty, too_long, embedded_nul
  const char* c_str() const& noexcept;
  std::string_view view() const& noexcept;
  friend bool operator==(const NcName&, const NcName&) = default;
 private:
  std::string s_;
};
class NcNameRef {                                    // non-owning, NUL-terminated, validated; the parameter type
 public:
  template <std::size_t N> consteval NcNameRef(const char (&literal)[N]);   // compile-time checked
  NcNameRef(const NcName& n) noexcept;
  const char* c_str() const noexcept;
  std::string_view view() const noexcept;
};

enum class Type : std::uint8_t { byte, ubyte, char_, short_, ushort, int_, uint, int64, uint64, float_, double_, string };
struct DimInfo { int id; NcName name; std::size_t length; };
struct VarInfo { int id; NcName name; Type type; std::vector<DimInfo> dims; };
struct DimRange { std::size_t start; std::size_t count; };
using Slab = std::vector<DimRange>;                  // rank must equal var rank → rank_mismatch
struct Global {};
using AttTarget = std::variant<Global, NcNameRef>;
template <class T> struct VarOptions { std::optional<T> fill; std::optional<int> deflate_level; std::optional<std::vector<std::size_t>> chunks; };

struct ReadLimits {
  std::size_t max_elements = std::size_t{1} << 27;   // per call and per reader result (~1 GiB of doubles)
  std::size_t max_att_bytes = std::size_t{1} << 20;
  std::uintmax_t max_text_bytes = std::uintmax_t{1} << 30;
  std::size_t slab_elements = std::size_t{1} << 20;  // cancellation granularity
};
struct ReadContext { ReadLimits limits{}; std::stop_token stop{}; };

template <class T> concept Numeric = /* one of */ std::same_as<T, double> or std::same_as<T, float> or
    std::same_as<T, std::int8_t> or std::same_as<T, std::int16_t> or std::same_as<T, std::int32_t> or std::same_as<T, std::int64_t>;

template <Numeric T> struct Masking {                // C9/SN §8.1, built in the native type T
  std::optional<T> fill;                             // _FillValue, else type default unless NC_NOFILL
  std::vector<T> missing_values;                     // missing_value (scalar or vector)
  std::optional<T> valid_min, valid_max;             // valid_min / valid_max / valid_range
  std::optional<double> scale, offset;               // scale_factor / add_offset: applied after masking
  constexpr Sample apply(T raw) const noexcept;      // masked or non-finite → Missing; else finite_or_missing(unpacked)
};

template <class F> decltype(auto) dispatch_numeric(Type t, F&& f);   // f.template operator()<T>() for the Numeric types

class File {
 public:
  static std::expected<File, NcError> open(const std::filesystem::path& p, ReadLimits limits);   // NC_NOWRITE
  File(File&&) noexcept;
  File& operator=(File&&) noexcept;
  File(const File&) = delete;
  File& operator=(const File&) = delete;
  ~File();
  std::expected<void, NcError> close() &&;

  std::expected<std::optional<DimInfo>, NcError> find_dim(NcNameRef name) const;
  std::expected<std::optional<VarInfo>, NcError> find_var(NcNameRef name) const;
  std::expected<std::vector<VarInfo>, NcError> variables() const;

  std::expected<std::optional<std::string>, NcError> text_att(AttTarget on, NcNameRef name) const;   // attlen-sized (B8); NC_CHAR or NC_STRING; raw bytes
  template <Numeric T> std::expected<std::optional<std::vector<T>>, NcError> numeric_att(AttTarget on, NcNameRef name) const;   // exact type

  template <Numeric T> std::expected<std::vector<T>, NcError> read(NcNameRef var, const Slab& s, const ReadContext& ctx) const;  // §4.3
  template <Numeric T> std::expected<Masking<T>, NcError> masking(NcNameRef var) const;   // type must equal T; _Unsigned → unsupported_unsigned
  /// dispatch_numeric ∘ read ∘ Masking::apply. int64/uint64 vars → type_mismatch.
  std::expected<std::vector<Sample>, NcError> read_samples(NcNameRef var, const Slab& s, const ReadContext& ctx) const;
  /// 2-D NC_CHAR rows, raw bytes including NULs; stride = dimension length (B7, B11). Policy is applied by the caller (C14).
  std::expected<std::vector<std::string>, NcError> read_char_rows(NcNameRef var, const ReadContext& ctx) const;
  std::expected<std::vector<std::string>, NcError> read_strings(NcNameRef var, const ReadContext& ctx) const;   // NC_STRING; freed via nc_call (B21)

  // ---- writing: only on a File handed to write_netcdf_atomic's body ----
  std::expected<DimInfo, NcError> define_dim(NcNameRef name, std::size_t length);              // 0 → zero_length_dim
  template <Numeric T> std::expected<VarInfo, NcError>
  define_var(NcNameRef name, std::span<const DimInfo> dims, const VarOptions<T>& opt);           // VarOptions{fill, deflate_level, chunks}
  std::expected<VarInfo, NcError> define_char_var(NcNameRef name, std::span<const DimInfo> dims);
  std::expected<void, NcError> put_att(AttTarget on, NcNameRef name, std::string_view text);      // byte length (B15); max_att_bytes
  template <Numeric T> std::expected<void, NcError> put_att(AttTarget on, NcNameRef name, std::span<const T> v);
  template <Numeric T> std::expected<void, NcError> put(NcNameRef var, std::span<const T> data, const Slab& s);   // exact var type; Π count == size
  std::expected<void, NcError> put_char_rows(NcNameRef var, std::span<const std::string> rows); // NUL-padded; too long → name_too_long
  std::expected<void, NcError> end_define();
 private:
  friend std::expected<void, Error> write_netcdf_atomic_impl(/*…*/);
  static std::expected<File, NcError> create(const std::filesystem::path& tmp);   // NC_NETCDF4 | NC_NOCLOBBER
  std::optional<int> ncid_;                         // nullopt after move/close → WrapperFault::closed
  bool writable_{false};
  std::filesystem::path path_;
  ReadLimits limits_;
};
}  // namespace mov::io::nc
```

`NcError` lives in `io/error.hpp`:

```cpp
enum class NcOp : std::uint8_t { open, create, close, abort, inquire, get_var, put_var, get_att, put_att, def_dim, def_var, enddef, free_string };
enum class WrapperFault : std::uint8_t { type_mismatch, rank_mismatch, count_mismatch, overflow, too_large, zero_length_dim, name_too_long, unsupported_unsigned, closed };
struct LibraryStatus { int code; };
using NcStatus = std::variant<LibraryStatus, WrapperFault>;
struct NcError { NcStatus status; NcOp op; std::string object; std::filesystem::path file; };   // == defaulted
```

### 4.3 Type rules

| `read<T>` from var type | double | float | int8 | int16 | int32 | int64 |
|---|---|---|---|---|---|---|
| double | ✓ | ✗ | ✗ | ✗ | ✗ | ✗ |
| float | ✓ | ✓ | ✗ | ✗ | ✗ | ✗ |
| byte | ✓ | ✗ | ✓ | ✓ | ✓ | ✓ |
| short | ✓ | ✗ | ✗ | ✓ | ✓ | ✓ |
| int | ✓ | ✗ | ✗ | ✗ | ✓ | ✓ |
| int64 | ✓ (time only; callers use `checked_time`) | ✗ | ✗ | ✗ | ✗ | ✓ |
| unsigned, char, string | ✗ | ✗ | ✗ | ✗ | ✗ | ✗ |

- `read_samples` masks in the native type and only then widens and unpacks (B4, B9).
- `put<T>` requires the variable type to equal `T` exactly.
- An `_Unsigned = "true"` attribute gives `unsupported_unsigned`. Callers turn that into
  `skipped_variable` (foreign files only).

### 4.4 Atomic write — `write_netcdf_atomic` (C16)

```cpp
template <std::invocable<nc::File&> Body>     // Body returns std::expected<void, Error>
std::expected<void, Error> write_netcdf_atomic(const std::filesystem::path& target, Body&& body);
```

Stages, each with an injectable fault through
`detail::write_netcdf_atomic_impl(target, body, FaultInjector)`; tests drive every stage:

1. `create`: `<target>.<16 hex random>.tmp` in the same directory, opened with `NC_NOCLOBBER`.
2. `body`. On error: `nc_abort`, remove the temp file, return the error.
3. `close`. On error: remove the temp file.
4. `fsync_file`: POSIX `open` + `fsync`; Windows `FlushFileBuffers`.
5. `rename`: POSIX `rename`; Windows `MoveFileExW(REPLACE_EXISTING | WRITE_THROUGH)`.
6. `fsync_dir`: POSIX only.

On any failure the target is byte-identical to before (B19) and no temp file remains.
`write_file_atomic` (text) uses the same stages with `O_EXCL`.

### 4.5 Bug classes 4–15

| Bug | Fix |
|---|---|
| B4 | No untyped API; typed reads; fill compared in the native type; narrowing reads refused; exact-type `put`. |
| B5 | RAII `File`; one `nc_call` choke point; `--wrap` open/close count tests (§7.2). |
| B6 | `open` returns `expected`; on failure no `File` exists to close. |
| B7, B11 | `read_char_rows` stride comes from the dimension. |
| B8 | `text_att` sizes from `nc_inq_attlen`, capped by `max_att_bytes`. |
| B9 | `Masking` covers fill, default fill, `missing_value`, valid range and NaN. |
| B10 | `numeric_att<int32_t>`, then `Epsg::make`. |
| B11 | `parse_cf_time_units` + `checked_time`. |
| B12 | `find_*` return `optional`; there are no name→id maps. |
| B13, B14 | `extent` → `optional`. |
| B15 | `put_att` byte length, exact `put` counts, `put_char_rows` padding. |

---

## 5. `mov::io` readers and writers

Rules for this section:
- Text parsers take `std::string_view`; file wrappers add I/O and `ReadContext`.
- **Every reader returns `Read<T>`** (C8).
- Every reader projects to `Location` at the boundary (C5) via `to_location`.
- Every model reader requires a `StationSelection` and an `Epsg` (no defaults).

### 5.0 Shared — `error.hpp`, `warning.hpp`, `read.hpp`, `text_file.hpp`

```cpp
struct ParseError { ParseErrc code; std::size_t line; std::optional<std::size_t> column; std::string context; };   // context ≤ 120 B
// ParseErrc: empty_input, missing_header, wrong_field_count, bad_integer, bad_number, bad_date, bad_time_units,
//            time_out_of_range, out_of_range, count_mismatch, corrupt_record, trailing_text
struct FileError { FileOp op; std::filesystem::path path; std::error_code ec; };   // FileOp: open, size, read, write, fsync, rename, remove; too big → errc::file_too_large
struct Cancelled {};
struct FormatError { FormatErrc code; std::string subject; std::optional<std::size_t> station; std::optional<std::size_t> index; };
// FormatErrc: not_this_format, missing_variable, missing_dimension, missing_attribute, partner_variable_missing,
//   station_count_mismatch, cold_start_required, time_missing, time_out_of_range, time_not_increasing,
//   layer_out_of_range, wrong_column_count, noncanonical_unit, duplicate_quantity,
//   SN §12: unsupported_version, bad_version, no_station_id, duplicate_station_id, bad_coordinates, bad_obs_count,
//   padding_not_missing, bad_encoding, wet_dry_inconsistent, bad_flag, unsupported_layout, unsupported_calendar,
//   unsupported_crs, no_data_variables, bad_ancillary, dimension_mismatch; writer: empty_collection
using Error = std::variant<FileError, ParseError, NcError, FormatError, Cancelled>;
// A reader whose StationTable::make fails maps TableErrc 1:1 to the FormatErrc of the same meaning
// (duplicate_station_id, no_station_id ← empty_station_id, bad_encoding ← embedded_nul/invalid_utf8,
// time_not_increasing, time_out_of_range, duplicate_quantity, dimension_mismatch ← column_*), keeping station/index.

enum class WarningCode : std::uint8_t {
  times_reordered, duplicate_times_dropped, conflicting_duplicate_times,          // normalize report
  legacy_sentinel_masked, nonfinite_masked, unrecognized_unit,                   // value-level
  partial_record_dropped, fewer_snapshots_than_header, epoch_used,               // model text
  header_line_skipped, duplicate_station_id_renamed, invalid_utf8_replaced,
  foreign_cf, crs_assumed, datum_unknown, tz_assumed_utc, skipped_variable,      // SN §12.7
  minor_newer, unknown_provider, unknown_quantity, legacy_dialect,
};
struct Warning { WarningCode code; std::string subject; std::size_t count{1}; };   // == defaulted

template <class T> struct Read {                         // writer monad over warnings
  T value;
  std::vector<Warning> warnings;
  template <class F> auto transform(F&& f) && -> Read<std::invoke_result_t<F, T&&>>;
  template <class F> auto and_then(F&& f) && -> std::invoke_result_t<F, T&&>;   // F: T&& → Read<U>; warnings concatenated (ours first)
};
/// Over expected: F: T&& → expected<Read<U>, E2>, with E2 convertible to E.
template <class T, class E, class F> auto and_then_read(std::expected<Read<T>, E>&& r, F&& f);
template <class V> inline constexpr auto lift = []<class E>(E&& e) -> V { return V{std::forward<E>(e)}; };

std::expected<std::string, FileError> read_text_file(const std::filesystem::path& p, const ReadLimits& l);  // size checked first (B3: ifstream)
template <std::invocable<std::ostream&> Body> std::expected<void, FileError> write_file_atomic(const std::filesystem::path& p, Body&& body);
```

Compile test (`tests/io/test_read_chain.cpp`, WP7) checks that the CLI chain
type-checks with exhaustive errors:

```cpp
using CliError = std::variant<mov::io::Error, mov::core::UnitError>;
std::expected<Read<std::string>, CliError> imeds_to_csv_in_feet(const std::filesystem::path& in) {
  return and_then_read(
      read_imeds(in, ReadContext{}).transform_error(lift<CliError>),
      [](ImedsFile&& f) -> std::expected<Read<std::string>, CliError> {
        return convert(std::move(f.table), 0, Unit{LengthUnit::foot})
            .transform([](StationTable&& t) { return Read<std::string>{.value = format_csv(t), .warnings = {}}; })
            .transform_error(lift<CliError>);
      });
}
STATIC_REQUIRE(std::same_as<decltype(imeds_to_csv_in_feet({})), std::expected<Read<std::string>, CliError>>);
// runtime: warnings of read_imeds survive to the result; a non-length column yields CliError holding UnitError
```

Text helpers (`src/io/detail/`, private):
- `LineCursor`: 1-based lines; strips a UTF-8 BOM at the start and one `\r` per line.
- `split_ws`, `split_on`.
- `parse_double`, `parse_int` (whole token; table in §7.2).
- `parse_model_number`: also maps `NaN`/`Inf`/`Infinity`/`****` to `Missing`, counted.
- `simplified`, `cut_at_nul`, `uniquify_ids` (`#2` suffixes).
- `reserve_capped(n, input_bytes / min_row_bytes)`.

### 5.1 CF time — `cf_time.hpp` (C13)

```cpp
enum class CfTimeUnit : std::uint8_t { millisecond, second, minute, hour, day };
constexpr std::int64_t unit_ms(CfTimeUnit u) noexcept;            // 1, 1000, 60000, 3600000, 86400000
struct CfTimeUnits { CfTimeUnit unit; Time epoch; };              // == defaulted
constexpr std::expected<CfTimeUnits, ParseError> parse_cf_time_units(std::string_view text);   // SN §7 grammar
enum class CfCalendar : std::uint8_t { standard, proleptic_gregorian };
constexpr std::optional<CfCalendar> parse_cf_calendar(std::string_view text) noexcept;          // absent → standard; others → nullopt
/// checked_time(value, unit_ms(u.unit), u.epoch); standard calendar before 1582-10-15 → nullopt
constexpr std::optional<Time> to_time(const CfTimeUnits& u, CfCalendar c, double value) noexcept;
```

Time variables are read with `read_samples`. A `Missing` time, or an out-of-range one,
gives `FormatError{time_missing | time_out_of_range}` with the index. The only exception
is SN L2 padding, which `obs_count` excludes.

### 5.2 IMEDS — `imeds.hpp`

```cpp
struct ImedsHeader { std::string source; std::string time_zone; std::optional<VerticalDatum> datum; std::optional<Unit> unit; };
struct ImedsFile { ImedsHeader header; StationTable table; };     // one schema column: GenericQuantity::value()
std::expected<Read<ImedsFile>, ParseError> parse_imeds(std::string_view text);
std::expected<Read<ImedsFile>, Error> read_imeds(const std::filesystem::path& p, const ReadContext& ctx);
std::expected<std::string, FormatError> format_imeds(const StationTable& t, std::string_view source = "MetOceanViewer");  // schema size 1 (wrong_column_count)
std::expected<void, Error> write_imeds(const std::filesystem::path& p, const StationTable& t, std::string_view source = "MetOceanViewer");
```

Read rules (LF §2.1 "Intended", plus):
- Rows are exactly 6 or 7 tokens (N3).
- Blank lines are skipped (N13).
- Coordinates go through `Location::make({.lat = …, .lon = …})`; failure → `out_of_range`.
- Exact sentinels → `Missing` plus `legacy_sentinel_masked{count}` (C9).
- Each station's rows go through `normalize`, with warnings.
- Duplicate names are uniquified for `id`, with a warning.
- `source = user`.

Write format (D15), byte-pinned:

```
% IMEDS generic format
% year month day hour min sec value
{source}    UTC    {datum | "none"}    {symbol(unit) | "unknown"}
{sanitized name}    {lat:.6f}    {lon:.6f}
{yyyy:04} {mm:02} {dd:02} {hh:02} {mi:02} {ss:02} {value:14.6f}
```

- Times are floored to whole seconds.
- Non-values are omitted (N18).
- Each run of whitespace or `,` in a name becomes `_`; an empty name becomes `station_{k}`.
- `\n` line endings, ending with exactly one `\n`.

### 5.3 ADCIRC ASCII — `adcirc_ascii.hpp`

```cpp
enum class AdcircKind : std::uint8_t { elevation /*61*/, velocity /*62*/, pressure /*71*/, wind /*72*/ };
std::expected<Read<std::vector<FileStation>>, ParseError> parse_adcirc_station_file(std::string_view text, Epsg crs);
// count, then "lon[,| ]lat[ name…]"; names simplified (N6); id = 0-based index; no name: empty; projected (C5)
std::expected<Read<StationTable>, Error>
parse_adcirc_ascii(std::string_view text, std::span<const FileStation> stations, AdcircKind kind, Time cold_start);
std::expected<Read<StationTable>, Error> read_adcirc_ascii(const std::filesystem::path& output, const std::filesystem::path& station_file,
                                                           Time cold_start, Epsg crs, const ReadContext& ctx);   // kind from suffix
```

`parse_adcirc_ascii` returns `io::Error`, not `ParseError`, because a station-count
mismatch is a `FormatError`.

- Internally, `detail::AdcircRecords { header; std::vector<double> seconds; detail::Matrix<double> raw[2]; }`
  (snapshots × stations) is parsed first. Allocation is capped by the input size.
- One shared axis: `checked_time(seconds, 1000, cold_start)`. Out of range → `ParseError{time_out_of_range}`.
- Classification (C9):
  - Elevation: `is_dry` → `Dry`.
  - Others: `raw <= -999` (either component) → `Missing`.
  - `NaN`/`Inf`/`****` → `Missing` plus `nonfinite_masked`.
- Incomplete runs:
  - Fewer complete records than the header's NSnaps → `fewer_snapshots_than_header{count}`.
  - A partial last record (record header or station lines cut off at EOF) is dropped, with
    `partial_record_dropped{count}`.
  - A malformed record followed by more data is mid-file corruption → `ParseError{corrupt_record}`.
- Non-increasing snapshot times (hot-start overlap) are normalized on the shared axis, with
  warnings.
- Schema:
  - 61 → `water_level` (m).
  - 62 → `current_u`, `current_v` (m s-1).
  - 71 → `air_pressure` (mH2O; the SN writer converts).
  - 72 → `wind_u`, `wind_v`.
- The cold start comes from `parse_utc_datetime` (N5).

### 5.4 ADCIRC netCDF — `adcirc_netcdf.hpp`

```cpp
struct AdcircNcCatalog { AdcircKind kind; std::vector<FileStation> stations; std::size_t times; };
struct AdcircNcRequest { std::optional<Time> cold_start; Epsg crs; StationSelection stations; };   // crs and stations required
std::expected<Read<AdcircNcCatalog>, Error> inspect_adcirc_netcdf(const std::filesystem::path& p, Epsg crs, const ReadContext& ctx);
std::expected<Read<StationTable>, Error> read_adcirc_netcdf(const std::filesystem::path& p, const AdcircNcRequest& req, const ReadContext& ctx);
```

- Detection: `model == "ADCIRC"`.
- Variable search order: `zeta`, `u-vel`, `pressure`, `windx`. A missing partner →
  `partner_variable_missing`. A lone `v-vel` → `not_this_format`.
- Elevation values: `dispatch_numeric` raw read, then `is_dry(widen(raw))` → `Dry`, else
  `Masking<T>::apply`.
- `cold_start` nullopt → CF `time:units`, with an `epoch_used` warning. If that fails →
  `cold_start_required`.
- **Chunk-aware reads.** Read time blocks `[t0, t0+B) × [min_sel, max_sel]`, with `B`
  chosen so a block is at most `slab_elements`, then gather the selected columns. Check
  `stop` between blocks.
  - Measurement owed in WP9: per-column strided reads vs blocks, for 1, 10 and 1000
    selected stations of a 10,000-station synthetic file, recorded in the test README.

### 5.5 DFlow-FM his — `dflow.hpp` (D26: synthetic fixtures only)

```cpp
enum class DflowDerived : std::uint8_t { current_speed_2d, current_direction_2d, current_speed_3d, wind_speed, wind_direction };
using DflowSource = std::variant<nc::NcName, DflowDerived>;
struct Flat    { DflowSource source; std::string long_name; };
struct Layered { DflowSource source; std::string long_name; std::size_t layers; };
using DflowVariable = std::variant<Flat, Layered>;                 // catalog entries; derived from layered inputs are Layered
class Layer {                                                      // 1-based, validated against one Layered variable
 public:
  static std::expected<Layer, FormatError> make(const Layered& v, std::size_t one_based);   // layer_out_of_range (N16)
  std::size_t zero_based() const noexcept;
};
struct AtLayer { Layered variable; Layer layer; };
using DflowChoice = std::variant<Flat, AtLayer>;
struct DflowCatalog { std::vector<FileStation> stations; std::vector<DflowVariable> variables; CfTimeUnits time_units; CfCalendar calendar; };
struct DflowRequest { DflowChoice choice; StationSelection stations; Epsg crs; };
std::expected<Read<DflowCatalog>, Error> inspect_dflow(const std::filesystem::path& p, Epsg crs, const ReadContext& ctx);   // B2
std::expected<Read<StationTable>, Error> read_dflow(const std::filesystem::path& p, const DflowRequest& req, const ReadContext& ctx);
```

- Station names: `cut_at_nul` then `simplified`.
- Variables on `laydimw` are excluded.
- No dry rule (C9; deviation from SN:252, see §9.2).
- Quantities: `waterlevel` → `water_level`; `x/y_velocity` → `current_u/v`; `windx/y` →
  `wind_u/v`; anything else → `GenericQuantity` from the name, or
  `GenericQuantity::value()` with a `unknown_quantity` warning.

### 5.6 Station netCDF — `station_netcdf.hpp`

```cpp
enum class StationNcLayout : std::uint8_t { orthogonal, incomplete, contiguous_ragged, single_station };
struct V5StationFile     { StationNcLayout layout; std::string version; StationTable table; };
struct ForeignCfFile     { StationNcLayout layout; StationTable table; };
struct LegacyStationFile { StationTable table; };
using StationFile = std::variant<V5StationFile, ForeignCfFile, LegacyStationFile>;
std::expected<Read<StationFile>, Error> read_station_netcdf(const std::filesystem::path& p, const ReadContext& ctx);

struct StationNcWriteOptions { std::string title{"MetOceanViewer station time series"}; std::optional<std::string> institution, source, references, comment; };
constexpr StationNcLayout choose_layout(const StationTable& t) noexcept;   // single_axis() ? orthogonal : incomplete
std::expected<StationNcLayout, Error> write_station_netcdf(const std::filesystem::path& p, const StationTable& t,
                                                            const StationNcWriteOptions& opt, Time now);   // now injected (determinism)
```

- The writer's own errors are only `empty_collection` (0 stations or 0 samples),
  `noncanonical_unit` (unit unknown or not convertible to the SN canonical unit; conversion
  uses `convert`), and I/O failures.
- Every other SN §12.8 precondition is guaranteed by `StationTable`, `Location`, `Sample`
  or `TableErrc`.
- `<q>_status` is written iff any sample of the column is `Dry` (Dry → 0, value → 1,
  Missing → −128).
- `vertical_datum` is written whenever the meta datum is engaged (§9.2).
- Reader per source:
  - **v5:** SN §12.1–12.6.
  - **Foreign:** SN *Foreign* column, L3 and scalar station (WP10b), `NC_STRING` ids,
    packing via `Masking`, `_Unsigned` → `skipped_variable`.
  - **Legacy A/B:** SN §11 + C14; `time_station_{:04}` looked up through `NcName::make`;
    legacy int64 times via `checked_time(int64 …)`; a missing `referenceDate` →
    `epoch_used` warning naming 1970-01-01; projection at the boundary.
  - **CRMS:** `not_this_format`.

### 5.7 HWM file — `hwm_file.hpp`

```cpp
/// lon, lat, ground, observed, modeled[, diff ignored]. A first line whose fields are ALL non-numeric is skipped
/// with header_line_skipped. Blank lines are skipped (N20). Bad number → ParseError{line, column}. is_dry → Dry.
std::expected<Read<std::vector<HighWaterMark>>, ParseError> parse_hwm_csv(std::string_view text, LengthUnit unit);
std::expected<Read<std::vector<HighWaterMark>>, Error> read_hwm_csv(const std::filesystem::path& p, LengthUnit unit, const ReadContext& ctx);
```

### 5.8 CSV export — `csv_export.hpp` (D27)

```cpp
/// RFC 4180. Header "station_id,station_name,time_utc,quantity,value,unit,datum"; one row per sample.
/// time_utc "{:%FT%T}Z" (ms). value "{:.6f}"; "" for Missing; "dry" for Dry (Q1).
/// Text cells starting with = + - @ \t \r get a leading ' (C18); quoting per RFC 4180.
std::string format_csv(const StationTable& t);
std::expected<void, Error> write_csv(const std::filesystem::path& p, const StationTable& t);
```

### 5.9 Detection and projection — `file_kind.hpp`, `projection.hpp`

```cpp
enum class FileKind : std::uint8_t { imeds, adcirc_ascii, adcirc_netcdf, dflow_netcdf, station_netcdf };
std::expected<FileKind, Error> detect_file_kind(const std::filesystem::path& p);   // netCDF content first, then suffix
struct ProjectionError { ProjectionErrc code; Epsg crs; };                          // unknown_crs, transform_failed
std::expected<Location, std::variant<ProjectionError, LocationError>> to_location(const NativePoint& p);
```

---

## 6. Layout and dependencies

```
src/core/include/mov/core/
  time.hpp units.hpp geo.hpp sample.hpp quantity.hpp datum.hpp        ← vocabulary I   (WP1)
  meta.hpp timeseries.hpp station.hpp station_table.hpp vector_series.hpp ← vocabulary II (WP2)
  series_ops.hpp datum_shift.hpp                                       ← operations     (WP3)
  hwm.hpp hwm_stats.hpp                                                ← HWM            (WP4)
  describe.hpp                                                         ← edge only
src/io/include/mov/io/
  error.hpp warning.hpp read.hpp text_file.hpp cf_time.hpp projection.hpp   (WP5)
  netcdf/file.hpp                                                           (WP6)
  imeds.hpp csv_export.hpp | adcirc_ascii.hpp hwm_file.hpp | adcirc_netcdf.hpp dflow.hpp | station_netcdf.hpp file_kind.hpp
src/io/detail/          text helpers, Matrix, uniquify (private)
src/io/netcdf/          file.cpp, nc_call.hpp — the ONLY place that includes <netcdf.h>
```

```
WP1 vocabulary I  → WP2 vocabulary II → WP3 ops
WP1 → WP4 HWM
WP1 → WP5 io foundation → WP6 nc
WP2, WP5 → WP7 IMEDS/CSV;  WP2, WP4, WP5 → WP8 ADCIRC ASCII/HWM file
WP2, WP6 → WP9 ADCIRC nc/DFlow;  WP2, WP3 (convert), WP6 → WP10a SN v5;  WP10a → WP10b
```

---

## 7. Test plan

Conventions:
- Catch2 v3. Tags `[core]`/`[io]` + component.
- `[constexpr]` marks `STATIC_REQUIRE`; `[regression][B#|N#]` marks bug regressions;
  `[legacy]` tests `SKIP()` when the legacy tree is absent; `[linux]` marks Linux-only tests.
- Fixtures live in `tests/fixtures/{core,io}/`.
- **Every value type** gets
  `STATIC_REQUIRE(std::regular<T> and std::is_nothrow_move_constructible_v<T>)`. Types
  without a default constructor (`Epsg`, `Location`, `StationSelection`) use
  `std::copyable<T> and std::equality_comparable<T>` instead.

### 7.1 Core

| Component | Test cases | constexpr |
|---|---|---|
| probe | each §1 feature; constexpr `llround`/`isfinite`/`fabs` | ✓ |
| time | `TimeRange` make/contains; `ValidRange`; `checked_time`: NaN, ±Inf, 2^53 edge exact, 2^53+1 rejected, epoch+offset overflow, int64 multiply overflow, negative values; `parse_utc_datetime` forms + column of first bad char; `parse_utc_datetime` TZ-independent (sets `TZ=America/Chicago`, compares to a literal `sys_days` value, N5) | all but TZ |
| units | exact factors; in/as round trip every enumerator (1e-12 rel); `conversion` same family / incompatible; temperature affine; `parse_unit` table; `"" → nullopt`; aliases canonicalized; `symbol(Unit&&)` deleted (`requires` check) | factors, conversion |
| geo | bounds; lon normalization; NaN; `Epsg` ≤ 0; `Epsg` not default-constructible; designated order | all |
| sample | `of(NaN) → nullopt`; `finite_or_missing`; `combine` precedence; `value()` | all |
| quantity | token round trip; `info` vs the SN §6 table (data-driven); `GenericQuantity::parse` rejects registry tokens and bad names; `datum_applicable` truth table | token, predicate |
| meta | datum on `wind_speed` rejected; `assume_*` when set → `already_set`; `with_label` total | — |
| timeseries | `make` length/order errors with index; `transform_samples`; `normalize`: clean fast path untouched; descents counted; first duplicate kept; conflicting count; stable order of equal times | — |
| station_table | each `TableErrc`; `single_axis` (one axis, equal axes, 1 ms difference); `series(i,k)` round trip; `from_series` schema mismatch; `StationSelection` duplicates/out of range | — |
| vector_series | non-pair rejected; B1 magnitude 0.050793…; N7; direction cases incl. (−1,−0.0) → 180; zero → Missing; derived meta table (§2.9) | — |
| series_ops | slice (both overloads); `shift_time` overflow; `scale_offset`; `convert` (table and series); `extent` (B13, B14) + monoid laws (exact); `Bucket` identity exact, associativity within 1e-12 on random splits, ties keep first; `quick_stats` peak equals `max_element` first; `residual` join/unit/datum-unknown/temperature/combine rule | `Bucket` small cases |
| datum | strings (N8); `from_heights` normalizes, MSL missing, equal duplicate ok, conflicting → error, NaN → error; `offset(d,d) = 0` even when unknown; antisymmetry; transitivity on dyadic values (exact) and random values (1e-12); F12 NOAA/XTide rows | all but F12 |
| datum_shift | pinned check order (4 cases); nulls/Dry kept; result datum; input unchanged | — |
| hwm | `is_dry` edges; `classify` at every break; defaults; invalid breaks | all |
| hwm_stats | `Moments` identity exact, `of(Dry)` = identity, fold halves ≈ whole; golden free and origin (F8, `golden.py` uses uncentred origin R²); `fit` alternative matches mode (N1); σ n−1 (D17); threshold rows (N2); legacy closed forms agree (free); error variants carry `total`/`wet` | `Moments` small |

### 7.2 io

| Component | Test cases |
|---|---|
| detail | **`parse_double` token table**: `+1.5`, `.5`, `5.`, `1e3`, `-9.9999E+004`, `0x1p3` (reject), `1,5` (reject), ` 1` (reject), `1.5x`, `nan`, `inf`, `""`, 400-digit mantissa, `1e400` (reject) — both implementation paths. Locale test: **precondition asserted**: `de_DE.UTF-8` must exist when `MOV_REQUIRE_LOCALES=1` (CI/dev image); otherwise `SKIP`. `parse_model_number` NaN/Inf/`****`. `LineCursor` BOM/CRLF/no final newline. `cut_at_nul` with junk after the NUL; `uniquify_ids`; `reserve_capped`. |
| text_file | B3 **ordering**: copy the fixture, chmod 0444, **assert opening for write fails** (skip if root), then read succeeds. Missing file. Directory. `max_text_bytes` exceeded. Atomic: fault injection at each stage leaves the target byte-identical and no temp file; success replaces. |
| cf_time | grammar cases incl. `milliseconds since 1970-01-01 00:00:00`; calendars; pre-1582; `to_time` out of range |
| nc::File | open nonexistent (B6); non-netCDF; moved-from → `closed`; **`-Wl,--wrap=nc_open,nc_create,nc_close,nc_abort` count test [linux]**: every open has exactly one close across 1000 open-then-fail paths (successful open, then a failing `find_var`/`read`/`text_att`) (B5); typed reads per §4.3 incl. `put` exact type (B4); masking: double/float fill, default fill (B9), NaN, `missing_value` vector, valid range, NC_NOFILL, `_Unsigned`, unpack after mask; `text_att` 120 B (B8) and above `max_att_bytes`; `read_char_rows` 20/50/64/300 (B7, B11); `find_*` absent (B12); `define_dim(0)`; `put_att` UTF-8 bytes (B15); count mismatch; `checked_product` overflow; `max_elements` → `too_large`; stop token honoured between slabs; `NcName` empty/257 B/embedded NUL; `NcNameRef` literal (compile-time, `requires`); `read_strings` LSan (B21); **non-ASCII and > 260-char paths** (all OSes); `NC_HAS_HDF5 == 1` (`netcdf_meta.h`); debug `nc_call` re-entry assert (death test via a child process) |
| imeds | goldens (`mllw`, `msl`); header tokens; N3; CRLF; tabs; BOM; blank lines (N13); empty; `< 3` header lines; bad date; **exact sentinels → Missing + count; −99998.9 and −999 kept as values (C9)**; unsorted → warning; duplicate names → `#2` + warning; out-of-range coords; read-only (B3); format golden bytes (D15); non-values omitted (N18); sanitize; floor seconds; `wrong_column_count`; round trip |
| adcirc_ascii | station file (N6; default `Station 0`; CRLF; count mismatch); fort.61 −99999 → Dry; fort.62 B1; partner −99999 → Missing (N7); fort.71/72; −950 kept as value (vs v4 −900); NaN/`****` → Missing + count; **partial last record dropped + warning**; **header NSnaps larger than content → warning**; **mid-file corruption → `corrupt_record`**; time beyond 2^53 → `time_out_of_range`; overlapping hot-start times → warnings |
| adcirc_netcdf | goldens; float variants (B4); default fill; zeta fill → Dry, u-vel → Missing; names; partner; cold start explicit vs `epoch_used`; selection required/out of range; non-increasing → error; masked time → `time_missing`; block read equals column read; ASCII == netCDF 1e-9; `[legacy]` originals |
| dflow | B2; name_len 64/20 (B11) incl. junk after NUL; time units; `Layer` 0 and n+1 (N16); `Flat` vs `AtLayer` typing; derived values and meta; `_FillValue`; NaN; float; missing dim/var (B12); catalog excludes `laydimw` |
| station_netcdf | SN §14.1 rows 1:1; `choose_layout`; status iff Dry; canonical conversion (ft → m, mH2O → hPa, ft3/s → m3 s-1); `noncanonical_unit`; `empty_collection`; injected `now` determinism; legacy names cut at NUL with a junk-padding fixture; legacy duplicate ids; legacy EPSG wrong type (B10); `epoch_used`; CRMS → `not_this_format`; `vertical_datum` on a generic `value` column round-trips |
| hwm_file | header only if all fields non-numeric (+ warning); `1,2,abc,…` first line → `ParseError`, not a header; blank lines (N20); 5/6/4 columns; range; Dry |
| csv_export | golden; quoting; `=SUM(A1)` station name → `'=SUM(A1)`; `-1.5` value **not** prefixed; Missing/Dry; ms timestamps; multi-column table |
| file_kind | each fixture; suffix rules; v5/foreign; CRMS-like |
| projection | 4326 passthrough; 26915 point (1e-7°); unknown code |
| read chain | `test_read_chain.cpp` compile-time type check plus one runtime run (§5.0) |

### 7.3 Fixtures

- Copied (< 1 MB) from `MetOceanViewer/function_tests`: `ReadIMEDS/*`,
  `ReadADCIRC/ASCII/*`.
- Generated at test time by `tests/fixtures/gen/make_nc_fixtures.cpp` (raw netCDF-C API,
  independent of `mov::io`, a CTest `FIXTURES_SETUP`):
  - ADCIRC (F1–F3, compact versions of the 4.2 MB originals);
  - DFlow (F5, from the D-Flow FM documentation, D26);
  - legacy A/B (F6; reproduces `hmdf.cpp:261-432`, including junk after the NUL in name
    rows, since v4's B15 over-read can produce it);
  - the SN CDL fixtures.
- Hand-written text: F4, F8, F9, F12.
- F8 golden numbers come from `golden.py`, written against the Python standard library
  (`statistics.linear_regression(proportional=True)` for the origin fit, with uncentred
  R² computed explicitly, and `fractions.Fraction` for exact sums). Its output is
  committed.
- **Hostile-structure set** (generator, WP10b), each expected to give a specific error and
  no crash or oversized allocation:
  - `station` dim of 2^31 with a data variable (→ `too_large`);
  - `obs_count` negative or larger than `obs`;
  - a 3-D `time`;
  - `name_len` 0 and 1-D char ids;
  - `_FillValue` of the wrong type;
  - `valid_range` with one element;
  - 5,000 data variables;
  - `coordinates` naming missing variables;
  - a `time` attribute `units` of 10 kB;
  - `stationLength_0001` huge;
  - dims in transposed order;
  - `NC_STRING` attributes on v5 files.
- Dev image: `de_DE.UTF-8` locale generated. **Dockerfile change for the build agent.**

### 7.4 Regression map

| Bug | Where | Bug | Where |
|---|---|---|---|
| B1 | vector_series, adcirc_ascii | N1 | hwm_stats fit alternative |
| B2 | dflow | N2 | hwm `is_dry`, threshold rows |
| B3 | text_file (ordered precondition), imeds | N3 | imeds |
| B4 | nc types; adcirc_nc, dflow, station_nc float | N5 | time `parse_utc_datetime` TZ |
| B5 | nc `--wrap` counts | N6 | adcirc station file |
| B6 | nc open; atomic stages | N7 | sample combine; adcirc partner |
| B7 | nc char rows; station_nc | N8 | datum strings |
| B8 | nc `text_att` | N13 | imeds |
| B9 | nc masking | N16 | dflow `Layer` |
| B10 | geo `Epsg`; station_nc | N17 | `empty()` |
| B11 | dflow, cf_time | N18 | imeds writer |
| B12 | nc `find_*`, dflow | N19 | spans + hardening; `StationTable` |
| B13, B14 | series_ops `extent` | N20 | hwm_file |
| B15 | nc `put_*`; station_nc | B19 | atomic writes (session test in Phase 6) |
| B16, B26, N15, N21, N22 | retired with CRMS (D7) | B17, B18, B20–B25 | later phases |

### 7.5 Fuzzing (D21)

Targets are built with Clang `-fsanitize=fuzzer,address,undefined`. Clang's
`-fsanitize=undefined` includes `float-cast-overflow`, so the fuzz preset covers the
double → integer casts that the owner left out of the GCC UBSan builds (D21).
`checked_time` makes those casts unreachable anyway. CI runs each target for 60 s;
nightly runs 10 min.

| Target | Oracle |
|---|---|
| `parse_imeds` | if ok: `parse(format(t)) == t` after 6-dp rounding |
| `parse_adcirc_ascii` + assemble (stations from the first bytes) | column lengths == axis length; times strictly increasing |
| `parse_adcirc_station_file` | `size() == count` |
| `parse_hwm_csv` | valid locations; finite `Wet` |
| `(units text, double)` → `parse_cf_time_units` + `to_time` | result is nullopt or within ±2^53 ms |
| `parse_vertical_datum`, `parse_unit`, `parse_quantity_token`, `parse_utc_datetime` | round trip / no UB |
| `parse_double` | both implementation paths agree |
| **structure fuzzer** | bytes → bounded netCDF schema (dims ≤ 8, vars ≤ 16, attribute types/lengths, legacy and SN names) → generated file → `read_station_netcdf`, `inspect_*`. Oracle: no crash, no allocation above `ReadLimits`, and either `Read` or `Error`. |

---

## 8. Work packages

| WP | Scope | Depends on | Agent | Size |
|---|---|---|---|---|
| WP1 | Vocabulary I: `time` (incl. `checked_time`, `parse_utc_datetime`), `units`, `geo`, `sample`, `quantity`, `datum` (enum, strings, `DatumTable`), probe | — | Sonnet | M |
| WP2 | Vocabulary II: `meta`, `timeseries` + `normalize`, `station`, `station_table` + `StationSelection`, `vector_series` | WP1 | Opus | L |
| WP3 | `series_ops` (`Bucket`, `extent`, `quick_stats`, `residual`, `convert`, `slice`, `shift_time`), `datum_shift` | WP2 | Sonnet | M |
| WP4 | `hwm`, `hwm_stats`, `golden.py` + F8 | WP1 | Sonnet (Opus reviews `Moments`) | M |
| WP5 | io `error`/`warning`/`read` (monad, `lift`), `text_file` (limits, atomic + fault injection), `detail` helpers, `cf_time`, `projection` (PROJ via vcpkg), fuzz scaffold + text targets | WP1 | Sonnet | M |
| WP6 | `nc::File`, `NcName`, `nc_call`, limits, masking, `write_netcdf_atomic`, `--wrap` tests, Windows path test, `make_nc_fixtures` + `FIXTURES_SETUP` | WP5 | Opus | L |
| WP7 | IMEDS, CSV, read-chain compile test | WP2, WP3, WP5 | Sonnet | M |
| WP8 | ADCIRC ASCII + station file, HWM file | WP2, WP4, WP5 | Sonnet | M |
| WP9 | ADCIRC netCDF, DFlow, block-read measurement | WP2, WP6 | Sonnet, Opus review | L |
| WP10a | SN writer and v5 reader (L1/L2, wet/dry, versioning, validation), `format-compliance` job | WP3, WP6 | Opus | L |
| WP10b | Legacy A/B + foreign CF (L3, scalar, packing), `detect_file_kind`, hostile set + structure fuzzer | WP10a | Sonnet, Opus review | L |

Waves:
1. WP1.
2. WP2 ∥ WP4 ∥ WP5.
3. WP3 ∥ WP6 ∥ WP8.
4. WP7 ∥ WP9 ∥ WP10a.
5. WP10b.

WP5, WP6, WP10a and WP10b touch CMake, vcpkg or CI files; schedule them with the build
agent. The `de_DE` Dockerfile change belongs with WP5.

---

## 9. Open items

### 9.1 Resolved: Dry samples in CSV export

Resolved by the maintainer on 2026-10-07: the CSV gets a `status` column (`value`, `missing`,
`dry`) after `value`. `value` is empty unless `status == value`, so the value column stays
numeric for pandas and spreadsheets. This extends D27's column list by one column.

### 9.2 `docs/station-netcdf.md` edits needed

1. **SN:252 (§8.2, "Reading legacy numeric sources").** It says D-Flow applies D16. This
   design exempts D-Flow (C9): D-Flow has no dry sentinel (a dry station reports bed level),
   and generic his variables such as bed level go below −999 m. **SN needs editing:** list
   only ADCIRC.
2. **SN §4.3 (line 143) and §10.2 (line 438) vs §11 (line 468).** §4.3 and §10.2 restrict
   `vertical_datum` to `water_level*`, but §11 maps the legacy `datum` attribute onto the
   generic `value` quantity. This design writes `vertical_datum` whenever the datum is
   engaged, and `datum_applicable` allows it on generic quantities. **SN §4.3 and §10.2
   should read "`water_level*` and generic quantities".**
3. SN §10.1 puts PROJ in "core"; it lives in io here (C19).
4. SN §12's reader returns `expected<StationFile, NcError>`; here it is
   `expected<Read<StationFile>, io::Error>`, and SN's error names map onto `FormatErrc`.
5. SN §12.8's writer preconditions are mostly ruled out by types here (§5.6). Only
   `empty_collection` and `noncanonical_unit` remain writer errors.
6. SN §11 accepts `%04i` and `%06i`. With dialect C dropped, only `{:04}` (widening past
   9999) is looked up.
7. SN §8.2's status trigger is defined here as "iff any sample of the column is Dry"
   (approved).
8. `VerticalDatum` adds `IGLD85` (the core enum is authoritative per SN §10.2).

### 9.3 Deferred

- The affine `Temperature` value type (Phase 3, first consumer).
- USGS daily cadence in `SeriesMeta` (Phase 3).
- `TimeRange::split` and chunk merging (Phase 3).
- The `.mvs` importer (Phase 6).
- Fuzzing netCDF from memory bytes (v5.x; the structure fuzzer covers schemas).

---

## Appendix A. Review resolution

Finding IDs are as triaged by the coordinator. **R** = resolved as asked; **P** =
partially adopted (reason given); **X** = rejected.

| ID | Finding | Status | Where |
|---|---|---|---|
| Ben Q1 | error composition | R: narrow in core, one variant per layer; `lift<V>`; `Read` writer monad + chain compile test | C8, §2.12, §5.0 |
| Ben Q2 | Dry on any quantity | R (accepted) | C1 |
| Ben B1, S1, S2; Sean S3 | datum/meta consistency | R: `datum_applicable` at construction; `assume_*`; derived-meta table; datum equality known-and-equal | C3, §2.6, §2.9, §2.10 |
| Ben S3; Sean S2 | finite by construction | R: `Sample::of → optional`; `finite_or_missing`; `value_not_finite`/`non_finite_value` deleted | C1, §2.4 |
| Ben S4; Sean S4 | `normalize` total over rows | R: `vector<Point>`, `adjacent_find` fast path, `descents`, conflicting warning; builder dropped | §2.7 |
| Ben S5 | narrow errors; split SeriesError | R: `ConstructionError` / `AlignmentErrc` | §2.7, §2.9 |
| Ben S6 | project at read boundary | R: `FileStation.location` (+ optional native) | C5 |
| Ben S7; Sean S8 | total QuickStats; NoWetMarks keeps total | R | §2.10, §3 |
| Ben S8; Sean N5 | `LinearFit` variant | R | §3 |
| Ben S9 | generic quantity identity | R: `GenericQuantity` | §2.5 |
| Ben S10; Sean S5 | folds | R: ordered left fold for `Moments`/`Bucket`; `transform_reduce` only for `Extent`; tolerance law tests | §2.10, §3, §7.1 |
| Ben N1; Sean S6 | units | R: `OtherUnit` only via `parse_unit`; `""` → nullopt; warning; deleted rvalue views | §2.2 |
| Ben N6; Sean S7 | DatumTable | R: no MSL slot; conflicts/NaN errors; total `offset(d,d)`; pinned shift order | §2.11 |
| Ben N15 | defer Temperature | P: value type deferred; `TemperatureUnit` kept, because `degC`/`degF` must classify as temperature for SN `units_metadata` and for the residual rule | C6 |
| Ben N16 | drop writer codes types rule out | R | §5.6 |
| Sean B1; Nate S6 | thread safety | R: no mutex; caller-serialized; `nc_call` + debug assert; include gate; slabs + `stop_token`; 8-thread test dropped | C11, §4.1 |
| Sean B2 | WP graph | R: vocabulary split WP1/WP2; `DatumTable` → WP1; waves recomputed | §6, §8 |
| Sean S1 | station record | R: `StationTable` (schema + axis pool + columns); `choose_layout` = `single_axis`; `ModelOutput` removed | C4, §2.8 |
| Sean N7 | transitivity tolerance | R: dyadic exact + random 1e-12 | §7.1 |
| Nate B1, B2 | time arithmetic | R: `checked_time` (both overloads), integer `CfTimeUnit`, masked time → error, fuzz targets, UBSan note | C13, §2.1, §5.1, §7.5 |
| Nate B3 | sizes | R: `checked_product`, `ReadLimits`, `too_large`, `reserve_capped`, required selection, text size check | C12, §4 |
| Nate B4 | names | R: `NcName`/`NcNameRef` (`consteval` literals) | §4.2 |
| Nate S1 | legacy names | R: `cut_at_nul` + junk fixture | C14, §7.3 |
| Nate S2 | masking | R: native-type `Masking<T>`, vector `missing_value`, `_Unsigned` → skip with warning (honouring it was rejected: no known source uses it), 64-bit rejected, exact `put` | §4.2–4.3 |
| Nate S3 | Windows paths | R: `nc_path` helper flagged for verification + CI test | §4.1 |
| Nate S4 | atomic write | R: six stages, `NC_NOCLOBBER`, `nc_abort`, private `create`, fault injection | §4.4 |
| Nate S5 | close policy, `--wrap` tests | R | §4.1, §7.2 |
| Nate S7 | chunk-aware reads | R + measurement owed in WP9 | §5.4 |
| Nate S10 | `Read<>` everywhere; `epoch_used` | R | §5 |
| Nate S11 | hostile set + structure fuzzer | R | §7.3, §7.5 |
| Nate S12 | non-vacuous tests | R: asserted locale precondition (Dockerfile change), B3 ordering, public N5 parser | §7.2 |
| Nate S13, S14 | text boundary | R: token table, BOM, all-non-numeric header rule, IMEDS id uniquify, `Location::make` with designated init | §5 |
| Owner | R² uncentred (D25), CSV long (D27), synthetic DFlow (D26), C9 veto (IMEDS exact sentinels; D16 model-only), formula injection, ADCIRC partial runs, Fortran NaN | R | C9, C10, C18, §5.3, §5.8 |
| Nits | ref-qualified views, sink overloads, `DimRange`, selection without duplicates, private `Moments` + null object, `StationFile` variant, `CfTimeUnit` enum, `Layered`/`AtLayer`, u/v pair check, parse-don't-validate ids, no `Epsg` default, designated initializers, no `<=>` on `TimeRange`/`Location`, `detail::Matrix`, `ImedsHeader` cleanup, regular/nothrow `STATIC_REQUIRE`s, decimation monoid (`Bucket`), `NC_HAS_HDF5` test, constexpr `<cmath>` probe | R | throughout |
| — | `StationId` keeps `<=>` | X (nit scope only covered `TimeRange`/`Location`): ids are used as ordered map keys and sorted lists | §2.8 |
