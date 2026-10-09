# Tide prediction engine, harmonics reader and builder (design)

Status: **proposed, revision 2** (2026-10-08). Revision 2 applies the type review, the
numerics/IO review and the owner's answers; Appendix A maps each finding to its resolution.
Implements plan §6 decision 31: v5 drops XTide and gets its own harmonic engine in `core`, a
reader for the published harmonics format in `io`, a builder in `tools/`, and (in Phase 3) a
`tides` provider.

Inputs:
- `docs/harmonics-json.md` (HJ): the normative file format, its schema
  `docs/schemas/harmonics-v1.schema.json`, the prediction model (HJ §5) and extremes (HJ §5.5).
- `docs/core-design.md` (CD): house rules and core/io types.
- `docs/research/tides-global-harmonics.md` (RS): data sources and licences.
- `docs/provider-apis.md` §2, §5 (PA).
- The legacy v4 XTide code, used for behaviour only (§9.1).

URLs were accessed on 2026-10-08. "Verified" means checked that day against the live CO-OPS API,
or by running the prototype of §1.

---

## 0. Decisions

| # | Topic | Decision |
|---|---|---|
| E1 | Nodal corrections | **NOS yearly scheme**: V₀ at 1 January 00:00 UTC, f and u at mid-year, speeds from 1900 rates (HJ §5.2). It matches every CO-OPS 6-minute point to a raw maximum of 1.81 mm; instantaneous evaluation misses by up to 31.5 mm (§1). Instantaneous evaluation exists only in `diagnostic::`. |
| E2 | Constituent model | SP 98 Darwin arguments and integer compounds of them, combined through a named monoid (`Argument`, §2.2). A constexpr NOS catalogue lives in core; the file's own dictionary is authoritative at run time (HJ §7.3). |
| E3 | Time base | `Time` (`sys_time<ms>`, UTC). Each sample's argument is `σ·Δh + (V₀+u)` with Δh = hours since 1 January of the sample's own year (≤ 8,784 h), wrapped into [0°, 360°) before the trig call. Sample grids are anchored to the Unix epoch with floor division, so they are correct before 1970. |
| E4 | Extremes | Per year segment (no extremum at the New Year step), on an epoch-anchored grid, with **guaranteed** root isolation: a cell is excluded only when the Lipschitz bound M = Σ f·H·ω² proves h′ has no root in it. Classified by the direction of the sign change. Then CO-OPS's 2-hour separation filter (HJ §5.5), measured on a year of CO-OPS data. |
| E5 | Subordinates | NOAA's rule (verified exact): time offsets, then a ratio or additive height relative to the offsets datum. Inverted pairs are dropped as a unit. The curve between events is XTide's warp, with a clamped ratio, labelled interpolated. |
| E6 | Datums | A reference series comes out relative to MSL, a subordinate series relative to `offsets.datum`, and both carry their datum as data (`TimeSeries` meta, `Events::datum`). Conversion goes through `shift` (CD §2.11, decision 18). `VerticalDatum` gains `hat`, `lat`, `dtl`. Each datum height records whether it was published or computed from harmonics. |
| E7 | Types | Resolved, read-only **views** (`ReferenceView`, `SubordinateView`) are the only inputs of prediction, and only `HarmonicsFile` builds them. Subordinates point at references through `ReferenceIndex`, so a chain cannot be expressed; the reference's offsets-datum height is checked once, so prediction has no "missing datum" error. |
| E8 | Reader | nlohmann/json `parse` with a **parser callback** that decodes and discards each station (measured: no memory growth over the input text). The decode is lenient into raw records that keep unknown members as data; strictness is decided after `version` is known. Errors are `SyntaxError{offset}` or `LocatedError{pointer, value}`. `parse_harmonics` stops at the first error in a pinned order; `validate_harmonics` accumulates. |
| E9 | Builder | Python 3.11+, standard library only, in `tools/harmonics/`. Hardened fetch (timeouts, retries, body caps, HTML rejection, pinned hashes), a licence allow-list that fails closed, deterministic order-independent merging. Computed datums come from the C++ CLI, so the engine stays the single implementation. Gate: the C++ validator plus the JSON Schema (§7). |
| E10 | Provider | Phase 3 `tides` provider: offline, reads the file chosen in Settings, CPU work on `QtConcurrent`, cancellable, never on the GUI thread (§9). |

---

## 1. Prototype evidence (2026-10-08)

A throwaway Python prototype implements HJ §5 and Appendix A. It lives in scratch, is not
committed, and WP H3 turns it into C++ tests and a fixture recorder. Its inputs are NOAA's
published constants (`harcon.json`, `phase_GMT`).

### 1.1 Series (every point)

The comparison uses CO-OPS `predictions` (datum MSL, GMT, 6-minute) over three windows:
2025-12-30 to 2026-01-02 (across the year boundary), 2026-06-29 to 07-04, and 2026-10-01 to 10-08.

| Station | Character | Points | Bias | Raw max | RMS | Instantaneous max |
|---|---|---|---|---|---|---|
| 8443970 Boston | semidiurnal, M2 1.4 m | 4,320 | ≤ 0.04 mm | 1.81 mm | 0.90 mm | 31.5 mm |
| 9414290 San Francisco | mixed | 4,320 | ≤ 0.02 mm | 1.26 mm | 0.51 mm | 13.2 mm |
| 8761724 Grand Isle | diurnal, small | 4,320 | ≤ 0.02 mm | 0.69 mm | 0.30 mm | 4.4 mm |
| 8720030 Fernandina Beach | semidiurnal, shallow | 4,320 | ≤ 0.02 mm | 1.37 mm | 0.64 mm | 17.6 mm |
| 9455920 Anchorage (114 constituents) | extreme shallow water | 864 (every 5th) | 0.2–1.8 mm | 42.8 mm (mean-removed) | 17 mm | — |

- The "instantaneous max" column uses every 10th point.
- **Anchorage.** Most of the error was naming. Aliasing `SIGMA1→SIG1`, `THETA1→THE1`,
  `OO2→OQ2-HORN`, `2MS8→2(MS)8`, `2MN8→2(MN)8` and `2MLNS6→2MNLS6` cut the maximum from 167 to
  43 mm. `M2KS2`, `2SNMK2` and `2KMSN2` (≤ 7 mm amplitude) remain undefined. This station is the
  stress test for the extended catalogue (WP H1).
- **Speeds.** The 37 SP 98 speeds match CO-OPS's published `speed` to ≤ 7.4e-6 °/h, which is
  NOAA's rounding.

### 1.2 High/low and the New Year step

- **Reference events, 2026-10-01 to 10-08** (17–31 per station, 10 s brute-force scan): times agree
  to ≤ 0.67 min and heights to ≤ 0.64 mm at the four stations. That is CO-OPS's rounding to the
  minute and the millimetre.
- **A full year (2026)** of CO-OPS `hilo` against every sign change of the prototype at 8443970,
  8761724, 8771450, 8771341 and 9455920:
  - Every CO-OPS event was found.
  - The prototype's extra events exist only at the Gulf stations (30, 50 and 76). They come in pairs
    0.01–1.99 h apart, with height differences ≤ 10.4 mm.
  - Consecutive CO-OPS events are never closer than 1.99 h, yet CO-OPS keeps pairs whose height
    difference is only 2 mm. The filter is on separation, not height: HJ §5.5.
- **The step at New Year.** At Boston it is −16.2 mm in 2026 and up to 36.7 mm over 2020–2039.
  - CO-OPS's 1-minute series shows it: Boston −1.573 → −1.566 m at 2032-01-01 00:00, and
    1.272 → 1.288 m at 2034-01-01 00:00.
  - Around 2026-01-01 the yearly model stays within 1.2 mm of CO-OPS on both sides, while the
    instantaneous residual jumps from +1.3 to −14.4 mm.
  - A naive sampled scan creates spurious extrema at 23:59/00:00 in most years; per-segment
    extremes (§4.1) cannot.
- **CO-OPS's own New Year events (informative).** CO-OPS evaluates events up to at least 13 minutes
  after 1 January with the previous year's arguments, but not an event 37 minutes after. It
  sometimes lists a step artefact at 23:59. The evidence:

  | Station, event | CO-OPS | Previous-year curve | Own-year curve |
  |---|---|---|---|
  | 8443970, 2032-01-01 00:02 L | −1.574 | −1.5738 | −1.5662 |
  | 8443970, 2034-01-01 00:07 H | 1.276 | 1.2759 | 1.2914 |
  | 9414290, 2035-01-01 00:13 H | 0.353 | 0.3523 | 0.3633 |
  | 8443970, 2028-01-01 00:37 L | −1.482 | −1.5015 | −1.4815 |
  | 8720030, 2036-12-31 23:59 H | 0.716 | rising through New Year | rising |

  HJ §5.5 does not reproduce these quirks. Golden comparisons exclude ±30 minutes around New Year.

### 1.3 Mid-year arguments and the independent implementations

- **Yearly versus instantaneous at t_M** (the claim in revision 1 that the two agree there was
  wrong).
  - f and u coincide at t_M. V differs by Σ a_k(ṙ_k(t) − ṙ_k(1900))·(t_M − t_Y), at most 4.3e-4°
    in 2026–2040 for every NOS constituent but M1.
  - M1 differs by b_Q·ṗ·(t_M − t_Y) ≈ 20.3°: its speed carries ṗ (HJ §7.2), its instantaneous
    argument does not.
- **pytides.**
  - pytides 0.0.4 (PyPI) is Python 2 only (implicit relative imports).
  - pytides2 0.0.5 (PyPI) pins `numpy<1.19.4` and does not build on Python ≥ 3.12 (`distutils`).
    Its source uses `collections.Iterable` (removed in 3.10). Python 3.11 could not be tried, because
    the host lacks `python3.11-venv`.
  - The WPringle fork (`github.com/WPringle/pytides`, commit `e6741a1`, 2021-05-05) is not
    pip-installable (Poetry pin to old numpy) but imports from source with numpy 2.5 on Python 3.13.
  - Compared at t_M in 2026 and 2040 (its astronomy is Meeus-based), it agrees with the yearly
    model to 0.02–0.2° for most constituents.
  - It differs by 1–3° and up to 0.1 in f for MU2, RHO, 2Q1 and 2MK3 (definitions differ), and by
    160° for M1.
  - Excluding M1, the largest single-constituent height difference at Boston is 1.1 mm and the sum
    of all of them is 3.5 mm. That is the honest tolerance of a pytides cross-check, and it only
    covers constituents whose definitions agree. pytides is therefore **not** a CI dependency
    (§8.3).
- **UTide 0.4.0** (PyPI, MIT, installs on Python 3.13) recovers the constants from 2 years of hourly
  prototype output at Boston:
  - M2 0.00°/−0.06 %, K1 0.00°/+0.18 %, S2 −0.15°/−0.22 %, O1 −0.12°/−0.09 %, N2 +0.10°/−0.19 %,
    K2 −0.01°/+0.56 %;
  - small constituents are worse (2N2 +3.3°), because the 15-constituent fit leaks the 22 omitted
    ones.

  This confirms the sign and reference of G against an independent analysis code.

### 1.4 Reader memory

Measured with nlohmann/json 3.12.0 on a 9.6 MB synthetic file. The single-header `json.hpp` has
SHA-256 `aaf127c0…5de63`.

| Approach | Peak RSS | Time |
|---|---|---|
| Text only | 23.6 MB | — |
| Whole DOM | 108.1 MB (+84 MB) | 0.57 s |
| Parser callback, decode and discard each station | 22.5 MB | 0.47 s |

Details in HJ §12.

---

## 2. `mov::core::tide`: constituents, arguments, astronomy, the file model

Headers are in `src/core/include/mov/core/tide/`, namespace `mov::core::tide`, standard library
only. House rules apply (CD §2, CLAUDE.md): regular value types, `expected` for every fallible
factory, ref-qualified views, designated initializers, no sentinels. `[[nodiscard]]` is omitted
below.

### 2.1 Small vocabulary: `phase.hpp`, `high_low.hpp`

```cpp
namespace mov::core::tide {

/// An angle in degrees, always in [0, 360).
class Phase {
 public:
  static constexpr std::optional<Phase> of(double deg) noexcept;   // nullopt iff non-finite
  /// r = fmod(deg, 360); if r < 0, r += 360; if r == 360 (−tiny + 360 rounds up), r = 0. Finite input only.
  static constexpr Phase wrap(double deg) noexcept;
  constexpr double degrees() const noexcept;
  constexpr double radians() const noexcept;
  friend constexpr bool operator==(Phase, Phase) = default;
 private:
  explicit constexpr Phase(double d) noexcept;
  double deg_;
};

template <class T>
struct HighLow {
  T high;
  T low;
  friend constexpr bool operator==(const HighLow&, const HighLow&) = default;
};

enum class Tide : std::uint8_t { high, low };
template <class T> constexpr const T& at(const HighLow<T>& v, Tide k) noexcept;   // v.high or v.low

}  // namespace mov::core::tide
```

`constexpr std::fmod` is C++23 (P0533), behind the CD §1 `<cmath>` probe.

### 2.2 Constituent definitions and the `Argument` monoid: `constituent.hpp`

```cpp
namespace mov::core::tide {

/// SP 98 node-factor formulas (HJ Appendix A.3). Token: "unity" or "sp98-<n>".
/// The schema's enum is drift-tested against this enum.
enum class NodeFactor : std::uint8_t { unity, f73, f74, f75, f76, f77, f78, f79, f144, f149, f206, f215, f227, f235 };
inline constexpr std::size_t node_factor_count = 14;
constexpr std::string_view to_token(NodeFactor f) noexcept;
constexpr std::optional<NodeFactor> parse_node_factor(std::string_view token) noexcept;

/// V = Σ v·(T, s, h, p, p₁) + c;  u = Σ u·(ξ, ν, ν′, 2ν″, Q, R, Qᵤ).
struct Darwin {
  std::array<std::int8_t, 5> v;
  std::int16_t c_deg;
  std::array<std::int8_t, 7> u;
  NodeFactor f;
  friend constexpr bool operator==(const Darwin&, const Darwin&) = default;
};

/// ^[A-Za-z0-9][A-Za-z0-9()_.+-]{0,31}$, stored inline (no allocation, constexpr).
class ConstituentName {
 public:
  static constexpr std::expected<ConstituentName, ConstituentNameError> make(std::string_view s);   // empty, too_long, bad_char
  constexpr std::string_view view() const& noexcept;
  std::string_view view() const&& = delete;
  friend constexpr auto operator<=>(const ConstituentName&, const ConstituentName&) = default;
 private:
  detail::FixedString<32> s_;
};

/// Terms of a compound: a map name → non-zero coefficient, sorted by name, no duplicates
/// (a JSON object cannot express the duplicates, and make() refuses them from any other source).
class CompoundTerms {
 public:
  struct Term { ConstituentName base; std::int8_t coefficient; };
  static std::expected<CompoundTerms, CompoundErrc> make(std::vector<Term> terms);   // empty, too_many (> 8), zero_coefficient, duplicate_base
  std::span<const Term> terms() const& noexcept;
  friend bool operator==(const CompoundTerms&, const CompoundTerms&) = default;
};
using Definition = std::variant<Darwin, CompoundTerms>;

/// The recipe of one constituent's equilibrium argument and node factor.
///
/// (Argument, +, Argument{}) is a commutative monoid: + adds the V, c and u coefficients and adds
/// the node-factor exponents, because f is a product (f(a + b) = f(a)·f(b)).
/// scale(k, a) multiplies V, c and u by k but the exponents by |k|: f is multiplied, never divided
/// (IHO Standard List, Annex B). Scaling is therefore NOT a module action:
/// scale(k, a) + scale(−k, a) is the identity only when a has no node factor.
/// Compounds are folds: Σᵢ scale(cᵢ, darwin(baseᵢ)).
class Argument {
 public:
  constexpr Argument() noexcept = default;                                      // identity
  static constexpr Argument darwin(const Darwin& d) noexcept;
  friend constexpr Argument operator+(const Argument& a, const Argument& b) noexcept;
  friend constexpr Argument scale(std::int8_t k, const Argument& a) noexcept;
  constexpr double speed_deg_per_hour() const noexcept;                         // HJ §7.2, including b_Q·ṗ
  constexpr double v0_deg(const Elements& at_year_start) const noexcept;        // linear in the elements
  double u_deg(const NodalTerms& at_mid_year) const noexcept;
  double f(const NodalTerms& at_mid_year) const noexcept;                       // Π f_formula^exponent
  friend constexpr bool operator==(const Argument&, const Argument&) = default;
 private:
  std::array<std::int16_t, 5> v_{};
  std::int32_t c_deg_{};
  std::array<std::int16_t, 7> u_{};
  std::array<std::uint8_t, node_factor_count> exponent_{};                     // unity's slot stays 0
};

}  // namespace mov::core::tide
```

`STATIC_REQUIRE` laws (§8.1):
- identity and associativity are exact on integer coefficients;
- `scale(1, a) == a`;
- `scale(2, a) == a + a`;
- `scale(−1, a) + a` has zero V/c/u but doubled exponents.

### 2.3 Catalogue, constituent set and astronomy: `catalogue.hpp`, `constituent_set.hpp`, `astronomy.hpp`

```cpp
namespace mov::core::tide {

struct CatalogueEntry { std::string_view name; std::variant<Darwin, std::array<std::pair<std::string_view, std::int8_t>, 4>> definition; std::uint8_t terms; };
/// The 37 NOS constituents (HJ Appendix B), then the SP 98 Table 2 / congen extension needed by
/// stations such as Anchorage. static_assert invariants: names unique case-insensitively; compound
/// terms name Darwin entries; T coefficient = species; NOAA's 37 published speeds match within 1e-5 °/h.
inline constexpr std::array nos_catalogue = std::to_array<CatalogueEntry>({ /* … */ });

/// Strong index, constructible only by ConstituentSet.
class ConstituentIndex {
 public:
  constexpr std::uint16_t value() const noexcept;
  friend constexpr auto operator<=>(ConstituentIndex, ConstituentIndex) = default;
 private:
  friend class ConstituentSet;
  explicit constexpr ConstituentIndex(std::uint16_t v) noexcept;
  std::uint16_t v_;
};

struct DictionaryEntry { ConstituentName name; Definition definition; };
struct DuplicateName { std::size_t entry; std::size_t first; };
struct UnknownTerm { std::size_t entry; ConstituentName term; };
struct TermNotDarwin { std::size_t entry; ConstituentName term; };
using DictionaryError = std::variant<DuplicateName, UnknownTerm, TermNotDarwin>;

/// A resolved dictionary: every entry flattened into an Argument.
class ConstituentSet {
 public:
  static std::expected<ConstituentSet, DictionaryError> make(std::vector<DictionaryEntry> entries);  // names unique case-insensitively
  std::size_t size() const noexcept;
  std::optional<ConstituentIndex> find(std::string_view exact_name) const noexcept;    // sorted index
  const ConstituentName& name(ConstituentIndex) const& noexcept;
  const Definition& definition(ConstituentIndex) const& noexcept;
  const Argument& argument(ConstituentIndex) const& noexcept;
  double speed(ConstituentIndex) const noexcept;                                       // derived; the file's is a checksum
};

inline constexpr Time sp98_epoch = std::chrono::sys_days{std::chrono::year{1899} / 12 / 31} + std::chrono::hours{12};
constexpr double julian_centuries(Time t) noexcept;          // one division of an exact integer ms count
struct Elements { double T, s, h, p, p1, N; };               // degrees, not reduced
constexpr Elements elements(double centuries) noexcept;      // HJ A.1 polynomials
constexpr Elements rates_1900() noexcept;                    // °/h
struct NodalTerms { double I, nu, xi, nu1, two_nu2, P, Q, R, Qu, Qa, Ra; };
NodalTerms nodal_terms(double N_deg, double p_deg) noexcept; // HJ A.2; runtime (trig constexpr only in C++26)
double node_factor(NodeFactor f, const NodalTerms& n) noexcept;

inline constexpr std::chrono::year first_year{1700}, last_year{2200};    // HJ §5.3
struct YearOutOfRange { std::chrono::year year; friend constexpr bool operator==(YearOutOfRange, YearOutOfRange) = default; };

struct ConstituentYear { Phase argument; double node_factor; };   // (V₀+u) at 1 Jan 00:00 UTC, f at mid-year
class YearArguments {
 public:
  std::chrono::year year() const noexcept;
  Time start() const noexcept;                                     // derived from year()
  const ConstituentYear& operator[](ConstituentIndex i) const& noexcept;
 private:
  friend std::expected<YearArguments, YearOutOfRange> year_arguments(const ConstituentSet&, std::chrono::year);
  std::chrono::year year_;
  std::vector<ConstituentYear> by_constituent_;
};
std::expected<YearArguments, YearOutOfRange> year_arguments(const ConstituentSet& set, std::chrono::year y);

}  // namespace mov::core::tide
```

`docs/schemas/constituents-nos.json`, the NOS dictionary in file syntax, is **generated** from
`nos_catalogue` by a test executable. A CI test fails if the committed file differs. The builder
reads it, so core is the single source of truth.

### 2.4 Licences: `licence.hpp`

```cpp
namespace mov::core::tide {
/// SPDX expression subset (HJ §6.1), parsed into tokens.
class Licence {
 public:
  static std::expected<Licence, LicenceError> parse(std::string_view expr);   // LicenceError{offset}
  std::span<const std::string> identifiers() const& noexcept;                 // in order of appearance
  bool carries_non_commercial() const noexcept;                               // any identifier with a "-NC" component
  std::string_view text() const& noexcept;
  friend bool operator==(const Licence&, const Licence&) = default;
};
}
```

### 2.5 The file in memory: `harmonics.hpp`

```cpp
namespace mov::core::tide {

struct SourceIndex      { std::uint32_t value; friend constexpr auto operator<=>(SourceIndex, SourceIndex) = default; };
struct ReferenceIndex   { std::uint32_t value; friend constexpr auto operator<=>(ReferenceIndex, ReferenceIndex) = default; };
struct SubordinateIndex { std::uint32_t value; friend constexpr auto operator<=>(SubordinateIndex, SubordinateIndex) = default; };
using StationIndex = std::variant<ReferenceIndex, SubordinateIndex>;
// The index types have private constructors befriended by HarmonicsFile (shown as aggregates for brevity).

struct Source { SourceToken id; StationText name; std::optional<std::string> url; Licence licence;
                std::string attribution; std::chrono::sys_days retrieved;
                std::optional<std::string> version, sha256, notes; };

struct Uncertainty { Length amplitude; Phase phase; };
struct Constant { ConstituentIndex constituent; Length amplitude; Phase phase; std::optional<Uncertainty> sd; };

enum class DatumOrigin : std::uint8_t { published, computed };   // computed = "computed from harmonics, not observed"
class Datums {
 public:
  const DatumTable& table() const& noexcept;                        // published ∪ computed (disjoint: HJ E-DATUM-CONFLICT)
  std::optional<DatumOrigin> origin(VerticalDatum d) const noexcept; // nullopt: unknown; MSL → published
  const std::optional<std::string>& published_epoch() const& noexcept;
  const std::optional<ValidRange>& computed_period() const& noexcept;
};

enum class Analysis : std::uint8_t { nos, utide, ttide, least_squares, published };   // absent = unknown
struct Provenance { std::optional<std::string> source_record; std::vector<std::string> also_in, dropped_constituents; std::optional<std::string> notes; };

struct StationInfo {
  StationKey id; StationText name; Location location;     // Location: lon in (−180, 180]
  SourceIndex source; std::optional<Licence> licence;
  std::optional<std::string> country, time_zone;
  Datums datums;
  std::optional<ValidRange> record; std::optional<double> record_years;   // ValidRange: CD §2.1
  std::vector<std::string> flags;                                          // kept verbatim, rendered as plain text
  Provenance provenance;
};

struct HeightRatio  { HighLow<double> ratio; };    // > 0
struct HeightOffset { HighLow<Length> offset; };
using HeightAdjustment = std::variant<HeightRatio, HeightOffset>;
class SubordinateOffsets {
 public:
  /// Minutes → ms: llround(min × 60000), −0 → 0; |Δt| ≤ 1 day; ratios finite and > 0.
  static std::expected<SubordinateOffsets, OffsetsErrc> make(HighLow<double> minutes, HeightAdjustment h, VerticalDatum datum);
  const HighLow<std::chrono::milliseconds>& time() const& noexcept;
  const HeightAdjustment& height() const& noexcept;
  VerticalDatum datum() const noexcept;
};

class HarmonicsFile;

/// Read-only views, created only by HarmonicsFile; valid while the file lives
/// (providers hold std::shared_ptr<const HarmonicsFile> for the duration of a task).
class ReferenceView {
 public:
  const StationInfo& info() const& noexcept;
  const ConstituentSet& constituents() const& noexcept;
  std::span<const Constant> constants() const& noexcept;    // non-empty, unique constituents, H ≥ 0
  Length z0() const noexcept;
  std::optional<Analysis> analysis() const noexcept;
 private:
  friend class HarmonicsFile;
  ReferenceView(const HarmonicsFile& f, ReferenceIndex i) noexcept;
  const HarmonicsFile* file_;                                // non-owning
  ReferenceIndex index_;
};
class SubordinateView {
 public:
  const StationInfo& info() const& noexcept;
  ReferenceView reference() const noexcept;
  const SubordinateOffsets& offsets() const& noexcept;
  Length reference_datum_height() const noexcept;           // d_R(D) above MSL; present by construction
 private:
  friend class HarmonicsFile;
  SubordinateView(const HarmonicsFile& f, SubordinateIndex i) noexcept;
  const HarmonicsFile* file_;
  SubordinateIndex index_;
};

/// Inputs to make(): references by station key, constants by constituent name. make() resolves
/// every name and id exactly once.
struct ConstantInput { ConstituentName constituent; double amplitude_m; double phase_deg; std::optional<std::pair<double, double>> sd; };
struct ReferenceInput { StationInfoInput info; std::vector<ConstantInput> constants; double z0_m; std::optional<Analysis> analysis; };
struct SubordinateInput { StationInfoInput info; StationKey reference; HighLow<double> minutes; HeightAdjustment height; VerticalDatum datum; };

struct DuplicateStation { std::size_t first, second; };
struct UnknownSource { std::size_t station; };
struct UnknownConstituent { std::size_t station, row; };
struct DuplicateRow { std::size_t station, row; };
struct ReferenceMissing { std::size_t subordinate; };
struct ReferenceIsSubordinate { std::size_t subordinate; };
struct OffsetDatumMissing { std::size_t subordinate; VerticalDatum datum; };
using HarmonicsError = std::variant<DuplicateStation, UnknownSource, UnknownConstituent, DuplicateRow,
                                    ReferenceMissing, ReferenceIsSubordinate, OffsetDatumMissing>;

class HarmonicsFile {
 public:
  static std::expected<HarmonicsFile, HarmonicsError> make(FileHeader h, std::vector<Source> sources, ConstituentSet set,
                                                           std::vector<ReferenceInput> references, std::vector<SubordinateInput> subordinates);
  const FileHeader& header() const& noexcept;
  std::span<const Source> sources() const& noexcept;
  const ConstituentSet& constituents() const& noexcept;
  std::size_t reference_count() const noexcept;
  std::size_t subordinate_count() const noexcept;
  ReferenceView reference(ReferenceIndex) const& noexcept;
  SubordinateView subordinate(SubordinateIndex) const& noexcept;
  std::optional<StationIndex> find(std::string_view id) const noexcept;   // sorted id index
  const Licence& effective_licence(StationIndex) const& noexcept;        // override, else source
  // && overloads of the view accessors are deleted (a view into a temporary dangles)
};

}  // namespace mov::core::tide
```

- References and subordinates live in separate vectors.
- A subordinate's reference is a `ReferenceIndex`, so it can only point at a reference station:
  chains cannot be expressed.
- `make` checks the offsets datum (`OffsetDatumMissing`). That is why prediction has no
  `MissingOffset` error, and why `HarmonicConstants` has no `index_out_of_range`: a
  `ConstituentIndex` only comes from the file's own set, and constants are only reachable through a
  view of the same file.

Vocabulary changes outside `tide/`:
- `VerticalDatum` gains `hat`, `lat`, `dtl` (tokens `HAT`, `LAT`, `DTL`; SN §10.2 says the core enum
  is authoritative), and `DatumTable` grows to 12 slots.
- `DataSource` gains `harmonics` (SN `station_provider` token `"harmonics"`, SN minor bump; `"xtide"`
  stays readable).
- `provider::Xtide` becomes `provider::Harmonics` with HJ's `<source>:<local>` grammar.
- `Cancelled` moves from io to core (`mov::core::Cancelled`), and `io::Error` keeps it through a
  using-declaration, so core prediction and io share one alternative.

---

## 3. Prediction: `predict.hpp`

```cpp
namespace mov::core::tide {

class Interval {                                             // 1 s ≤ step ≤ 1 day
 public:
  static constexpr std::expected<Interval, IntervalErrc> make(std::chrono::milliseconds step) noexcept;
  constexpr std::chrono::milliseconds step() const noexcept;
};

/// One budget for all prediction work: term evaluations = evaluation points × constants.
struct PredictionLimits { std::uint64_t max_term_evaluations = 1'000'000'000; };   // about 3–5 s single-threaded
struct TooMuchWork { std::uint64_t estimated, limit; friend constexpr bool operator==(TooMuchWork, TooMuchWork) = default; };
using PredictionError = std::variant<YearOutOfRange, TooMuchWork, Cancelled>;

/// Heights above the station's MSL at t = k·step (k integer, counted from 1970-01-01T00:00Z with floor
/// division, also before 1970) within [begin, end). Meta: water_level_prediction, metre, datum msl.
std::expected<TimeSeries, PredictionError>
predict(const ReferenceView& station, TimeRange range, Interval step, const PredictionLimits& limits = {}, std::stop_token stop = {});

std::expected<Length, PredictionError> height_at(const ReferenceView& station, Time t);

namespace diagnostic {   // tests and investigations only; never user-visible output (HJ §5.2)
enum class NodalMode : std::uint8_t { nos_yearly, instantaneous };
std::expected<TimeSeries, PredictionError>
predict(const ReferenceView& station, TimeRange range, Interval step, NodalMode mode, const PredictionLimits& limits = {});
}
}
```

### 3.1 Algorithm

1. **Grid.** `k₀ = ceil_div(begin_ms, step_ms)` and `k₁ = ceil_div(end_ms, step_ms)`, where
   `ceil_div(a, b) = a / b + (a % b > 0)`. C++ truncation is already the ceiling for negative `a`.
   The samples are `k·step` for k in [k₀, k₁). Tests cover ranges entirely before 1970 and ranges
   straddling it.
2. **Budget.** `(k₁ − k₀) × constants` is computed with `checked_product` and compared with
   `max_term_evaluations` before any work (`TooMuchWork{estimated, limit}`).
3. **Segments.** Split at each 1 January 00:00 UTC. Per segment, call `year_arguments`
   (`YearOutOfRange` outside [1700, 2200]) and precompute per constant `A = f·H`,
   `φ₀ = (V₀+u) − G` and `ω = σ` (°/h).
4. **Evaluate.** For each sample, with `Δh = (t − start_of_year).count() / 3.6e6`:
   `h = z0 + Σ A·cos(rad(wrap(ω·Δh + φ₀)))`, where `wrap` is `Phase::wrap`: fmod, then +360 if
   negative, then 0 if the result rounded to 360. Check `stop` once per segment and every 2¹⁶
   samples (`Cancelled`).
5. **Output.** `TimeSeries::make` (always finite samples) with meta `{water_level_prediction, metre,
   msl}`. The caller relabels it with the station name.

### 3.2 Numerical concerns

| Concern | Treatment |
|---|---|
| Time precision | `Time` is integer ms. Δh, a double computed from at most 3.2e10 ms, is accurate to about 1e-12 h within a year. |
| Argument growth | ω·Δh ≤ 116 °/h × 8,784 h ≈ 1.02e6°. Wrapping before the radian conversion keeps the absolute error near 1e-10°. The per-year origin is NOS's own, so the year step (§1.2) matches CO-OPS. |
| Year step | Intrinsic to the method, up to 3.7 cm at Boston. Not smoothed. Residuals (EX #3) see it, as they see it in NOAA's data. |
| Polynomial validity | SP 98 elements are fitted about 1900. Years are limited to [1700, 2200] (HJ §5.3). |
| UT1/TT | Ignored. UT1−UTC < 0.9 s gives ≤ 0.007° for M2. `sys_time` has no leap seconds, like UTC civil time. |
| Summation | ≤ 512 terms of ≤ 10 m. Plain double summation errs far below 1e-9 m. |
| Cost | 10 years of 6-minute samples × 37 constants is 3.2e7 term evaluations, about 0.3 s (to be measured in H3). If needed: a phasor recurrence re-anchored daily (error < 1e-12), or parallel year segments. |

---

## 4. Extremes and subordinate stations: `events.hpp`, `extremes.hpp`, `subordinate.hpp`

### 4.1 Reference extremes

```cpp
namespace mov::core::tide {
struct Event { Time time; Length height; friend constexpr bool operator==(const Event&, const Event&) = default; };
struct TideEvent { Tide kind; Event event; friend constexpr bool operator==(const TideEvent&, const TideEvent&) = default; };
/// Events carry their datum like a TimeSeries does.
struct Events { VerticalDatum datum; std::vector<TideEvent> events; friend bool operator==(const Events&, const Events&) = default; };
std::expected<Events, MissingOffset> shift(Events e, VerticalDatum to, const DatumTable& table);   // overload of CD §2.11 shift

struct ExtremaOptions {
  std::chrono::milliseconds scan_step{std::chrono::minutes{6}};         // performance only; correctness does not depend on it
  std::chrono::milliseconds tolerance{std::chrono::seconds{1}};
  std::chrono::milliseconds min_separation{std::chrono::hours{2}};      // HJ §5.5; 0 = every sign change (diagnostics)
};
/// Heights above MSL (datum msl).
std::expected<Events, PredictionError>
extremes(const ReferenceView& station, TimeRange range, const ExtremaOptions& o = {}, const PredictionLimits& l = {}, std::stop_token stop = {});
}
```

The algorithm follows HJ §5.5. Within each year segment `[t_Y, t_{Y+1}) ∩ [begin − s, end + s)`
(s = `scan_step`):

1. **Grid.** Points at `k·scan_step` since the epoch (floor division as in §3.1), plus the segment
   ends. Results therefore do not depend on where the range starts.
2. **Bound.** `g = h′` is analytic: `g(t) = −Σ A·ω_rad·sin(φ(t))`, with ω in rad/h. Its Lipschitz
   constant is `M = Σ f·H·ω_rad²` (rad/h²), an upper bound on |h″|.
3. **Isolation, for each half-open cell [a, b).** Let `s(t) = (g(t) > 0)`, so zero counts as
   negative and a root exactly on a grid point belongs to exactly one cell.
   - If `|g(a)| + |g(b)| > M·(b − a)`, g has no root in the cell: skip it. This is the only way a
     cell is discarded, so no extremum is ever missed.
   - Else, if `s(a) ≠ s(b)`: bracket, refine by bisection or Brent on g until `b − a < tolerance`,
     and classify by direction: s from true to false is a **high**, false to true is a **low**.
   - Else (same sign, not excluded): the cell may hold an even number of roots, for example two
     extrema one minute apart. Split at the midpoint and recurse. A cell narrower than `tolerance`
     with no sign change is a tangency and is not an extremum. Depth is at most log₂(scan_step /
     tolerance) ≈ 9.
4. **Boundaries.** The segment end belongs to the next segment and is never itself an extremum, so
   the step at New Year cannot create one.
5. **Height.** `h(root)` from the segment's own arguments. Times are rounded to whole ms.
6. **Merge and filter.** Merge the segments, then apply the separation filter: repeatedly remove the
   adjacent opposite-kind pair with the smallest separation while it is < `min_separation`. Ties go
   to the earlier pair. Then drop events outside [begin, end).
7. **Budget.** Estimated term evaluations are `(grid points × 1 + expected extrema × 64) × constants`
   (64 is the refinement cap). The estimate is checked up front and the actual count during the
   run, both against `max_term_evaluations`.

The exact CO-OPS boundary of the 2-hour rule (< versus ≤, tie order) is pinned by the H3 goldens
(§8.2). If CO-OPS differs, the default and HJ §5.5 change in a patch release.

### 4.2 Subordinate extremes (HJ §5.4)

```cpp
namespace mov::core::tide {
struct SubordinateEvent { Tide kind; Event event; Event reference; friend bool operator==(const SubordinateEvent&, const SubordinateEvent&) = default; };
struct SubordinateEvents { VerticalDatum datum; std::vector<SubordinateEvent> events; std::size_t dropped_pairs; };

std::expected<SubordinateEvents, PredictionError>
subordinate_extremes(const SubordinateView& station, TimeRange range, const ExtremaOptions& o = {},
                     const PredictionLimits& l = {}, std::stop_token stop = {});
}
```

1. `d = station.reference_datum_height()` (present by construction, E7).
2. Compute reference events (§4.1, separation filter on) over
   `[begin − max(Δt_H, Δt_L, 0) − 1 day, end − min(Δt_H, Δt_L, 0) + 1 day)`, so every output time is
   bracketed.
3. Map each event: `t + Δt_kind`, and either `r_kind·(h − d)` or `(h − d) + a_kind`.
4. **Inversion.** Walk the mapped events in reference order. Where an event is not strictly later
   than its predecessor, remove **both** (the pair as a unit, which keeps high/low alternation) and
   count it in `dropped_pairs`. Repeat until the times are strictly increasing. XTide documents the
   same pathology (`SubordinateStation.cc:194-207`). The app shows the count as a warning. A
   station that exercises it is 8635299 Port Royal (−139/−182 min).
5. Keep the events in [begin, end). The datum is `offsets.datum`.

Whether CO-OPS re-applies the 2-hour filter after mapping is pinned in H4 with a subordinate of a
Gulf reference.

### 4.3 Subordinate curve (interpolated; not a NOAA product)

```cpp
std::expected<TimeSeries, PredictionError>
subordinate_predict(const SubordinateView& station, TimeRange range, Interval step,
                    const PredictionLimits& l = {}, std::stop_token stop = {});
// meta: water_level_prediction, metre, datum = offsets.datum, label suffix " (interpolated)"
```

This is XTide's warp, restated from `thirdparty/xtide-2.15.1/libxtide/SubordinateStation.cc:209-218`
(behaviour only, not copied). For t in [Eᵢ, Eᵢ₊₁) between consecutive subordinate events, with
uncorrected reference times τᵢ, τᵢ₊₁, reference heights ρᵢ, ρᵢ₊₁ (relative to D) and corrected
heights ηᵢ, ηᵢ₊₁:

```
τ  = τᵢ + (τᵢ₊₁ − τᵢ) · (t − Eᵢ) / (Eᵢ₊₁ − Eᵢ)                          // map the time in
if |ρᵢ₊₁ − ρᵢ| ≤ 1e-9 · max(|ρᵢ|, |ρᵢ₊₁|, 1 m):                              // relative threshold
    q = (t − Eᵢ) / (Eᵢ₊₁ − Eᵢ)                                                  // linear fallback
else:
    q = clamp((h_ref,D(τ) − ρᵢ) / (ρᵢ₊₁ − ρᵢ), 0, 1)                           // clamp: no overshoot
h(t) = ηᵢ + (ηᵢ₊₁ − ηᵢ) · q
```

The clamp matters because the reference curve can wiggle beyond its listed extremes between two
filtered events (the 2-hour filter removes wiggle pairs, §1.2). Without the clamp the subordinate
curve would overshoot its own events.

**Property test** (random constants, random offsets of both kinds, random ranges):
- h(Eᵢ) = ηᵢ exactly;
- h stays within [min(ηᵢ, ηᵢ₊₁), max(ηᵢ, ηᵢ₊₁)];
- h is monotone between events;
- event kinds alternate;
- with identity offsets (ratio 1, Δt 0) and the separation filter off, h equals `h_ref − d` to
  1e-12 m wherever the reference is monotone between its events.

---

## 5. Datum and unit integration

| Series | Datum out of core | Shifting to X |
|---|---|---|
| Reference series and events | `msl` | `shift(series, X, info.datums.table())` / `shift(events, X, …)`. Fails with `MissingOffset` if X is unknown, never silently 0 (decision 18). |
| Subordinate series and events | `offsets.datum` (NOAA: `mllw`) | Needs the subordinate's own `datums` containing both D and X; otherwise only D is offered. CO-OPS itself serves subordinates only in MLLW (verified). |

- **Units.** `convert(series, Unit{LengthUnit::foot})`, exact 0.3048. v4 used ×3.28084.
- **Origin labels.** `Datums::origin(X)` tells the UI and exporters whether X was published or
  computed from harmonics. Computed datums are labelled "computed from harmonics, not observed" in
  the datum selector, the chart axis and export metadata (HJ §8.4, owner decision 2026-10-08).
- **Offered datums.** The app offers exactly what `Datums::table()` can deliver, plus MSL for
  reference stations. This fixes v4's "always MLLW" labelling (§9.1).

---

## 6. `mov::io` harmonics reader and writer

### 6.1 API and errors: `harmonics_json.hpp`

```cpp
namespace mov::io {

struct HarmonicsLimits {                                    // HJ §10.3
  std::uintmax_t max_file_bytes = std::uintmax_t{64} << 20; // the read stops at max + 1 bytes
  std::uintmax_t max_json_bytes = std::uintmax_t{64} << 20; // after gunzip
  std::size_t max_depth = 16;
  std::size_t max_string_bytes = 4096;
  std::size_t max_array_length = 4096;                      // every array but /stations
  std::size_t max_stations = 1'000'000;
  std::size_t max_values_per_station = 65'536;              // bounds one station's transient DOM
};

enum class SyntaxErrc : std::uint8_t { size, gzip, not_json, encoding };
struct SyntaxError { SyntaxErrc code; std::size_t offset; friend bool operator==(const SyntaxError&, const SyntaxError&) = default; };

enum class JsonErrc : std::uint8_t {
  duplicate_member, limit, not_this_format, version, major, schema, convention, duplicate_source,
  duplicate_station, duplicate_constituent, duplicate_row, unknown_source, unknown_constituent,
  compound_term, speed_mismatch, reference, offset_datum, datum_conflict, licence, date,
};
/// RFC 6901 pointer, stored as tokens; rendered to text only by to_string() at the edge.
class JsonPointer {
 public:
  using Token = std::variant<std::uint32_t /*array index*/, std::string /*member*/>;
  std::span<const Token> tokens() const& noexcept;
  std::string to_string() const;                            // "~" → "~0", "/" → "~1"
  friend bool operator==(const JsonPointer&, const JsonPointer&) = default;
};
/// The offending value, not prose. Strings are truncated to 64 B (UTF-8 boundary).
using JsonValue = std::variant<std::monostate, std::int64_t, double, std::string>;
struct LocatedError { JsonErrc code; JsonPointer pointer; JsonValue value; friend bool operator==(const LocatedError&, const LocatedError&) = default; };
using JsonError = std::variant<SyntaxError, LocatedError>;
// io::Error gains JsonError. WarningCode gains bom_ignored, minor_newer (with per-reason counts in
// Warning::subject/count), constituent_redefined, unused_constituent, unused_source.

/// Stops at the first error, in the order of HJ §10.4. The fuzz entry point.
std::expected<Read<core::tide::HarmonicsFile>, JsonError>
parse_harmonics(std::span<const std::byte> bytes, const HarmonicsLimits& limits = {}, std::stop_token stop = {});

/// Accumulates every error it can reach (all of them after a syntax error is impossible), plus warnings and a summary.
struct HarmonicsSummary { std::size_t references, subordinates, sources, constituents; std::vector<std::pair<std::string, std::size_t>> licences; };
struct ValidationReport { std::vector<JsonError> errors; std::vector<Warning> warnings; std::optional<HarmonicsSummary> summary;
                          bool ok() const noexcept { return errors.empty(); } };
ValidationReport validate_harmonics(std::span<const std::byte> bytes, const HarmonicsLimits& limits = {});

std::expected<Read<core::tide::HarmonicsFile>, Error>
read_harmonics(const std::filesystem::path& p, const HarmonicsLimits& limits = {}, std::stop_token stop = {});

struct HarmonicsWriteOptions { bool gzip = false; };
std::string format_harmonics(const core::tide::HarmonicsFile& f, Time now);                 // HJ §3 layout, deterministic
std::expected<void, Error> write_harmonics(const std::filesystem::path& p, const core::tide::HarmonicsFile& f,
                                           Time now, const HarmonicsWriteOptions& o = {}); // atomic (CD §4.4)
}
```

Bytes are `std::span<const std::byte>` throughout. They are reinterpreted as `char` only at the
nlohmann call. `std::basic_string<std::byte>` is not used, because the standard provides no
`char_traits<std::byte>`.

### 6.2 Choice: parser callback, not SAX

| | nlohmann `parse` + `parser_callback_t` (chosen) | nlohmann `sax_parse` + own handler |
|---|---|---|
| DOM building | nlohmann's own, per value | our own SAX→DOM builder (about 150 lines to write, test and fuzz) |
| Memory (§1.4) | 22.5 MB peak for 9.6 MB of text: stations are decoded at their `object_end` and discarded (`return false`) | the same, by design |
| Syntax error offset | `json::parse_error::byte` from the one exception nlohmann throws for bad input, caught at a single boundary | `parse_error(position, …)` callback, no exception |
| Duplicate members, limits | in the callback (`key`, `object_start`/`end`, `value` events with `depth`) | in the handler |
| Stopping after an error | not possible: the callback records the first error, then returns `false` for every later event. The rest is lexed without building anything, which is linear and bounded by `max_json_bytes`. | `return false` |

The callback design is plainer: no hand-written DOM builder, same memory, and the one exception it
relies on is nlohmann's documented error channel, caught in one place. Decoding never calls a
throwing nlohmann accessor (`is_*` plus `get_ptr<const T*>`, never `get<T>()` or `at()`). A second
`catch (const nlohmann::json::exception&)` maps to an internal error, and the fuzz target treats
reaching it as a bug.

### 6.3 Pipeline

1. **Read** at most `max_file_bytes + 1` bytes (an oversize file is detected without trusting
   `stat`) → `SyntaxError{size}`.
2. **gunzip** if the input starts with `1F 8B` (§6.5) → `size` or `gzip`.
3. **UTF-8.** Strip a leading BOM (warning), then validate the whole text with core's
   `detail::is_valid_utf8` → `SyntaxError{encoding, offset}` with the exact first bad byte. Doing
   it here rather than through nlohmann's lexer gives a precise offset for every case.
4. **Parse with the callback.** The callback keeps:
   - a path stack of tokens, holding indices and member ids, not strings;
   - per-object member sets, held as small sorted vectors (`duplicate_member`);
   - depth, string, array and per-station value counts (`limit`);
   - the current top-level member.

   At the `object_end` of each element of `/stations` (depth 2) it decodes `parsed` into a
   `RawStation` and returns `false`, so the station never stays in the DOM. Other top-level members
   (all small) stay in the DOM and are decoded after the parse. A syntax error arrives as
   `parse_error` → `SyntaxError{not_json, byte − 1}`.
5. **Lenient decode into raw records** (phase 1). Locations are stored as indices (`PathRef{station,
   row, field}`); a `JsonPointer` is built only when an error is reported.

   ```cpp
   struct UnknownMember { PathRef where; std::string name; };
   struct UnknownKind   { PathRef where; std::string kind; };
   using RawStation = std::variant<RawReference, RawSubordinate, UnknownKind>;   // each Raw* keeps std::vector<UnknownMember>
   ```

   Types, ranges, enums and nulls are checked here (`schema`). Unknown members and kinds are
   recorded as data, not judged yet.
6. **Version gate.** `format`, then `version`, then `conventions` (HJ §10.4).
   - Same or older minor: the first recorded unknown member or kind is `schema`, with its pointer.
   - Newer minor: they are ignored, and the **skip cascade** runs: dictionary entries with an
     unknown definition kind or node-factor token, then references with a row naming a skipped
     entry, then stations of unknown kind, then subordinates whose reference was skipped. Each is
     counted by reason in `W-MINOR-NEWER`.
7. **Semantic resolution** (phase 2):
   - sources: uniqueness and `Licence::parse`;
   - `ConstituentSet::make` and the speed checksum;
   - `HarmonicsFile::make`, which resolves ids, rows, references and offset datums and checks datum
     conflicts.

   Each `HarmonicsError` maps to a `LocatedError` via the station and row indices kept in phase 1.

**Why the variant approach rather than a first pass for `version`.** JSON member order is free:
`version` may come after 10 MB of stations. A first pass would lex the whole text twice (+0.47 s
measured). Recording unknown members costs nothing for conforming files, and one pass also keeps
the error order deterministic (HJ §10.4).

**Memory, honestly.** Peak is approximately:
- the input bytes;
- plus the decompressed text (≤ 64 MiB);
- plus one station's DOM, bounded by `max_values_per_station` (worst case about 6.5 MB);
- plus the raw and typed records, about 25 B per constituent row plus per-station strings
  (about 10 MB for a 10k-station file).

The worst case is about 150 MiB at the default limits. A realistic 10 MB file needs about 45 MB
(§1.4 plus the result). If that ever matters, gunzip can feed the callback parser incrementally
through nlohmann's iterator input adapter; this is not planned.

### 6.4 Decode details

- **Integers.** nlohmann's integer events, or a float with zero fraction (HJ §3), are accepted where
  the schema says integer. Ranges are checked before narrowing.
- **Locale.** nlohmann's lexer handles `localeconv()`. A test runs the reader under `de_DE.UTF-8`
  (CD §7.2).
- **Positions.** `lon == −180` is `schema` (HJ §8.1: reject, not normalise); other positions go
  through `Location::make`.
- **Time offsets.** Minutes become `llround(min × 60000)` ms, and −0 becomes 0, inside
  `SubordinateOffsets::make`.
- **Text.** Strings with control characters are `schema` (HJ §3). The app renders every string from
  the file as plain text (`Text.PlainText` in QML).

### 6.5 gzip and build changes

- `io/detail/gzip.hpp`: `std::expected<std::string, SyntaxError> gunzip(std::span<const std::byte>,
  std::uintmax_t max_out, std::stop_token)`.
  - zlib `inflateInit2(…, 16 + MAX_WBITS)`, inflating in 1 MiB chunks;
  - the output cap is checked before each append (bomb guard), and `stop` between chunks;
  - a truncated stream, CRC failure, second member or trailing bytes is `gzip`.
- `vcpkg.json` gains `nlohmann-json` and `zlib`. zlib is already in the closure through HDF5 and is
  now a direct dependency. `target_link_libraries(mov_io PRIVATE nlohmann_json::nlohmann_json
  ZLIB::ZLIB)`; neither appears in public headers (include gate as for netCDF, CD §4.1).
- **Threading.** No global state, so the reader may run on any worker. It does not use the netCDF
  serial queue (CD C11).

### 6.6 Writer

Deterministic per HJ §3: sorted stations and dictionary, one station per line, numbers rounded per
HJ §9 and printed with `std::format("{}")`. Atomic via `write_file_atomic` (CD §4.4), gzip if
`o.gzip`. The writer is used by round-trip tests, the fuzz oracle and the CLI (`harmonics subset`,
`harmonics add-computed-datums`, §7.7).

### 6.7 Fuzzing (decision 21)

| Target | Limits | Oracle |
|---|---|---|
| `fuzz_harmonics_json` (bytes → `parse_harmonics`) | `max_file_bytes = 1 MiB`, `max_json_bytes = 4 MiB`, `max_values_per_station = 4,096` | No crash, UB or leak; the internal-error catch is never reached; if ok, `parse(format(x)) == x`; `validate_harmonics` reports `ok()` iff `parse_harmonics` succeeds, and its first error equals parse's |
| `fuzz_gunzip` | `max_out = 4 MiB` | Output ≤ cap; only `size` or `gzip` errors |

- libFuzzer options follow from the limits:
  - `-rss_limit_mb=256`, against a predicted peak of about 4 + 4 + 0.4 + 4 MiB plus sanitizer
    overhead;
  - `-malloc_limit_mb=32`;
  - `-max_len=1048577`.
- Corpus: the HJ §13 example, every `tests/fixtures/io/harmonics/**` file, and a `-dict` of format
  tokens.

---

## 7. Builder: `tools/harmonics/`

### 7.1 Language: Python (recommended)

| Criterion | Python 3.11+ stdlib | C++ with `mov::io` |
|---|---|---|
| HTTP for about 4,900 CO-OPS requests | `urllib.request`, explicit timeouts and retries | a new HTTP client dependency for a dev-only tool |
| KML, CSV, JSON, gzip, SHA-256 | stdlib | tinyxml2 plus hand-rolled CSV |
| Iterating on messy upstream data | fast | slow compile-edit cycle |
| Repo precedent | `tools/*.py`, the decision-23 station-list builder, `unittest` style | none |
| Two implementations disagreeing | avoided: the builder never computes arguments, predictions or datums. Its dictionary comes from the core-generated `constituents-nos.json`; computed datums come from the C++ CLI (§7.7); the output must pass the C++ reader and the schema. | n/a |

### 7.2 Layout and commands

```
tools/harmonics/
  fetch.py        # online: inputs → cache dir + manifest.json (URL, date, status, bytes, SHA-256)
  build.py        # offline, deterministic: cache → harmonics.json[.gz] (no computed datums yet)
  noaa.py  pringle.py  ticon.py        # source parsers → common Record
  merge.py        # identity clusters, precedence, provenance
  catalogue.py    # docs/schemas/constituents-nos.json + aliases.json
  aliases.json    # per-source constituent name → canonical (LAMBDA2→LAM2, SGM→SIGMA1, RHO1→RHO, …)
  licences.json   # allow-list; source and gesla_source → SPDX
  pins.json       # expected SHA-256 of KML and CSV inputs
  test_*.py       # unittest; fixtures in tests/fixtures/tools/harmonics/
```

```sh
python -I tools/harmonics/fetch.py --cache ~/.cache/mov-harmonics --noaa --pringle --ticon [--accept-new-hash]
python -I tools/harmonics/build.py --cache ~/.cache/mov-harmonics --out stage1.json \
    [--prefer noaa-coops,pringle,ticon-4] [--match-radius-m 1000] [--ticon-types Coastal] [--keep-local-time-suspects]
build/dev/src/cli/metocean-data harmonics add-computed-datums stage1.json harmonics-2026-10-08.json.gz \
    --period 2007-01-01/2026-01-01 --only-sources pringle,ticon-4
build/dev/src/cli/metocean-data harmonics validate harmonics-2026-10-08.json.gz         # gate: C++ reader
python -I tools/check_harmonics_schema.py harmonics-2026-10-08.json.gz                   # gate: jsonschema, pinned
```

### 7.3 Fetch robustness

| Concern | Rule |
|---|---|
| Timeouts | connect and read timeout 30 s per request |
| Retries | 3 attempts, exponential backoff (2, 4, 8 s) with ±25 % jitter, on network errors, 429 and 5xx only |
| Rate | at most 2 requests per second to CO-OPS; `User-Agent: MetOceanViewer-harmonics-builder/<version>` |
| Body cap | read at most cap + 1 bytes: JSON endpoints 16 MiB, KML 64 MiB, TICON CSV 128 MiB; over the cap fails |
| HTML rejection | a body whose `Content-Type` is `text/html` or whose first non-blank byte is `<` on a JSON endpoint fails. This catches login pages, CDN error pages and the KML export's consent page. CO-OPS JSON errors (HTTP 200 with `{"error": …}`, PA §2.2) are recorded per station as "no data", not retried. |
| Pinned files | KML and CSV SHA-256 are compared with `pins.json`; a change fails unless `--accept-new-hash`, which rewrites the pin and logs old and new hashes |
| Manifest | one entry per expected request: URL, status, bytes, SHA-256, time. `build.py` refuses to run if any expected entry is missing or failed ("manifest incomplete"), so a partial fetch can never produce a quietly smaller file |

### 7.4 Pipeline

| Step | NOAA CO-OPS (HJ §14.1) | Pringle KML (HJ §14.2) | TICON-4 CSV (HJ §14.3) |
|---|---|---|---|
| Fetch | `stations.json?type=tidepredictions&expand=tidepredoffsets&units=metric` (1 request); `type=harcon` list (1,371); per station `harcon.json?units=metric` and `datums.json?units=metric`. About 4,900 requests, about 45 min | KML export, pinned SHA-256 | SEANOE CSV (47 MB), pinned SHA-256 |
| Parse | `phase_GMT` only; omit zero rows; datums STND→MSL; skip GT/MN/DHQ/DLQ/HWI/LWI; LAT/HAT from the top level | `ExtendedData`; `_phase`/`_phs`; ignore `M2_Seasonal_*`; drop the `NOAA_Database` layer; drop `M4 = 0` placeholders | group by `tide_gauge_name`; cm → m; phase → [0, 360); `dd/mm/yyyy` |
| Ids | `noaa-coops:<id>` | `pringle:<layer>/<name>` (+ `#2`… within a layer) | `ticon-4:<tide_gauge_name>` |
| Names | canonical NOAA spelling, plus aliases for the extended catalogue | upper-case to canonical | `aliases.json`; undefined names go to `provenance.dropped_constituents` (warning if H > 1 cm) |
| Filters | drop subordinates whose reference is not in the output | none | `type == Coastal`; exclude `local_time_suspected` gauges (`gesla_source` `wsv`, `rws`) unless `--keep-local-time-suspects`. A constant 1 h shift would be wrong under DST, so they are excluded, not corrected. |
| Licence (§7.6) | `LicenseRef-US-Government-Public-Domain` | `LicenseRef-unknown-pringle` (owner decision) | per `gesla_source` from `licences.json`; unmapped fails the build |
| Flags and analysis | `hilo_only` on subordinates; `analysis: nos` | `utide` (KHOA/GESLA/UHSLC/JMA), `published` (FES truth) | `record_quality` → flags; `least_squares` |
| Datums | published `datums` + `datum_epoch` | `computed_datums` (§7.7) | `computed_datums` (§7.7) |

### 7.5 Merge and deduplication (deterministic, order-independent)

1. **Canonical order first.** Sort all records by (source precedence, id). Nothing downstream
   depends on input order or on `dict`/`set` iteration order.
2. **Candidate pairs.** Two records are candidates if they share an external id (NOAA id in a
   TICON name, UHSLC/GLOSS numbers), or if they lie within `--match-radius-m` (default 1 km) **and**
   their M2 agree (|ΔH| ≤ max(2 cm, 10 %), |ΔG| ≤ 10°). The M2 condition applies only when both
   records have M2; without it, distance alone does not match.
3. **Clusters** are the connected components of the candidate graph (union–find over the sorted
   records). The result does not depend on discovery order.
4. **Winner** of each cluster: the highest precedence (default `noaa-coops > pringle > ticon-4`,
   owner decision 2026-10-08), then the most constituents, then the smallest id. The others'
   `source:record` go to the winner's `provenance.also_in`.
5. **Inconsistency guard.** Cluster members matched by id whose M2 phases differ by more than 30°
   give the winner the flag `possible_datum_issues` and are logged. This catches local time
   labelled as UTC.
6. Records within the radius whose M2 disagrees stay distinct and are logged as near-duplicates.
7. **Tests.** Shuffle the inputs (10 random permutations): byte-identical output. Run the whole
   build under `PYTHONHASHSEED=0` and `PYTHONHASHSEED=12345`: byte-identical output apart from
   `created`.

### 7.6 Licences: allow-list, fail closed

- `licences.json` holds:
  - an **allow-list** of complete expressions: `LicenseRef-US-Government-Public-Domain`,
    `CC-BY-4.0`, `LicenseRef-unknown-pringle` (owner decision 2026-10-08) and `CC0-1.0`;
  - per-source defaults;
  - a **complete** `gesla_source → expression` map for TICON-4.
- An expression is allowed only if it is on the list, after parsing and normalising with the same
  SPDX-subset grammar as core's `Licence` (Python port, tested against the same token cases).
- Anything else is excluded and counted, including every expression that carries a non-commercial
  term, which never appears on the list (decision 31).
- A `gesla_source` value missing from the map **fails the build**. A new upstream contributor cannot
  slip in under a default.
- The file's `licence_summary` lists every allowed expression with its station count.
- Pringle: the owner contacts William Pringle before any public redistribution of a built file. The
  builder prints that reminder whenever `LicenseRef-unknown-pringle` stations are included.

### 7.7 Computed datums (owner decision 2026-10-08)

`metocean-data harmonics add-computed-datums IN OUT --period START/END --only-sources …` is part of
WP H8:
- It reads the stage-1 file with the C++ reader.
- For every reference station of the listed sources with no published datums, it runs `extremes`
  (§4.1, separation filter on) over the period and computes HJ §8.5's HAT, MHHW, MHW, DTL, MTL, MLW,
  MLLW and LAT, rounded to 1e-4 m.
- It writes them under `computed_datums` with the period, using the C++ writer.

Cost: 19 years × about 1,400 events per year × refinement, about 0.1 s per station, so about 10 min
for 6,700 stations single-threaded. The command parallelises over stations with a thread pool;
`core` stays single-threaded per call.

### 7.8 Validation gate and CLI (WP H8)

```
metocean-data harmonics validate FILE              # validate_harmonics: every error and warning (code, pointer, value); exit 0/1
metocean-data harmonics info FILE                  # counts, sources, licences, version
metocean-data harmonics subset FILE (--ids … | --bbox …) --out OUT
metocean-data harmonics add-computed-datums IN OUT --period START/END [--only-sources …]
metocean-data tides predict --file FILE --station ID --start … --end … (--interval 6m | --hilo)
                    [--datum MLLW] [--units m|ft] [--format csv|imeds|netcdf]
```

---

## 8. Test plan

Catch2 tags: `[core][tide]`, `[io][harmonics]`, `[constexpr]`, `[golden]`, `[property]`,
`[regression][B24]`.

### 8.1 Core constexpr, law and unit tests

| Test | Assertion |
|---|---|
| Catalogue invariants | `STATIC_REQUIRE`: unique names (case-insensitive); compound terms resolve to Darwin entries; T coefficient = species; token round trip for every `NodeFactor` |
| Drift | Schema enums (`node_factor`, `datum_token`, `computed_datum_token`, `analysis`) equal the core enums' tokens; `constituents-nos.json` equals the generated file |
| Speeds | `STATIC_REQUIRE` M2 = 28.9841042 ± 1e-7; NOAA's 37 published speeds within 1e-5 °/h |
| Doodson | Darwin→Doodson mapping gives M2 255.555, K1 165.555, O1 145.555, Sa 056.555 |
| `Argument` monoid | identity and associativity exact; commutativity; `scale(1,a) == a`; `scale(2,a) == a + a`; `scale(−1,a) + a` has zero V/c/u and doubled exponents (documents the non-linearity) |
| `Phase` | `wrap` of −0.0, −1e-14 (must give 0, not 360), 360, 720.5, −359.9; `of(NaN)` → nullopt |
| Elements and arguments | 2026 values equal HJ §13.2 to 1e-6. A congen golden table (1900–2100 × NOS 37: (V₀+u), f), generated once with `congen -b 1900 -e 2100` in the dev image and committed as CSV with its command, agrees to 1e-4° and 1e-6 |
| Compound laws | f(MS4) = f(M2); u(MSF) = −u(M2); (V₀+u)(2MK3) = 2(V₀+u)(M2) − (V₀+u)(K1) mod 360; f(M4) = f(M2)² |
| Mid-year identity (corrected, §1.3) | at t_M, `diagnostic::` instantaneous and yearly f and u are equal to 1e-12; V differs by Σ a_k(ṙ_k(t) − ṙ_k(1900))Δ, compared with the analytic value to 1e-9°; M1 differs by b_Q·ṗ·Δ to 1e-9° |
| Grid | pre-1970 and straddling ranges give samples at exact multiples of the step; `ceil_div` table (−7/3, −6/3, 0, 6/3, 7/3) |
| Budget | `TooMuchWork` before any work for series and for extremes; the extremes runtime counter trips on a pathological constant set |
| Cancellation | stop requested mid-segment → `Cancelled` |

### 8.2 Golden tests against CO-OPS (`[golden]`)

`tools/harmonics/record_noaa_fixtures.py` records public-domain data into
`tests/fixtures/core/tide/noaa/`: harcon, datums and offsets JSON, 6-minute `predictions` (MSL)
and `hilo` (MSL and MLLW) for each window.

| Station | Why | Windows |
|---|---|---|
| 8443970 Boston | large semidiurnal; largest year step | 2025-12-30 to 2026-01-02; 2026-06-29 to 07-04; 2024-02-27 to 03-02 (leap day); 1995-03-01 to 03-04; 2045-09-01 to 09-04; **New Year 2031-12-31 to 2032-01-02 and 2033-12-31 to 2034-01-02** |
| 9414290 San Francisco | mixed | year boundary; mid-year; New Year 2034-12-31 to 2035-01-02 |
| 8761724 Grand Isle | diurnal, small, wiggles | all of 2026 hilo (separation filter) |
| 8771450 Galveston Pier 21, 8771341 Galveston Bay Entrance | mixed, many wiggles | all of 2026 hilo (separation filter: 50 and 76 wiggle extrema) |
| 8720030 Fernandina Beach | shallow compounds; reference for 8720001 | 2026-10-01 to 10-08; New Year 2036-12-31 to 2037-01-02 (CO-OPS artefact) |
| 9455920 Anchorage | 114 constituents | 2026-10-01 to 10-08; all of 2026 hilo |
| 8720001 Kings Ferry (subordinate, ratio) | +245/+249 min | 2026-10-01 to 10-08, hilo |
| 8518989 Castleton (subordinate, additive) | negative offsets | same |
| 8635299 Port Royal (subordinate) | −139/−182 min, inversion | same |
| a subordinate of 8771450 (chosen in H4) | does CO-OPS re-filter after mapping? | all of 2026 hilo |

| Check (each asserted separately, every point) | Prototype | CI assertion |
|---|---|---|
| Series bias (mean residual) | ≤ 0.04 mm | abs ≤ 0.2 mm |
| Series raw max residual | 1.81 mm | ≤ 3 mm |
| Series RMS | ≤ 0.90 mm | ≤ 1.5 mm |
| Extremes: event sets equal (outside ±30 min of New Year) | equal for a full year at 5 stations (with the 2 h filter) | equal |
| Extremes: time, height | 0.67 min, 0.64 mm | ≤ 1 min, ≤ 3 mm |
| Subordinate hilo | exact by hand | ≤ 1 min, ≤ 5 mm, same count |
| Anchorage series raw max | 42.8 mm (3 constituents undefined) | ≤ 50 mm, tightened as the catalogue is vetted |
| New Year windows | §1.2 | our per-segment events match the own-year curve; no event at 23:59/00:00; documented divergence from CO-OPS asserted (so a CO-OPS change is noticed) |

### 8.3 Synthetic and cross-implementation checks

| Test | Method |
|---|---|
| Lipschitz isolation (core, CI) | Synthetic constants that make two extrema 1 min apart inside one 6-minute cell; a tangency (h′ touches zero); extrema exactly on grid points. Assert found, not reported, reported once. Results identical for range starts shifted by 1 ms … 1 step (epoch anchoring). |
| Self round trip (core, CI) | Predict one year hourly from known constants, fit (H, G) back by least squares with the same yearly f/u: 1e-9 m and 1e-7°. |
| Subordinate property test (core, CI) | §4.3 properties over 10,000 random cases (Catch2 generators, fixed seed). |
| UTide recovery (optional job `harmonics-crosscheck`, pinned `utide==0.4.0`) | 2 years of hourly engine output at Boston, fitting the 15 largest constituents. M2, S2, N2, K1, O1 and K2 phases within 0.2° and amplitudes within 0.6 % (§1.3 measured 0.15° and 0.56 %). |
| pytides | Not a CI dependency: no PyPI release installs on Python ≥ 3.12 (§1.3). The one-off comparison (§1.3) is recorded here. It may be re-run by hand from the WPringle fork at commit `e6741a1`, imported from source, restricted to constituents whose definitions agree (not M1, MU2, RHO, 2Q1, 2MK3, OO1), with tolerance Σ H_k·(abs(Δf_k) + f_k·abs(Δ(V+u)_k)) evaluated per case (3.5 mm at Boston). |

### 8.4 io

| Area | Cases |
|---|---|
| Error order | One minimal fixture per code under `tests/fixtures/io/harmonics/invalid/`, asserting the exact `JsonError` alternative, code, **pointer and value**. Multi-error fixtures assert `parse_harmonics` reports the first per HJ §10.4 and `validate_harmonics` all of them. |
| Manifest | three categories, valid / invalid / newer (HJ §16); the C++ test checks the reader half, the `format-compliance` job the schema half (pinned `jsonschema`) |
| Strictness after version | an unknown member before `version` in a 1.0 file → `schema` at its pointer; the same in a 1.3 file → ok plus `W-MINOR-NEWER` |
| Skip cascade | 1.3 file with an unknown node factor, used by reference R, which has subordinate S, plus an unknown station kind → R, S and the unknown station skipped; counts per reason asserted |
| Order independence | `stations` before `constituents`/`sources`; shuffled members; same result |
| Duplicates | duplicate member at top level, in a station, in `datums`, in `compound` |
| Numbers | `2.0` as integer; `-0`; `1e2`; `lon: -180` → `schema`; fractional minutes; under `de_DE.UTF-8` |
| Limits | depth 17; 5 KiB string; array of 4,097; one station with 65,537 values; `max_stations + 1`; a file of `max_file_bytes + 1` (read stops there); gzip bomb (1 MiB → 1 GiB) → `size` with no allocation over the cap (ASan) |
| gzip | valid; truncated; bad CRC; trailing garbage; two members; plain JSON named `.gz` |
| BOM, UTF-8, text | BOM → warning; invalid UTF-8 → `encoding` at the exact offset; control character in a name → `schema` |
| Licence | parse table (`CC-BY-4.0 AND (MIT OR CC0-1.0)`, `LicenseRef-unknown-pringle`, `CC-BY-NC-SA-4.0` → carries NC, `(` unbalanced → `licence`) |
| Round trip | `parse(format(f)) == f` for the worked example and a 1k-station random file; `format` deterministic |
| Writer | atomic stages with fault injection (CD §4.4); gzip output re-readable |
| Fuzz | §6.7 |

### 8.5 Builder (`python -I -m unittest discover tools/harmonics`)

- Mini fixtures: 3 NOAA stations (reference, ratio subordinate, additive subordinate), a
  3-placemark KML (one per field-naming variant), and a 100-row TICON CSV including a CMEMS gauge,
  a `wsv` gauge and an unmapped `gesla_source`.
- Assertions:
  - unit conversions; phase normalisation; alias mapping; dropped-constituent reporting;
  - clustering decisions (id match, radius + M2 match, M2 mismatch kept);
  - order independence (shuffles) and `PYTHONHASHSEED` determinism;
  - fetch hardening against a local test server: timeout, 503 then success, 429, body over the cap,
    HTML body, hash change without and with `--accept-new-hash`, an incomplete manifest refused by
    `build.py`;
  - licences: NC excluded, unlisted excluded, unmapped `gesla_source` fails the build;
  - output passes `jsonschema` and, when `MOV_BUILD_DIR` is set, the C++ validator.

---

## 9. App and provider integration (Phase 3, then Phase 4/5 UI)

### 9.1 v4 behaviour (reference only, do not port)

v4 never offered current predictions. Its XTide station list included current stations only
incidentally (bug 4 below).

| v4 offered | Where | v5 |
|---|---|---|
| Station list: embedded `xtide_stations.csv`, 4,166 rows (957 Ref, 3,209 Sub) | `libraries/libmetocean/stationlocations.cpp:145-190` | Stations from the user's harmonics file (water level) |
| Fixed 300 s interval | `xtidedata.cpp:33` | User-selectable 1/5/6/10/15/30/60 min, default 6 (CO-OPS default), plus high/low only |
| Whole-day range, [start 00:00, end + 1 d 00:00) | `MetOceanViewer/src/xtide.cpp:60-66` | `TimeRange` from the left panel, UTC |
| Units m/ft via ×3.28084 | `xtide.cpp:125-130` | `convert`, exact 0.3048 ft |
| Datums MLLW, MLW, MSL, MHW, MHHW, NGVD29, NAVD88 from CSV offsets relative to MLLW | `stationlocations.cpp:166-186`, `hmdfstation.cpp:158-184` | `Datums` from the file (§5); only the datums present, labelled published or computed |
| Save data (IMEDS/netCDF) and plot (JPG/PDF) | `uixtidetab.cpp:51-124` | SN/IMEDS/CSV writers (Phase 2) and Phase 5 export |

v4 bugs **not** to reproduce (each gets a regression test where it maps onto v5 code):

1. Stations are looked up **by name** (`tideprediction.cpp:66`), which is ambiguous for duplicate
   names. v5 uses ids.
2. Start and end are built in the **station's local zone** (`tideprediction.cpp:76-81`) while the
   output is parsed as UTC (`:91-96`). v5 is UTC end to end.
3. The datum is labelled `"mllw"` regardless of the TCD's datum (`tideprediction.cpp:104`). The CLI
   labels "MLLW" after shifting to another datum, and also when the shift failed
   (`MetOceanData/metoceandata.cpp:335-345`, plan bug 24). v5 sets the meta datum from §5;
   `[regression][B24]`.
4. The list included 1,042 XTide **current** stations (names ending in "Current"; 1,049 rows
   contain the word) and plotted them as water levels: every series is labelled "Water Surface
   Elevation" (`xtide.cpp:139-141`) and requested and labelled in metres
   (`tideprediction.cpp:83,103`). v5's format 1.0 has no current stations, and a future 1.1 would
   need a distinct quantity.
5. `-999999` offset sentinels tested with `< -900` (`stationlocations.cpp:174-179`, also `:90-95`).
   v5 uses `std::optional` and `Datums`.
6. `harmonics.tcd` is copied to the config directory once and never refreshed
   (`tideprediction.cpp:41-46`), so upgrades kept stale constants. v5 reads the user-chosen file in
   place.

### 9.2 Provider

```cpp
// src/providers/tides/ (Qt at the edge only)
struct HighLowOnly {};
using TideSampling = std::variant<core::tide::Interval, HighLowOnly>;
struct TideRequest {
  core::StationId<core::provider::Harmonics> station;
  core::TimeRange range;
  TideSampling sampling;
  core::VerticalDatum datum;
};
using TideResult = std::variant<core::TimeSeries, core::tide::Events>;   // mirrors TideSampling
struct TideResponse { TideResult result; std::vector<io::Warning> warnings; };   // e.g. dropped inverted pairs

class TidesProvider {
 public:
  explicit TidesProvider(std::shared_ptr<const core::tide::HarmonicsFile> file);   // immutable, shared
  QFuture<std::expected<TideResponse, ProviderError>> fetch(const TideRequest& r);  // QtConcurrent::run; stop_token from the QPromise
};
```

- **Settings.** `tides/harmonicsFile` (path), set through Settings › Tides › "Choose harmonics
  file…". On choose, the file is validated off-thread (`validate_harmonics`) and its summary is
  shown before acceptance: version, counts by kind and source, licences with counts, warnings.
- **Startup.** If a path is set, the file is loaded in the background (plan §2.3). A missing or
  invalid file disables the Tides layer with an inline message, never a modal error. No file is
  bundled (decision 31).
- **Map layer** "Tides" replaces XTide in plan §2.6: distinct icons for reference and subordinate
  stations, a source filter, and GeoJSON from C++ (plan §2.4).
- **Details panel.** Name, id, source, effective licence, attribution (all as plain text),
  `record`, flags, and "Predictions, not observations. Not for navigation."
- **Datum selector.** Exactly the datums §5 can deliver, with "(computed)" on computed ones.
- **Concurrency.** Prediction is pure CPU on `QtConcurrent::run`. A new selection cancels the
  previous `stop_token`. The file is immutable and shared through `std::shared_ptr<const …>`, and
  views never outlive the task that holds the pointer.
- **Export.** SN `water_level_prediction` with `station_provider = "harmonics"` (SN minor bump);
  IMEDS; CSV. Events export as CSV rows with a `kind` column.

---

## 10. Work packages

| WP | Scope | Depends on | Agent | Size |
|---|---|---|---|---|
| H0 | Config and doc sync: provider-apis §5 marked superseded; SN registry tokens (`harmonics`, HAT/LAT/DTL); `vcpkg.json` (+`nlohmann-json`, +`zlib`); CLAUDE.md build notes. Plan decision 31 is already JSON (e99c8abe). | — | coordinator | S (0.5 d) |
| H1 | `Phase`, `HighLow`, `Argument` monoid, `constituent`, `catalogue` (NOS 37 + extended set for Anchorage, vetted aliases), `astronomy`, constexpr and law tests, generated `constituents-nos.json` + drift tests, congen golden table | H0 | Opus | L (4 d) |
| H2 | `ConstituentSet`, `YearArguments`, `Licence`, `HarmonicsFile` + views, `Datums`, vocabulary changes (`VerticalDatum` +3, `DataSource::harmonics`, `provider::Harmonics`, `Cancelled` to core) | H1 | Opus | M (3 d) |
| H3 | `predict`, `height_at`, `diagnostic::`, `extremes` (Lipschitz isolation, separation filter); fixture recorder; goldens §8.2 (reference stations, full-year hilo, New Year windows); self round trip | H2 | Opus (engine), Sonnet (fixtures) | L (4 d) |
| H4 | `subordinate_extremes`, `subordinate_predict` (clamped warp), `Events` and `shift`; subordinate goldens; property test | H3 | Opus | M (3 d) |
| H5 | io: callback parser, raw records, version gate and skip cascade, `validate_harmonics`, errors with pointers and values, gzip, writer, fixtures + manifest (3 categories), `format-compliance` schema step, fuzz targets | H2, H0 | Sonnet, Opus review | L (5 d) |
| H6 | Optional `harmonics-crosscheck` CI job (UTide recovery), non-blocking | H3 | Sonnet | S (1 d) |
| H7 | `tools/harmonics` builder: hardened fetch, parsers, clustering, licence allow-list, report, unittest | H1 (catalogue JSON), H5 (validator) | Sonnet, Opus review of §7.5–7.6 | L (5–6 d) |
| H8 | CLI: `harmonics validate/info/subset/add-computed-datums`, `tides predict` | H4, H5 | Sonnet | M (2–3 d) |
| H9 | Phase 3 `tides` provider + settings key; Phase 4 layer and Details hooks follow the GUI plan | H4, H5, Phase 3 provider scaffolding | Sonnet | M (2–3 d) |

Waves: 1 (H0 ∥ H1), 2 (H2), 3 (H3 ∥ H5), 4 (H4 ∥ H6 ∥ H7), 5 (H8), 6 in Phase 3 (H9). About
30–34 working days in total.

- H1–H6 are headless core/io work with Phase 2 standing: ≥ 90 % line coverage (decision 22),
  warnings as errors, fuzzing.
- H7 is tooling.
- H8–H9 belong to Phase 3, replacing the parity item "XTide predictions".
- Reviewers per CLAUDE.md: ben-deane, sean-parent and jason-turner for H1–H4; neckbeard-nate,
  sean-parent and conor-hoekstra for H5; uncle-bob-martin and neckbeard-nate for H7–H8;
  bryce-lelbach for H9.

---

## 11. Owner decisions and open questions

Decided by the owner on 2026-10-08:

1. **Precedence** for the same gauge in several sources stays `noaa-coops > pringle > ticon-4`
   (§7.5).
2. **Pringle stations are included by default**, with licence `LicenseRef-unknown-pringle` on every
   such station, as an explicit allow-list entry (§7.6). The owner contacts William Pringle before
   any public redistribution of a built file.
3. **Computed datums.** For non-NOAA stations the builder computes MLLW, MHHW, LAT, HAT and the other
   tidal datums from a 19-year prediction. Each is flagged in the format as computed from harmonics,
   not observed (HJ §8.4–8.5; §7.7).
4. **Currents** are a possible post-v5.0 feature (format 1.1), not a commitment. v4 never offered
   currents (§9.1).

5. **Schema URL.** The schema `$id` is
   `https://raw.githubusercontent.com/zcobell/MetOceanViewer/master/docs/schemas/harmonics-v1.schema.json`.
   It resolves once v5 merges to `master`.
6. **Licence.** The spec text and the schema are GPL-3.0-or-later, like the code.
7. **Format name.** The format keeps the name `metoceanviewer-harmonics`.

---

## Appendix A. Review resolution (revision 2)

**R** = resolved as asked; **P** = partly (reason given).

| Review | Finding | Status | Where |
|---|---|---|---|
| Type | B1 resolved views; drop `MissingOffset`, `index_out_of_range` | R | §2.5 views; `subordinate_*` take `SubordinateView`; `reference_datum_height()` total |
| Type | B2/S4 strictness after version | R: variant approach (`RawStation` with `UnknownKind`, unknown members as data). Justified against a first pass (second lexing, +0.47 s). Skip cascade transitive and counted; pointers as indices, strings built only on error. | §6.3 steps 5–6; HJ §11 |
| Type | SF1 sd in `Constant` | R | §2.5 |
| Type | SF2 resolve once; separate storage; strong indices; no chains | R: `ReferenceIndex`, `SubordinateIndex`, `SourceIndex`, `StationIndex`; `make` resolves; subordinate → `ReferenceIndex` only | §2.5 |
| Type | SF3 name the monoid; sorted non-zero terms | R: `Argument` (+, identity; `scale` non-linear via \|k\| on exponents); `CompoundTerms` sorted map, non-zero, unique | §2.2 |
| Type | SF4 `Events{datum, vector}` + `shift`; `SubordinateEvent{kind, event, reference}`; result mirrors sampling | R | §4.1, §4.2, §9.2 |
| Type | SF5 error variants with values; `PredictionError` variant reusing `Cancelled`; `validate_harmonics` accumulating; pinned fail-first order | R: `SyntaxError{offset}` / `LocatedError{code, pointer, value}`; `PredictionError = variant<YearOutOfRange, TooMuchWork, Cancelled>` with `Cancelled` moved to core | §6.1, §3, HJ §10.4 |
| Type | NIT `Phase` [0, 360) | R | §2.1 |
| Type | NIT drop analysis "unknown" | R (absent = unknown; schema enum updated) | §2.5, HJ §8.2 |
| Type | NIT `YearArguments` cleanup | R (`ConstituentYear` per index; `start()` derived; private construction) | §2.3 |
| Type | NIT `HighLow<T>`; `time:{high,low}` in the format | R: `HighLow` in core; format `offsets.time_min: {high, low}` (`height` already had high/low) | §2.1, HJ §8.3 |
| Type | NIT `NodalMode` → `diagnostic::` | R | §3 |
| Type | NIT generated or drift-tested schema enums | R (drift tests; `$comment` markers in the schema) | §8.1 |
| Type | NIT version pattern accepting newer minors; third manifest category | R | schema `version`; HJ §11, §16 |
| Type | NIT reuse `ValidRange` | R (`record`, `computed_period`) | §2.5 |
| Type | Q(a) id namespace vs source | R: the namespace **is** the source; the `source` member is removed. Mismatch is impossible; an unknown namespace is `E-UNKNOWN-SOURCE`. | HJ §8.1, H8 |
| Type | Q(b) lon −180 | R: canonical (−180, 180]; files with −180 are **rejected** (`schema`), not normalised | HJ §8.1, schema |
| Numerics/IO | B1 false "instantaneous == yearly at t_M" | R: analytic difference stated (≤ 4.3e-4°; M1 ≈ 20.3°). pytides tolerance derived from measured astronomy and definition differences (3.5 mm at Boston, restricted set). Installability checked: no PyPI pytides installs on Python ≥ 3.12; the fork runs from source. | §1.3, §8.3, HJ §5.2 |
| Numerics/IO | B2 extremes: epoch grid, Lipschitz isolation, direction classification, half-open cells | R | §4.1 |
| Numerics/IO | S1 per-segment extremes, none at the discontinuity; New Year golden window; CO-OPS behaviour recorded | R: CO-OPS fetched at Boston and four other New Years; its quirks documented and excluded from comparison | §1.2, §4.1, §8.2, HJ §5.5 |
| Numerics/IO | S2 inverted pairs as a unit; clamp; relative threshold; property test | R | §4.2, §4.3 |
| Numerics/IO | S3 honest memory; lower `max_json_bytes`; fuzz `rss_limit`; callback path considered | R: measured DOM 108 MB vs callback 22.5 MB; callback chosen as plainer; 64 MiB limits; per-station value cap; fuzz limits derived | §1.4, §6.2, §6.3, §6.7 |
| Numerics/IO | S5 licence allow-list failing closed; unmapped `gesla_source` fails; SPDX tokenised | R | §7.6, §2.4, HJ §6.1 |
| Numerics/IO | S6 fetch timeouts/retries/body cap/HTML; manifest completeness; `--accept-new-hash`; `PYTHONHASHSEED`; order-independent clustering | R | §7.3, §7.5, §8.5 |
| Numerics/IO | S7 floor-division alignment before 1970 | R | §3.1, §8.1 |
| Numerics/IO | S8 one cost limit (samples × rows) covering extremes | R (`max_term_evaluations`; estimate up front plus runtime counter) | §3, §4.1 |
| Numerics/IO | S9 bias and raw max separately, every point | R: re-measured on every point (bias ≤ 0.04 mm, raw max 1.81 mm); separate CI assertions | §1.1, §8.2 |
| Numerics/IO | NIT `fmod` wrap twice | R | §2.1, §3.1 |
| Numerics/IO | NIT "exact by construction" wording | R (removed; §3.2 states the error instead) | §3.2 |
| Numerics/IO | NIT `char_traits<std::byte>` | R (span of bytes; reinterpret at the nlohmann boundary) | §6.1 |
| Numerics/IO | NIT control characters; plain-text rendering | R (schema pattern; `Text.PlainText`) | HJ §3, §6.4, §9.2 |
| Numerics/IO | NIT ms rounding for fractional minutes; −0 | R | §2.5, HJ §8.3 |
| Numerics/IO | NIT cap the read at `max_file_bytes + 1` | R | §6.3 |
| Numerics/IO | Q prominence | R by data: CO-OPS filters on a 2 h separation, not height (full-year comparison, 5 stations). The rule's boundary is pinned in H3. | §1.2, §4.1, HJ §5.5 |
| Owner | answers 1–4 | R | §11, §7.5–7.7, HJ §8.4–8.5, §15 |
| Owner | correction "v4 never had currents" | R (parity framing removed; incidental current stations recorded as v4 bug 4) | §9.1 |
