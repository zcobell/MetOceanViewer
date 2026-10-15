# Station catalog JSON, format `metoceanviewer-stations` 1.0

Normative spec of the file that lists one provider's stations and what each of them can
serve: the NOAA CO-OPS, USGS and NDBC station layers of MetOceanViewer v5 and the stations
the `metocean-data` CLI selects from (`docs/providers-design.md` §4.6 and §7, plan §6
decisions 6, 23 and 33). It is published so that a third party can read or write a catalog
without reading our code. The machine-readable structure is
[`docs/schemas/station-catalog-v1.schema.json`](schemas/station-catalog-v1.schema.json)
(JSON Schema draft 2020-12). This text is authoritative where the two differ, and §8 lists
the rules a schema cannot express.

MetOceanViewer reads and writes the format in `io` (`mov/io/station_catalog.hpp`); the CLI's
`stations build` writes it (providers-design §9), the app compiles a snapshot of each
provider's catalog in and refreshes it into its app-data directory (§10).

The keywords MUST, SHOULD and MAY are used in the RFC 2119 sense.

Contents: 1 decisions, 2 scope, 3 encoding, 4 top level, 5 stations, 6 provider blocks,
7 canonical form, 8 reading and validation, 9 versioning, 10 loading and refreshing
(informative), 11 tests and compliance, 12 size, 13 worked example.

---

## 1. Decisions at a glance

| # | Choice | Why (short) |
|---|---|---|
| C1 | One file per provider: `provider` names it, and every station is that provider's. | The app loads and refreshes the providers independently; a USGS outage cannot touch the CO-OPS layer. |
| C2 | UTF-8 JSON (RFC 8259). `format` = `"metoceanviewer-stations"`, `version` = `"<major>.<minor>"`, as the station netCDF format (SN §13). A reader refuses another major and reads a newer minor with a warning (§9). | Detects the file kind and allows evolution. |
| C3 | One station per line, sorted by id; every number rounded to the precision of its quantity; one canonical text per catalog (§7). | Two builds of the same lists differ only where a station changed: a snapshot diff is a review of what changed upstream. |
| C4 | Missing means absent. `null` is never used; a default (no air gap, no predictions, an empty set) is written by leaving the member out. | One way to say "missing"; no sentinels (plan §7). |
| C5 | Ids are in the provider's canonical form (`StationId<P>`), and a bad or repeated id is an error. | The source parsers skip bad rows and count them; a catalog therefore never contains one, and one that does was edited by hand or damaged. |
| C6 | Each station carries one block named after its provider (`coops`, `usgs`, `ndbc`) holding what the station can serve (`core::Capabilities<P>`). | The CLI and the app's product picker ask `core::offered()` of exactly these facts, without the network. |
| C7 | Datum heights are metres above the station's MSL, keyed by datum token, MSL itself omitted, as in the harmonics format (HJ H6). | Maps 1:1 onto `core::DatumTable` (MSL pivot, decision 18) without a rebase, so a height reads back exactly. |
| C8 | A reader refuses unknown members and tokens in a file of its own minor or an older one; in a newer minor it ignores and counts them. | A misspelt member (`"lst"` for `"last"`) is an error, not a station that never ended. |
| C9 | `generated` more than one day after the reader's clock is an error. | A cache written by a clock running ahead would otherwise win the startup choice (§10) for as long as the skew lasts. |

---

## 2. Scope

**In scope.** The stations of a provider's map layer and the facts about each that decide what
can be asked of it: CO-OPS water-level, met and air-gap stations with their sensors, tide
predictions and datums; USGS sites with their map series (decision 33: instantaneous `00060`,
`00065`, `62620` or `62619`, ended series included); NDBC stations with met data.

**Not in scope.**
- Data of any kind.
- Every series of a USGS site: the app fetches a site's other series when the site is selected
  (providers-design §6.6), so the catalog can say that a site has a discharge series but not
  that it has a water-temperature one.
- Tide stations of a harmonics file, which has its own format (`docs/harmonics-json.md`).
- Retired CO-OPS stations (decision 6): a catalog builder leaves them out.

---

## 3. Encoding

| Aspect | Rule |
|---|---|
| Syntax | RFC 8259 JSON text whose top-level value is an object. |
| Character encoding | UTF-8. Writers MUST NOT emit a byte order mark; a reader skips a leading one. |
| Member names | Unique within each object; a repeated member is an error (`bad_member`). |
| Literals | `null` MUST NOT appear. `true` appears where §6 says; `false` is accepted and means the default. |
| Text | Any JSON string without U+0000. Control characters are written as escapes. Consumers MUST show names as plain text, never as markup (plan bug 23). |
| Numbers | Decimal JSON numbers, read as binary64 doubles. Parsing does not depend on the process locale. |
| Dates | `YYYY-MM-DD`, a real proleptic Gregorian date in the years 0000–9999. |
| Timestamps | `YYYY-MM-DDThh:mm:ssZ`, UTC, whole seconds, years 0000–9999. |
| Layout | §7. Readers accept any white space and any member order. |
| MIME type | `application/json`. |

---

## 4. Top-level object

| Member | Type | Req. | Meaning |
|---|---|---|---|
| `format` | string | yes | Exactly `"metoceanviewer-stations"`. A file without it is not this format. |
| `version` | string | yes | `"<major>.<minor>"`, each a decimal number without leading zeros. This spec is `1.0` (§9). |
| `provider` | string | yes | `noaa_coops`, `usgs` or `ndbc`: the station netCDF `station_provider` tokens (SN §5) of the three catalog providers. |
| `generated` | timestamp | yes | When the catalog was built. The startup choice and the refresh rules compare it (§10). |
| `sources` | array | yes | The documents the catalog was built from, each `{"url": text, "retrieved": timestamp}`, in the order they were fetched. May be empty (a hand-made catalog). At most 256. |
| `stations` | array | yes | §5. May be empty. |

Any other member is an error in a 1.0 file (C8).

---

## 5. Stations

Every station is an object with these members and the block of its provider (§6):

| Member | Type | Req. | Meaning |
|---|---|---|---|
| `id` | string | yes | The id in its provider's canonical form: CO-OPS `^[0-9]{7}$`; USGS `^[A-Z0-9]+-[A-Za-z0-9]+$` with the agency prefix upper case (`USGS-07374000`); NDBC `^[A-Z0-9]{5}$`, upper case. Text that `StationId<P>::make` would change (surrounding space, a lower-case agency or NDBC id) is not canonical and is an error. |
| `name` | text | yes | The station's name as the provider lists it; may be empty. |
| `lon`, `lat` | number | yes | WGS 84 degrees. `lat` in [−90, 90]; `lon` in [−180, 360], which a reader brings into (−180, 180] (so −180 becomes 180). Writers write (−180, 180]. |
| `first` | date | no | The first day the station reported, when the provider says. |
| `last` | date | no | The last day; absent while the station is ongoing (never v4's 2050, L9). Not before `first`. |
| `coops`, `usgs` or `ndbc` | object | yes | Exactly one, the catalog's provider's (§6). |

---

## 6. Provider blocks

### 6.1 `coops`

What `core::Capabilities<provider::Coops>` holds (providers-design §3.3). Every member is
optional; an absent member is the default.

| Member | Type | Default | Meaning |
|---|---|---|---|
| `observed` | array of tokens | `[]` | The observed water-level series: `six_minute` (`water_level`), `one_minute` (`one_minute_water_level`), `hourly` (`hourly_height`). From the active water-level sensors. |
| `met` | array of tokens | `[]` | The met products of the active sensors: `air_temperature`, `water_temperature`, `wind`, `air_pressure`, `humidity`, `conductivity`, `salinity`, `visibility`. |
| `air_gap` | boolean | `false` | An active `AIR GAP` sensor (the bridges of the air-gap list). |
| `predictions` | token | `none` | From the tide-prediction list: `any_interval` (a harmonic station, type R), `hilo_only` (a subordinate station, type S, which serves high and low waters only), `none` (not in the list). Not the station's `tidal` flag (providers-design §1.1). |
| `datums_offered` | array of tokens | `[]` | The datums the data API serves at the station that the core can name: `MHHW`, `MHW`, `MTL`, `MSL`, `MLW`, `MLLW`, `NAVD88`, `STND`, `IGLD85` (decision 32: never `NGVD29`). STND is served at every station whether listed or not (`core::offered`). |
| `datums` | object | absent | The station's datum table: heights in metres above its MSL, keyed by the core datum tokens (`MHHW`, `MHW`, `MTL`, `MLW`, `MLLW`, `NAVD88`, `NGVD29`, `IGLD85`, `STND`, `HAT`, `LAT`, `DTL`). `MSL` is the pivot: absent, or exactly 0. Absent when the station's `datums.json` failed or listed no MSL (the Great Lakes stations). |

A token array lists each token at most once (at most 64 elements). Tokens are case-sensitive.

### 6.2 `usgs`

| Member | Type | Default | Meaning |
|---|---|---|---|
| `series` | array of objects | `[]` | The site's map series (`core::usgs::SeriesSet`), at most 4,096. |

Each series:

| Member | Type | Req. | Meaning |
|---|---|---|---|
| `id` | string | yes | The time series id: 32 lower-case hexadecimal digits. Unique within the site. |
| `parameter` | string | yes | The parameter code: five digits. |
| `statistic` | string | yes | The statistic code: five digits (`00011` instantaneous). |
| `parameter_name` | string | no | USGS's name of the parameter; absent when empty. |
| `primary` | boolean | no | The site's Primary series of its parameter and statistic; at most one per pair. Absent is `false`. |
| `unit` | string | no | The unit as udunits text (`ft`, `ft3 s-1`): what `core::parse_unit` reads back as the same unit. Not blank. |
| `first`, `last` | date | no | The series' period, as for a station. |

### 6.3 `ndbc`

| Member | Type | Default | Meaning |
|---|---|---|---|
| `owner` | text | `""` | `activestations.xml`'s `owner`. |
| `program` | text | `""` | Its `pgm`. |
| `platform_type` | text | `""` | Its `type` (`buoy`, `fixed`, …). |
| `elevation_m` | number | absent | Its `elev`, metres. |

---

## 7. Canonical form

A writer SHOULD write, and MetOceanViewer's writer does, exactly this text; the same catalog
then always gives the same bytes:

- **Layout.** Line 1 is `{"format":…,"version":…,"provider":…,`; then one line each
  (with one leading space) for `generated`, `sources` (the whole array) and `"stations":[`;
  then one station per line, compact, each but the last followed by `,`; then `]}` and a
  newline. With no station the last line is ` "stations":[]}`.
- **Order.** Members in the order of the tables above. Stations sorted by id (byte order),
  USGS series by id. Token arrays in the order the tables list the tokens (the core
  enumerator order); datum heights in the order `MHHW`, `MHW`, `MTL`, `MLW`, `MLLW`,
  `NAVD88`, `NGVD29`, `IGLD85`, `STND`, `HAT`, `LAT`, `DTL`.
- **Absent defaults.** Empty arrays, `false`, `none`, empty NDBC texts, an empty parameter
  name and an absent table are left out (C4); `name` is always written.
- **Numbers.** Positions rounded to 1e-6 degrees (about 0.1 m), heights and elevations to
  1e-4 m, then printed in fixed notation without trailing zeros or a trailing point
  (`-87.2112`, `30`, `0`); a value that rounds to zero is `0`, never `-0`. Rounding is to
  the nearest decimal, ties to even on the exact binary value. A position is rounded before
  it is normalized, so −179.9999996 is written `180`.
- **Times.** `generated` and `retrieved` floored to the second.
- **Text.** `"` and `\` escaped; U+0000–U+001F and U+007F as `\b`, `\f`, `\n`, `\r`, `\t`
  or `\u00xx` (lower-case hex); everything else as it is.

What a catalog in memory can hold but the format cannot is refused before anything is
written (`format_station_catalog`): a date or time outside the years 0000–9999
(`time_out_of_range`), a series' parameter name that is not UTF-8 (`bad_encoding`), a unit
whose text does not read back as the same unit (`noncanonical_unit`). The writer replaces the
target atomically (`write_file_atomic`): the old file or the whole new one.

---

## 8. Reading and validation

The schema covers structure, types, enums, ranges and the id patterns. A reader enforces this
section as well, including what the schema cannot express.

### 8.1 Errors (the file is rejected)

MetOceanViewer reports each as an `io::Error`. A `ParseError` carries the line and column of
the value at fault (of its object when a member is missing) and the text from there.

| Error | Rule |
|---|---|
| `ParseError empty_input`, `not_json` | Nothing but white space; not JSON (where the parser stopped), including a NUL byte anywhere. |
| `ParseError too_large` | Nesting deeper than 16; more than the reader's text limit; more stations than its element limit allows (a station costs 16 elements, a USGS series 1 more); more than 4,096 series at a station, 256 sources or 64 tokens in an array. |
| `FormatError not_this_format` | `format` absent, not a string, or another text (subject: the text). |
| `FormatError bad_version` | `version` absent or not `<major>.<minor>` (subject: the text). |
| `FormatError unsupported_version` | A major other than 1 (subject: the version). |
| `ParseError bad_member` | A member missing, repeated, or of the wrong JSON type (`null` included); a `provider` other than the three; an id not in its canonical form; text with U+0000; a token the format does not know; a token repeated in its array; a station without its provider's block, with two blocks, or with another provider's; a blank `unit`; in a file of this minor or an older one, any unknown member (C8). |
| `ParseError bad_date` | A date or timestamp not exactly in its form, or not a real date or time of day. |
| `ParseError out_of_range` | A position outside the ranges of §5; a `last` before its `first`; an `MSL` height other than 0. |
| `FormatError inconsistent_metadata` | A USGS station whose series repeat an id or have two Primary series of one parameter and statistic (subject: the station id and the rule). |
| `FormatError duplicate_station_id` | Two stations with one id (subject: the smallest such id). |
| `FormatError future_dated` | `generated` more than one day after the reader's clock (subject: its text). |

### 8.2 Warnings (the file is accepted)

| Warning | When |
|---|---|
| `minor_newer` | The file's minor version is newer than the reader's (subject: the version). |
| `newer_content_ignored` | In such a file, the members and tokens the reader does not know, ignored, one warning per place with its count. The subject is a path: `licence`, `sources[].hash`, `stations[].tz`, `stations[].coops.currents`, and for a token `stations[].coops.met[]=rain_fall` or `stations[].coops.predictions=currents_only`. Past 32 distinct places the rest are counted under `other`. An ignored token leaves its default: an unknown `predictions` is `none`. |

### 8.3 Order of checks

A reader that stops at the first error reports it in this order, so a file always gives the
same error:

1. syntax, nesting, repeated members and the size limits, in document order;
2. `format`, then `version`;
3. the first value the format refuses (§8.1 `bad_member`, `bad_date`, `out_of_range`,
   `inconsistent_metadata`), in document order: the header's members and the stations
   alike, whichever comes first in the text, so a file whose `version` follows its
   stations is still judged by its version first;
4. repeated station ids;
5. `generated` against the clock.

### 8.4 What a reader returns

Positions and heights come back on the grid of §7 (rounded as the writer rounds them), so
writing what was read reproduces it: `parse(format(c)) == c` for every catalog `c` the
reader returns, and `format(parse(t)) == t` for every canonical text `t`. The stations come
back sorted by id however the file orders them.

---

## 9. Versioning and compatibility

| Change | Bump | Examples |
|---|---|---|
| Wording, examples | none | |
| A new optional member, a new token in a set or in `predictions`, a new provider block member | minor | `1.1`: a station time zone; a CO-OPS met product |
| A change of meaning or unit of an existing member, a new required member, a removal, a new provider | major | |

| File | Reader behaviour |
|---|---|
| Major 1, minor ≤ the reader's | Read strictly: unknown members and tokens are errors (C8). |
| Major 1, newer minor | `minor_newer`; unknown members and tokens are ignored and counted (§8.2). |
| Any other major | `unsupported_version`. |

The schema describes 1.0 in full; its `version` pattern accepts every 1.x, so a validator can
tell a wrong major from a newer minor. A newer-minor file usually fails the 1.0 schema's
`additionalProperties`; that is expected. Writers write the newest version they implement.

---

## 10. Loading and refreshing (informative)

How MetOceanViewer uses a catalog (providers-design §7); pure functions in
`mov/io/station_catalog.hpp`, the network and the files in `providers`.

- **Snapshot and cache.** Each provider has a snapshot compiled into the program
  (`data/stations/<provider>.json`) and, after a refresh, a cache in the app-data directory
  (`stations/<provider>.json`). A cache that does not read (unparsable, future-dated, another
  provider's: `catalog_of<P>` gives `wrong_provider`) is reported once and ignored.
- **Startup** (`choose_catalog`): the cache when its `generated` is strictly later than the
  snapshot's, else the snapshot.
- **Staleness** (`is_stale`): a catalog generated more than 30 days before now is refreshed in
  the background, at most once per launch.
- **Refresh** (`accept_refresh`): the refreshed catalog replaces the current one only if it has
  at least half as many stations, rounded up (10 → 5 is accepted, 10 → 4 refused); otherwise it
  is discarded with a warning, so an upstream outage cannot empty the map.
- **The CO-OPS build** (`mov/io/wire/coops_catalog.hpp`): the water-level, met and air-gap
  lists are merged by id. A station's capabilities are the union of its records; its name,
  position and validity are the first list's (water level, then met, then air gap), and a
  later list that differs gives `station_lists_disagree` naming the station. Its predictions
  come from the tide-prediction list; then each station's `datums.json` adds its offered datums
  and its table. A failed `datums.json` leaves the station without a table.

---

## 11. Tests and compliance

- `tests/fixtures/io/station_catalog/` holds the golden files and the cases; `cases.txt` gives
  each file's reader verdict and schema verdict (`valid`, `invalid`, or `any` for a newer
  minor). `coops_lists.json` is the catalog of the recorded CO-OPS lists and `datums.json`
  bodies in `tests/fixtures/io/coops/`; the worked example (§13) is three of its stations.
- The C++ tests (`tests/io/test_station_catalog*.cpp`, `test_coops_catalog.cpp`) check the
  goldens byte for byte, the round trip over generated catalogs, every rule of §8 with its
  line and column, the versioning, the limits and the load and refresh rules, and that the
  schema's enums are the reader's tokens.
- `tools/check_json_schemas.py` validates the schema, gives each fixture its schema
  verdict, and checks that §13 is `worked_example.json` byte for byte. It runs as the ctest
  `json_schemas` (in the dev image; it skips where the pinned `jsonschema` is not
  installed) and in the `format-compliance` CI job.
- `fuzz_station_catalog` (libFuzzer): no crash; every catalog the reader returns is written,
  read back equal and written again to the same bytes.

---

## 12. Size

At about 200 bytes a station line (the CO-OPS and NDBC goldens average 204 and 215), the
CO-OPS catalog (about 600 stations) is about 120 KB and the NDBC one (905) about 200 KB. A
USGS site with one series is about 300 bytes; its catalog's size waits for P3's measured site
count (v4 had 10,503 sites: about 3 MB).

---

## 13. Worked example

This file is complete and valid: three stations of the recorded CO-OPS lists of 2026-10-15 (a
bridge with an air-gap sensor, a harmonic tide station with its datums, and a Great Lakes
station whose datums.json has no MSL). It is the fixture `worked_example.json`, which the
tests read, write back byte for byte, and validate against the schema.

<!-- worked-example:begin -->
```json
{"format":"metoceanviewer-stations","version":"1.0","provider":"noaa_coops",
 "generated":"2026-10-15T16:50:00Z",
 "sources":[{"url":"https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations.json?type=waterlevels&expand=details,sensors","retrieved":"2026-10-15T16:49:00Z"},{"url":"https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations.json?type=airgap&expand=details,sensors","retrieved":"2026-10-15T16:49:00Z"},{"url":"https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations.json?type=tidepredictions","retrieved":"2026-10-15T16:49:00Z"}],
 "stations":[
{"id":"8517986","name":"Verrazano-Narrows Air Gap","lon":-74.04483,"lat":40.606224,"first":"2008-03-20","coops":{"air_gap":true}},
{"id":"8729840","name":"Pensacola","lon":-87.2112,"lat":30.4044,"first":"1923-04-30","coops":{"observed":["six_minute","one_minute","hourly"],"met":["air_temperature","water_temperature","wind","air_pressure"],"predictions":"any_interval","datums_offered":["MHHW","MHW","MTL","MSL","MLW","MLLW","NAVD88","STND"],"datums":{"MHHW":0.195,"MHW":0.186,"MTL":0.003,"MLW":-0.179,"MLLW":-0.188,"NAVD88":-0.09,"STND":-2.757}}},
{"id":"9063020","name":"Buffalo","lon":-78.89047,"lat":42.877445,"first":"1860-04-01","coops":{"observed":["six_minute","hourly"],"met":["air_temperature","water_temperature","wind","air_pressure","humidity"],"datums_offered":["IGLD85"]}}
]}
```

Pensacola's datums are its `datums.json` values (relative to STND) minus MSL's 2.757 m:
MLLW 2.569 − 2.757 = −0.188, and STND itself −2.757.
