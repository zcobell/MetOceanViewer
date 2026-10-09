# Provider API reference (Phase 3)

Status: **verified reference**, researched 2026-10-06 (all "accessed" dates below are
2026-10-06; live requests were made against the real services that day, so the
request/response examples are real). Legacy references are to commit `e5a4e0af`
(v4.5.1). Recorded samples live in `docs/provider-apis/` (every file < 6 KB) and are
named in each section.

Contents: [1 USGS](#1-usgs-water-data-apis--highest-priority) |
[2 NOAA CO-OPS](#2-noaa-co-ops) | [3 NDBC](#3-ndbc) | [4 CRMS](#4-crms) |
[5 XTide](#5-xtide) | [6 GitHub Releases](#6-github-releases-update-check) |
[7 Cross-cutting rules](#7-cross-cutting-notes-for-the-v5-domain-model-and-tests)

---

## 1. USGS Water Data APIs (HIGHEST PRIORITY)

### 1.1 Migration status and timeline (summary)

| Item | Status | Source |
|---|---|---|
| `waterservices.usgs.gov` (all of it: `iv`, `dv`, `site`, `stat`, ...) | **Scheduled for decommission 22 Feb 2027.** Intentional outages on 27 Jan 2027 and 16 Feb 2027; USGS also adds deliberate delays to legacy requests beforehand. | dataRetrieval issue #934 (quotes the USGS notice); USGS blog "WaterServices APIs will be decommissioned early 2027" |
| `nwis.waterdata.usgs.gov/.../uv` (the page v4 uses for "historic") | Listed in **NWISWeb Decommission Campaign 3**, running **Nov 2026 through Feb 2027**; after it "there will no longer be any access to legacy NWISWeb pages". RDB/tabular output is not being carried over. | USGS blog "NWISWeb Decommission Campaign 3" and "Campaign 2" |
| Replacement | **USGS Water Data APIs**, OGC API - Features, `https://api.waterdata.usgs.gov/ogcapi/v1/` (V1 released; version header observed `api-version: 1.9.8`) | `https://api.waterdata.usgs.gov/docs/ogcapi/` |
| Other retirements (not used by v4) | gwlevels API: errors since 2026-06-01. SensorThings API: errors since 2026-02-01. "Observations API" on labs.waterdata.usgs.gov: decommissioned **2026-10-19**. | USGS blog "api-decom-fall-2025", "api-observations-decom" |
| New parameter codes added after June 2026 | Only available via the new services (not NWISWeb) | USGS blog "nwisweb-no-params" |

Observed on 2026-10-06: the legacy `waterservices.usgs.gov/nwis/iv`, `/dv` and
`nwis.waterdata.usgs.gov/usa/nwis/uv` RDB requests that v4 makes **still return HTTP 200**
(`waterservices` in ~0.2 s, `nwis...uv` in ~1.3 s), so v4.5.1 works today but has
roughly four months left. **v5 must target only the new API.** If a v4 hotfix is wanted
(open decision), it is a straight port of the USGS provider to the new API; see 1.9.

Sources (accessed 2026-10-06):
- https://github.com/DOI-USGS/dataRetrieval/issues/934
- https://waterdata.usgs.gov/blog/api-waterservices-decom/
- https://waterdata.usgs.gov/blog/nwisweb-decommission-campaign3/
- https://waterdata.usgs.gov/blog/nwisweb-decommission-campaign2/
- https://waterdata.usgs.gov/blog/api-decom-fall-2025/
- https://waterdata.usgs.gov/blog/tags/decommission (index of all notices)
- https://api.waterdata.usgs.gov/docs/ogcapi/ (landing), `/docs/ogcapi/keys/`, `/docs/ogcapi/migration/`

### 1.2 Base URL, formats, keys

- Base: `https://api.waterdata.usgs.gov/ogcapi/v1`. (A `/ogcapi/v0` path also answers and
  returns links rewritten to `v1`; use `v1`.)
- Landing/conformance: `/ogcapi/v1?f=json`; collection list: `/ogcapi/v1/collections?f=json`;
  per-collection filterable fields: `/collections/{id}/queryables?f=json`; schema:
  `/collections/{id}/schema?f=json`.
- Output (`f=`): `json` (GeoJSON `FeatureCollection`, default), `jsonld`, `html`, `csv`.
  CSV has columns `x,y,<properties...>` (lon, lat first) and is the most compact option.
  Content type for JSON is `application/json; charset=utf-8` with
  `content-crs: <http://www.opengis.net/def/crs/OGC/1.3/CRS84>`; all coordinates are
  lon/lat WGS84 (EPSG:4326, x = longitude).
- **API key**: obtain at `https://api.waterdata.usgs.gov/signup` (api.data.gov style
  key). Send either `?api_key=KEY` or header `X-Api-Key: KEY`. Prefer the header so the
  key never lands in logs/URLs/fixtures. Without a key requests work (verified: 200
  with no key) but are subject to a lower rate limit and 429 once exceeded. An invalid
  key returns `{"error":{"code":"API_KEY_INVALID","message":"..."}}`
  (`usgs_error_invalid_key.json`) - note this error shape differs from the OGC shape below.
- **Rate limits**: the official keys page says only that a key "means you can make more
  requests per hour", and that `X-RateLimit-Limit` / `X-RateLimit-Remaining` response
  headers report it; it publishes no numbers. Secondary (non-official) reports say
  1,000 requests/hour per key and about 30/hour/IP for `DEMO_KEY`; **treat as
  unverified**. No `X-RateLimit-*` headers were visible on unauthenticated responses
  (cached edge responses). Design: read those headers when present, honor 429
  (`Retry-After` if sent, else back off), and let the user store a key in settings.
  Plain `api.data.gov` limits apply, so a shipped-in-binary shared key is not advisable.

### 1.3 Collections (verified list, 2026-10-06)

Data: `continuous`, `latest-continuous`, `daily`, `latest-daily`, `field-measurements`,
`latest-field-measurements`, `channel-measurements`, `peaks`, `edr/daily` (EDR).
Metadata: `monitoring-locations`, `time-series-metadata`, `combined-metadata`,
`field-measurements-metadata`, `time-series-revisions`, `time-series-methods`.
Code tables: `parameter-codes`, `statistic-codes`, `site-types`, `agency-codes`,
`altitude-datums`, `time-zone-codes`, `reliability-codes`, `states`, `counties`,
`countries`, `hydrologic-unit-codes`, `medium-codes`, `methods`, `method-categories`,
`method-citations`, `citations`, `combined-method-citations`, `aquifer-*`,
`national-aquifer-codes`, `coordinate-*-codes`, `topographic-codes`.

Legacy to new mapping (from the official migration guide):

| Legacy | New |
|---|---|
| `/iv` | `continuous` (history) / `latest-continuous` (latest value) |
| `/dv` | `daily` / `latest-daily` |
| `/site` | `monitoring-locations` / `time-series-metadata` / `combined-metadata` |
| `/gwlevels`, `/measurements` | `field-measurements`, `channel-measurements` |
| `/peak` | `peaks` |

### 1.4 Identifiers, parameters, statistics

- **Monitoring location id** is `{agency}-{number}`, e.g. `USGS-01646500`. Not all
  agencies are USGS (`MD007-385839077055701` appeared in a bbox query). The v4 CSV has
  `SiteNumber` (`01646500`) and `SiteNumber2` (`USGS01646500`, no hyphen); the v5
  station table must store the hyphenated id. Station ids may be 8 to 15 digits.
- **Parameter codes** are 5-digit strings (`"00060"` discharge ft3/s, `"00065"` gage
  height ft, `"62620"` ocean/estuary elevation NAVD88 ft, `"63680"` turbidity FNU);
  look up with `/collections/parameter-codes/items?id=00060,00065&properties=id,parameter_name,unit_of_measure`.
  Keep them as strings (leading zeros) in a strong `ParameterCode` type.
- **Statistic ids** are 5-digit strings (`00001` max, `00002` min, `00003` mean,
  `00011` instantaneous). `continuous` data is statistic `00011`; `daily` is usually
  `00003` but daily min/max exist, so a daily request must select a statistic or a
  `time_series_id`.
- `time_series_id` is a 32-hex string that ties items to a `time-series-metadata` row
  (`id`). A (location, parameter, statistic) can have several series (sub-locations,
  `primary` = `"Primary"` or not). v4 collapsed these implicitly; v5 should pick
  `primary` series by default and expose the rest.

### 1.5 Time series data: `continuous` (replaces iv and archived uv) and `daily` (replaces dv)

Request (instantaneous, 1 hour, no geometry):

```
GET https://api.waterdata.usgs.gov/ogcapi/v1/collections/continuous/items
    ?monitoring_location_id=USGS-01646500
    &parameter_code=00060
    &datetime=2026-10-01T00:00:00Z/2026-10-01T00:30:00Z
    &skipGeometry=true
    &properties=time,value,unit_of_measure,approval_status,qualifier
    &limit=3&f=json
X-Api-Key: <key>
```

Abbreviated response (`docs/provider-apis/usgs_continuous_slim_page1.json`, full-property
variant in `usgs_continuous_items.json`):

```json
{"type":"FeatureCollection",
 "features":[{"type":"Feature","id":"1372955a-...",
   "properties":{"time":"2026-10-01T00:00:00+00:00","value":"2700",
                 "approval_status":"Provisional","qualifier":null},
   "geometry":null}, ...],
 "numberReturned":3,
 "links":[{"rel":"next","href":"https://api.waterdata.usgs.gov/ogcapi/v1/collections/continuous/items?cursor=NjZhNGUz...&monitoring_location_id=USGS-01646500&...&limit=3&f=json"},
          {"rel":"self",...},{"rel":"alternate",...},{"rel":"collection",...}],
 "timeStamp":"..."}
```

Full-property items add `time_series_id`, `monitoring_location_id`, `parameter_code`,
`statistic_id` (`"00011"`), `unit_of_measure` (`"ft^3/s"`), `last_modified`,
`method_category`, plus `geometry` Point `[lon, lat]` of the site (repeated on every
item; wasteful, so use `skipGeometry=true`).

Daily:

```
GET .../collections/daily/items?monitoring_location_id=USGS-01646500&parameter_code=00060&statistic_id=00003&datetime=2026-09-28/2026-09-30&f=json
```
Item properties: `time` is a **date** (`"2026-09-28"`, no time/zone; the site's
local day), `value` `"3800"`, `unit_of_measure` `"ft^3/s"`, `approval_status`
`"Provisional"` (older data `"Approved"`), `qualifier` null, `statistic_id` `"00003"`.
`usgs_daily_items.json`. **Observed: daily items came back newest first** while
`continuous` came back oldest first; always sort after merging pages.

Query semantics (all verified):

- `datetime`: ISO 8601 instant, closed interval `a/b`, half-open `a/..` or `../b`, or a
  duration (`P7D` forms per docs). Offsets other than `Z` are accepted. Daily accepts
  plain dates.
- Any response property is a filter (`monitoring_location_id`, `parameter_code`,
  `statistic_id`, `time_series_id`, `approval_status`, ...), comma lists allowed
  (`id=62620,00065`). Unknown properties return a 400 (`usgs_error_unknown_property.json`).
- `properties=a,b,c` selects returned fields; `skipGeometry=true` drops geometry
  (`"geometry": null`); `bbox=minLon,minLat,maxLon,maxLat`; `crs` selectable (EPSG:4326
  default); `sortby` supported (OGC); `limit` default is server-chosen.
- **Max request size**: `limit` max is **50000** (100000 returns
  `{"code":"InvalidParameterValue","description":"Limit of 50000 exceeded"}`); **the time
  envelope for `continuous` must be <= 1100 days** (error
  `"The requested time envelope is too large, the query must be limited to 1100 day(s) or less for this collection."`,
  `usgs_error_time_envelope.json`). The migration guide states "three years". So v5 must
  chunk long requests into <= ~1000-day windows (and `daily` has no tested cap; probe
  before assuming).
- Real-world size: 1 month of 5-minute discharge = 8,640 items in one page; 50,000
  items of full GeoJSON with geometry measured **35.9 MB**. Use `properties=` +
  `skipGeometry=true` or `f=csv`, and stream-parse.
- **Pagination**: cursor based. Follow `links[rel=next].href` verbatim until no `next`
  link exists (that is the documented end condition). Do not construct cursors. Keep
  `limit` constant between pages. `numberMatched` is generally absent for time series
  (present on small code tables), so do not rely on it for progress.
- **Missing data**: represented by `null` values (migration guide; replaces the legacy
  `noDataValue`). `value` is a **string** ("transmitted as strings ... to preserve
  precision"), so parse with `from_chars` and treat null/empty as "absent sample", not
  NaN or -999999.
- **Qualifier / approval**: `approval_status` is a word (`Provisional`, `Approved`),
  replacing legacy `P`/`A`. `qualifier` is a nullable string (e.g. ice/equipment
  codes); model as an enum with an `Other(string)` arm. Provisional data is
  revisable; surface it in the UI and in exports.
- **Time zones**: `continuous` times are always UTC (`+00:00`). `daily` is a bare local
  date. Site tz metadata: `monitoring-locations` has `time_zone_abbreviation` (`EST`)
  and `uses_daylight_savings` (`Y`/`N`); `/collections/time-zone-codes` lists codes. The
  v4 path (rdb `tz_cd` column to the broken `Timezone::offsetFromUtc` table) disappears
  entirely. Verified across the 2020 DST transition that the UTC timestamps stay on the
  5-minute grid.
- **Units**: `unit_of_measure` is a per-item free string (`ft^3/s`, `ft`, `_FNU`,
  `pH Units`). **Units are not selectable**; the API returns the native USGS unit
  (feet, ft3/s, deg C...). Metric conversion is the client's job and needs a
  `Unit` mapping table keyed by this string (plus the parameter-codes table for the
  canonical `unit_of_measure`).
- **History**: `continuous` reaches into the archive (verified 1990-10-01 data for
  USGS-01646500, "Approved"); `time-series-metadata` `begin`/`end` give the covered span
  (`begin` 1972-06-09 for the instantaneous series, `1930-03-01` for daily mean).
  One endpoint therefore covers v4's "instantaneous", "daily" **and** "historic".

### 1.6 Station list for the map layer

Two usable shapes:

1. **All locations** (`monitoring-locations`): fields include `id`, `monitoring_location_name`,
   `site_type_code` (`ST` stream, `ST-TS` tidal stream, `ES` estuary, `OC` ocean, `LK`,
   `GW` well, ... full list from `/collections/site-types`), `state_name`, `altitude`,
   `vertical_datum` (`NAVD88`), `drainage_area`, `time_zone_abbreviation`,
   `uses_daylight_savings`, `hydrologic_unit_code`, geometry. Example:
   `.../monitoring-locations/items?site_type_code=ST&bbox=-92,28,-88,31&limit=50000&f=csv&properties=id,monitoring_location_name,site_type_code`
   returned 2,361 rows in 226 KB. Many locations are groundwater wells or have no
   continuous data; `monitoring-locations` has no "has real-time data" flag.
2. **Locations that actually have a given series** (`time-series-metadata`, or
   `combined-metadata` which joins location and series fields):
   `.../time-series-metadata/items?parameter_code=62620&limit=50000&skipGeometry=true&f=csv&properties=monitoring_location_id,begin,end,computation_identifier`.
   Fields: `monitoring_location_id`, `parameter_code`, `parameter_name`,
   `statistic_id`, `computation_identifier` (`Instantaneous`, `Mean`, ...),
   `computation_period_identifier`, `begin`, `end` (and `*_utc`), `primary`,
   `sublocation_identifier`, `unit_of_measure`, `web_description`
   (`Discontinued`), `parent_time_series_id`, `data_gap_interval`.
   Join with (1) for coordinates. `end` close to now means "active"; this replaces the
   v4 hard-coded 10,503-row `usgs_stations.csv` (which had no validity dates).

Recommendation: the `tools/` station-list builder (plan §2.7) should run both queries
(paged by `limit=50000`, key in header), keep only series v4 offers (discharge `00060`,
gage height `00065`, tidal/estuary elevation `62620`/`62619`..., plus whatever the
owner decides), and emit one normalized asset with `ValidRange{start, optional end}`
from `begin`/`end`. An optional in-app refresh can call the same queries. One caveat:
`properties=` rejects some field names on `time-series-metadata` (400 "unknown
properties specified" for a `begin_utc` request); use `/queryables` to discover valid names.

### 1.7 Mapping v4 USGS capabilities to the new API

| v4 mode (`usgswaterdata.cpp:62-69`) | v4 URL | v5 |
|---|---|---|
| 1 "instant" (<~120 days) | `waterservices.usgs.gov/nwis/iv/?sites=ID&startDT&endDT&format=rdb` | `collections/continuous/items`, `datetime` interval, parameter(s) from `time-series-metadata` |
| 2 "daily" | `waterservices.usgs.gov/nwis/dv/...` | `collections/daily/items` + `statistic_id` |
| 0 "historic" (archive) | `nwis.waterdata.usgs.gov/usa/nwis/uv?format=rdb&site_no&begin_date&end_date` | same `continuous` endpoint, chunked at <= 1100 days (no separate archive server any more) |

v4 behaviors to preserve: one product per parameter returned from a single request
(the legacy RDB returned all parameters for a site in one table; the new API needs one
`parameter_code` per query or a comma list, so first fetch `time-series-metadata` for
the station to populate the product picker); the series label comes from the parameter
description (`parameter_name`).

Legacy bugs not to copy: the end date was extended by one day silently
(`endDate().addDays(1)`); a series with < 3 points was dropped without a message; header
parsing split on double spaces and compared descriptions; time zone abbreviation via an
ambiguous fixed-offset table; `MetOceanData` indexes `station(i)` after skips (plan bug
24); only monotonically increasing timestamps were kept (silently discarding
corrections/out-of-order rows).

### 1.8 Fields that must not become sentinels

`value` (nullable), `qualifier` (nullable), `approval_status` (enum incl. unknown),
`method_category` (null), `monitoring-locations.altitude`/`drainage_area`/`basin_code`
(null), `time-series-metadata.end` (null or far past = ended; use `optional`),
`daily` `time` (a date, not a timestamp: use `sys_days`/`year_month_day`, not a
midnight instant), `unit_of_measure` (null seen on field-measurement metadata).

### 1.9 Fixtures to record

`usgs_continuous_items.json`, `usgs_continuous_slim_page1.json` (add a second page via
the `next` link), `usgs_daily_items.json`, `usgs_monitoring_location.json` and the four
error bodies are captured. Still to record (large or situational; describe and generate
in the capture script, do not commit big ones): a CSV page of `time-series-metadata`
(`f=csv`), a response containing `null` values and a non-null `qualifier`, a 3-year
boundary request (expects the 1100-day error), an empty result (`numberReturned: 0`), a
429 body/headers, and a tidal site (`ST-TS`, e.g. `USGS-02492000`).
Live nightly test: one `continuous` request for a stable site, asserting only schema
(never values).

---

## 2. NOAA CO-OPS

Docs (accessed 2026-10-06): data API `https://api.tidesandcurrents.noaa.gov/api/prod/`;
metadata API `https://api.tidesandcurrents.noaa.gov/mdapi/prod/`.

### 2.1 datagetter

Base `https://api.tidesandcurrents.noaa.gov/api/prod/datagetter` (unchanged from v4).
Parameters: `product`, `station`, `begin_date`/`end_date` (formats `yyyyMMdd`,
`yyyyMMdd HH:mm`, `MM/dd/yyyy`, `MM/dd/yyyy HH:mm`; v4 uses `yyyyMMdd hh:mm` URL-encoded),
`date`/`range`, `datum`, `time_zone` (`gmt`|`lst`|`lst_ldt`), `units`
(`metric`|`english`), `interval`, `format` (`json`|`xml`|`csv`), `application`
(recommended identifier, send `MetOceanViewer`; v4 did).

Products (docs): `water_level`, `hourly_height`, `high_low`, `daily_mean`, `monthly_mean`,
`one_minute_water_level`, `predictions`, `air_gap`, `air_temperature`,
`water_temperature`, `wind`, `air_pressure`, `conductivity`, `visibility`, `humidity`,
`salinity`, `currents`, `currents_predictions`, `ofs_water_level`, (`currents_header`).

**Maximum range per request** (official, and `water_level` 6-min verified: a 2-month
request returned a "31 days" error):

| Product / interval | Max span |
|---|---|
| 1-minute data | 4 days |
| 6-minute (water_level, met products, predictions at 6 min) | 31 days (docs say "1 month") |
| hourly (`hourly_height`, predictions `interval=h`) | 1 year |
| `high_low` | 1 year |
| `daily_mean` | 10 years |
| `monthly_mean` | 200 years |

Chunking rule for v5: split `[begin, end]` into windows of at most 31 days (v4 used 30,
which is safe but the next window began at the previous end, producing a duplicate
sample v4 then skipped with `start=1`; v5 should use half-open windows
`[a, a+31d)` and de-duplicate by timestamp). Hourly and daily products can use larger
windows (1 y / 10 y). Add a short delay between chunk requests: the docs say CO-OPS
throttles heavy single-client use; no numeric limit is published.

**Datum**: accepted `MHHW, MHW, MTL, MSL, MLW, MLLW, NAVD, STND` (also `CRD`, `IGLD`,
`LWD` for lakes/rivers per docs). **`datum` is mandatory for `water_level`,
`hourly_height` and `predictions`** (verified: omission returns
`Wrong Datum: Datum cannot be null or empty`; an invalid value returns "The supported
Datum values are: ..."); it must be omitted/ignored for met products (v4 omits it when
`datum == "Stnd"`; sending `datum` on `wind` was accepted in testing). Station datums
differ per station: `GET .../mdapi/prod/webapi/stations/{id}/datums.json?units=metric`
returns `{"epoch","units","OrthometricDatum","datums":[{"name":"MSL","description":"Mean Sea Level","value":2.757},...]}`
(values are relative to STND; `docs/provider-apis/coops_datums.json`). Some stations
lack some datums and return an error for them.

**Interval**: v4 sent an empty `interval=`. Predictions default to 6-minute; valid values
`h, 1, 5, 6, 10, 15, 30, 60, hilo`; met products `h` for hourly else the 6-minute default.

**Time zone**: v4 always requests `time_zone=GMT`; keep that. Timestamps are
`"yyyy-MM-dd HH:mm"` strings without a zone marker; `lst`/`lst_ldt` are station-local
and must not be used internally.

**Units** (`units=`): metric = meters, degC, m/s, hPa, km (visibility); english = feet,
degF, knots, mb, nautical miles. The `wind` speed/gust unit is m/s (metric) or knots.

### 2.2 Response shapes (all recorded)

`water_level` (`coops_water_level.json`):
```json
{"metadata":{"id":"8729840","name":"Pensacola","lat":"30.4044","lon":"-87.2112"},
 "data":[{"t":"2026-10-01 00:00","v":"0.409","s":"0.004","f":"0,0,0,0","q":"p"}, ...]}
```
`v` value (string), `s` sigma, `f` four quality flags (O/I/F/R/L-style), `q` quality
`p` preliminary or `v` verified. `wind` (`coops_wind.json`): `"s"` speed, `"d"` direction
degrees, `"dr"` compass text, `"g"` gust, `"f"` 2 flags. Air/water temperature,
air_pressure: `{"t","v","f":"0,0,0"}`. **`predictions` has a different envelope and no
metadata**: `{"predictions":[{"t":"...","v":"0.11"}, ...]}` (v4 handles via
`data`/`predictions` keys; model as two parsers). `humidity` and many met products are
only present at stations that carry the sensor. CSV (`coops_water_level.csv`) header is
`Date Time, Water Level, Sigma, O or I (for verified), F, R, L, Quality` (note the
inconsistent header spacing; v5 should use JSON only).

**Errors are HTTP 200 with a JSON error body**, `{"error":{"message":"..."}}`
(XML: `<error>text</error>`), with a leading space in the message and trailing blank
lines. Recorded: range exceeded (`coops_error_range.json`), bad station
(`"The station is not a valid station or there is system error."`), no data
(`"No data was found. This product may not be offered at this station at the requested
time."`), bad datum. So error detection must key off the presence of `error`, never
off HTTP status, and "no data" must be a distinct `NetError`/`NoData` alternative
(and not a failure when stitching chunks).

Missing values inside `data`: records may have empty `"v":""` (the v4 parser skipped
unparsable values via `toDouble(&ok)`); keep that as "absent sample".

### 2.3 Metadata API (station list)

- All stations by capability: `GET .../mdapi/prod/webapi/stations.json?type=waterlevels`
  (302 stations on 2026-10-06), `type=met` (315), plus `tidepredictions`, `currents`,
  `harcon`, `benchmarks`. Station objects: `id`, `name`, `lat`, `lng`, `state`,
  `timezone`, `timezonecorr`, `tidal`, `greatlakes`, `shefcode`, and `self` links to
  `details`, `sensors`, `datums`, `harcon`, `floodlevels`, `benchmarks`, `products`
  (`coops_stations_waterlevels_first2.json`; includes `?units=` option).
- Per-station: `stations/{id}.json`, `stations/{id}/datums.json`,
  `stations/{id}/sensors.json`, `stations/{id}/products.json`, `.../harcon.json`.
- Gap: v4's embedded `noaa_stations.csv` has 2,663 stations including historical ones with
  start/end dates and pre-computed datum offsets (`-999999` sentinel = unavailable,
  `present` = 2050). `type=waterlevels` returns only the **active** set. The station-list
  builder must decide whether historic stations (whose data is still retrievable) are
  kept (see open questions). Datum offsets should come from `datums.json`, stored as
  `std::optional<Length>` per datum.

### 2.4 v4 parity: products

v4 `Noaa::getNoaaProductId` (`noaa.cpp:329`): 0 `water_level` observed vs `predictions`
(two requests), 1 `water_level`, 2 `hourly_height`, 3 `predictions`, 4 `air_temperature`,
5 `water_temperature`, 6 `wind` speed, 7 `humidity`, 8 `air_pressure`, 9 `wind`
direction, 10 `wind` gusts. Datum applies to indices 0-3 only (`Stnd` otherwise). v4
also optionally uses VDatum: request `MSL` then apply a station datum shift client
side. Units: metric/english label tables at `noaa.cpp:150-170`.

Legacy bugs to not copy: product/unit selection by combo index; `"wind:speed"` split on
`:`; `product2` for predictions requested with the same `datum` handling; errors from a
later chunk overwrite earlier ones; failing chunk silently ignored (only the final
`numSnaps > 3` check); `qDebug()` of the URL; the redirect handling workaround.

### 2.5 Fields that must not become sentinels

`v` (`optional<double>` / absent sample), `s`, `g`, `d` (wind direction missing),
`f`/`q` flags (enum + raw), datum offsets (`optional`), station `end` date
(`present` as `nullopt`, not 2050), `timezone`.

### 2.6 Fixtures

Captured: water_level, wind, predictions, air_temperature, datums, stations (first 2),
four error bodies, one CSV. Still to capture: a multi-chunk pair (two adjacent windows
with the overlapping boundary sample), `hourly_height`, `high_low`, a record with empty
`v`, `daily_mean`. Station `8729840` (Pensacola) has water level, wind, air/water temp,
air pressure; humidity is **not** available there.

---

## 3. NDBC

Docs (accessed 2026-10-06): https://www.ndbc.noaa.gov/faq/measdes.shtml (units/missing),
file indices under https://www.ndbc.noaa.gov/data/. No API key, no published rate limit;
the site has no machine-readable terms; keep requests sequential and cache.

### 3.1 Endpoints

| Use | URL | Notes |
|---|---|---|
| Historical standard met, one file per station-year | `https://www.ndbc.noaa.gov/data/historical/stdmet/{id}h{YYYY}.txt.gz` (e.g. `42040h2019.txt.gz`) | gzip; **404 for years that do not exist**; the current year's file does **not** exist (`42019h2026` is 404; the newest is last complete year) |
| Same via CGI wrapper (what v4 uses) | `https://www.ndbc.noaa.gov/view_text_file.php?filename=42040h2019.txt.gz&dir=data/historical/stdmet/` | still works (200 `text/plain`, decompressed). **For a missing year it returns HTTP 200 with body `Unable to access data file`**, which v4 hid with `d.length() > 4`. Prefer the direct `.gz` file and treat 404 as "year absent" |
| Real-time, last 45 days, newest first | `https://www.ndbc.noaa.gov/data/realtime2/{ID}.txt` | station ids are upper-case here (e.g. `32ST0.txt`); 404 if absent (`42040` and `42019` returned 404 today; not all stations have one). Files `.ocean`, `.srad`, `.spec` etc. are the other feeds |
| Monthly (current-year gap fill) | `https://www.ndbc.noaa.gov/data/stdmet/{Mon}/{id}.txt.gz` directories exist (`.../stdmet/Sep/`) | the index lists per-station monthly files; the file name pattern in the listing must be checked at implementation time (a direct `Sep/42019.txt.gz` request was 404) |
| Station metadata | `https://www.ndbc.noaa.gov/activestations.xml` | `<stations created=... count="1354"><station id lat lon elev name owner pgm type met currents waterquality dart/>` (`ndbc_activestations_head.xml`); active stations only, includes `met="y"` flag (905 stations) |

Gap in v4: because v4 only builds `h{year}` URLs, the **current year (and the last
partial months) are unobtainable**. v5 should fall back: historical files for complete
years, then monthly or `realtime2` (45 days) for the rest.

### 3.2 File format

Whitespace-delimited text, **UTC only** ("Both Realtime and Historical files show times
in UTC only"), metric units. Observed header variants:

```
2005 (1 header line):  YYYY MM DD hh mm  WD  WSPD GST  WVHT  DPD   APD  MWD  BAR    ATMP  WTMP  DEWP  VIS  TIDE
   (older years omit the minute column: "YY MM DD hh WD WSPD ...", 4-digit year, hourly)
2019 (2 header lines): #YY  MM DD hh mm WDIR WSPD GST  WVHT   DPD   APD MWD   PRES  ATMP  WTMP  DEWP  VIS  TIDE
                       #yr  mo dy hr mn degT m/s  m/s     m   sec   sec degT   hPa  degC  degC  degC   mi    ft
realtime2:             #YY  MM DD hh mm WDIR WSPD GST  WVHT   DPD   APD MWD   PRES  ATMP  WTMP  DEWP  VIS PTDY  TIDE
                       #yr  mo dy hr mn degT m/s  m/s     m   sec   sec degT   hPa  degC  degC  degC  nmi  hPa    ft
```
(`ndbc_hist_2005_head.txt`, `ndbc_hist_2019_head.txt`, `ndbc_realtime2_head.txt`.)

Observations: the header names differ by era (`WD` vs `WDIR`, `BAR` vs `PRES`); the
`mm` column appears from about 2005 on (v4 detected it with `d[4] == "mm"`, which is
fragile; v5 must key on the header tokens); `PTDY` exists only in realtime; **visibility
units differ**: statute miles in historical, nautical miles in realtime. Column set can
differ between years of the same station, so parse each file by its own header and
build the series union by column name (v4 used the first file's header for all).

**Missing values**: historical = runs of 9s whose width depends on the column:
`999` (WDIR, MWD), `99.0` (WSPD, GST, VIS), `99.00` (WVHT, DPD, APD, TIDE),
`999.0` (ATMP, WTMP, DEWP), `9999.0` (PRES/BAR, per NDBC docs "variable numbers of
9's"); realtime = literal `MM`. v4 matched four string literals (`999`, `99.0`, `99.00`,
`999.0`) and so would pass `9999.0` pressure through as a real value and would
mis-handle `MM` (`toDouble` fails, but the failure is not checked: `v[k].toDouble()` with
no `ok`, yielding 0.0). v5: per-column sentinel table, applied at the parser boundary
only, producing `optional<double>`. Note that real data can legitimately equal 99.0 (a
gust, for example), so the column-specific width matters.

### 3.3 Units

m/s, degT, m, s, hPa, degC; TIDE in ft (the only non-metric one); VIS mi or nmi
(see above). v4 labeled names via the `c_dataTypes/c_dataNames` table
(`WDIR`/`WD`, `WSPD`, `GST`, `WVHT`, `DPD`, `APD`, `MWD`, `BAR`, `PRES`, `ATMP`, `WTMP`,
`DEWP`, `VIS`, `TIDE`); v5 should additionally support `PTDY` for realtime.

### 3.4 Fields that must not become sentinels

Every measurement column (`optional<double>`); wave and wind direction (`optional`);
`TIDE`; station `elev`; "year absent" is a normal not-found result, not an error.

### 3.5 Fixtures

Captured: 2005 and 2019 headers plus first rows, realtime head, `activestations.xml` head.
Add: a full tiny historical year trimmed to ~50 lines containing missing markers in
every column, a realtime file with `MM`s, a year with the old 4-column time layout
(pre-2005), and the `Unable to access data file` body (to prove the wrapper is not used).

---

## 4. CRMS (Coastwide Reference Monitoring System)

> **Superseded 2026-10-06:** the bulk-ZIP/netCDF recommendations in 4.2-4.4 below are
> obsolete. A per-station, per-time-range path does exist; see the final section
> "CRMS: API investigation (2026-10-06)". Sections 4.1-4.3 remain accurate as a description
> of v4 and of the CSV column layout.

### 4.1 What v4 does

- The app reads a **prebuilt netCDF**: `https://metoceanviewer.s3.amazonaws.com/crms.nc`
  (referenced in `crmsdialog.cpp:36`; the user must download it manually into
  `Generic::crmsDataFile()`; `crmsdata.cpp` reads `stationLength_%06d`,
  `data_station_%06d`, `time_station_%06d`, dim `numParam`).
- `ProcessCrmsDatabase` (`crmsdatabase.cpp`) converts the CIMS **Continuous Hydrographic
  (Hourly)** CSV into that netCDF. It keeps every column except `Station ID`, `Date
  (mm/dd/yyyy)`, `Time (hh:mm:ss)`, `Time Zone`, `Sensor Environment`, `Geoid`,
  `Organization Name`, `Comments`, `Latitude`, `Longitude` as a "parameter", stores
  `float` data with a fill value for blanks, and converts times using
  `CST -> +21600 s`, `CDT -> +18000 s` (UTC offsets of -6 h / -5 h).
- Stations come from the embedded `crms_stations.csv` (23,029 rows `lon,lat,station-id`,
  e.g. `AT02-01`, i.e. the 4-character prefixed ids, not the `CRMS0002-H01` form used in
  the CSV).

### 4.2 **Finding: the S3 file is stale and enormous**

`curl -I https://metoceanviewer.s3.amazonaws.com/crms.nc` on 2026-10-06 returns **HTTP 200,
`Content-Length: 1,269,467,846` (1.27 GB), `Last-Modified: Mon, 20 Sep 2021`**.
The dialog text claims it is "updated every Monday morning"; it has not been rebuilt
since 2021. Anything v5 builds on top of this file serves five-year-old data.

### 4.3 Current upstream source (CIMS, Louisiana CPRA)

- Portal: `https://cims.coastal.louisiana.gov/` (alias `cims.coastal.la.gov`; the CRMS
  site `lacoast.gov/crms/` points raw-data users here). Data download is
  interactive (ASP.NET forms emailing a CSV: `DataDownload/DataDownload.aspx?type=hydro_hourly`),
  **but there is a stable bulk path with no login**: `FullTableExports.aspx` links to
  plain files, regenerated **every Sunday morning** (verified `Last-Modified` 2026-10-04):
  - `https://cims.coastal.louisiana.gov/RequestedDownloads/ZippedFiles/Full_Continuous_Hydrographic.zip`
    (1.69 GB, all CIMS hourly data)
  - `https://cims.coastal.louisiana.gov/RequestedDownloads/ZippedFiles/CRMS_Continuous_Hydrographic.zip`
    (1.28 GB, CRMS sites only - what ProcessCrmsDatabase expects)
  - `.../Full_Discrete_Hydrographic.zip`, `CRMS_Discrete_Hydrographic.zip` (monthly/discrete)
  - Data are CSV in a ZIP. The page states a file naming convention document and the Data
    Dictionary exist on CIMS.
- Header of the hourly CSV (`crms_hourly_head.csv`), verified:
  `Station ID,Date (mm/dd/yyyy),Time (hh:mm:ss),Time Zone,Sensor Environment,Raw Water Temperature (°C),Adjusted Water Temperature (°C),Raw Specific Conductance (uS/cm),Adjusted ...,Raw Salinity (ppt),Adjusted Salinity (ppt),Raw Water Level (ft),Adjusted Water Level (ft),Raw Water Elevation to Marsh (ft),... Elevation to Datum (ft),Raw Marsh Mat Elevation (ft),...,Geoid,Raw Battery (V),...,Raw Wind Speed (mph),...,Raw Wind Direction (degrees),...,Raw Velocity (ft/sec),...,Raw Precipitation (tips/hour),Adjusted Precipitation (inches),Raw Air Pressure (mm of Hg),...,Raw Total Chlorophyll (micrograms/L),...,Organization Name,Comments,Latitude,Longitude`
  - Sample row: `CRMS0002-H01,9/29/2007,09:00:00,CST,Surface Water,26.29,26.29,17718.10,...,GEOID99,6.20,...,COASTAL ESTUARY SERVICES LLC,,30.10031149,-89.79156699`
  - **Dates are `m/d/yyyy` (not zero padded), time `HH:mm:ss`, `Time Zone` column is `CST`/`CDT`**
    (v4 assumption; whether CRMS stays CST year-round needs verification against the
    Data Dictionary before the converter trusts `CDT` rows). Encoding is **not UTF-8**
    (the degree sign is a Latin-1 byte, `0xB0`); decode as Windows-1252/ISO-8859-1.
  - Per the CIMS page, parameters: water temperature, specific conductance, salinity,
    water level (ft), water elevation to marsh/datum (**ft NAVD88**, GEOID99/GEOID12A
    in the `Geoid` column, i.e. the geoid model changes between periods), plus rare
    wind, velocity, rain, pressure, chlorophyll. Latitude/longitude are WGS84.
  - Hourly records are ~1 per hour; the CIMS page example: ~8,760 per year per station.
    The 1.28 GB zip hints at ~hundred-million-row scale; the converter must stream.
- *(Outdated, see the investigation section.)* No documented REST/JSON API exists, but the
  CIMS download form is scriptable and answers synchronously (only the bulk zips were
  considered when this was first written). Coverage in the CIMS page for one project line: 1992-01-01 to
  2026-10-06.

### 4.4 Recommendations (OBSOLETE - superseded by the investigation section)

- `tools/crms-to-netcdf` should download `CRMS_Continuous_Hydrographic.zip` (or take a
  path), stream-read the first CSV inside, and write the new single station-netCDF
  dialect; **run it in CI weekly and publish to a GitHub Release asset or object
  store**, so the staleness cannot recur. (Open question: who hosts the artifact.)
- In-app, never read 1.3 GB at once; keep the netCDF + per-station lazy reads (as v4).
  Also consider offering a "download latest CRMS file" action that checks `Last-Modified`
  or `ETag`.
- Preserve both Raw and Adjusted columns (v4 treated all numeric columns as flat
  parameters named by header; v5 should model `{parameter, processing: Raw|Adjusted}`).
- Legacy bugs not to copy: plan §1.2 items 16 and 26 (unchecked `nc_open`, blank lines,
  200-byte buffers, last station dropped without trailing newline, `max_element` on an
  empty vector); fill value in float with `==` compare; station name mapped by display name.

### 4.5 Fields that must not become sentinels

Every measurement (blank cell = absent, not `fillValue`), `Geoid` (enum with Unknown),
`Time Zone` (an explicit alternative, not an offset hard-code), `Comments`,
`Sensor Environment`, station `end` date.

### 4.6 Fixtures

Captured: header + 3 data rows (`crms_hourly_head.csv`, 1.8 KB, **Latin-1 bytes preserved,
do not re-encode when committing**). Add: a hand-made row with blank cells, a `CDT` row,
a short file with no trailing newline, a file with CRLF, and a row spanning the DST change.

---

## 5. XTide

> **Superseded (2026-10-08, plan decision 31).** v5 drops XTide. Tide predictions come from
> our own engine and the `metoceanviewer-harmonics` JSON format: see `docs/harmonics-json.md`
> and `docs/harmonics-engine.md`. This section is kept as a record of the v4 dependency.

### 5.1 What is vendored

`thirdparty/xtide-2.15.1/` (2016-02-23) and `thirdparty/libtcd-2.2.7/`; harmonics file
`libraries/libmetocean/harmonics.tcd` (identical to `thirdparty/xtide-2.15.1/harmonics.tcd`,
1.8 MB). Its embedded metadata: libtcd v2.2.7, `[LAST MODIFIED] = 2015-12-27`
(so the app ships tide constants that are **nearly 11 years old**). v4 uses
`xtidedata.cpp`/`tideprediction.cpp` and the embedded `xtide_stations.csv` (4,166
stations with `MLLW..NAVD` offsets, `-999999` sentinel for unknown).

### 5.2 Latest upstream (https://flaterco.com/xtide/, files at https://flaterco.com/files/xtide/; accessed 2026-10-06)

- **XTide 2.16**: `xtide-2.16.tar.xz` dated **2026-01-21** (DOS build 2026-02-10).
  Earlier: 2.15.6 (2025-06-16), 2.15.5 (2022-05-11), 2.15.4 (2022-02-13), 2.15.3
  (2020-06-29), 2.15.2 (2019-02-24), 2.15.1 (2016-02-23).
- **libtcd 2.2.7-r3** (`libtcd-2.2.7-r3.tar.xz`, 2020-06-26) is the latest; v5 can keep
  libtcd 2.2.7 (same API) but should take the r3 patches.
- **Harmonics**: yearly releases, latest `harmonics-dwf-20251228-free.tar.xz` (873 KB
  compressed, 2025-12-28), also an `-SQL` variant (787 KB). Release cadence: late
  December / early January each year (20180101 ... 20251228). Both are built with
  libtcd 2.2.7-r3.
- Not yet checked: XTide 2.16 release notes and whether its `libxtide` API changed from
  2.15.1 (a build smoke test in CI is the real check). Verify the 2.16 build with a
  C++23 compiler.

### 5.3 Licensing

XTide program: GPL-3 or later ("some individual source files are public domain"),
compatible with the GPL-3 app (`https://flaterco.com/xtide/disclaimer.html`).
**Harmonic constants have separate terms**: `https://flaterco.com/xtide/harmonics_boilerplate.txt`
(distribution assumes all liability; "NOT FOR NAVIGATION"; the maintainer accepts only
data with explicit permission; location names for some regions carry a reference to
legalese, e.g. the POL data). The "free" harmonics file excludes data with restricted
terms; **ship only the `-free` file** (the SQL file is the same data for building).
The release page notes US data come from NOAA (public domain), and alternative sets
using TICON-4 (CC BY 4.0) exist elsewhere, with mixed underlying licenses. The app UI
should carry the "not for navigation" disclaimer and attribute NOAA.

### 5.4 Recommendations

- Pin XTide 2.16 + libtcd 2.2.7-r3 + `harmonics-dwf-20251228-free`, record SHA-256 in
  the repo, vendor as separate CMake targets (plan §2.7).
- Regenerate `xtide_stations.csv` (station list and datum offsets) from the harmonics
  file by a `tools/` step instead of the shell scripts (`assembleXTideDatums.sh`).
- Datum offsets: `std::optional<Length>` (not `-999999`); v4 also mislabels XTide output
  as "MLLW" (plan bug 24).
- Tests: golden predictions for 2-3 reference stations and fixed dates, compared with
  tolerance against NOAA `predictions` (offline fixtures from section 2).

### 5.5 Fields that must not become sentinels

Datum offsets (`optional<Length>`), station type (`Ref`/`Sub`, enum), current-vs-tide
(enum), station validity.

---

## 6. GitHub Releases (update check)

Replaces `raw.githubusercontent.com/.../mov_release_revision.txt` (plan §1.4; also
`updatedialog.cpp` bugs, plan item 23).

- Endpoint: `GET https://api.github.com/repos/zcobell/MetOceanViewer/releases/latest`
  (docs: https://docs.github.com/en/rest/releases/releases#get-the-latest-release;
  accessed 2026-10-06). Returns "the most recent non-prerelease, non-draft release" by
  `created_at`. Fields used: `tag_name` (`v4.5.1`), `name`, `html_url`, `published_at`,
  `prerelease`, `draft`, `body` (markdown notes - escape; do not inject into rich text
  as v4 did), `assets[].name/browser_download_url`.
  Sample: `github_latest_release.json` (5.6 KB). Today's latest is `v4.5.1`
  (published 2021-07-30).
- Rate limits for unauthenticated requests: **60 requests/hour per IP**
  (https://docs.github.com/en/rest/using-the-rest-api/rate-limits-for-the-rest-api;
  observed `x-ratelimit-limit: 60`). On exceed: `403` or `429` with
  `x-ratelimit-remaining: 0` and `x-ratelimit-reset` (epoch seconds). Fine for a once
  per launch check; use `If-None-Match` with the stored `ETag` (304 responses are
  cheap) and persist the last-checked time to limit calls.
- Required request headers: a `User-Agent` (Qt sets none by default; set
  `MetOceanViewer/<version>`), `Accept: application/vnd.github+json`,
  `X-GitHub-Api-Version: 2022-11-28`.
- Semantics to decide: v5 betas must be flagged `prerelease: true` on GitHub or `latest`
  will not show them; compare using SemVer on `tag_name` with a leading `v` stripped
  (a strongly typed `Version`, not the v4 Dev/Rev hand-written `operator<`). To offer
  pre-release channels, call `/releases?per_page=5` instead.
- Errors: 404 (no releases yet, which is today's state for `v5` tags), 403/429 (rate
  limit), network failure: all are "check skipped", never a modal error.

Fixtures: the sample plus a prerelease-first `/releases` list, a 304, a 403 with
rate-limit headers, and a release with `tag_name` lacking `v`.

---

## 7. Cross-cutting notes for the v5 domain model and tests

1. **No in-band sentinels** (plan §2.2). The upstream sentinels to convert at the parser
   edge only: NOAA `-999999` datum offsets and `present` (2050) in the embedded CSVs;
   NDBC `99/999/9999` widths and `MM`; CRMS blank cells and fill values; XTide
   `-999999`; USGS legacy `noDataValue` (gone; now `null`); USGS `value` strings.
2. **Errors in-band in a 200 body** (CO-OPS `error` object; NDBC `Unable to access data
   file`; USGS errors use 4xx with two different JSON shapes: OGC `{code,type,description}`
   and api.data.gov `{error:{code,message}}`). Each provider parser returns
   `expected<..., ProviderError>` with a distinct `NoData` alternative.
3. **Time**: all four web providers can be requested/delivered in UTC (CO-OPS
   `time_zone=gmt`, NDBC files, USGS `continuous`); only USGS `daily` (local date) and
   CRMS (`CST`/`CDT`) need explicit handling; do not port `timezone.cpp`.
4. **Chunking and pagination are provider policy** (CO-OPS 31 d / 1 y / 10 y; USGS
   <= 1100 d + cursor pages of <= 50000; NDBC one file per year; CRMS one 1.3 GB file).
   Put them behind the provider concept, not in the UI.
5. **Cancellation/timeouts**: USGS pages up to ~36 MB of JSON (with geometry) make
   streaming parse and `properties=`/`skipGeometry`/`f=csv` mandatory for responsiveness.
6. **Fixture policy**: committed samples in `docs/provider-apis/` are for parser tests;
   promote them to `tests/data/` in Phase 3. Do not commit API keys; the recording script
   must pass keys by header and scrub them. USGS and CO-OPS content is public domain /
   US Government work; NDBC likewise. CRMS data have a CIMS Disclaimer
   (`/Disclaimer`); check before committing more than a few rows. XTide harmonics:
   see 5.3.
7. **Nightly live job** (non-blocking, plan §5 Phase 3): one request per provider
   asserting only structure (collections still listed, product keys still present,
   `Last-Modified` of the CRMS bulk zip within 14 days, NDBC header tokens unchanged).

## 8. Open questions for the owner

1. **v4 hotfix for USGS?** v4.5.1 stops getting data when `waterservices` is
   decommissioned (22 Feb 2027, with outages 27 Jan and 16 Feb). Do we ship a v4.5.2
   with the new `continuous`/`daily` endpoints, or accept that v4 breaks? (Extends the
   existing open decision 4.)
2. **USGS API key policy.** Per-user key entered in settings (recommended), or a
   project-owned key proxy? Unauthenticated use works but is throttled; official limits
   are not published.
3. **Historic stations.** v4's NOAA list includes ~2,360 retired stations with archived
   data; the metadata API's `type=waterlevels` returns only active ones. Keep the
   historical set (needs a one-time list from another source, e.g. the legacy CSV)?
4. **CRMS hosting.** *(Moot if CRMS is fetched live per station; see the investigation section.)* The S3 `crms.nc` is stale since 2021-09-20. Who hosts the refreshed
   artifact (GitHub Releases asset vs S3 bucket, weekly CI job)? Is it acceptable to
   publish a derived 1+ GB file, given the CIMS disclaimer?
5. **CRMS time zone.** *(RESOLVED: CST year-round, see the investigation section.)* Confirm from the CIMS Data Dictionary whether `CST`/`CDT` rows
   are truly local-with-DST or CST year-round.
6. **NDBC current-year data.** Accept the `realtime2` 45-day window plus monthly files to
   cover the gap, or only historical complete years as v4 effectively did?
7. **XTide.** Approve moving to 2.16 + libtcd 2.2.7-r3 + `harmonics-dwf-20251228-free`
   (and shipping only the "free" file).
8. **Products beyond v4 parity.** The new USGS API and CO-OPS expose more (field
   measurements, peaks, high/low, currents, salinity/conductivity). Parity only for 5.0?

---

## CRMS: API investigation (2026-10-06)

All endpoints below were probed live on **2026-10-06** (access date for every URL). Samples
are under `docs/provider-apis/crms-*`.

### A. Verdict

**There is no documented, supported CRMS API, but there is a working, unauthenticated,
synchronous per-station / per-date-range retrieval path**, plus a real ArcGIS REST service
for station metadata. Whether that counts as "a live API" for decision 7 is the owner's
call, because the data path is a scripted ASP.NET WebForms postback, not REST. Facts:

| Need | Mechanism | Official? | Stable? |
|---|---|---|---|
| Station list + lat/lon | ArcGIS REST `MapServer` query (JSON/GeoJSON), no auth | Yes (public CPRA GIS server, standard Esri REST) | High |
| Per-station date coverage | CIMS ASMX JSON method `getDatesForStations` | Undocumented (site internal, but listed in the service's own help page) | Medium |
| Hourly data, station + date range | CIMS `DataDownload.aspx?type=hydro_hourly` WebForms postbacks, returns a ZIP containing CSV | No, it is the website form | **Low** (VIEWSTATE flow; any UI change breaks it) |
| Bulk | `RequestedDownloads/ZippedFiles/*.zip` (weekly) | Yes, linked from `FullTableExports.aspx` | High |
| Real-time / REST JSON for observations | None found | n/a | n/a |

Not found, after checking: a REST/JSON/OGC/SensorThings data endpoint on CIMS
(`/api`, `/swagger`, `/RequestedDownloads/` return 403/404, and the home page, the download
page and the ASMX WSDL expose no data-returning JSON method); any hydrographic series in the
CPRA ArcGIS services (they carry station *attributes* only); a Socrata/CKAN portal
(`data.la.gov` and `data.louisiana.gov` do not resolve from the test host, and a web search
found no CRMS dataset there).

### B. Station metadata (ArcGIS REST, no auth)

Server: `https://cimsgeo3.coastal.louisiana.gov/arcgis/rest/services` (ArcGIS Server 10.81;
`cimsgeo.coastal.louisiana.gov` is a mirror with the same folders). Verified layers:

- `prot_rest/monitoring_stations/MapServer/0` (`CPRA_monitoring`), capabilities
  `Map,Query,Data`, `maxRecordCount` 100000, 29,272 rows (one per station x data type;
  station table joined with `mondata`). Fields used:
  `CPRA_monitoring.STATION_ID` (e.g. `CRMS0002-H01`, **the same id the download form uses**),
  `CPRA_monitoring.BASIN`, `CPRA_monitoring.LATDD`/`LONGDD` (WGS84 decimal degrees),
  `mondata.DATA_TYPE` (`Hydrography`, `Vegetation`, ...), `mondata.FREQUENCY`
  (`Hourly`, `30 Minute`, `Yearly`, ...), `mondata.SENSOR_ENV` (`Surface Water`,
  `Flotant Marsh`, `Marsh Well`), `mondata.STATUS` (`Active`/`Inactive`), `mondata.REAL_TIME`.
- `prot_rest/crms_points/MapServer/0` (393 CRMS *sites* by `SITE_ID`, `CRMS0002`; no data type
  and no hourly suffix).

Example (449 hourly-style CRMS stations match `STATION_ID LIKE 'CRMS%-H%'` on 2026-10-06):

```
GET https://cimsgeo3.coastal.louisiana.gov/arcgis/rest/services/prot_rest/monitoring_stations/MapServer/0/query
    ?where=CPRA_monitoring.STATION_ID LIKE 'CRMS%-H%'
    &outFields=CPRA_monitoring.STATION_ID,CPRA_monitoring.BASIN,CPRA_monitoring.LATDD,CPRA_monitoring.LONGDD,mondata.FREQUENCY,mondata.STATUS
    &outSR=4326&orderByFields=CPRA_monitoring.STATION_ID&f=json        (or f=geojson)
```

Sample: `crms-arcgis-stations-sample.json` (3 features). Caveats: the layer also carries legacy
`AT04-01`-style rows (815 rows with `FREQUENCY='Hourly'` and `DATA_TYPE='Hydrography'`
across both id styles) so filter on the `CRMSnnnn-Hnn` pattern; the layer has no hourly
start/end dates, so combine with `getDatesForStations` (below). This replaces the
embedded 23,029-row `crms_stations.csv` and its `AT02-01` ids for CRMS.

### C. Per-station coverage (undocumented JSON ASMX)

`POST https://cims.coastal.louisiana.gov/Services/DataDownloadService.asmx/getDatesForStations`,
`Content-Type: application/json; charset=utf-8`, no auth/cookies needed:

```
{"tb_StationName":"CRMS0002-H01","datatype":"CONTINUOUS_HYDROGRAPHIC"}
-> {"d":"[\"CRMS0002-H01 (09/29/2007 to 04/28/2026 [162,864])\"]"}      (crms-cims-getDatesForStations.json)
```

(`d` is a JSON string containing a JSON array: `"<id> (<first mm/dd/yyyy> to <last> [<row count>])"`.)
Other ops on the same service (`ValidateStation`, `GetCrmsStationIDList`,
`GetProjStationIDList`, `DownloadReqStats`, `DownloadSummaryStats`, `DownloadReqQtrRpt`)
are statistics/UI helpers; **none returns observations**. The last observation date above
(2026-04-28) was about five months behind "today": CIMS data are released after QA/QC, they
are not real time.

### D. Observation retrieval (scriptable form, synchronous)

Page: `https://cims.coastal.louisiana.gov/DataDownload/DataDownload.aspx?type=hydro_hourly`
(ASP.NET WebForms; no login; cookie jar used within one session). Working sequence, verified
with `crms-cims-download-example.py` (stdlib Python, 2 KB):

1. `GET` the page; collect all hidden inputs (`__VIEWSTATE`, `__EVENTVALIDATION`, ...).
2. `POST` them with `__EVENTTARGET=ctl00$MainContent$RBL_ProjCrmsList$2` and
   `ctl00$MainContent$RBL_ProjCrmsList=Filter by Station` (autopostback; reveals the
   station box).
3. `POST` with `ctl00$MainContent$TB_StationsList=<value from getDatesForStations>` (the full
   string `CRMS0002-H01 (09/29/2007 to 04/28/2026 [162,864])`; the bare id gave "Station
   Not found in the Tabular Database") and `ctl00$MainContent$btnStation=`.
4. `POST` `TB_StationsList`, `TB_FromDate=mm/dd/yyyy`, `TB_ToDate=mm/dd/yyyy`,
   `BTN_DownLoad=Download`. The reply is HTML containing the "enter a file name" modal.
5. `POST` the new hidden fields plus `TB_Filename=x`, `HF_Filename=x`, `BTN_OkFilename=Ok`.
   Reply is **`200 application/zip`, `Content-Disposition: inline;filename=x.zip`**, delivered
   in the same HTTP response (no email, no polling).

(The "Download With Preview" button returns an HTML table, only the newest 100 rows.)

Zip contents: `<name>.csv` (Latin-1; header exactly as in `crms_hourly_head.csv`: 44 columns),
`Hourly Hydro Read Me.pdf`, `HydroHourly.html`, `HydroHourly.xml` (the data dictionary, ESRI
metadata). Measured cost (one station, `CRMS0002-H01`):

| Range | Time | Zip size | Rows |
|---|---|---|---|
| 2024-03-01..03-20 | 1.2 s | 141 KB | 480 |
| 2023-01-01..12-31 | 12 s | 335 KB | ~8,760 |
| 2007-09-29..2026-04-28 (full, 162,864 rows) | **>120 s, client timed out** | n/a | n/a |

So cost scales roughly linearly at ~1.4 s per 1,000 rows plus an overhead of ~1 s; a v5
client would need a long timeout (or windowed requests of 1-2 years). The fixed ~155 KB of
PDF/HTML/XML are repeated on every download. Limits (rate, max range, concurrency) are
unpublished; I made about 12 requests and saw no throttling. The multi-station "Filter by
CRMS Sites / Projects" modes were not tested. "Include Non-Standard Data" is processed
off-line and emailed, so it is out.

Sample: `crms-cims-hourly-2024-dst-nov.csv` (header plus 72 rows, 2-4 Nov 2024, **Latin-1
bytes preserved, do not re-encode**).

### E. USGS mirror

The USGS Water Data API (`https://api.waterdata.usgs.gov/ogcapi/v1`) has 52 monitoring
locations whose names contain `CRMS` (queried with CQL
`monitoring_location_name LIKE '%CRMS%'`; agency `USGS`), but only **12** of them have
time-series metadata (gage height, salinity, specific conductance, water temperature, WL above
marsh; some `Points` = continuous values to 2026, others daily only). That is about 3% of the
449 CRMS hydrographic stations, so it is a partial supplement at best and does not remove the
need for CIMS. Those stations are normal USGS sites, already reachable by the USGS provider
without CRMS-specific code.

### F. Time zone: RESOLVED, CST (UTC-6) year-round, never CDT

Evidence:

1. **Metadata.** `HydroHourly.xml` (shipped in every download; ESRI FGDC/ISO attribute
   dictionary): field `Time_Zone`, "Time zone in Central Standard Time (CST)", domain value
   `CST`. The `Time Zone` column is populated with `CST` for every row sampled.
2. **Spring forward, 10 Mar 2024.** 20-day pull 2024-03-01..03-20: 480 rows = 20 x 24, all
   `CST`, hours 00:00-04:00 present on 3/9 and 3/10 including the 02:00 that does not exist in
   local clock time.
3. **Fall back, 3 Nov 2024.** Pull 2024-10-25..11-10: 408 rows = 17 x 24 hours, 408 unique
   timestamps, zero gaps, all `CST`, and no duplicated 01:00 on 11/3.
4. The first bulk CSV rows (2007-09-29 09:00 `CST`, a date inside US daylight time) are also
   labelled CST.

Conclusion: timestamps are **local standard time, UTC-6, with no DST adjustment**, so
`UTC = local + 6 h` always. v4's `CDT -> +5 h` branch is dead code for current CIMS output;
v5 should parse the column and treat `CST` as UTC-6, and return an error (not silently UTC;
legacy N15) on any other token, including `CDT`. Only hourly data were verified. The monthly
discrete product and the older pre-2007 records were not.

### G. Assessment against the owner's rule

Arguments that this is "a live API": per-station, per-date-range, synchronous, no login or
key, about 1 s for short ranges, the station list is true REST/JSON, and the id spaces join
cleanly (`CRMS0002-H01`). Arguments against: the observations come from a scraped WebForms
postback with VIEWSTATE (no contract, no versioning, no stated terms for automated use), a
zip with ~155 KB of constant overhead per request, slow for multi-year ranges, a
`TB_StationsList` value format that depends on a second undocumented call, and the data lag
(about 5 months). I rate the fragility **high** for the observation path and **low** for the
station list. If the owner accepts that, the provider is feasible with a small isolated
`crms_cims` adapter behind the provider interface, a contract test against the live site
(fail-soft and non-blocking in CI), and a "CRMS data source may change" disclaimer.

### H. If the owner decides to remove CRMS instead

Delete or amend, in `docs/rearchitecture-plan.md` and the repo:

- Plan §1.2 findings B16 and B26 and N15/N21/N22 become legacy-only (no v5 tests).
- Plan §2 target tree: `tools/crms-to-netcdf` (line 177); `src/providers/` CRMS entry (line
  173); the `ProcessCrmsDatabase` row in the mapping table (line 303); the CRMS mention
  in the map-layers list (line 268, 256's "multi-year CRMS" example).
- Phases: the CRMS item under async providers (line 396), the "Build `tools/crms-to-netcdf`"
  line (401), the "CRMS database" checklist item (421).
- Decision 7 and open decisions 4 and 5 (hosting, time zone) are closed.
- `docs/provider-apis.md` section 4 and CRMS rows of the provider tables/ToS note;
  `docs/legacy-formats.md` section 7 (CRMS ingest/netCDF dialect "C"), the `crms_stations.csv`
  inventory row (section 8), fixtures F7 and test ids for B16/B26.
- Drop the CRMS netCDF from file-type detection tests (`legacy-formats.md` section 2 note).
- Remove `crms_stations.csv` from the v5 data set; v4 keeps the S3 file as is.
- UI: no CRMS layer/tab; keep a note in the v5 release notes that CRMS was dropped.

### I. Questions for the owner

1. Does the scripted CIMS download path (section D) qualify as a "live API" under decision 7,
   given it has no published contract? If yes, accept an isolated, best-effort adapter with
   a live-site contract test? If no, remove CRMS per section H.
2. If kept: acceptable to ask CPRA (CIMS contact) whether they would endorse or expose a
   supported endpoint, and to put a descriptive `User-Agent` and a request-rate cap in the
   adapter?
3. If kept: is a ~5-month data lag and 10-60 s waits for multi-year ranges acceptable, or
   should the UI offer capped ranges (for example, last 12 months)?
