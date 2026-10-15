# Series JSON, format `metoceanviewer-series` 1.0

Normative spec of the JSON that `metocean-data … --format json` writes: the series of one or
more stations, for scripts and web tools that would rather not read netCDF
(`docs/providers-design.md` §4.7, `docs/v5-extras.md` #6). The machine-readable structure is
[`docs/schemas/series-v1.schema.json`](schemas/series-v1.schema.json) (JSON Schema draft
2020-12); this text is authoritative where the two differ. MetOceanViewer writes the format
(`mov/io/series_json.hpp`) and does not read it: its interchange formats for series are the
station netCDF (`docs/station-netcdf.md`), IMEDS and CSV.

The keywords MUST, SHOULD and MAY are used in the RFC 2119 sense.

---

## 1. Decisions at a glance

| # | Choice | Why (short) |
|---|---|---|
| J1 | UTF-8 JSON (RFC 8259); `format` = `"metoceanviewer-series"`, `version` = `"<major>.<minor>"` as the station catalog (`docs/station-catalog.md` §9). | One versioning rule for the project's JSON formats. |
| J2 | A station holds its series; a series holds parallel arrays `time` and `value` (and `status` when needed). | One line of JavaScript or pandas per series; no object per sample. |
| J3 | A Missing or Dry sample is `null` in `value`; `status` says which, and is written only when a series has a Dry sample. | Missing is the common case and needs no second array; Dry (model output) must not be mistaken for a gap (plan decision 16). |
| J4 | Times are UTC text with milliseconds, `YYYY-MM-DDThh:mm:ss.sssZ`; values are the shortest decimal text that reads back as the same double. | Exact: a reader gets back the series bit for bit. |
| J5 | Daily values (decision 34) have `date` (the station's local calendar day) and `statistic` in place of `time`. | A daily value belongs to a day, not an instant (`core::DailySeries`). |
| J6 | Units are udunits text, quantities and datums the station netCDF tokens. | The same vocabulary as the station netCDF format (SN §6, §10.2). |

---

## 2. Encoding

As the station catalog (`docs/station-catalog.md` §3): UTF-8 without a byte order mark,
unique member names, text without U+0000 (control characters escaped), decimal numbers.
`null` appears only in `value`.

---

## 3. Top level

| Member | Type | Req. | Meaning |
|---|---|---|---|
| `format` | string | yes | Exactly `"metoceanviewer-series"`. |
| `version` | string | yes | `"1.0"` for this spec. |
| `stations` | array | yes | §4, in the order the writer was given them. May be empty. |

---

## 4. Stations and series

| Station member | Type | Req. | Meaning |
|---|---|---|---|
| `id` | text | yes | The station id, not empty. |
| `name` | text | yes | The station's name; may be empty. |
| `provider` | token | no | Where the data came from: a station netCDF `station_provider` token (`noaa_coops`, `usgs`, `ndbc`, `harmonics`, `adcirc`, `dflowfm`, `user`); absent when unknown. |
| `lon`, `lat` | number | yes | WGS 84 degrees, `lon` in (−180, 180], `lat` in [−90, 90]. |
| `series` | array | yes | The station's series. |

| Series member | Type | Req. | Meaning |
|---|---|---|---|
| `quantity` | token | yes | A quantity registry token (`water_level`, `wind_speed`, …; SN §6) or a generic one (`usgs_00010`). |
| `label` | text | no | A human label; absent when empty. |
| `unit` | text | no | udunits text (`m`, `m s-1`, `ft3 s-1`); absent when the series has no unit. |
| `datum` | token | no | The vertical datum token (`MLLW`, `NAVD88`, …; SN §10.2); absent when none applies. |
| `statistic` | string | daily | The USGS statistic code of daily values (`00003` mean, `00001` maximum, `00002` minimum). |
| `time` | array of strings | instantaneous | UTC instants, `YYYY-MM-DDThh:mm:ss.sssZ`. |
| `date` | array of strings | daily | Calendar days, `YYYY-MM-DD`. |
| `value` | array | yes | A number per sample, or `null` for a Missing or Dry one. |
| `status` | array of tokens | no | Per sample `value`, `missing` or `dry`; written only when a sample is Dry. |

A series has `time`, or `date` and `statistic`, never both.

---

## 5. Rules the schema cannot express

- `time` (or `date`), `value` and `status` of one series have the same length.
- Times are strictly increasing, and so are dates.
- `status` is `value` exactly where `value` is a number.
- Years outside 0000–9999 are written with as many digits as they need, a negative one with a
  minus sign (as the CSV export does).

---

## 6. Canonical form

MetOceanViewer's writer is deterministic: line 1 is `{"format":…,"version":"1.0",`, line 2
` "stations":[`, then one station per line, each but the last followed by `,`, then `]}` and a
newline (` "stations":[]}` with no station). Members are in the order of the tables above;
text is escaped as in the station catalog (§7 there). Files are written atomically and
streamed in 64 KiB chunks, so a large export is never held in memory twice.

---

## 7. Versioning

As the station catalog (`docs/station-catalog.md` §9): a minor version adds optional members
or tokens, a major version changes the meaning of existing ones. A consumer SHOULD ignore
members it does not know in a newer minor.

---

## 8. Worked example

Two stations: a CO-OPS gauge with a gap, and a model point with a dry sample, a time before
1970 and an all-missing column. It is the golden file `tests/fixtures/io/series_json/table.json`,
which the tests write byte for byte and `tools/check_json_schemas.py` validates.

<!-- worked-example:begin -->
```json
{"format":"metoceanviewer-series","version":"1.0",
 "stations":[
{"id":"8729840","name":"Pensacola","provider":"noaa_coops","lon":-87.2112,"lat":30.4044,"series":[{"quantity":"water_level","label":"Water level","unit":"m","datum":"MLLW","time":["2026-10-01T00:00:00.000Z","2026-10-01T00:06:00.000Z","2026-10-01T00:12:00.000Z"],"value":[0.123,null,-1e-07]},{"quantity":"wind_speed","unit":"m s-1","time":["2026-10-01T00:00:00.000Z","2026-10-01T00:06:00.000Z","2026-10-01T00:12:00.000Z"],"value":[3.6,4,null]}]},
{"id":"model 1","name":"Grid \"point\"\n","lon":-89.5,"lat":29,"series":[{"quantity":"water_level","label":"Water level","unit":"m","datum":"MLLW","time":["1969-12-31T23:59:59.005Z","2026-10-01T00:00:00.000Z"],"value":[null,2.5],"status":["dry","value"]},{"quantity":"wind_speed","unit":"m s-1","time":["1969-12-31T23:59:59.005Z","2026-10-01T00:00:00.000Z"],"value":[null,null]}]}
]}
```

Daily values (`daily.json`):

```json
{"format":"metoceanviewer-series","version":"1.0",
 "stations":[
{"id":"USGS-07010000","name":"MISSISSIPPI RIVER AT ST. LOUIS, MO","provider":"usgs","lon":-90.1796,"lat":38.629,"series":[{"quantity":"discharge","label":"Discharge, cubic feet per second","unit":"ft3 s-1","statistic":"00003","date":["2026-09-30","2026-10-01","2026-10-02"],"value":[512000,null,498000.5]}]}
]}
```
