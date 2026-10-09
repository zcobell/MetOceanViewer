# Tidal harmonics JSON, format `metoceanviewer-harmonics` 1.0

Normative spec of the file that carries tidal harmonic constants, datums and subordinate-station
offsets for MetOceanViewer v5 (plan §6 decision 31). It is published so that third parties can
write and read the format without reading our code. The machine-readable structure is
[`docs/schemas/harmonics-v1.schema.json`](schemas/harmonics-v1.schema.json) (JSON Schema draft
2020-12). This text is authoritative where the two differ, and §10 lists the rules a schema cannot
express. The engine that consumes the file is designed in `docs/harmonics-engine.md` (HE).

This spec and the schema are licensed GPL-3.0-or-later, like the rest of the repository. The
licence covers the documents. A data file written in the format carries the licences of its own
data (§6).

The keywords MUST, SHOULD and MAY are used in the RFC 2119 sense. Every URL was accessed on
2026-10-08. **Verified** means the claim was checked on 2026-10-08 against the live CO-OPS API or
by the prototype in HE §1. **Source** means the claim is taken from the cited document.

Contents: 1 decisions, 2 scope, 3 encoding, 4 top level, 5 the prediction model (5.5 extremes),
6 sources and licences, 7 constituent dictionary, 8 stations (8.5 computed datums), 9 numbers,
10 validation, 11 versioning, 12 size, 13 worked example, 14 source mapping (informative),
15 currents (possible 1.1), 16 tests, 17 references, A formulas, B the NOS dictionary.

---

## 1. Decisions at a glance

| # | Choice | Why (short) |
|---|---|---|
| H1 | UTF-8 JSON (RFC 8259) with a JSON Schema (draft 2020-12). `.json` or gzip `.json.gz`; the reader detects gzip from its magic bytes. | Owner decision 2026-10-08. Readable, diffable, and parseable by every language. |
| H2 | Top-level `format` = `"metoceanviewer-harmonics"`, `version` = SemVer `"1.0.0"`. A reader rejects an unknown major (§11). | Detects the file kind and allows evolution. |
| H3 | The file is **self-describing**: a constituent dictionary gives each name's Darwin arguments (SP98) or compound definition, its speed, and its node-factor formula. | Constituent names are unreliable across sources (§7.3). A name alone does not define a prediction. |
| H4 | Amplitudes in metres. Phases are Greenwich phase lags in degrees, referred to UTC, against the SP98 equilibrium argument. Nodal corrections are evaluated per year in the NOS way (V₀ at 1 January 00:00 UTC, f and u at mid-year). All of this is stated in a mandatory `conventions` object. | This scheme reproduces every CO-OPS 6-minute prediction tested to a raw maximum of 1.81 mm (verified, HE §1). Instantaneous nodal evaluation misses them by up to 31.5 mm. |
| H5 | Per-station constituents are `[name, amplitude, phase]` rows (optionally `[…, amp_sd, phase_sd]`). | Each triple stays together, so length mismatches cannot happen, and the schema can check each row. It costs about 5 % more than parallel arrays uncompressed and is equal after gzip (§12). |
| H6 | Each station has a reference level of MSL (`z0` above MSL, default 0). Datums are objects of token → height above that MSL, in metres: `datums` for values the source published, `computed_datums` for values computed from the harmonics (§8.5). | Maps 1:1 onto core `DatumTable` (MSL pivot, decision 18). Computed values are never mistaken for observed ones (owner decision 2026-10-08). |
| H7 | Subordinate stations name a reference station in the same file and give `{high, low}` time offsets in minutes, a `{high, low}` height ratio or additive offset, and the datum the heights are relative to. | This is exactly NOAA's `tidepredoffsets` model, verified against NOAA output (§5.4). |
| H8 | A station id is `<source>:<local>`: its namespace **is** its source, and through the source it has a licence. A station may override the licence (SPDX expression, §6.1). | One fact stored once (no separate `source` member to disagree with the id). Licence stays per station, which the Slackwater TCD lost (research §2). |
| H9 | Missing means absent. `null` is not used anywhere in 1.0. NaN and Infinity cannot occur, because JSON has no literal for them. | One way to express "missing"; no sentinels (plan §7). |
| H10 | Currents (major/minor axis constants, flood/ebb offsets) are not in 1.0. They are a possible post-v5.0 feature; if pursued, they arrive in 1.1 as a new station kind that 1.0 readers skip (§15). | Owner decision 2026-10-08: not a v5.0 commitment. v4 never offered currents (HE §9.1). |

---

## 2. Scope and non-goals

**In scope.**
- Water-level tide stations: reference stations (harmonic constants) and subordinate stations
  (offsets from a reference station).
- Tidal and geodetic datum heights.
- Per-station provenance and licence.

**Not in 1.0.**
- Currents (§15; a possible post-v5.0 feature, not a commitment).
- Ice or seasonal (time-varying) constants.
- Multiple reference stations per subordinate.
- Offsets that differ between higher and lower high/low water, and Admiralty-style corrections
  that vary with spring/neap. XTide does not support these either
  (https://flaterco.com/xtide/harmonics.html).
- Model grids: this is a station format, not an atlas such as TPXO or FES.
- Any guarantee of navigational quality. Producers SHOULD put "not for navigation" in
  `licence_summary`.

---

## 3. Encoding

| Aspect | Rule |
|---|---|
| Syntax | RFC 8259 JSON text whose top-level value is an object. |
| Character encoding | UTF-8 (RFC 8259 §8.1). Writers MUST NOT emit a byte-order mark. Readers ignore a leading BOM and warn (`W-BOM`, as RFC 8259 permits). Invalid UTF-8 is an error (`E-ENCODING`). |
| Member names | Unique within each object. RFC 8259 §4 only says "SHOULD", but this format requires it; a duplicate is `E-DUPLICATE-MEMBER`. Many parsers keep the last value silently, which is why the reader detects duplicates itself (HE §6.3). |
| Literals | `null` MUST NOT appear. `true`/`false` are not used in 1.0. |
| Text | Every string value is free of control characters (U+0000–U+001F, U+007F); a newline in `description` or `notes` is not allowed either (`E-SCHEMA`). Consumers MUST render names, attributions and notes as plain text, never as markup (v4 injected remote text into rich text, plan bug 23). |
| Numbers | Decimal JSON numbers only. Where the schema says `integer`, a number with no fractional part (`2` or `2.0`) is accepted, matching JSON Schema 2020-12. |
| Compression | Optional gzip (RFC 1952), one member, suggested extension `.json.gz`. The reader detects it by the magic bytes `1F 8B`, not by the extension, and bounds the decompressed size (§10.3). zlib is already in the dependency closure through netCDF-C/HDF5; HE §6.5 adds it as a direct dependency. |
| Layout (SHOULD) | One element of `stations` per line, compact inside the element, and every other member pretty-printed. Diffs then show one changed line per station, and it costs about 1 % over fully compact text. |
| Determinism (SHOULD) | Stations sorted by `id` (byte order), dictionary entries sorted by (species, speed, name), and numbers in shortest round-trip form after the rounding of §9. Two builds of the same inputs then differ only in `created`. |
| MIME type | `application/json`; gzip as `application/gzip`. |

---

## 4. Top-level object

| Member | Type | Req. | Meaning |
|---|---|---|---|
| `format` | string | yes | Exactly `"metoceanviewer-harmonics"`. A file without it is not this format (`E-FORMAT`). |
| `version` | string | yes | SemVer `MAJOR.MINOR.PATCH` without pre-release or build suffix. This spec is `1.0.x` (§11). |
| `created` | string | yes | UTC timestamp `YYYY-MM-DDThh:mm:ssZ`. |
| `generator` | object | yes | `name`, `version`, optional `command` (the builder invocation, for reproducibility). |
| `title` | string | no | Human title. |
| `description` | string | no | Free text, for example the builder's summary report. |
| `licence_summary` | string | yes | Human summary of the licences in the file and the disclaimer. Per-station licences are authoritative (§8.1). |
| `conventions` | object | yes | Fixed values in 1.0 (§5.1). A reader checks every one of them (`E-CONVENTION`). |
| `sources` | array ≥ 1 | yes | §6. |
| `constituents` | array ≥ 1 | yes | The constituent dictionary (§7). |
| `stations` | array ≥ 1 | yes | §8. |

Any other member is an error in a 1.0 file. A reader ignores unknown members only when the file's
minor version is newer than its own (§11).

---

## 5. The prediction model (normative)

### 5.1 `conventions`

| Member | 1.0 value | Meaning |
|---|---|---|
| `amplitude_unit` | `"m"` | Amplitudes, `z0`, datum heights and additive offsets are in metres. |
| `phase` | `"greenwich_phase_lag_deg"` | G in degrees, `0 ≤ G < 360`, as defined in §5.2. NOAA calls it `phase_GMT`, the "epoch" (verified field). |
| `time_standard` | `"UTC"` | t is UTC. UT1−UTC (< 0.9 s) is ignored, which is at most 0.007° for M2. |
| `astronomy` | `"sp98"` | Mean longitudes and orbital terms from Schureman (1958), SP 98 Table 1 (Appendix A). |
| `nodal_evaluation` | `"nos_yearly"` | The yearly evaluation of §5.2. |
| `reference_level` | `"MSL"` | Heights are relative to the station's mean sea level (§8.4). |

### 5.2 Reference stations

For a reference station with constants (Hₖ, Gₖ), the height above the station's MSL at UTC time t is

```
h(t) = z0 + Σₖ fₖ(Y) · Hₖ · cos( σₖ · (t − t_Y) + (V₀+u)ₖ(Y) − Gₖ )
```

- **Y** is the UTC calendar year containing t (proleptic Gregorian), **t_Y** is `Y-01-01T00:00:00Z`,
  and `t − t_Y` is in hours.
- **σₖ** is the constituent speed in degrees per hour, derived from its definition (§7.2): the rates
  of Appendix A.1 evaluated at 1900-01-01T00:00Z. The `speed` member of the file is a checksum, not
  an input.
- **(V₀+u)ₖ(Y)** is the equilibrium argument. V₀ is the linear combination of the
  astronomical arguments (T, s, h, p, p₁, c) **at t_Y**. u is the linear combination of the nodal
  terms (ξ, ν, ν′, 2ν″, Q, R, Qᵤ) evaluated **at mid-year**, `t_M = t_Y + (t_{Y+1} − t_Y)/2`
  (2 July 12:00 in a common year, 2 July 00:00 in a leap year). T is the hour angle of the mean sun at
  Greenwich, so T = 180° at 00:00 UTC.
- **fₖ(Y)** is the node factor from the constituent's formula, evaluated at **mid-year**.
- Compound constituents: σ and (V₀+u) are the integer combinations of their terms, and f is
  `Π fᵢ^|cᵢ|` (§7.2).
- **Year boundary.** f and u change from one year to the next, so h has a **step at 1 January
  00:00 UTC**. At Boston (8443970) it is −16.2 mm in 2026 and up to 36.7 mm over 2020–2039
  (prototype). NOAA's own series has the same step (verified): around 2026-01-01 00:00 the yearly
  model stays within 1.2 mm of CO-OPS on both sides, while the instantaneous model's residual jumps
  from +1.3 mm to −14.4 mm, and CO-OPS's 1-minute series steps from −1.573 to −1.566 m at
  2032-01-01 00:00 at Boston. Readers MUST NOT smooth the step. Extremes are found per year segment
  (§5.5), so the step never creates an extremum.
- **Source.** This is SP 98's tabulated practice: Table 14, f for the middle of each year; Table 15,
  (V₀+u) at the beginning of each year. libcongen implements the same scheme and documents "V
  portion of argument, evaluated at beginning of year" and "u portion of argument, evaluated at
  mid-year". Its speeds are taken at 1900, which libcongen says is needed "for compatibility with US
  National Ocean Service (NOS) predictions" (congen 1.7 README and the `Congen` header,
  https://flaterco.com/files/xtide/congen-1.7-r2.tar.xz).
- **Verified.** This model, with NOAA's 37 published constituents, reproduces every 6-minute CO-OPS
  prediction (datum MSL) at 8443970, 9414290, 8761724 and 8720030 over three windows (2025-12-30 to
  2026-01-02 across the year boundary, 2026-06-29 to 07-04, 2026-10-01 to 10-08; 17,280 points).
  The bias is at most 0.04 mm in magnitude and the raw maximum difference is 1.81 mm (RMS ≤ 0.90 mm),
  which is CO-OPS's millimetre rounding plus its millimetre-rounded constants. Evaluating V, u and f
  instantaneously instead misses by up to 31.5 mm (HE §1). The zero bias means CO-OPS MSL
  predictions use `z0 = 0`.
- **Instantaneous evaluation is not the yearly model at mid-year.** At t_M, f and u coincide, but V
  does not: the yearly model advances V with the 1900 rates, so for an ordinary constituent the two
  differ by `Σ a_k·(ṙ_k(t) − ṙ_k(1900))·(t_M − t_Y)`, at most 4.3e-4° in 2026–2040 (prototype). For
  M1 the speed's `b_Q·ṗ` term (§7.2) has no instantaneous counterpart, so the two differ by
  `b_Q·ṗ·(t_M − t_Y)` ≈ 20.3°.

Engines MAY offer instantaneous evaluation as a diagnostic, but MUST NOT use it for output that
claims conformance with this spec.

### 5.3 Valid years

The SP 98 polynomials are fitted about 1900. Conforming readers MUST support Y in **[1700, 2200]**.
Outside that range they MAY refuse to predict (`YearOutOfRange` in HE). The file itself carries no
year range.

### 5.4 Subordinate stations

Let R be the reference station and D the subordinate's `offsets.datum` (§8.3).

1. **Reference extremes.** The high/low events of R per §5.5 (separation filter applied), with
   heights relative to D at R: `h_D(t) = h(t) − d_R(D)`, where d_R(D) is R's height of D from
   `datums` or `computed_datums` (0 when D is `MSL`).
2. **Subordinate extremes.** For a reference high at (t, h_D):
   - time `t + time_min.high`;
   - height `height.high × h_D` (`ratio`) or `h_D + height.high` (`offset`).
   Lows use the `low` values the same way. Heights are relative to datum D **at the subordinate**.
   An event whose mapped time is not later than its predecessor's is dropped together with that
   predecessor (the pair is removed as a unit), so highs and lows keep alternating.
3. **Verified against NOAA, 2026-10-01** (datum MLLW, `interval=hilo`):
   - **8720001**, ratio 0.49/1.16, +245/+249 min, reference 8720030. Reference high at 04:32,
     1.913 m, gives 08:37 and 0.937 m; NOAA publishes 08:37 and 0.938 m. Reference low at 10:20,
     0.158 m, gives 14:29 and 0.183 m; NOAA publishes 14:29 and 0.183 m.
   - **8518989**, additive `F` (−0.06/+0.03 m), −17/−29 min, reference 8518995. The low is
     07:23 → 06:54 and 0.084 → 0.114 m, the high 12:10 → 11:53 and 1.371 → 1.311 m. Both match NOAA.

   CO-OPS serves subordinate predictions only as `interval=hilo` and only for `datum=MLLW`.
   `interval=6` and `datum=MSL` both return "No Predictions data was found" (verified).
4. **The curve between extremes** is not part of NOAA's product, and this spec does not standardise
   it. MetOceanViewer uses XTide's warp interpolation (HE §4.3) and labels the curve as
   interpolated.

### 5.5 Extremes (normative where this spec uses them)

Subordinate events (§5.4) and computed datums (§8.5) need a definition of a station's high and low
waters that every implementation reproduces.

1. **Per year segment.** Within each UTC year segment `[t_Y, t_{Y+1})`, evaluated with that year's
   arguments, an extremum is a point where h′ changes sign: + to − is a *high*, − to + is a *low*.
   A zero of h′ without a sign change is not an extremum. The segment boundary itself is never an
   extremum, whatever the step there does.
2. **Separation filter.** Repeatedly take the adjacent (high, low) or (low, high) pair with the
   smallest time separation; while that separation is **less than 2 hours**, remove both events.
   The result alternates high/low.
3. **Evidence (verified 2026-10-08).** CO-OPS `interval=hilo` lists for all of 2026 were compared with
   every sign change of the prototype at 8443970, 8761724, 8771450, 8771341 and 9455920:
   - every CO-OPS event was found;
   - the extras exist only at the mixed/diurnal Gulf stations (30, 50 and 76 events), always in
     pairs separated by 0.01–1.99 h, with height differences ≤ 10.4 mm;
   - consecutive CO-OPS events are never closer than 1.99 h, yet CO-OPS keeps pairs whose height
     difference is only 2 mm. CO-OPS therefore filters on time separation, not on height.

   The exact comparison at the 2-hour boundary (< versus ≤) and the removal order are pinned by the
   golden tests of HE §8.2 and are fixed by a patch release of this spec if CO-OPS differs.
4. **CO-OPS quirks at New Year (informative; implementations do not reproduce them).** CO-OPS
   evaluates events that fall up to at least 13 minutes after 1 January 00:00 with the *previous*
   year's arguments, and sometimes lists a spurious event at 23:59 created by the step. Examples:
   - Boston 2032-01-01 00:02 L −1.574 m, where the previous year's curve gives −1.5738 m and the
     current year's −1.5662 m;
   - 8720030 2036-12-31 23:59 H 0.716 m, on a curve that is rising on both sides of New Year.

   This spec's per-segment rule differs from CO-OPS by design in a window of about ±30 minutes
   around 1 January 00:00 UTC. Conformance tests exclude that window from comparisons with CO-OPS.

---

## 6. `sources`

| Member | Type | Req. | Meaning |
|---|---|---|---|
| `id` | token `^[a-z][a-z0-9-]{0,31}$` | yes | Unique within the file (`E-DUPLICATE-SOURCE`), for example `noaa-coops`, `pringle`, `ticon-4`. It is the namespace of its stations' ids. |
| `name` | string | yes | Human name. |
| `url` | string | no | Landing page or API base. |
| `licence` | SPDX expression (§6.1) | yes | The default licence of this source's stations. Use `LicenseRef-<name>` for licences SPDX does not list, for example `LicenseRef-US-Government-Public-Domain` or `LicenseRef-unknown-pringle`. |
| `attribution` | string | yes | The required attribution text, shown by the app next to a station from this source. |
| `retrieved` | date | yes | The date the input was downloaded. |
| `version` | string | no | Upstream release, for example `TICON-4`. |
| `sha256` | hex64 | no | Hash of the downloaded input file (KML, CSV); for API sources, of the cached response bundle. |
| `notes` | string | no | Filters applied, known issues. |

The source `id` is the namespace of its stations' ids (§8.1).

### 6.1 Licence expressions

A licence is an SPDX licence expression restricted to:
- licence identifiers `[A-Za-z0-9.+-]+`, including `LicenseRef-[A-Za-z0-9.-]+`;
- the operators `AND`, `OR` and `WITH`;
- parentheses.

Consumers tokenise the expression rather than search it as text (`E-LICENCE` if it does not parse).
An expression **carries a non-commercial term** if any identifier has a `-NC` component (for
example `CC-BY-NC-4.0`, `CC-BY-NC-SA-4.0`). An unknown licence is written as an explicit
`LicenseRef-unknown-<source>`, never by omission, so that tools can allow or refuse it by name.

---

## 7. Constituent dictionary

### 7.1 Entry

| Member | Type | Req. | Meaning |
|---|---|---|---|
| `name` | `^[A-Za-z0-9][A-Za-z0-9()_.+-]{0,31}$` | yes | Unique within the file, **compared case-insensitively** (`E-DUPLICATE-CONSTITUENT`). This prevents `MSf`/`MSF`- and `Sa`/`SA`-style aliasing. Station rows must use the exact spelling. |
| `speed` | number, °/h | yes | Checksum: it must equal the speed derived from the definition within 1e-5 °/h (`E-SPEED-MISMATCH`). NOAA publishes speeds rounded to 7 significant digits; M6 differs by 7.4e-6, which this tolerance admits. |
| `darwin` | object | one of | `V`: `[T, s, h, p, p₁, c]` (integers; c in degrees). `u`: `[ξ, ν, ν′, 2ν″, Q, R, Qᵤ]` (integers). `f`: node-factor token (§7.2). |
| `compound` | object | one of | `{ "<name>": <non-zero integer>, … }` with 1–8 terms. Every term names a **`darwin`** entry of the same file (`E-COMPOUND-TERM`), so there is no nesting and no cycle. |
| `description` | string | no | Free text. |

### 7.2 Derived quantities

For a Darwin entry with V = (a_T, a_s, a_h, a_p, a_p₁, c) and u = (b_ξ, b_ν, b_ν′, b_2ν″, b_Q, b_R, b_Qᵤ):

```
speed      = a_T·Ṫ + a_s·ṡ + a_h·ḣ + a_p·ṗ + a_p₁·ṗ₁ + b_Q·ṗ                    (°/h, rates at 1900-01-01T00:00Z)
V₀(t_Y)    = a_T·T + a_s·s + a_h·h + a_p·p + a_p₁·p₁ + c                         (at t_Y)
u(t_M)     = b_ξ·ξ + b_ν·ν + b_ν′·ν′ + b_2ν″·2ν″ + b_Q·Q + b_R·R + b_Qᵤ·Qᵤ    (at t_M)
f(t_M)     = formula(f)                                                          (Appendix A.3)
```

- The `b_Q·ṗ` term in `speed` is SP 98's M₁ special case (¶124, as implemented in libcongen).
  It is non-zero only for M₁-type definitions.
- For a compound `{nᵢ: cᵢ}`: `speed = Σ cᵢ·speedᵢ`, `V₀+u = Σ cᵢ·(V₀+u)ᵢ`, `f = Π fᵢ^|cᵢ|`.
  The rule is the IHO one ("f is always obtained by multiplication and not by division", IHO
  Standard List of Tidal Constituents, 2017, Annex B) and libcongen's `operator*=`/`operator+=`.

**Doodson equivalence (informative).** T = τ − s + h, so the Doodson coefficients are
(τ, s, h, p, N′, p₁) = (a_T, a_s + a_T, a_h − a_T, a_p, 0, a_p₁). Add 5 to each one after the first
to get the familiar number: M₂ (2, −2, 2, 0, 0) → 255.555, K₁ → 165.555, O₁ → 145.555,
Sa (0, 0, 1) → 056.555. The IHO extended Doodson number additionally encodes c in units of 90°.

### 7.3 Why the dictionary is in the file

- Names collide. libcongen's input file documents NOS versus IOS definitions with the same name
  (`SA-IOS`, `MF-IOS`, `S1-IOS`, `OO1-IOS`, `R2-IOS`). It records phase-reversed alternatives
  (KJ2, RP1/PSI1) and the NOS M₁ node factor "about 50 % greater than [it] should be". It also states
  that "constituent names are unreliable identifiers".
- Spellings vary between sources: NOAA `LAM2`/`RHO`, XTide `LDA2`/`RHO1`, TICON-4 `LAMBDA2`/`RHO1`/
  `SGM`/`EP2` (verified in the TICON-4 CSV).
- With the definition in the file, a reader computes the right prediction even for a constituent it
  has never heard of. A reader whose built-in catalogue defines the same name differently warns
  (`W-CONSTITUENT-REDEFINED`) and **uses the file's definition**.
- Node-factor families other than SP 98 (for example IHO Annex A formulas such as M1B or γ₂, or
  Foreman's satellites) are not in 1.0. A later minor version may add tokens (§11).

The canonical dictionary for NOAA-derived data is Appendix B. It is generated from the core
catalogue (HE §2.3) and committed as `docs/schemas/constituents-nos.json` (HE WP H1).

---

## 8. Stations

### 8.1 Common members

| Member | Type | Req. | Meaning |
|---|---|---|---|
| `id` | `<source>:<local>`, ≤ 255 B | yes | Unique within the file (`E-DUPLICATE-STATION`). The namespace MUST be a `sources[].id` (`E-UNKNOWN-SOURCE`), and that source is the station's source. The local part is any UTF-8 without control characters, for example `noaa-coops:8761724` or `ticon-4:a121tg-a12-nld-cmems`. An id is stable across builds while the gauge's winning source (HE §7.5) stays the same. |
| `name` | string 1–255 | yes | Display name. |
| `lat`, `lon` | number | yes | WGS 84 degrees. `lat ∈ [−90, 90]`, `lon ∈ (−180, 180]`. The canonical range excludes −180: writers write 180, and readers **reject** −180 (`E-SCHEMA`) rather than normalising it, so each position has one spelling. |
| `licence` | SPDX expression | no | Overrides the source licence for this station. The **effective licence** is `licence` if present, else the source's. |
| `kind` | `"reference"` \| `"subordinate"` | yes | Selects §8.2 or §8.3. |
| `country` | ISO 3166-1 alpha-3 | no | |
| `time_zone` | IANA name | no | For "local time" display only. All data are UTC. |
| `datums` | object | no | Datum heights published by the source (§8.4). |
| `datum_epoch` | string | no | Epoch of `datums`, for example `"1983-2001"` (NTDE) or `"2012-2016"` (a modified epoch). |
| `computed_datums` | object | no | Reference stations only: `{"period": {"start", "end"}, "heights": {token: m}}`, datum heights computed from the harmonics (§8.5). |
| `record` | object | no | `start`, `end` (dates) and `years` (effective length) of the analysed record. |
| `flags` | array of tokens | no | Open vocabulary, unique items. 1.0 defines `hilo_only` (the source publishes only extremes), `possible_datum_issues`, `possible_qc_issues` (from TICON-4 `record_quality`) and `local_time_suspected`. Readers keep unknown flags unchanged. |
| `provenance` | object | no | `source_record` (the id in the source), `also_in` (ids or `source:record` strings of duplicates merged into this station), `dropped_constituents` (names the builder could not define), `notes`. |

### 8.2 Reference station (`kind: "reference"`)

| Member | Type | Req. | Meaning |
|---|---|---|---|
| `constituents` | array 1–512 of rows | yes | `[name, H, G]` or `[name, H, G, H_sd, G_sd]`. `name` must be in the dictionary (`E-UNKNOWN-CONSTITUENT`) and appear at most once per station (`E-DUPLICATE-ROW`). `H ≥ 0` m, `0 ≤ G < 360`, standard deviations ≥ 0. Rows with H = 0 are allowed; writers SHOULD omit them. |
| `z0` | number | no | Height of the analysis mean above the station's MSL, in metres. Default 0. |
| `analysis` | enum | no | How the source derived the constants: `nos`, `utide`, `ttide`, `least_squares` or `published`; absent means unknown. Informative only in 1.0: the engine always applies §5.2. Differences between nodal conventions (for example Foreman's in UTide versus SP 98) are typically below a few millimetres for major constituents, well inside these sources' uncertainty. |
| `offsets` | — | forbidden | |

### 8.3 Subordinate station (`kind: "subordinate"`)

| Member | Type | Req. | Meaning |
|---|---|---|---|
| `offsets.reference` | station id | yes | A station of the same file with `kind: "reference"` (`E-REFERENCE`). There are no chains. |
| `offsets.time_min` | `{"high": Δ_H, "low": Δ_L}`, minutes | yes | Added to the reference high and low times. abs(Δ) ≤ 1440. NOAA publishes integers; fractions are allowed and are rounded to the nearest millisecond, and −0 equals 0. |
| `offsets.height` | object | yes | Either `{"type": "ratio", "high": r_H, "low": r_L}` with r > 0, or `{"type": "offset", "high": a_H, "low": a_L}` in metres. NOAA `heightAdjustedType` R maps to `ratio` and F to `offset` (§14.1). |
| `offsets.datum` | datum token or `"MSL"` | yes | The datum D that the reference heights are taken relative to before the adjustment, and that the results are relative to. NOAA uses `MLLW`. Unless D is `MSL`, the reference station MUST have D in `datums` or `computed_datums.heights` (`E-OFFSET-DATUM`). |
| `constituents`, `z0`, `analysis`, `computed_datums` | — | forbidden | |

A subordinate's own `datums` (when present) let the app shift its predictions from D to another
datum. Without them the predictions exist only relative to D.

### 8.4 Datums

`datums` (published by the source) and `computed_datums.heights` (computed from the harmonics, §8.5)
map a token to the datum's height **above the station's MSL**, in metres. MSL itself is never
listed (it is 0 by definition). 1.0 tokens:

| Token | Meaning |
|---|---|
| `HAT`, `LAT` | Highest/lowest astronomical tide |
| `MHHW`, `MHW` | Mean higher high water, mean high water |
| `DTL`, `MTL` | Diurnal tide level, mean tide level |
| `MLW`, `MLLW` | Mean low water, mean lower low water |
| `NAVD88`, `NGVD29`, `IGLD85` | Geodetic datums (height of the datum's zero above MSL) |
| `STND` | Station (gauge) datum |

- A datum height is a fixed number. Height *differences* such as NOAA `GT`, `MN`, `DHQ`, `DLQ` and
  the intervals `HWI`/`LWI` are not datums and MUST NOT appear.
- Sign: to express a series from MSL in datum X, **subtract** `datums[X]`. For example, MLLW at
  Fernandina Beach is −1.004, so h_MLLW = h_MSL + 1.004 m.
- A token appears in at most one of `datums` and `computed_datums.heights` (`E-DATUM-CONFLICT`):
  a published value is never shadowed by a computed one.
- A reader whose datum model lacks a 1.0 token keeps the value and warns `W-DATUM-UNSUPPORTED`.
  MetOceanViewer extends its model to every 1.0 token (HE §2.5, E6).
- Consumers MUST show computed heights as **computed from harmonics, not observed** wherever they
  show a datum (labels, exports, metadata).

### 8.5 Computed datums

`computed_datums` holds tidal datums derived from the station's own constants, for sources that
publish none (Pringle, TICON-4; owner decision 2026-10-08). The flag is the member itself: every
height under `computed_datums.heights` is computed, every height under `datums` is published.

| Member | Meaning |
|---|---|
| `period.start`, `period.end` | Dates; the computation covers `[start, end)`. It MUST span at least 18.61 years (one nodal cycle); MetOceanViewer's builder uses 19 calendar years, 2007-01-01 to 2026-01-01, and records it. |
| `heights` | Token → height above MSL in metres. Allowed tokens: `HAT`, `MHHW`, `MHW`, `DTL`, `MTL`, `MLW`, `MLLW`, `LAT`. |

Definitions, over the high/low events of §5.5 in the period (heights include `z0`):

| Token | Value |
|---|---|
| `HAT`, `LAT` | Highest high and lowest low event. |
| `MHW`, `MLW` | Mean of all high events, of all low events. |
| `MHHW`, `MLLW` | Split the period into tidal days of 24.84 h starting at `period.start`. Take the mean, over the tidal days that contain at least one high (low), of the day's highest high (lowest low). |
| `MTL` | (MHW + MLW) / 2 |
| `DTL` | (MHHW + MLLW) / 2 |

These are astronomical-tide statistics, not NOAA's tabulated datums (which come from observations
over a National Tidal Datum Epoch). For a NOAA station the two typically agree to a few
centimetres; the builder never computes datums for a station that publishes them.

---

## 9. Numbers and precision

| Quantity | Unit | Range (schema) | Writer precision (SHOULD) | Notes |
|---|---|---|---|---|
| amplitude H, sd | m | 0–100 | round to 1e-5 m (0.01 mm) | NOAA publishes mm; TICON-4 cm with 6 decimals |
| phase G, sd | ° | [0, 360) | round to 1e-3°, then normalise into [0, 360) | Normalise after rounding, so 359.9996 → 0 |
| `z0`, datum heights, additive offsets | m | ±100 / ±1000 | 1e-4 m | |
| ratios | — | (0, 100] | as published | NOAA 2 decimals |
| time offsets | min | ±1440 | as published | Consumers convert to integer milliseconds by rounding half away from zero |
| `lat`, `lon` | ° | see §8.1 | 1e-6° (≈ 0.1 m) | |
| `speed` | °/h | 0–1000 | full double, shortest round trip | Checksum only |

- Readers parse every number as a binary64 double (`speed`, heights, phases) or an integer where the
  schema says so.
- Parsing MUST NOT depend on the process locale; HE §8 tests it under `de_DE.UTF-8`.
- Missing values are absent members. A station with no datums has no `datums` member; it does not
  have `{"MLLW": null}`.

---

## 10. Validation

The schema covers structure, types, ranges and enums. The reader enforces this section, including
everything the schema cannot express. Every error carries a **JSON Pointer** (RFC 6901) to the
offending value, and the offending value itself where there is one. Errors found before the JSON
text is parsed (size, gzip, syntax, encoding) carry a byte offset instead.

### 10.1 Errors (the file is rejected)

| Code | Rule |
|---|---|
| `E-SIZE` | The input exceeds the compressed or decompressed size limit (§10.3). |
| `E-GZIP` | Corrupt, truncated or multi-member gzip, or bytes after the member. |
| `E-NOT-JSON` | Syntax error (byte offset). |
| `E-ENCODING` | Invalid UTF-8. |
| `E-DUPLICATE-MEMBER` | Repeated member name in one object. |
| `E-LIMIT` | A structural limit was exceeded: depth, string length, array length (§10.3). |
| `E-FORMAT` | `format` is absent or has another value. The reader reports "not this format". |
| `E-VERSION` | `version` is not SemVer. |
| `E-MAJOR` | The major version is not 1. |
| `E-SCHEMA` | A schema rule failed: type, required member, enum, range, unknown member in a same-or-older-minor file, null, or a 4-element row. |
| `E-CONVENTION` | A `conventions` value differs from §5.1. |
| `E-DUPLICATE-SOURCE`, `E-DUPLICATE-STATION`, `E-DUPLICATE-CONSTITUENT`, `E-DUPLICATE-ROW` | Uniqueness (constituent names case-insensitively). |
| `E-UNKNOWN-SOURCE`, `E-UNKNOWN-CONSTITUENT` | A dangling reference (a station id whose namespace is not a source; a row naming no dictionary entry). |
| `E-LICENCE` | A licence expression does not parse (§6.1). |
| `E-COMPOUND-TERM` | A compound term names a missing or non-`darwin` entry. |
| `E-SPEED-MISMATCH` | abs(speed − derived) > 1e-5 °/h. |
| `E-REFERENCE` | `offsets.reference` is missing or is not a reference station. |
| `E-OFFSET-DATUM` | The reference has `offsets.datum` neither in `datums` nor in `computed_datums.heights`. |
| `E-DATUM-CONFLICT` | A token appears in both `datums` and `computed_datums.heights`, or a computed period is shorter than 18.61 years. |
| `E-DATE` | `created`, `record` or `computed_datums.period` dates are not valid calendar dates, or an end precedes its start. |

### 10.2 Warnings (the file is accepted)

| Code | When |
|---|---|
| `W-BOM` | A leading BOM was ignored. |
| `W-MINOR-NEWER` | The file's minor version is newer than the reader's. Unknown members are ignored and unusable items are skipped transitively (§11); the warning carries the count per reason. |
| `W-CONSTITUENT-REDEFINED` | The definition differs from the reader's built-in catalogue entry of the same name. The file wins. |
| `W-DATUM-UNSUPPORTED` | A valid datum token that the reader's datum model cannot represent. |
| `W-UNUSED-CONSTITUENT`, `W-UNUSED-SOURCE` | A dictionary entry or source that no station references. |

### 10.3 Limits (MetOceanViewer reader defaults; other readers MAY differ)

| Limit | Default |
|---|---|
| Input file size (read at most limit + 1 bytes, so an oversize file is detected without trusting its stated size) | 64 MiB |
| Decompressed JSON size (zip-bomb guard) | 64 MiB |
| Nesting depth | 16 (the format needs 6) |
| String length | 4 KiB |
| Array length (any array but `stations`) | 4,096 |
| `stations` | 1,000,000 |
| Rows per station | 512 (schema) |

A realistic global file is about 10 MB of JSON (§12), so 64 MiB leaves six-fold headroom while
bounding the reader's memory (HE §6.3).

### 10.4 Order of checks

A reader that stops at the first error reports the first failure in this order, so the same file
always gives the same error:

1. size, then gzip;
2. syntax, encoding, duplicate members and structural limits, in document order;
3. `format`, then `version` (`E-VERSION`, `E-MAJOR`), then `conventions`;
4. schema rules, in document order;
5. `sources` (uniqueness, licences), then the dictionary (names, terms, speeds);
6. stations in document order (namespace, rows, datums, dates);
7. cross-station rules (duplicate ids, then subordinate references and offset datums).

A validator MAY instead report every failure it can reach; steps after a syntax error are skipped.

---

## 11. Versioning and compatibility

`version` is SemVer. The JSON Schema for every 1.x lives at
`docs/schemas/harmonics-v1.schema.json`. It describes the newest 1.x minor in full, and its
`version` pattern accepts every 1.x, so a validator can tell "wrong major" from "newer minor". A
newer-minor file usually fails the older schema's `additionalProperties`; that is expected, and
the test manifest treats such files as a third category (§16).

| Change | Bump | Examples |
|---|---|---|
| Wording, examples, a fixed typo; no structural or semantic change | patch | `1.0.1` |
| New optional member, new token in an open vocabulary (flags, datum tokens, node-factor families), new station `kind`, new top-level array | minor | 1.1 currents (§15) |
| Change of meaning, unit or convention, a new required member, removal | major | `nodal_evaluation` other than `nos_yearly` as the meaning of existing data |

Reader policy:

| File | Reader behaviour |
|---|---|
| Same major, minor ≤ reader's | Read strictly: unknown members are `E-SCHEMA`. |
| Same major, newer minor | Warn `W-MINOR-NEWER`. Ignore unknown members. Skip, transitively: dictionary entries with an unknown definition kind or node-factor token; reference stations with a row naming a skipped entry; stations of an unknown `kind`; subordinates whose reference was skipped. Count each skip by reason in the warning. |
| Other major | `E-MAJOR`. |

Writers always write the newest version they implement. Files are never edited in place;
MetOceanViewer writes them atomically (core-design C16).

---

## 12. Size and performance

Measured with a synthetic file (10,000 reference stations × 37 constituents, random values,
realistic metadata, generated in scratch on 2026-10-08). Real data compress better than random values.

| Layout | Compact | gzip -9 |
|---|---|---|
| **rows `[name, H, G]`** (chosen) | 10.2 MB | 2.4 MB |
| parallel arrays `{name:[], amp:[], phase:[]}` | 9.7 MB | 1.9 MB |
| object keyed by name `{"M2":[H,G]}` | 10.2 MB | 2.4 MB |

Rows cost 5 % uncompressed over parallel arrays. In exchange, an amplitude cannot be separated from
its phase, the schema validates each row, and a row diffs as one unit. The keyed-object layout has
the same size but turns duplicate constituents into duplicate member names, which typical parsers
drop silently.

A realistic global file has about 3,500 NOAA stations (1,260 reference with about 25–37 rows each,
2,242 subordinates of about 250 B each), about 2,800 Pringle pins (8–9 rows) and about 3,900
TICON-4 coastal gauges (50 rows). That gives about 8–10 MB uncompressed and 2–3 MB gzipped.

Reader memory, measured with nlohmann/json 3.12.0 (GCC 12, `-O2`) on the 9.6 MB synthetic file:

| Approach | Peak RSS | Parse time |
|---|---|---|
| Input text only (baseline) | 23.6 MB | — |
| Whole-document DOM | 108.1 MB (+84 MB, about 9× the text) | 0.57 s |
| Parser callback that decodes and discards each station (HE §6.3) | 22.5 MB (no growth over the text) | 0.47 s |

The typed in-memory result adds about 25 B per constituent row (about 10 MB for this file).

---

## 13. Worked example

### 13.1 File

This file is complete and valid. It validates against the schema (checked with `jsonschema` 4.26 on
2026-10-08). Values are from CO-OPS: reference 8720030, 7 of its 37 constituents for brevity, with
datums converted from STND-relative to MSL-relative (§14.1); subordinate 8720001 with its published
offsets.

```json
{
  "format": "metoceanviewer-harmonics",
  "version": "1.0.0",
  "created": "2026-10-08T12:00:00Z",
  "generator": {"name": "mov-harmonics-builder", "version": "5.0.0-dev"},
  "title": "Worked example (docs/harmonics-json.md section 13)",
  "licence_summary": "NOAA CO-OPS data: U.S. Government work, public domain. Not for navigation.",
  "conventions": {"amplitude_unit": "m", "phase": "greenwich_phase_lag_deg", "time_standard": "UTC",
                  "astronomy": "sp98", "nodal_evaluation": "nos_yearly", "reference_level": "MSL"},
  "sources": [
    {"id": "noaa-coops", "name": "NOAA CO-OPS Metadata API",
     "url": "https://api.tidesandcurrents.noaa.gov/mdapi/prod/",
     "licence": "LicenseRef-US-Government-Public-Domain",
     "attribution": "NOAA National Ocean Service, Center for Operational Oceanographic Products and Services",
     "retrieved": "2026-10-08"}
  ],
  "constituents": [
    {"name": "M2",  "speed": 28.9841042, "darwin": {"V": [2, -2, 2, 0, 0, 0],   "u": [2, -2, 0, 0, 0, 0, 0], "f": "sp98-78"}},
    {"name": "S2",  "speed": 30.0,       "darwin": {"V": [2, 0, 0, 0, 0, 0],    "u": [0, 0, 0, 0, 0, 0, 0],  "f": "unity"}},
    {"name": "N2",  "speed": 28.4397295, "darwin": {"V": [2, -3, 2, 1, 0, 0],   "u": [2, -2, 0, 0, 0, 0, 0], "f": "sp98-78"}},
    {"name": "K1",  "speed": 15.0410686, "darwin": {"V": [1, 0, 1, 0, 0, -90],  "u": [0, 0, -1, 0, 0, 0, 0], "f": "sp98-227"}},
    {"name": "O1",  "speed": 13.9430356, "darwin": {"V": [1, -2, 1, 0, 0, 90],  "u": [2, -1, 0, 0, 0, 0, 0], "f": "sp98-75"}},
    {"name": "M4",  "speed": 57.9682084, "compound": {"M2": 2}},
    {"name": "MS4", "speed": 58.9841042, "compound": {"M2": 1, "S2": 1}}
  ],
  "stations": [
    {"id": "noaa-coops:8720001", "name": "Kings Ferry", "lat": 30.7867, "lon": -81.84, "kind": "subordinate", "country": "USA", "offsets": {"reference": "noaa-coops:8720030", "time_min": {"high": 245, "low": 249}, "height": {"type": "ratio", "high": 0.49, "low": 1.16}, "datum": "MLLW"}, "flags": ["hilo_only"], "provenance": {"source_record": "8720001"}},
    {"id": "noaa-coops:8720030", "name": "Fernandina Beach, FL", "lat": 30.671356, "lon": -81.46584, "kind": "reference", "country": "USA", "time_zone": "America/New_York", "constituents": [["M2", 0.87, 33.7], ["S2", 0.134, 62.1], ["N2", 0.196, 17.8], ["K1", 0.103, 209.1], ["O1", 0.077, 214.4], ["M4", 0.03, 203.7], ["MS4", 0.016, 230.7]], "datums": {"MHHW": 0.995, "MHW": 0.888, "DTL": -0.005, "MTL": -0.029, "MLW": -0.947, "MLLW": -1.004, "NAVD88": 0.161, "STND": -1.522}, "datum_epoch": "1983-2001", "analysis": "nos", "provenance": {"source_record": "8720030", "notes": "7 of 37 published constituents, for illustration"}}
  ]
}
```

### 13.2 Test vectors (from the prototype; the C++ tests pin them to 1e-6)

Astronomy at t₂₀₂₆ = 2026-01-01T00:00Z: T = 180.0000°, s = 67.9854°, h = 280.6660°, p = 61.2922°,
p₁ = 283.3877°. At t_M = 2026-07-02T12:00Z: N = 332.5024°, I = 28.1112°, ν = −5.0417°, ξ = −4.5441°,
ν′ = −3.5949°, 2ν″ = −7.6009°.

| Constituent | speed °/h | (V₀+u) 2026 ° | f 2026 |
|---|---|---|---|
| M2 | 28.9841042 | 66.3564 | 0.96735 |
| S2 | 30.0000000 | 0.0000 | 1.00000 |
| N2 | 28.4397295 | 59.6632 | 0.96735 |
| K1 | 15.0410686 | 14.2609 | 1.10312 |
| O1 | 13.9430356 | 50.6487 | 1.16682 |
| M4 | 57.9682084 | 132.7128 | 0.93577 |
| MS4 | 58.9841042 | 66.3564 | 0.96735 |

Predicted heights at 8720030 above MSL (7 constituents):

| UTC | h, m |
|---|---|
| 2026-10-01T00:00Z | −0.6242 |
| 2026-10-01T04:30Z | 0.8267 |
| 2026-10-01T12:00Z | −0.7987 |

Extremes (§5.5; none of these pairs is closer than 2 h), with MLLW = MSL − 1.004 m, and the
subordinate events from §5.4:

| Reference event (UTC) | h MSL | h MLLW | Subordinate 8720001 (UTC) | h MLLW |
|---|---|---|---|---|
| H 2026-10-01T04:27:17 | 0.8269 | 1.8309 | 08:32:17 | 0.8972 (×0.49) |
| L 2026-10-01T10:26:04 | −1.1306 | −0.1266 | 14:35:04 | −0.1468 (×1.16) |
| H 2026-10-01T17:04:06 | 1.1760 | 2.1800 | 21:09:06 | 1.0682 |
| L 2026-10-01T23:13:23 | −0.9378 | 0.0662 | 2026-10-02T03:22:23 | 0.0768 |

With all 37 constituents, NOAA's published 2026-10-01 events at 8720030 are H 04:32 1.913 m and
L 10:20 0.158 m (MLLW). The 7-constituent example differs, as it should.

---

## 14. Source mapping (informative, for producers)

### 14.1 NOAA CO-OPS Metadata API (`https://api.tidesandcurrents.noaa.gov/mdapi/prod/`)

| API (verified 2026-10-08) | Format |
|---|---|
| `webapi/stations.json?type=harcon`: 1,371 stations | candidate reference stations |
| `stations/{id}/harcon.json?units=metric` → `HarmonicConstituents[]{number, name, description, amplitude, phase_GMT, phase_local, speed, comments}`, `units: "meters"` | rows `[canonical(name), amplitude, phase_GMT]`. **Use `phase_GMT`, never `phase_local`.** `phase_local` is referred to the station's time meridian. Example, 8761724 M2: `phase_GMT` 160.9 versus `phase_local` 347.0. Rows with amplitude 0 (unanalysed, phase 0) are omitted. `speed` is checked against the dictionary within 1e-5. `comments` (for example "Vector Average of 5 years (2021-2025)") goes to `record`/`provenance.notes`. Subordinate stations return an empty list. |
| `stations/{id}/datums.json?units=metric` → `datums[]{name, value}` (relative to STND), top-level `LAT`, `HAT`, `epoch`, `OrthometricDatum` | `datums[X] = value_X − value_MSL` for X ∈ {MHHW, MHW, DTL, MTL, MLW, MLLW, NAVD88, NGVD29, IGLD85, STND} plus LAT/HAT. **Skip** GT, MN, DHQ, DLQ, HWI and LWI, which are ranges and intervals. `epoch` → `datum_epoch`. NAVD88 is missing at some stations (8761724 lists none although `OrthometricDatum` is NAVD88): leave it absent. |
| `stations.json?type=tidepredictions&expand=tidepredoffsets&units=metric`: one request, 3,502 stations (1,260 `R`, 2,242 `S`) | for `type == "S"`: `offsets.reference = "noaa-coops:" + refStationId`; `timeOffsetHighTide/LowTide` (minutes) → `time_min.high/low`; `heightAdjustedType` `R` → `ratio` (1,974 stations), `F` → `offset` (268 stations) in metres; `datum` = `MLLW`; flag `hilo_only`. |
| `datagetter?product=predictions&interval=hilo` | High/low lists drop extremum pairs closer than 2 h and have New Year quirks (§5.5). |
| units | `F` offsets are in feet unless `units=metric`. Verified: 8518989 gives −0.2/0.1 by default and −0.06/0.03 with `units=metric`. |
| Dangling references | 109 distinct `refStationId`; one is not a listed reference station. The builder drops such subordinates and logs them. |
| Licence | U.S. Government work, public domain. NOAA asks that modified data not be presented as official (research §2). |

### 14.2 William Pringle's compilation (Google My Maps KML, `mid=1yvnYoLUFS9kcB5LnJEdyxk2qz6g`)

Verified on the 2026-10-08 export (SHA-256 `c43def1417c6e3d4…c02758`, 8,227,382 B):

- 8 folders: `truth_pelagic`, `truth_shallow`, `truth_coastal_woce`, `NOAA_Database`,
  `JMA_Database`, `KHOA_Database`, `GESLA_NoRepeats`, `UHSLC_Fast`.
- Read `ExtendedData/Data[@name]`, not the HTML `description`.
- Field names vary by layer: `<C>_amp` with `<C>_phase` or `<C>_phs`; `Lat_orig` versus `Lat-orig`.
- `NOAA_ID` is a float string (`"1611347.0"`).
- `M2_Seasonal_phs` holds Excel-mangled dates (`"25-Aug"`); ignore it.
- Amplitudes are in metres. Phases are Greenwich lags "referenced to GMT" (map page).
- `NOAA_Database` duplicates CO-OPS at lower precision (8 constituents, `M4` = 0 placeholder). The
  builder drops this layer whenever CO-OPS is an input.
- KHOA/GESLA/UHSLC carry `Start_Time`, `End_Time`, `GaplessLength`, `CNUM`, `PTV` and `SNR_CN`.
  These go to `record` and `provenance.notes`. Set `analysis: "utide"` for these layers and
  `"published"` for the FES "truth" layers.
- Licence: none stated. Use source licence `LicenseRef-unknown-pringle`, which every Pringle station
  inherits, and name the per-layer originators in `attribution`. The owner includes these stations
  in their own files and contacts Pringle before any public redistribution of a built file (owner
  decision 2026-10-08).
- Ids: `pringle:<layer>/<placemark name>`, with `#2`, `#3` suffixes for duplicate names within a layer.
- No datums: the builder adds `computed_datums` (§8.5).

### 14.3 TICON-4 (SEANOE DOI 10.17882/109129, CC BY 4.0, CSV `data/122848.csv`)

Verified on the CSV header and the first 50 rows:

- Long format, one row per gauge and constituent. Columns `lat, lon, con, amp, pha, amp_std,
  pha_std, missing_obs, no_of_obs, years_of_obs, start_date, end_date, gesla_source,
  tide_gauge_name, type, country, record_quality, datum_information`.
- **`amp` is in centimetres** (North Sea M2 = 27.38). Divide by 100.
- `pha` is in (−180, 180]. Normalise into [0, 360).
- Dates are `dd/mm/yyyy`.
- Station key: `tide_gauge_name`. `gesla_source` (for example `gesla4.CMEMS`) drives the licence
  override.
- 50 constituents per gauge. Names need aliases: `LAMBDA2`, `SGM` (σ₁), `EP2` (ε₂), `RHO1`, plus
  constituents absent from the NOS catalogue: `MSQM`, `MTM`, `3L2`, `MA2`, `MB2`, `S3`, `T3`, `R3`
  and others. Constituents the dictionary cannot define go to `provenance.dropped_constituents`.
- Pitfalls documented by the Slackwater maintainers
  (https://github.com/openwatersio/slackwater-database/blob/main/sources/ticon/README.md):
  - the manual swaps the MKS2 and 3N2 rows; the data match the IHO MKS2 speed 29.0662415°/h;
  - Sa is Doodson 056.555, which equals NOS SA, not the IHO alternative with p₁;
  - German `wsv` and Dutch `rws` gauges are labelled UTC but recorded in local time. Flag them
    `local_time_suspected`; the builder excludes them by default (HE §7.4).
- `type` ∈ {Coastal, River, Lake}. Producers SHOULD keep `Coastal` only.
- `record_quality` maps to flags.
- `datum_information` (`MSL`, `DVR90`, `Unspecified`) describes the gauge zero, **not** a tidal
  datum. Do not map it to `datums`; the builder adds `computed_datums` (§8.5).

---

## 15. Currents (possible 1.1, not a commitment)

What CO-OPS exposes (verified):
- `harcon.json` for current stations, with a `bin` parameter: `binNbr`, `binDepth`,
  `constituentName`, `majorAmplitude`, `majorPhaseGMT`, `minorAmplitude`, `minorPhaseGMT`, `azi`,
  `majorMeanSpeed`, `minorMeanSpeed`. Units are "meters, centimeters/second" (ACT1616 bin 1:
  M2 major 93.53 cm/s, `majorPhaseGMT` 322.3).
- `currentpredictionoffsets.json`: `refStationId`, `refStationBin`, `meanFloodDir`, `meanEbbDir`,
  `mfcTimeAdjMin`, `sbeTimeAdjMin`, `mecTimeAdjMin`, `sbfTimeAdjMin`, `mfcAmpAdj`, `mecAmpAdj`.
- `stations.json?type=currentpredictions` returns 4,439 stations (types H 2,221, S 1,939, W 279).

Currents are a possible post-v5.0 feature (owner decision 2026-10-08). If pursued:
- New station kinds `current_reference` (per-bin rows `[name, major_amp, major_G, minor_amp,
  minor_G]` in m s⁻¹, plus `azimuth`, `bin`, `depth`) and `current_subordinate` (flood/ebb/slack
  offsets and ratios).
- 1.0 readers skip both kinds with `W-MINOR-NEWER` (§11).

---

## 16. Tests and compliance

- The schema is checked with `jsonschema` (pinned) in the existing `format-compliance` CI job
  (`tools/compliance/requirements.txt`).
- Fixtures live in `tests/fixtures/io/harmonics/{valid,invalid,newer}/`. A manifest records, for
  each fixture, the expected schema verdict and the expected reader result. There are three
  categories:
  - **valid**: schema-valid, reader `ok`;
  - **invalid**: reader error code; schema-invalid for structural rules, schema-valid for the semantic
    rules of §10.1 the schema cannot express (dangling references, speeds, …);
  - **newer**: a 1.x file with x > 0 using members the current schema does not know. The schema
    verdict is not asserted; the reader must return `ok` with `W-MINOR-NEWER` and the expected skip
    counts.
- The §13 example is fixture `valid/worked_example.json`. Its test vectors (§13.2) are golden
  values in core tests (HE §8).
- Producers SHOULD run both the schema and `metocean-data harmonics validate <file>` (HE §7.8)
  before publishing a file.

---

## 17. References (all accessed 2026-10-08)

- Schureman, P. (1958). *Manual of Harmonic Analysis and Prediction of Tides*. Special Publication
  98, revised (1940) edition, reprinted 1958 with corrections. U.S. Coast and Geodetic Survey.
  Scan: https://flaterco.com/files/xtide/SP98-1958.pdf (Table 1 astronomical elements, Table 2
  constituents and node-factor formula numbers, Tables 14/15).
- Flater, D. *congen* 1.7 (libcongen, `congen_input.txt`, README, `Congen` header), GPL-3:
  https://flaterco.com/files/xtide/congen-1.7-r2.tar.xz (listed at
  https://flaterco.com/xtide/files.html).
- IHO TWCWG. *Standard List of Tidal Constituents* (B. Simon, SHOM; J. Page, UKHO), updated
  2017-05-08: https://flaterco.com/files/xtide/Constituents-20170508.pdf (XDO numbers, nodal
  codes, Annex A formulas, Annex B compound rules).
- Foreman, M.G.G. (1977, revised 2004). *Manual for Tidal Heights Analysis and Prediction*.
  Pacific Marine Science Report 77-10, Institute of Ocean Sciences (cited by libcongen for
  Doodson-style satellites).
- Doodson, A.T. (1921). The harmonic development of the tide-generating potential. *Proc. R. Soc.
  Lond. A* 100, 305–329.
- Parker, B.B. (2007). *Tidal Analysis and Prediction*. NOAA Special Publication NOS CO-OPS 3:
  https://repository.library.noaa.gov/view/noaa/50576 (the PDF could not be retrieved
  automatically: HTTP 403 from the repository's CDN).
- NOAA CO-OPS Metadata API documentation: https://api.tidesandcurrents.noaa.gov/mdapi/prod/
  (Harmonic Constituent, Current Harmonic Constituent, Tide Prediction Offsets and Current
  Prediction Offsets field tables). Data API:
  https://api.tidesandcurrents.noaa.gov/api/prod/datagetter.
- XTide: subordinate station offsets, https://flaterco.com/xtide/harmonics.html. Implementation:
  `thirdparty/xtide-2.15.1/libxtide/SubordinateStation.cc:106-217` (vendored, GPL-3).
- TICON-4: https://www.seanoe.org/data/00980/109129/. Slackwater TICON notes:
  https://github.com/openwatersio/slackwater-database/blob/main/sources/ticon/README.md.
- Pringle, W. Global Tide Gauge Database: https://sites.nd.edu/william-pringle/global-tide-gauge-database/
  (KML export `https://www.google.com/maps/d/kml?mid=1yvnYoLUFS9kcB5LnJEdyxk2qz6g&forcekml=1`).
- RFC 8259 (JSON), RFC 6901 (JSON Pointer), RFC 1952 (gzip), RFC 2119; JSON Schema 2020-12:
  https://json-schema.org/draft/2020-12; SPDX licence expressions: https://spdx.github.io/spdx-spec/.

---

## Appendix A. SP 98 formulas (normative for `astronomy: "sp98"`)

Angles are in degrees. `T_c` is in Julian centuries of 36,525 days since **1899-12-31T12:00:00Z**,
computed from UTC. Source: SP 98 Table 1, as coded in libcongen `V_terms`/`midyear_terms`.

### A.1 Mean longitudes

`″` = arcseconds; `rev` = 360°. The hour angle of the mean sun is `T = 360° × (days since the epoch)`,
which is 180° at 00:00 UTC.

| Element | Constant | × T_c | × T_c² | × T_c³ |
|---|---|---|---|---|
| s (moon) | 270°26′14.72″ | 1336 rev + 1,108,411.2″ | +9.09″ | +0.0068″ |
| h (sun) | 279°41′48.04″ | 129,602,768.13″ | +1.089″ | 0 |
| p (lunar perigee) | 334°19′40.87″ | 11 rev + 392,515.94″ | −37.24″ | −0.045″ |
| N (lunar node) | 259°10′57.12″ | −(5 rev + 482,912.63″) | +7.58″ | +0.008″ |
| p₁ (solar perigee) | 281°13′15.0″ | 6,189.03″ | +1.63″ | +0.012″ |

- Rates (°/h, for speeds) are the T_c derivatives at 1900-01-01T00:00Z divided by 876,600 h per
  Julian century: Ṫ = 15°/h, ṡ = 0.5490165°/h, ḣ = 0.0410686°/h, ṗ = 0.0046418°/h,
  ṗ₁ = 0.0000020°/h (prototype).
- Constants: ω = 23°27′8.26″ (obliquity, 1900), i = 5°8′43.3546″ (lunar inclination).

### A.2 Nodal terms (from N and p at mid-year)

```
cos I  = cos ω cos i − sin ω sin i cos N                  (18° < I < 29°)
sin ν  = sin i sin N / sin I                              ν = asin(sin ν)
sin Ω  = sin ω sin N / sin I;  cos Ω = cos N cos ν + sin N sin ν cos ω
ξ      = N − atan2(sin Ω, cos Ω)
ν′     = atan2(sin 2I sin ν, sin 2I cos ν + 0.3347)
2ν″    = atan2(sin² I sin 2ν, sin² I cos 2ν + 0.0727)
P      = p − ξ
Q      = atan2(0.483 sin P, cos P)          Qᵤ = P − Q          Qₐ = 1 / √(2.31 + 1.435 cos 2P)
R      = atan2(sin 2P, cot²(I/2)/6 − cos 2P)                    Rₐ = 1 / √(1 − 12 tan²(I/2) cos 2P + 36 tan⁴(I/2))
```

### A.3 Node factors (token → formula; SP 98 formula numbers)

| Token | f | Used by (NOS) |
|---|---|---|
| `unity` | 1 | S2, P1, S1, SA, SSA, T2, R2 |
| `sp98-73` | (2/3 − sin² I) / 0.5021 | MM |
| `sp98-74` | sin² I / 0.1578 | MF |
| `sp98-75` | sin I cos²(I/2) / 0.38 | O1, Q1, 2Q1, RHO |
| `sp98-76` | sin 2I / 0.7214 | J1 |
| `sp98-77` | sin I sin²(I/2) / 0.0164 | OO1 |
| `sp98-78` | cos⁴(I/2) / 0.9154 | M2, N2, 2N2, NU2, MU2, LAM2 |
| `sp98-79` | sin² I / 0.1565 | KJ2 |
| `sp98-144` | (1 − 10 sin²(I/2) + 15 sin⁴(I/2)) cos²(I/2) / 0.5873 | M1C |
| `sp98-149` | cos⁶(I/2) / 0.8758 | M3 |
| `sp98-206` | f₇₅ / Qₐ | M1 |
| `sp98-215` | f₇₈ / Rₐ | L2 |
| `sp98-227` | √(0.8965 sin² 2I + 0.6001 sin 2I cos ν + 0.1006) | K1 |
| `sp98-235` | √(19.0444 sin⁴ I + 2.7702 sin² I cos 2ν + 0.0981) | K2 |

---

## Appendix B. NOS dictionary (the 37 constituents CO-OPS publishes)

Canonical names are NOAA's spellings. Definitions are SP 98 Table 2 as used by NOS; speeds are
derived per §7.2, and NOAA's published speeds agree to ≤ 7.4e-6 °/h (verified).

| # | Name | Speed °/h | Definition (`V` / `u` / `f`, or compound) |
|---|---|---|---|
| 1 | M2 | 28.9841042 | [2,−2,2,0,0,0] / [2,−2,0,0,0,0,0] / sp98-78 |
| 2 | S2 | 30.0000000 | [2,0,0,0,0,0] / 0 / unity |
| 3 | N2 | 28.4397295 | [2,−3,2,1,0,0] / [2,−2,…] / sp98-78 |
| 4 | K1 | 15.0410686 | [1,0,1,0,0,−90] / [0,0,−1,0,0,0,0] / sp98-227 |
| 5 | M4 | 57.9682084 | {M2: 2} |
| 6 | O1 | 13.9430356 | [1,−2,1,0,0,90] / [2,−1,0,0,0,0,0] / sp98-75 |
| 7 | M6 | 86.9523126 | {M2: 3} |
| 8 | MK3 | 44.0251729 | {M2: 1, K1: 1} |
| 9 | S4 | 60.0000000 | {S2: 2} |
| 10 | MN4 | 57.4238337 | {M2: 1, N2: 1} |
| 11 | NU2 | 28.5125831 | [2,−3,4,−1,0,0] / [2,−2,…] / sp98-78 |
| 12 | S6 | 90.0000000 | {S2: 3} |
| 13 | MU2 | 27.9682084 | [2,−4,4,0,0,0] / [2,−2,…] / sp98-78 |
| 14 | 2N2 | 27.8953548 | [2,−4,2,2,0,0] / [2,−2,…] / sp98-78 |
| 15 | OO1 | 16.1391017 | [1,2,1,0,0,−90] / [−2,−1,0,0,0,0,0] / sp98-77 |
| 16 | LAM2 | 29.4556253 | [2,−1,0,1,0,180] / [2,−2,…] / sp98-78 |
| 17 | S1 | 15.0000000 | [1,0,0,0,0,0] / 0 / unity |
| 18 | M1 | 14.4966939 | [1,−1,1,0,0,−90] / [1,−1,0,0,1,0,0] / sp98-206 (NOS form; speed includes ṗ via b_Q, so the yearly and instantaneous arguments differ by about 20° at mid-year, §5.2) |
| 19 | J1 | 15.5854433 | [1,1,1,−1,0,−90] / [0,−1,0,0,0,0,0] / sp98-76 |
| 20 | MM | 0.5443747 | [0,1,0,−1,0,0] / 0 / sp98-73 |
| 21 | SSA | 0.0821373 | [0,0,2,0,0,0] / 0 / unity |
| 22 | SA | 0.0410686 | [0,0,1,0,0,0] / 0 / unity |
| 23 | MSF | 1.0158958 | {S2: 1, M2: −1} (SP 98 p. 48: MSf is S2 − M2, so u = −u(M2)) |
| 24 | MF | 1.0980331 | [0,2,0,0,0,0] / [−2,0,0,0,0,0,0] / sp98-74 |
| 25 | RHO | 13.4715145 | [1,−3,3,−1,0,90] / [2,−1,0,0,0,0,0] / sp98-75 |
| 26 | Q1 | 13.3986609 | [1,−3,1,1,0,90] / [2,−1,0,0,0,0,0] / sp98-75 |
| 27 | T2 | 29.9589333 | [2,0,−1,0,1,0] / 0 / unity |
| 28 | R2 | 30.0410667 | [2,0,1,0,−1,180] / 0 / unity |
| 29 | 2Q1 | 12.8542862 | [1,−4,1,2,0,90] / [2,−1,0,0,0,0,0] / sp98-75 |
| 30 | P1 | 14.9589314 | [1,0,−1,0,0,90] / 0 / unity |
| 31 | 2SM2 | 31.0158958 | {S2: 2, M2: −1} |
| 32 | M3 | 43.4761563 | [3,−3,3,0,0,0] / [3,−3,0,0,0,0,0] / sp98-149 |
| 33 | L2 | 29.5284789 | [2,−1,2,−1,0,180] / [2,−2,0,0,0,−1,0] / sp98-215 |
| 34 | 2MK3 | 42.9271398 | {M2: 2, K1: −1} |
| 35 | K2 | 30.0821373 | [2,0,2,0,0,0] / [0,0,0,−1,0,0,0] / sp98-235 |
| 36 | M8 | 115.9364169 | {M2: 4} |
| 37 | MS4 | 58.9841042 | {M2: 1, S2: 1} |

`[2,−2,…]` abbreviates `[2,−2,0,0,0,0,0]`. Stations with more constituents (for example Anchorage
9455920, 114 published) need further definitions: SP 98 Table 2 and libcongen's
`congen_input.txt` have about 175. NOAA spellings must be aliased to them, for example
`SIGMA1`↔`SIG1`, `THETA1`↔`THE1`, `2MS8`↔`2(MS)8`, `2MN8`↔`2(MN)8`, `2MLNS6`↔`2MNLS6`, `OO2`↔`OQ2-HORN`
and `RP1`→`PSI1` (phase-flipped). Three NOAA names (`M2KS2`, `2SNMK2`, `2KMSN2`) have no counterpart
there yet. HE §1 quantifies the effect.
