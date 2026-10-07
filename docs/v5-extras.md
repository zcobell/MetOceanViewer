# v5.0 cheap extras (proposal)

Status: **proposed**, 2026-10-06. Owner direction (plan §6.10): parity is the v5.0
baseline; cheap additions may be proposed but must not delay it. Nothing here is on the
parity critical path. Baseline facts are from commit `e5a4e0af` (v4.5.1) and
`docs/provider-apis.md` ("PA" below).

"Cheap" = rides on infrastructure v5 builds anyway, about 1-3 days, no new heavyweight
dependency, no new hosted infrastructure. Cost is the increment over parity.

## Summary table

| # | Candidate | v4 had it? | Support | Cost | Phase | Risk | Verdict |
|---|---|---|---|---|---|---|---|
| 1 | CO-OPS extra products (see note A) | 11 products (`noaa.cpp:329`) | PA §2.1 product list | 1-2 d (met/scalar ones 0.5 d) | 3 (provider), 4 (picker) | Low | **v5.0** (scalar set); defer currents |
| 2 | Quick stats in Details panel | HWM stats only; none for series | none needed | 1 d | 2 (core), 4/5 (UI) | Low | **v5.0** |
| 3 | Observed minus predicted residual | No (two overlaid series only, `noaa.cpp:101`) | PA §2.1 (same requests) | 1-2 d | 2 (core), 5 | Low-Med (time alignment) | **v5.0** |
| 4 | Station search by name/id | No (map only) | station table | 1-2 d | 4 | Low | **v5.0** |
| 5 | Favorites / recent stations | No | `QSettings` | 1 d | 4 | Low | **v5.0** |
| 6 | CLI JSON output | No (`MetOceanData` is prompt-driven, bugs 24-25) | CLI already being rebuilt | 0.5 d | 3 | Low | **v5.0** |
| 7 | CO-OPS high/low (`hilo`) predictions | No | PA §2.1 `interval=hilo`, `high_low` | 1 d | 3, 5 | Low | v5.x |
| 8 | USGS extra parameters | Per-site parameters via product box | PA §1.4, §1.5 | ~0 (parity) | 3 | Low | **parity, no extra** |
| 9 | USGS daily min/max stats | dv only (mean) | PA §1.4 stat ids `00001/00002` | 0.5 d | 3 | Low | v5.x |
| 10 | USGS field measurements | No | `field-measurements` collection (PA §1.3) | 2-3 d | 3, 5 | Med (sparse points, new chart mode) | v5.x |
| 11 | NDBC spectral / extra feeds | stdmet only | PA §3.1 (`.spec`, `.ocean`, ...) | 3+ d | 3, 5 | Med-High | defer, reject for 5.0 |
| 12 | XTide currents stations | Tide only | harmonics file has current stations | 2-3 d | 3 | Med (flood/ebb direction, datums) | v5.x |
| 13 | Nearest stations to a click | No | station table, haversine | 1 d | 4 | Low | v5.x |
| 14 | Offline cache of fetched series | No (v4 refetches; v5 must start offline) | disk cache | 2-3 d | 3 | Med (invalidation, ToS) | v5.x |
| 15 | Compare two arbitrary stations | Only via user-series/model projects | `TimeSeries` + chart | 2-3 d | 5/6 | Med (UI, units, datum) | v5.x |
| 16 | Permalink / session share via URL | No | custom URL scheme | 2 d + OS registration | 6/7 | Med (per-OS handlers, signing) | reject |

## Recommended for v5.0 (in order)

1. Quick stats in the Details panel (#2).
2. Station search by name/id (#4).
3. Favorites and recents (#5).
4. Residual series (#3).
5. CO-OPS scalar products (#1), and CLI JSON output (#6) as a half-day freebie.

Together about 6-9 days. Each is independently droppable; cut from the bottom of the
list if parity slips.

## Notes on v5.0 items

**A. CO-OPS extra products (#1).** v4 offers water level (6 min and hourly), predictions,
air temperature, water temperature, wind speed/direction/gusts, humidity and air
pressure (`noaa.cpp:329`, PA §2.4). So `water_temperature`, `humidity` and
`air_pressure` are already parity; do not count them as extras. Genuine new, nearly free
scalar products with the same JSON envelope and 31-day chunking (PA §2.1, §2.2):
`conductivity`, `salinity`, `visibility`, `air_gap`, `one_minute_water_level` (4-day
cap, so many chunks; gate behind a warning on long ranges). Because the `CoopsProduct`
variant (plan §2.2) makes each alternative carry `units()`, `label()` and
`query_params()`, each product is roughly one alternative plus one fixture. Offer only
products listed in the station's `products.json` (PA §2.3), so the picker never shows a
dead choice. Daily/monthly products (`daily_mean`, `monthly_mean`, `hourly_height`
already parity) are skipped: low value on a time-series chart.
Not included: `currents` and `currents_predictions` (vector-valued, bin selection,
separate station layer; revisit with #12) and `ofs_water_level` (model data, different
semantics).

**B. Quick stats (#2).** Min, max, mean, count, time of peak (and valid-data fraction)
as a pure function over `TimeSeries` (`optional<double>` samples, so gaps are skipped
rather than counted as zero). The plan already puts "headline statistics (peak observed,
peak predicted/modeled)" in the Details panel (§2.6), so this is mostly generalizing it
to every series, with one test table. Show values in the display time zone. No new
dependency. Skeptical note: stop at these five numbers; do not add percentiles or
harmonic analysis.

**C. Residual (#3).** v4 only overlays observed and predicted (`ObsVsPred`, plan §2.2).
Add a derived series `observed - predicted` (surge/non-tidal residual) when both exist.
It is a pure function: inner-join on exact timestamps (both are 6-minute from CO-OPS,
GMT, same datum, per PA §2.1), producing a new `TimeSeries` with a label and units.
Rules to avoid a bug farm: same datum and units are enforced by the type (return
`expected<TimeSeries, Error>` otherwise); no interpolation in v5.0, so mismatched grids
give a short or empty result and an explicit message; hourly vs 6-minute is rejected, not
resampled. UI: one toggle in the legend, chart shows it as an extra line (or lower
panel if Qt Graphs makes that easy; otherwise same axis). Only CO-OPS water level
qualifies in 5.0. The same function later serves model-minus-observed (phase 6), which
is the real payoff, since HWM/model comparison stats then reuse it.

**D. Station search (#4).** With all ~40k stations in GeoJSON layers (plan §2.4) there is
no way to find a known gauge except panning. Search box over the normalized station
asset: case-insensitive substring on name and id, across enabled provider layers,
result click flies the camera and selects. In-memory linear scan of ~40k short strings
is a few milliseconds; do it off-thread per plan §2.3 or just debounce. No index, no
fuzzy matching, no geocoder (a geocoder would need a hosted service: rejected).

**E. Favorites and recents (#5).** A list of `{provider, StationId}` plus the last-used
product/datum per station, kept in `QSettings` (or the session JSON schema, plan §2.6).
UI is a star toggle in the Details header and a short dropdown in the search box.
Typed ids (plan §2.2) prevent cross-provider mixups. Skip syncing and folders.

**F. CLI JSON output (#6).** `metocean-data --format json|csv|imeds` writing a
`TimeSeries` (metadata object plus `t`/`v` arrays, null for gaps). v4's CLI was
interactive and buggy (plan bug 24-25); the Phase 3 rewrite is flag-driven, so a JSON
writer is a small function beside the CSV one and makes the CLI scriptable. Use
`std::format`/a small writer, not a JSON dependency, unless one is already adopted for
sessions (§2.6).

## Notes on deferred and rejected items

- **#7 High/low tide predictions.** Real, cheap API support (`interval=hilo`, PA §2.1)
  but discrete events do not fit a line series. Needs a marker-series or table view.
  Pair with XTide, which can also emit high/low. v5.x.
- **#8 USGS parameters.** v4's product box already lists each site's parameters
  (`usgs.cpp:92-120`), and PA §1.7 requires preserving that. Discharge, gage height,
  temperature and the rest come for free from the metadata-driven picker; this is
  parity, not an extra.
- **#9 USGS daily min/max.** Trivial (a different `statistic_id`), but needs a
  statistic selector in the left panel. Fold into v5.x with #10 if users ask.
- **#10 Field measurements.** Sparse manual points suit a scatter marker series, not a
  line. Needs new chart mode and new fixtures; skip for 5.0.
- **#11 NDBC spectral/other feeds.** Different file formats per feed (`.spec`, `.ocean`,
  `.srad`; PA §3.1), spectral density needs a new plot type (not a time series). Reject
  for 5.0. A cheaper subset: `.ocean` and `.spec` summary columns are time series and
  could join stdmet later.
- **#12 XTide currents.** Harmonics files contain current stations, but they need
  velocity/direction semantics, a separate station layer and a different datum story;
  v4's CSV offsets (`MLLW..NAVD`) do not apply. Do after XTide 2.16 upgrade is stable.
- **#13 Nearest stations.** Cheap, but a tapped point on a vector map already selects the
  nearest marker through hit-testing. Revisit only if user testing shows a gap.
- **#14 Offline cache.** Plan §2.3 requires that offline is a normal state; a cache is
  the natural complement, but correctness costs (keyed by request, expiry for
  "latest" data, chunk boundaries, size cap, USGS/NDBC terms) exceed 3 days. A minimal
  in-memory cache of the current session's series (no disk) is free and covers re-selecting
  a station; do that inside parity work, not as a feature.
- **#15 Compare two stations.** Needs per-series datum/unit reconciliation and a
  multi-selection UI. Project layers (plan §2.6) already let users overlay imported
  files; do the generalization after phase 6 proves the overlay model.
- **#16 Permalink/URL sharing.** Without a backend it is a custom URL scheme requiring
  per-OS registration, signing interplay (signing accounts do not exist yet, §6.1) and
  security review of parsing untrusted URLs. Sessions (JSON) already cover sharing a
  view as a file. Reject.

## Constraints on all items

- No new hosted infrastructure, no new heavyweight dependency, no geocoding service.
- Each addition gets fixture tests (plan §7) and, for products, a recorded response.
- Anything touching a provider must respect PA §7 rules (no sentinels, per-provider
  rate limiting, error shapes).
- Cut order if parity slips: CLI JSON, extra products, residual, favorites, search,
  stats.
