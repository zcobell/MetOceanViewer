# Pringle "Global Tide Gauge Database" vs XTide harmonics: evaluation for MetOceanViewer v5

Research date: 2026-10-08. All URLs accessed 2026-10-08 unless noted. Repo untouched. Scratch is under `~/.cache/metoceanviewer-dev/tmp/tides-research`.

## 0. Bottom line

1. William Pringle's database is real, but it is a **Google My Maps map**, not a versioned dataset. It has about 2,800 station pins and 8 to 10 constituents per pin. It has no license statement, no datums and no subordinate stations. Most of the station data comes from agencies whose terms vary. The GESLA-sourced layers are 2012 to 2016 vintage.
2. The one citable, licensed Pringle artifact is a 614-station Indian Ocean / West Pacific CSV (Mendeley Data, CC BY 4.0, 2017). It has 8 constituents and no datums.
3. **A discovery that matters more.** The XTide `harmonics-dwf-20251228-free` file is **US-only (NOAA)**. Its 8,063 records are 8,026 "USA" plus 37 Pacific territory stations, all sourced from the CO-OPS Metadata API. The v4 `harmonics.tcd` in this repo is the same: 4,165 records, essentially all U.S. v5 on "XTide free" therefore gives no global tide predictions at all.
4. The better candidate for global coverage is **TICON-4** (DGFI-TUM, CC BY 4.0, 4,838 gauges, 50 constituents, DOI 10.17882/109129). It is already packaged together with NOAA as an XTide/libtcd-compatible **TCD file** by `openwatersio/slackwater-database` (monthly releases, MIT code). XTide's own file page points to it as the way to get global coverage.
5. **Recommendation:** do not use Pringle's map as a data source. Keep XTide/libtcd as the prediction engine and **complement** the free NOAA file with a TICON-4-based global TCD (the Slackwater TCD, or one we build ourselves), with the licensing filters described in section 2. Details are in sections 5 and 6.

## 1. What Pringle's dataset is

### 1.1 The "Global Tide Gauge Database" (Notre Dame page and Google My Maps)

- Page: https://sites.nd.edu/william-pringle/global-tide-gauge-database/ (page 17, copyright 2026). Its text: "Map of stations around the world with known tidal harmonics, compiled from various sources as listed in description. All amplitudes are in meters, and all phase lags are in degrees referenced to GMT."
- The page is only an `<iframe>` embedding a Google My Map: `https://www.google.com/maps/d/u/1/embed?mid=1yvnYoLUFS9kcB5LnJEdyxk2qz6g`. There are no other files or links on the page. (The page needs a browser User-Agent; plain curl gets HTTP 403.)
- **Still live.** The map exports as KML with no authentication: `https://www.google.com/maps/d/kml?mid=1yvnYoLUFS9kcB5LnJEdyxk2qz6g&forcekml=1`. This returned HTTP 200, 8,227,382 bytes, on 2026-10-08. SHA-256 of that export: `c43def14...c758`. This is the only machine-readable route, and it depends on Google My Maps keeping public sharing and the KML endpoint working.
- Map description (the source list, verbatim in substance):
  - "truth" gauges from the FES2012 project, `ftp://ftp.legos.obs-mip.fr/pub/FES2012-project/data/gauges/2013-12-16/`. See also Stammer et al. 2014, doi:10.1002/2014RG000450.
  - NOAA CO-OPS harmonic constituents.
  - JMA (Japan Meteorological Agency) station2017 database.
  - KHOA (Korea Hydrographic and Oceanographic Agency).
  - GESLA "NoRepeats" (gesla.org).
  - UHSLC "Fast" (ftp.soest.hawaii.edu/uhslc/fast/).
- **Contents of the KML (parsed):** 8 layers, 2,817 placemarks. Stations are duplicated across layers, so this is not 2,817 unique gauges.

| Layer | Stations | Constituents | Extra metadata |
|---|---|---|---|
| truth_pelagic | 151 | M2 S2 N2 K2 K1 O1 P1 Q1 | none |
| truth_shallow | 196 | same 8 | none |
| truth_coastal_woce | 56 | same 8 | none |
| NOAA_Database | 1,030 | same 8 + M4 (always 0) | NOAA_ID |
| JMA_Database | 240 | M2 S2 K1 O1 only | site code, original lat/lon |
| KHOA_Database | 41 | 8 + M4 + "M2_Seasonal" | start/end, gapless length, CNUM, PTV, SNR |
| GESLA_NoRepeats | 812 | 8 + M4 + M2_Seasonal | country, contributor, start/end, gapless length, PTV, SNR |
| UHSLC_Fast | 291 | 8 + M4 + M2_Seasonal | UH and GLOSS ids, country, record stats |

- **Station ids and names:** only the NOAA layer carries an id (NOAA_ID); the others are names, with the UHSLC layer also carrying UH/GLOSS ids and the JMA layer a site code/number. There are no stable ids across layers.
- **Methodology** is not in the map. The Mendeley metadata and secondary sources say that where agencies publish harmonics they are used as published, and elsewhere UTide (MATLAB) was run on up to about 21 years of water levels. The KML fields show:
  - UTide-style statistics: CNUM, PTV (percent of variance explained), SNR_CN.
  - Gapless record length. GESLA stations have a median of 17.6 years; KHOA 16.3 years; 18 GESLA stations are under 1 year.
- **Vintage:** GESLA layer end dates run to at most 2015-05 (median 2012-12). KHOA ends 2016-12-31. This is GESLA-2 era data, now about 10 years old, with no versioning.
- **Datum and reference:** all amplitudes are in metres. Amplitudes are about the mean (a constituent set has no datum by construction), and the map gives no datum offsets, MSL/MLLW relationships, or tidal datums. Phases are in degrees lag "referenced to GMT" (so UTC / Greenwich phase lag), per the page. Nodal-correction convention is not documented. The NOAA layer is simply NOAA's published GMT phases (which are referred to nodal-corrected, Greenwich-argument conventions). UTide-derived values use UTide's Greenwich-phase convention. A prediction engine must apply the matching convention.
- **Provenance check against XTide's data.** For the 937 NOAA ids in both Pringle's map and the Slackwater TCD, M2 amplitudes and phases agree exactly after rounding (median absolute difference 0.0). Pringle's NOAA layer is just a rounded 8-constituent subset of the NOAA data. It adds nothing there, while NOAA and XTide have 25 or more constituents per reference station (median 25 in the free TCD; 49.5 in the Slackwater TCD).
- **License:** none stated on the page or in the map. Underlying sources have their own terms: NOAA is public domain; GESLA is mixed (research-only / non-commercial for some contributors); JMA, KHOA, UHSLC and the LEGOS FES gauges each have their own terms.
- **Maintenance:** no dated release or changelog.

### 1.2 The citable Pringle dataset: Mendeley Data

- **Major tidal constituents for the Indian Ocean and Western Pacific Basin**, William Pringle, University of Notre Dame. DOI 10.17632/tjyjn56jbf.1, version 1, published 2017-12-01.
- URL: https://data.mendeley.com/datasets/tjyjn56jbf/1. API: `https://data.mendeley.com/public-api/datasets/tjyjn56jbf`.
- **Licence: CC BY 4.0.** One file: `INDWPAC_StationList_NoProprietary.csv`, 82,099 bytes, SHA-256 `67536062...1648` (I re-hashed the download and it matches).
- **Content:** 614 stations. Columns: `Name,Lon,Lat,Source` plus `{M2,S2,N2,K2,K1,O1,P1,Q1}_{amp,phs}`.
- **Sources (counts):** JMA 181, ST727 130, GESLA 107, Truth_Shelf 52, KHAO 35, Truth_Pelagic 31, Fang1999 29, UHSLC_Fast 19, Robertson2008 13, Fang2004 6, Wei2016 5, NOAA 4, Krien2016 2.
- **Methods text:** some values are from published papers (Fang 1999/2004, Krien 2016, Robertson & Ffield 2008, Wei 2016). "The rest of the data (GELSA-2, KHOA and UHSLC FD) has been produced through harmonic decomposition of time series of the surface elevations by Utide in MATLAB." It states "Use for model comparison/validation and related research (do not use for navigational purposes)".
- **Associated paper:** Pringle, Wirasaet, Suhardjo, Meixner, Westerink, Kennedy, Nong (2018), "Finite-element barotropic model for the Indian and Western Pacific Oceans: tidal model-data comparisons and sensitivities", Ocean Modelling 129, 13-38, doi:10.1016/j.ocemod.2018.07.003 (https://repository.library.noaa.gov/view/noaa/63561). It reports an average RMSE of 14 cm against coastal gauges. The Notre Dame / secondary descriptions attribute the global compilation to "Pringle (2019)"; I could not locate a paper or data record for that citation.
- Regional only (Indian Ocean / West Pacific), 8 constituents, no datums. The "NoProprietary" in the name means the proprietary stations were removed, so this is a licensed subset of the map.

### 1.3 Other Pringle/Notre Dame items checked

- Zenodo 10.5281/zenodo.3759896 (v1, 2020-04-26, CC BY 4.0, 12.2 GB): "Global Storm Tide Modeling on Unstructured Meshes with ADCIRC v55 - Simulation Results and Model Setup" (paper: Pringle, Wirasaet, Roberts, Westerink, GMD 14, 1125-1145, 2021, doi:10.5194/gmd-14-1125-2021). The GMD paper cites DOI 10.5281/zenodo.3911282 for the archive. It holds **simulated** harmonic constituents on model meshes, not gauge constants. It validates against Stammer et al. (2014) gauges and TPXO9-Atlas. It is not a station database.
- GitHub `WPringle`: `pytides` (a fork of sahitono/pytides, MIT, updated 2026-06-22), `detide`, `GLOCOFFS` (MIT; a JS/web flood-forecast viewer with no tide-gauge dataset in its tree), `adcirc`, etc. No tide-gauge data repository. His institutional pages: Notre Dame site above; Argonne affiliation per his GitHub profile. Nothing at Argonne hosts a gauge database that I could find.
- The pytides fork matters only as a possible MIT prediction-engine reference (see section 4).

## 2. License and redistribution inside a GPL-3 app

| Data | License | Ship inside the GPL-3 app? |
|---|---|---|
| Pringle Google My Map (KML) | None stated; contains JMA, KHOA, GESLA, UHSLC, FES-gauge data with their own terms | **No.** There is no basis to redistribute, and no stable download. |
| Pringle Mendeley CSV | CC BY 4.0 | Yes with attribution (Pringle and the primary sources). Small and regional, 8 constituents. Not worth it. |
| XTide `harmonics-dwf-*-free` | NOAA public domain data, reprocessed by D. Flater; disclaimer ("not for navigation", user assumes all liability), notice that it is not official government material (`harmonics_boilerplate.txt`) | Yes, already planned (docs/provider-apis.md section 5.3). |
| TICON-4 | CC BY 4.0 (SEANOE page and metadata) | Yes with attribution (citation text below). **But** it is derived from GESLA-4, whose license page says "the data are accessible, but are covered by several different licences, some of which are non-commercial, by-attribution, or a combination of conditions." GESLA names CMEMS, UZ and CV as research-only (consultancy needs the data owners). |
| Slackwater database | Code MIT; data "unless otherwise noted CC BY 4.0"; a few stations CC BY-NC 4.0 marked `commercial_use:false`; NOAA public domain | Yes with attribution, after dropping the NC stations. |

- **GPL-3 compatibility.** CC BY 4.0 data is not code; a GPL-3 program can ship it alongside as data with its attribution notice. CC BY-NC data is a problem only if the product is distributed commercially or if the maintainers want a clean "free for any use" statement. The owner decides; the safe path is to exclude NC stations.
- **Attribution required for TICON-4:** Hart-Davis, M., Dettmering, D., Seitz, F. (2025). TICON-4: TIdal CONstants based on GESLA-4 sea-level records. SEANOE. https://doi.org/10.17882/109129 (licence CC BY 4.0). SEANOE also asks that Piccioni et al. (2019), doi:10.1002/gdj3.72, be cited when used in a publication.
- **Specific risk in the Slackwater TCD, verified.** I dumped the 2026-10-08 release with libtcd 2.2.7. Every one of its 8,738 records has restriction "Public Domain" and no legalese, including all TICON-4 records. At least **281** TICON station ids carry `cmems` (the CMEMS-sourced stations, which are the CC BY-NC ones per the Slackwater README). The TCD therefore **loses the per-station license information**. v5 must not consume that TCD blindly. Options: build our own TCD from the JSON `data/` files (which carry `license`, `attribution`, `commercial_use`), or filter by station id against the JSON data.
- NOAA data is public domain, but NOAA asks that modified data not be presented as official government material (quoted in the harmonics README).
- XTide's own `harmonics_boilerplate.txt` says "whoever chooses to redistribute these data assumes all liability", and that all "free" data are public domain while "nonfree" data are limited to non-commercial use. The maintainer ended maintenance of non-U.S. data in 2012.

## 3. Comparison with XTide `harmonics-dwf-20251228-free`

Counts below come from dumping each file with the repo's vendored libtcd 2.2.7, compiled in scratch (I wrote a small dump program there).

| | Pringle map (KML) | XTide free 20251228 | Slackwater TCD 20261008 | TICON-4 raw |
|---|---|---|---|---|
| Records | 2,817 pins (dups across layers) | 8,063 | 8,738 | 4,838 gauges (manual), 50 constituents |
| Tide stations | about 2,800 | 3,348 (1,292 reference, 2,056 subordinate) | 6,179 (3,938 reference, 2,241 subordinate) | 4,838 reference-like |
| Current stations | 0 | **4,715** (knots; many subordinate) | 2,559 | 0 |
| US share | NOAA layer 1,030 | essentially all (8,026 "USA") | 4,073 U.S. tide stations | about 575 NOAA + USGS/CRMS/etc. |
| Non-US tide stations | about 1,700 across layers, mostly Japan/Korea/GESLA | about 37 (Marshall Is., FSM, Palau) | 2,106 (TICON 2,660 records incl. rivers; Canada 259, Japan 216, France, Germany 147 each, Australia 130, Netherlands 92, UK 76, Sweden 75, Mexico 68) | all |
| Constituents per reference station | 4 to 9 | median 25 (min 7, max 119) | median 49.5 (min 13, max 120) | 50 (zeroed when not significant) |
| Datum info | none | Datum offset in file, mostly MLLW (1,241 of 8,063 say MLLW; 6,771 "Unknown" are mostly currents and subordinates) | Datum names include MLLW, LAT, LLWLT, NLLW, MSL, MLWS; offsets computed | `datum_information` is the GESLA free-text vertical reference, not a tidal datum |
| Subordinate stations | none | yes (time and height offsets) | yes (NOAA subordinates) | none |
| Update cadence | none (static, GESLA-2 era) | yearly (late Dec) | monthly (NOAA) + TICON-4 releases | irregular (TICON-3 2022, TICON-4 2025) |
| Quality flags | PTV, SNR_CN, record length for non-NOAA layers | none beyond NOAA | no | record_quality (4 classes), amp_std, pha_std, years_of_obs, missing_obs |

- **Quality notes.**
  - NOAA constants are the authoritative US source and XTide/Slackwater reproduce them.
  - Pringle's NOAA layer is a lossy copy (rounded, 8 constituents, M4 = 0 placeholder).
  - TICON-4 stations: median record 19.0 years, 3,642 of the 4,838 have at least 10 years, and 2,450 at least 18.6 years. Record quality: 4,212 "No obvious issues", 394 "Possible quality control issues", 137 "Possible datum issues", 95 both. The user manual notes timing-reference problems found by comparing duplicate sources (local time labelled as UTC at some German/Dutch gauges). The Slackwater database re-fits those gauges from the water levels. 938 TICON-4 gauges are rivers and 125 lakes; the app should filter on `type = Coastal` for tides.
  - TICON-4 includes inland gauges (USGS/CRMS/CDWR), which are not tidal gauges in the XTide sense.
- **Currents.** Only XTide/NOAA (and Slackwater, importing NOAA currents) have currents. Pringle's and TICON-4 data are elevation only.
- **Slackwater cross-checks its predictions against NOAA** across 3,300+ stations (own README, Feb 2026): median timing difference 0.6 min, 95% of stations under 4.3 min, median height error 5 mm, 95th percentile 17 mm. I did not independently reproduce this.

## 4. What v5 would need to use TICON-4 or Pringle's constants

### 4.1 Engine and conventions

A harmonic engine needs, for each constituent:
- speed;
- equilibrium argument V0 at the epoch;
- nodal factors f and u for the chosen year or time;
- the station's amplitude and phase lag (Greenwich/UTC) from the dataset.

Conventions in each data source (NOAA GMT, UTide Greenwich, TICON least-squares with whatever argument) must be matched, or predictions are wrong by tens of degrees at some stations.

The TICON-4 manual does not state the phase reference or nodal convention. The Slackwater maintainers validated TICON phases against raw GESLA-4 data, and they document (https://github.com/openwatersio/slackwater-database/blob/main/sources/ticon/README.md) these deviations from the manual:
- Table 1 swaps the MKS2 and 3N2 rows (the published phases match the IHO speed 29.0662415 deg/h for MKS2, a Doodson 257.555 line).
- Sa is on Doodson 056.555.
- The German `wsv` and Dutch `rws` gauges are labelled UTC but are in local time.

**Using TICON-4 therefore needs a vetted mapping of its 50 names to Doodson numbers/speeds and nodal rules.** Taking this from Slackwater saves much of that work.

### 4.2 Options

A. **Keep XTide (libxtide + libtcd) as the engine, feed it a TCD.** This is what the TCD route gives. libtcd 2.2.7 reads Slackwater's file with no changes (I confirmed this by reading it with the repo's vendored copy). libtcd also has `create_tide_db`/`add_tide_record`, so we can generate our own TCD from TICON-4 (CSV), NOAA (CO-OPS API), and optionally Pringle's constants. The engine, nodal factors, subordinate offsets, currents and datum handling already exist and are tested. libxtide reads the file via `HFILE_PATH`. This is the lowest-risk path.

B. **Own C++ predictor** (no XTide). Candidates for algorithms and test vectors:
   - UTide (Codiga; MATLAB/Python, MIT) for analysis/prediction with Greenwich phases.
   - pytides (MIT; Pringle's fork WPringle/pytides) as a small Schureman-style nodal-correction implementation.
   - NOAA/Schureman tables (public).
   - `@slackwater/engine` (MIT, TypeScript and Swift; the Swift/TS code is a readable reference for the TICON name mapping and IHO nodal rules).
   - An IHO/Foreman-style implementation. Effort for a tested engine plus validation: about 2 to 3 weeks, mostly spent on conventions and shallow-water/compound constituents, and it duplicates what libxtide already provides.

C. **Pringle's map only:** parsing is trivial (KML or CSV), but it needs the 4 to 9 constituent engine anyway. Predictions from 8 constituents are poor in shallow water, and the sources are stale.

### 4.3 Validation strategy

- Golden tests at 3 to 5 NOAA stations (offline CO-OPS prediction fixtures), asserting heights within a stated tolerance and high/low times within a few minutes. The repo's docs/provider-apis.md section 5.4 already plans this for XTide.
- For TICON stations, compare against published agency predictions for 3 to 5 gauges where public (e.g. UKHO is not public; use UHSLC or JMA/Australian BOM tables if licensing permits as test fixtures, or raw GESLA records).
- Cross-validate engine against UTide on synthetic data (a known sum of constituents with a known phase convention).
- Unit tests for the Doodson-to-speed table, V0 at known epochs, and nodal f/u vs Schureman tables.
- Round-trip tests of the TCD builder: build, read back via libtcd, compare amplitudes.
- Regression test: Pringle vs NOAA for the 937 shared ids (M2 amplitude and phase should match to rounding) as a data-pipeline sanity check.

### 4.4 Effort estimate

| Task | Effort |
|---|---|
| Option A with the Slackwater TCD (download, pin SHA-256, filter or re-license by station, UI attribution, 3 to 5 golden tests) | 3 to 5 days |
| Option A with our own TCD builder from NOAA + TICON-4 (tool in `tools/`, constituent name/speed mapping, license columns, CI check) | 1.5 to 2.5 weeks |
| Add the Pringle CSV/KML as an extra layer | 1 to 2 days, low value |
| Option B own engine | 2 to 3 weeks beyond the above, plus ongoing maintenance |

## 5. Recommendation

**Complement XTide; do not replace it with Pringle's data.**

1. Keep XTide 2.16 + libtcd 2.2.7-r3 + `harmonics-dwf-*-free` as the baseline for U.S. tides and currents (the only free source of currents).
2. Add a **global tide layer from TICON-4 (+ optionally the Slackwater packaging)**, delivered as a second TCD. Either:
   - download on first use rather than bundling (avoids shipping NC stations and keeps the app small), or
   - bundle a filtered TCD (about 2.6 MB) built in CI from the Slackwater JSON data, with attribution strings.
3. Do not use Pringle's database as a runtime source. Reasons: no license, no versioning, only a Google map export, 4 to 9 constituents, stale GESLA-2 era records, no datums, and its NOAA layer duplicates a coarser copy of NOAA's numbers. It remains useful as an offline validation set for global tide-model comparisons.
4. Since the "free" XTide file is US-only, update plan section 5/provider-apis.md section 5.3 and decision 9 wording: "global" predictions need a second source. v4 (the existing `xtide_stations.csv`, 4,166 stations) is also essentially US-only.

## 6. Open questions for the owner

1. Is non-US tide prediction a v5 requirement? Today the free XTide file gives none. Is CC BY-NC (CMEMS-derived, about 281 stations) acceptable, or must it be excluded? Is the app going to be distributed commercially?
2. Prefer bundling a filtered TCD at build time or downloading a dataset on first use (with SHA-256 pin and cache dir under the app data path)? Dependence on an external hobby project (Slackwater is v1.0.0-beta, 25 stars, MIT) argues for pinning a release and ideally building our own TCD from TICON-4 + NOAA.
3. Do we want to contact William Pringle to ask (a) the license/permission for the map data, (b) whether a newer, updated GESLA-4-based database exists (the owner's recollection "extremely comprehensive" may refer to an unpublished version), and (c) which version his current work uses? The public map I found has about 1,000 NOAA + about 1,800 other pins.
4. Do we also want TICON "River" and "Lake" gauges? (They are not tidal predictions in XTide's sense.)
5. Vertical datum: TICON gives constituents about MSL only. Datum offsets would need the Slackwater-derived datums (computed from GESLA-4 by a third party, with ~0.03 m agreement to agency datums per their README) or "relative to MSL" labelling only. Which is acceptable for display?
6. Should subordinate and current stations stay XTide-only (US), as there is no global equivalent?
7. Should I contact the TICON authors/GESLA about explicit redistribution terms for the TICON-4 CSV (the SEANOE license is CC BY 4.0 but GESLA's own license lists non-commercial sources)?

## 7. Source list (all accessed 2026-10-08)

- https://sites.nd.edu/william-pringle/global-tide-gauge-database/ and the KML export of Google My Map `1yvnYoLUFS9kcB5LnJEdyxk2qz6g`.
- https://data.mendeley.com/datasets/tjyjn56jbf/1 (API: https://data.mendeley.com/public-api/datasets/tjyjn56jbf).
- https://zenodo.org/record/3759896; GMD paper https://gmd.copernicus.org/articles/14/1125/2021/ ; Ocean Modelling paper https://repository.library.noaa.gov/view/noaa/63561 .
- https://github.com/WPringle (repository listing).
- TICON-4: https://www.seanoe.org/data/00980/109129/ (CSV `data/122848.csv`, 47,420,699 bytes, SHA-256 `cea4a8d1...3371c3580`; manual `data/122852.pdf`); TICON paper https://archimer.ifremer.fr/doc/00838/94993 ; DGFI page https://dgfi.tum.de/science-data-products/ticon/ ; "Tide of the Time" preprint https://egusphere.copernicus.org/preprints/2026/egusphere-2026-346/ .
- GESLA license: https://gesla787883612.wordpress.com/license/ .
- Slackwater database: https://github.com/openwatersio/slackwater-database (README, `sources/ticon/README.md`, `packages/tcd/README.md`, `docs/datums.md`); release v1.0.0-beta.20261008, asset `slackwater-20261008.tcd` (2,607,827 bytes, SHA-256 `3e15e655...f03e0d`); engine https://github.com/openwatersio/slackwater (MIT).
- XTide: https://flaterco.com/xtide/files.html ; `https://flaterco.com/files/xtide/harmonics-dwf-20251228-free.tar.xz` (extracted `.tcd` SHA-256 `33f97cb7...eb1`); https://flaterco.com/xtide/harmonics_boilerplate.txt .
