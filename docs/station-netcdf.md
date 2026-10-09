# Station time-series netCDF, format `station-timeseries` 1.0

Normative spec for the single station time-series netCDF format that MetOceanViewer v5 writes
(plan §6 item 14, §2.2). Readers additionally accept the legacy dialects (legacy-formats.md §5).
Keywords MUST/SHOULD/MAY are used in the RFC 2119 sense. "CF §x.y" cites the CF Conventions 1.11
(https://cfconventions.org/Data/cf-conventions/cf-conventions-1.11/cf-conventions.html,
fetched 2026-10-06). "Verified" means exercised on 2026-10-06 with the prototype files in §9
(netCDF4-python 1.7.4, xarray, `cfchecker` 4.1.0, IOOS `compliance-checker` 6.1.0, CF standard
name table v95); "not verified" means expected from documentation only.

Contents: 1 decisions, 2 representation, 3 file format, 4 structure, 5 attributes,
6 quantities, 7 time, 8 missing data / wet-dry, 9 CDL examples, 10 CRS and datum,
11 legacy mapping, 12 reader rules, 13 versioning, 14 tests and compliance, 15 owner questions.

---

## 1. Decisions at a glance

| # | Choice | Why (short) |
|---|---|---|
| D1 | CF-1.11 DSG, `featureType = "timeSeries"`, one file = one collection of stations that share the same set of data variables | owner decision 14 (CF §9, App. H.2) |
| D2 | Two deterministic layouts of one format: **orthogonal** (CF §9.3.1) when every station has an identical time axis, else **incomplete multidimensional** (CF §9.3.2) plus an `obs_count` helper variable | round-trips arbitrary per-station axes; best generic-tool support (§2) |
| D3 | netCDF-4 / HDF5, enhanced data model **but** only CF-classic constructs: no groups, no user types, no `NC_STRING`, no unlimited dimensions; deflate level 2 + shuffle + explicit chunking | padding costs ~nothing on disk, strings stay checker-friendly (§3) |
| D4 | `time` is `double`, `"milliseconds since 1970-01-01 00:00:00"`, calendar `proleptic_gregorian`, always UTC | exact for every integer ms within +/-285,000 years, matches `sys_time<ms>` (§7) |
| D5 | Data are `double`; missing = explicit `_FillValue` (NC_FILL_DOUBLE), never NaN, never a magic number; model "dry" is a separate CF flag variable | no in-band sentinels reach the app (§8) |
| D6 | Station coordinates are always geographic WGS 84 `lon`/`lat` (EPSG:4326), recorded by a `crs` grid-mapping variable | any consumer can place the station; reprojection is upstream (§10) |
| D7 | Strings are `char` arrays (UTF-8) sized to the longest value actually present | passes the official `cfchecker`, no fixed-200 stride bugs [B7][B15] |

---

## 2. Representation

### 2.1 Candidates

| | Orthogonal (CF §9.3.1) | Incomplete multidim. (CF §9.3.2) | Contiguous ragged (CF §9.3.3) | Indexed ragged (CF §9.3.4) |
|---|---|---|---|---|
| Per-station time axes | one shared axis only | any; padded with missing | any | any |
| Time storage | `time(time)` once | `time(station, obs)` | `time(obs)` | `time(obs)` |
| Waste | none | padding to longest series (fill only) | none | none |
| xarray `open_dataset` | `time` becomes an index, `sel(time=...)` works (verified) | 2-D `time` decoded to `datetime64` (verified); no index | raw 1-D arrays; user must split by `row_size` | same, split by index var |
| Panoply / ncview / NCO | plain 2-D variable; all three plot and slice it (not verified) | same | Panoply reads DSGs via NetCDF-Java (not verified); ncview shows one long 1-D line; NCO slicing needs offset arithmetic | same as contiguous |
| Writer needs sizes up front | no | no | yes (we have them) | no |
| Appendable | n/a | n/a | only via unlimited `obs` | yes |

### 2.2 Choice and justification

v4 data fall in three workloads:

1. one fetched or predicted station (NOAA/USGS/NDBC/harmonics): trivially orthogonal;
2. model station output (ADCIRC, D-Flow FM; hundreds to thousands of stations, 1e4-1e6 shared times): orthogonal is
   mandatory here. Incomplete storage would add a full `time` copy per station (8 B x stations x times), and xarray
   would materialize it as `datetime64[ns]` (another 8 B/sample) when loaded;
3. user collections of observed series with different spans and cadences (legacy dialects A/B, IMEDS): different axes.

**Rule (normative).** The writer emits layout L1 (orthogonal) iff there is at least one station, every station has the same
non-empty time vector (same length, bit-identical values), and L2 (incomplete) otherwise. A single station is therefore
always L1. The rule is a pure function of the data, so the same input always yields the same layout (testable, no option).

Why L2 and not ragged for the heterogeneous case:

- netCDF-4 allocates a chunk only when something is written to it, so padding that falls in whole unwritten
  chunks costs no disk space, and deflate collapses partially filled chunks. Disk waste is negligible; the remaining cost
  is the *logical* array size seen by whole-array readers (xarray, Panoply). For this app's data (tens to a few
  thousand stations, longest series ~1e6 samples at 6-minute cadence over decades) that is acceptable.
- Every mainstream tool understands a 2-D `(station, obs)` variable; none of them reconstruct ragged arrays.
- A writer-side escape hatch exists for pathological ratios (§13): a later minor version may emit contiguous ragged
  (L3). Readers SHOULD already accept L3 (§12.3), which is cheap (`row_size` prefix sums) and also lets v5 open foreign
  CF-DSG files.
- `obs_count(station)` (non-CF helper, ignored by CF tools; CF §2.6 allows extra variables) gives our reader the exact
  valid length per station in O(1) instead of scanning a padded time row, which would be O(stations x obs) in the
  pathological case.

Non-goals for 1.0: different time axes per *variable* of the same station (ObsVsPred with different samples is two files,
or one file per `TimeSeries` set), vertical layers/profiles (D-Flow 3-D variables export one chosen layer), trajectories,
station sets that grow by appending, multiple CRSs in a file.

---

## 3. File format

| Aspect | Spec |
|---|---|
| Extension | `.nc` (CF §2.1) |
| Format | `NC_FORMAT_NETCDF4` (HDF5), **not** `..._CLASSIC_MODEL`. Reason: per-variable deflate/shuffle and chunking (§2.2 relies on chunked allocation); a classic file would physically store all padding. Cost: libnetcdf with HDF5 on every platform (vcpkg `netcdf-c[hdf5]`), no `scipy.io.netcdf`. |
| Data-model subset | Only CF §2.2 types `char`, `byte`, `int`, `double`. No `NC_STRING` (reason in D7: `cfchecker` 4.1.0 reports `ERROR (2.2) ... string type (vlen types not supported)`, verified), no groups, no compound/enum/vlen types, no unlimited dimensions, no `_FillValue` on `char` variables. |
| Dimensions | all fixed-size. A dimension of length 0 cannot be defined (`NC_UNLIMITED == 0` in the C API), hence rules in §12. |
| Compression | `nc_def_var_deflate(shuffle=1, deflate=1, level=2)` on every variable with >= 2 dimensions that holds samples (`time` in L2, data, status). Instance variables and the L1 `time` coordinate are contiguous. |
| Chunking | Explicit `nc_def_var_chunking` for sample variables, `(r, c)` with `c = min(n_cols, 65536)`, `r = clamp(65536 / c, 1, n_station)` (about 512 KiB for `double`). L1 and L2 use the same rule on `(station, time)` / `(station, obs)`. Readers MUST NOT depend on chunk shape. |
| Fill mode | `nc_set_fill(NC_FILL)` (default) so unwritten padding reads back as `_FillValue`. |
| Atomic write | Write to a unique temporary file (`.mov-<16 hex>.tmp`) in the same directory, sync and `nc_close`, fsync, then rename over the target; on any error remove the temporary file and leave the old file intact (core-design.md §4.4; the same rule as session save, legacy-formats.md §10.3, [B19]). Never modify a file in place. |
| String encoding | UTF-8 bytes, trailing `NUL` padding, `_Encoding = "utf-8"` (xarray/netCDF4-python convention, harmless to others). Lengths come from `strlen` of the UTF-8 bytes, never `QString::length()` [B15]. |
| Endianness | native; readers use the library's conversion. |

---

## 4. Structure

Naming follows CF §2.3 (ASCII letters, digits, underscore; leading letter).

### 4.1 Dimensions

| Dimension | Size | Present |
|---|---|---|
| `station` | n_station >= 1 (the DSG *instance* dimension, CF §9.2) | always |
| `time` | n_time >= 1 | L1 only |
| `obs` | max over stations of n_s, >= 1 (the DSG *element* dimension) | L2 only |
| `station_id_len`, `station_name_len`, `station_provider_len` | longest UTF-8 byte length present, >= 1 | one per `char` variable that exists |

### 4.2 Variables

`S` = `station`, `T` = `time` (L1) or `obs` (L2). "Req." is for files written by v5; the reader requirements are in §12.

| Variable | Type | Dims | Req. | Attributes (exact) |
|---|---|---|---|---|
| `station_id` | char | `(S, station_id_len)` | yes | `cf_role="timeseries_id"` (CF §9.5), `standard_name="platform_id"`, `long_name="station identifier"`, `_Encoding="utf-8"`. Unique, non-empty. Provider-native id (`"8761724"`, `"USGS-07374000"`). |
| `station_name` | char | `(S, station_name_len)` | yes | `standard_name="platform_name"`, `long_name="station name"`, `_Encoding`. Non-empty (writer substitutes `"Station <id>"` if the source has none). Not required unique. |
| `station_provider` | char | `(S, station_provider_len)` | no | `long_name="data provider"`, `_Encoding`. Tokens: `noaa_coops`, `usgs`, `ndbc`, `harmonics`, `adcirc`, `dflowfm`, `user` (the core `DataSource` enum is authoritative; `harmonics`, the tidal harmonics engine of plan decision 31, replaced `xtide` before 1.0 was released, so 1.0 has never had `xtide`). |
| `lat` | double | `(S)` | yes | `standard_name="latitude"`, `long_name="station latitude"`, `units="degrees_north"`, `axis="Y"` (CF §4.1, §9.5). No missing values. |
| `lon` | double | `(S)` | yes | `standard_name="longitude"`, `long_name="station longitude"`, `units="degrees_east"`, `axis="X"` (CF §4.2). Written in [-180, 180]. No missing values. |
| `elevation` | double | `(S)` | no | `standard_name="altitude"`, `long_name="station elevation above the geoid"`, `units="m"`, `positive="up"`, `axis="Z"`, `_FillValue`. Reserved; v5.0 writes it only if the domain model carries an elevation (CF §4.3). When present it is appended to every `coordinates`. |
| `crs` | int | scalar | yes | see §10 |
| `time` | double | L1 `(time)` coordinate variable; L2 `(station, obs)` auxiliary coordinate | yes | `standard_name="time"`, `long_name="time"`, `units="milliseconds since 1970-01-01 00:00:00"`, `calendar="proleptic_gregorian"`, `axis="T"` (CF §4.4, §4.4.1). L2 additionally `_FillValue = 9.969209968386869e+36` (CF §2.5.1: missing is allowed in auxiliary, not in coordinate variables). |
| `obs_count` | int | `(station)` | L2 only | `long_name="number of valid samples in this station time series"`. Value n_s, 0 <= n_s <= `obs`. Non-CF helper. |
| `<quantity>` (>= 1) | double | `(station, T)` | >= 1 data variable | see §4.3 |
| `<quantity>_status` | byte | `(station, T)` | per §8.2 | see §8.2 |

Dimension order is `(station, T)` in both layouts, so a data variable has the same shape semantics in L1 and L2 (CF §9.3
allows either order; CF §9.3 requires the unlimited dimension, if any, to be the leading one, and we use none).

### 4.3 Data variables

Each data variable carries one physical quantity for all stations (`double`, same `T` axis as every other data variable).

| Attribute | Value | Req. | CF |
|---|---|---|---|
| `_FillValue` | `9.969209968386869e+36` (= `NC_FILL_DOUBLE`), type `double` | yes | §2.5.1 |
| `coordinates` | `"time lat lon station_id station_name"` (+ ` elevation` when present). Identical in L1 and L2 (listing a coordinate variable is optional but permitted, CF §5, H.2.1) | yes | §5, §9.5 |
| `grid_mapping` | `"crs"` (single-word form; see §10 for why not the extended form) | yes | §5.6 |
| `standard_name` | from the registry (§6); omitted for the generic `value` quantity | if registry | §3.3 |
| `long_name` | human label from the registry, or the source label for `value` | yes | §3.2 |
| `units` | UDUNITS string from the registry (§6); for `value` the source unit text, or the attribute is omitted when the unit is unknown | yes | §3.1 |
| `units_metadata` | `"temperature: on_scale"` on temperature variables only (new in CF 1.11) | on temperatures | §3.1.2 |
| `vertical_datum` | token (§10.2); `water_level*` and generic (`value`) quantities; omitted when the datum is unspecified | if known | non-CF, §2.6 |
| `ancillary_variables` | `"<quantity>_status"` when a status variable exists | per §8.2 | §3.4 |
| `comment` | optional free text (e.g. "shifted from MSL to NAVD88 with NOAA VDatum") | no | §2.6.2 |

Data variables MUST NOT use `scale_factor`/`add_offset`/`valid_*`/`missing_value` (reader tolerance in §12.5).

---

## 5. Global attributes

| Attribute | Value | Req. | Notes |
|---|---|---|---|
| `Conventions` | `"CF-1.11"` exactly | yes | CF §2.6.1 requires a string *containing* `CF-1.11`; we write nothing else (no UGRID/ACDD tokens) |
| `featureType` | `"timeSeries"` | yes | CF §9.4: required except for the orthogonal representation, where "highly recommended"; H.2 says it "must be included" for time series; we always write it |
| `title` | caller-supplied, default `"MetOceanViewer station time series"` | yes | CF §2.6.2 |
| `history` | one line `"<date_created>: created by MetOceanViewer <app version> (station-timeseries <format version>)"` | yes | CF §2.6.2. v5 never edits files in place, so there is exactly one line |
| `date_created` | ISO 8601 UTC, `"YYYY-MM-DDThh:mm:ssZ"` | yes | ACDD-style, not a CF attribute |
| `metoceanviewer_format` | `"station-timeseries"` | yes | file-kind detection key (checked before any legacy dialect, §12.1) |
| `metoceanviewer_format_version` | `"1.0"` (string `major.minor`) | yes | §13 |
| `institution` | creator/provider organisation | no | CF §2.6.2 |
| `source` | data origin, e.g. `"NOAA CO-OPS API"`, `"ADCIRC 55.02"`, `"user IMEDS file"` | no | CF §2.6.2 |
| `references` | URLs of provider documentation/terms | no | CF §2.6.2 |
| `comment` | free text | no | CF §2.6.2 |

Global string attributes are written with their byte length (`nc_put_att_text(len = strlen)`), never a fixed buffer
[B8][B15]. Unknown attributes (global or variable) MUST be ignored by readers (CF §2.6).

---

## 6. Quantity registry

The data-variable **name is the quantity token** (this fixes the token even when two tokens share a CF standard name,
e.g. observed and predicted water level). The registry lives in `core` (single source of truth; this table is its 1.0
content). Writers store the *canonical* units below (core converts exactly beforehand, plan §6 item 13); display units are a GUI concern.
Derived quantities (speed/direction from components, plan §2.2/legacy-formats.md §4) are never stored; the one exception is
`difference`, which has no other source. A `difference` has no `standard_name` (the attribute is omitted) and takes the units of the
series it was computed from; it is never a water level, so it carries no datum.

| Token | `standard_name` (all verified present in table v95) | `units` | Source (v4 / provider) |
|---|---|---|---|
| `water_level` | `water_surface_height_above_reference_datum` | `m` | CO-OPS water_level, hourly_height; USGS gage height 00065 (datum `STND`) and tidal elevation 62620; ADCIRC `zeta`, D-Flow `waterlevel` |
| `water_level_prediction` | `water_surface_height_above_reference_datum` | `m` | CO-OPS and harmonics predictions |
| `air_temperature` | `air_temperature` | `degC` | CO-OPS, NDBC ATMP |
| `water_temperature` | `sea_water_temperature` | `degC` | CO-OPS, NDBC WTMP |
| `dew_point` | `dew_point_temperature` | `degC` | NDBC DEWP |
| `wind_speed` | `wind_speed` | `m s-1` | CO-OPS, NDBC WSPD |
| `wind_direction` | `wind_from_direction` | `degree` | CO-OPS, NDBC WDIR; bearing clockwise from north, direction the wind blows *from* |
| `wind_gust` | `wind_speed_of_gust` | `m s-1` | CO-OPS, NDBC GST |
| `wind_u`, `wind_v` | `eastward_wind`, `northward_wind` | `m s-1` | ADCIRC `windx`/`windy`, D-Flow `windx`/`windy` |
| `air_pressure` | `air_pressure` | `hPa` | CO-OPS, NDBC PRES/BAR |
| `relative_humidity` | `relative_humidity` | `percent` | CO-OPS |
| `conductivity` | `sea_water_electrical_conductivity` | `S m-1` | CO-OPS |
| `visibility` | `visibility_in_air` | `m` | CO-OPS, NDBC VIS |
| `current_u`, `current_v` | `eastward_sea_water_velocity`, `northward_sea_water_velocity` | `m s-1` | ADCIRC `u-vel`/`v-vel`, D-Flow `x_velocity`/`y_velocity` (only when the model grid is geographic) |
| `wave_height` | `sea_surface_wave_significant_height` | `m` | NDBC WVHT |
| `wave_period_dominant` | `sea_surface_wave_period_at_variance_spectral_density_maximum` | `s` | NDBC DPD |
| `wave_period_average` | `sea_surface_wave_mean_period` | `s` | NDBC APD |
| `wave_direction` | `sea_surface_wave_from_direction` | `degree` | NDBC MWD |
| `discharge` | `water_volume_transport_in_river_channel` | `m3 s-1` | USGS 00060 (ft3/s converted exactly, 0.028316846592) |
| `difference` | none | the unit of its operands (no fixed canonical unit) | derived: observed minus predicted (residual), written only when a user saves one; `vertical_datum` is never written for it |
| `value` | none | source text, or omitted if unknown | any series whose quantity is unknown (user IMEDS, legacy dialects A/B) |

Why `water_surface_height_above_reference_datum` for every water level, not `sea_surface_height_above_mean_sea_level`
for MSL: the table entry says the datum is an arbitrary reference and its altitude "should be provided in a variable with
standard name `water_surface_reference_datum_altitude`" (unknown for NOAA datums, so omitted); one uniform name keeps the
reader independent of datum. `sea_surface_height_above_geoid` is rejected because tidal datums (MLLW, MHHW) are not geoids.
The datum is carried in `vertical_datum` (§10.2).

Registry growth is a minor-version change (§13). A file may contain tokens outside the registry (foreign or future): the
reader keeps `standard_name`/`long_name`/`units` and treats the quantity as generic.

---

## 7. Time

| Item | Spec |
|---|---|
| Type | `double`. Every integer millisecond with abs(t) < 2^53 is exact; 2^53 ms is year ~287396, and the CF checker/xarray decode `double` universally, while `int64` is unsupported by older ncview/NCO builds (not verified). |
| Units | `"milliseconds since 1970-01-01 00:00:00"`. CF §4.4: the reference time is required; with the zone omitted it defaults to UTC, so no zone suffix is written (`Z`/`T` forms are UDUNITS extensions CF does not promise). CF §4.4 notes `since` is the recommended keyword. |
| Calendar | `proleptic_gregorian` (CF §4.4.1) = the `std::chrono` civil calendar; no ambiguity for pre-1582 dates (v4 valid-date lower bound is 1900). CF §4.4.1 ignores leap seconds in all calendars, which equals `sys_time` semantics. |
| Writer | Writes `static_cast<double>(ms_since_epoch)`. Times MUST be strictly increasing within each station (CF Table 9.1: "strict monotonically increasing"). The writer cannot see anything else: a `core::StationTable` holds strictly increasing axes by construction, so de-duplication and sorting happen upstream (`core::normalize` in the readers, the provider layer). |
| L1 | `time(time)` is a coordinate variable: no missing values (CF §2.5.1), strictly increasing, the same for all stations. |
| L2 | `time(station, obs)`: valid samples are the first `obs_count[s]` entries and strictly increasing; the remaining entries are `_FillValue` (CF §9.6: unused elements of data and auxiliary coordinates must be missing). |
| Reader | Accept any CF time `units` of the form `<seconds, minutes, hours or days> since <date>[ T<time>][ zone]` (parser spec: legacy-formats.md §4 "Intended parser"), calendars `standard`, `gregorian`, `proleptic_gregorian` (and absent = `standard`); other calendars => `UnsupportedCalendar`. Convert with `ref + llround(value * unit_ms)`; non-finite times are errors. A pre-1582 date in `standard`/`gregorian` => error (mixed calendar is not reproduced). |
| Time zone | Always UTC in the file. Display zones are a GUI concern (plan §2.2). Legacy `timezone` attribute handling: §11. |

---

## 8. Missing data and wet/dry

### 8.1 Missing

- Writer: a missing sample is written as the variable's `_FillValue`. NaN and +/-Inf are never written (writer returns
  `NonFiniteValue` for them: they indicate an upstream bug, not missing data).
- Reader: sample is missing iff its value equals the variable's `_FillValue` (compared at the variable's own type), is NaN/Inf,
  or lies outside `valid_min`/`valid_max`/`valid_range` when present (CF §2.5.1). If no `_FillValue` exists, the library default
  for the variable type applies (`nc_inq_var_fill`). Missing becomes `std::nullopt`; **no fill value, `-99999`, `-999`, or
  `-DBL_MAX` is ever visible to `core`** (plan §2.2) and there are no magic-number thresholds in the reader (fixes [B9]).
- A station may be entirely missing for one variable (sensor absent) while present in others.

### 8.2 Wet/dry for model output

v4 encodes "dry" as `-99999` in ADCIRC output and the app tests `<= -999` (decision 16). In v5 the reader boundary converts
those to an explicit `Dry` state; the file stores it as a CF status flag, not as a number (CF §3.4, §3.5, Example 3.5):

| Item | Spec |
|---|---|
| When | Written for a data variable iff at least one of its samples is `Dry` (model water levels from the ADCIRC readers, or any column a user built that way: `Dry` is allowed on any quantity). Absent => no sample is dry. |
| Variable | `byte <quantity>_status(station, T)` e.g. `water_level_status` |
| Attributes | `_FillValue=-128b`, `standard_name="status_flag"`, `long_name="<label> wet/dry status"`, `flag_values=0b,1b`, `flag_meanings="dry wet"`, `valid_range=0b,1b`. The data variable gets `ancillary_variables="<quantity>_status"`. |
| Sample states | status 0 (dry): data MUST be `_FillValue`. status 1 (wet): data MUST be a valid value. status missing (`-128`): unclassified; data is a value or missing. L2 padding: status is `-128` (CF §9.6). |
| Reader | The two MUST-rules above are validation errors if violated (`WetDryInconsistent{station, index}`). The in-memory sample is `Value(x)`, `Dry` or `Missing`. A dry sample never carries a number. |
| Reading legacy numeric sources | The ADCIRC model-output readers (ASCII and netCDF elevation) apply decision 16 (`value <= -999` is dry) once, at their boundary, before anything reaches this writer. D-Flow has no dry sentinel (a dry station reports bed level, which can lie below −999 m), so the rule does not apply to it. |

---

## 9. CDL examples

Both files were produced by the prototype generator and passed the checks in §14.2 (values illustrative). `ncdump` prints
`_` for missing/fill values. Chunking and deflate are not shown by `ncdump -h`; they follow §3.

### 9.1 Incomplete layout (L2): two stations, 3 and 5 samples, different time axes

```
netcdf station_timeseries_incomplete {
dimensions:
	station = 2 ;
	station_id_len = 7 ;
	station_name_len = 28 ;
	station_provider_len = 10 ;
	obs = 5 ;
variables:
	char station_id(station, station_id_len) ;
		station_id:long_name = "station identifier" ;
		station_id:standard_name = "platform_id" ;
		station_id:cf_role = "timeseries_id" ;
		station_id:_Encoding = "utf-8" ;
	char station_name(station, station_name_len) ;
		station_name:long_name = "station name" ;
		station_name:standard_name = "platform_name" ;
		station_name:_Encoding = "utf-8" ;
	char station_provider(station, station_provider_len) ;
		station_provider:long_name = "data provider" ;
		station_provider:_Encoding = "utf-8" ;
	double lat(station) ;
		lat:standard_name = "latitude" ;
		lat:long_name = "station latitude" ;
		lat:units = "degrees_north" ;
		lat:axis = "Y" ;
	double lon(station) ;
		lon:standard_name = "longitude" ;
		lon:long_name = "station longitude" ;
		lon:units = "degrees_east" ;
		lon:axis = "X" ;
	int crs ;
		crs:grid_mapping_name = "latitude_longitude" ;
		crs:longitude_of_prime_meridian = 0. ;
		crs:semi_major_axis = 6378137. ;
		crs:inverse_flattening = 298.257223563 ;
		crs:crs_wkt = "GEOGCRS[\"WGS 84\",DATUM[\"World Geodetic System 1984\",ELLIPSOID[\"WGS 84\",6378137,298.257223563,LENGTHUNIT[\"metre\",1]]],PRIMEM[\"Greenwich\",0,ANGLEUNIT[\"degree\",0.0174532925199433]],CS[ellipsoidal,2],AXIS[\"geodetic latitude (Lat)\",north,ORDER[1],ANGLEUNIT[\"degree\",0.0174532925199433]],AXIS[\"geodetic longitude (Lon)\",east,ORDER[2],ANGLEUNIT[\"degree\",0.0174532925199433]],ID[\"EPSG\",4326]]" ;
		crs:epsg_code = "EPSG:4326" ;
	double time(station, obs) ;
		time:_FillValue = 9.96920996838687e+36 ;
		time:standard_name = "time" ;
		time:long_name = "time" ;
		time:units = "milliseconds since 1970-01-01 00:00:00" ;
		time:calendar = "proleptic_gregorian" ;
		time:axis = "T" ;
	int obs_count(station) ;
		obs_count:long_name = "number of valid samples in this station time series" ;
	double water_level(station, obs) ;
		water_level:_FillValue = 9.96920996838687e+36 ;
		water_level:coordinates = "time lat lon station_id station_name" ;
		water_level:grid_mapping = "crs" ;
		water_level:standard_name = "water_surface_height_above_reference_datum" ;
		water_level:long_name = "water level" ;
		water_level:units = "m" ;
		water_level:vertical_datum = "MLLW" ;
		water_level:ancillary_variables = "water_level_status" ;
	byte water_level_status(station, obs) ;
		water_level_status:_FillValue = -128b ;
		water_level_status:standard_name = "status_flag" ;
		water_level_status:long_name = "water level wet/dry status" ;
		water_level_status:flag_values = 0b, 1b ;
		water_level_status:flag_meanings = "dry wet" ;
		water_level_status:valid_range = 0b, 1b ;
	double water_temperature(station, obs) ;
		water_temperature:_FillValue = 9.96920996838687e+36 ;
		water_temperature:coordinates = "time lat lon station_id station_name" ;
		water_temperature:grid_mapping = "crs" ;
		water_temperature:standard_name = "sea_water_temperature" ;
		water_temperature:long_name = "water temperature" ;
		water_temperature:units = "degC" ;
		water_temperature:units_metadata = "temperature: on_scale" ;

// global attributes:
		:Conventions = "CF-1.11" ;
		:featureType = "timeSeries" ;
		:title = "Example" ;
		:institution = "X" ;
		:source = "NOAA CO-OPS API" ;
		:history = "2026-10-06T12:00:00Z: created by MetOceanViewer 5.0.0 (station-timeseries 1.0)" ;
		:date_created = "2026-10-06T12:00:00Z" ;
		:metoceanviewer_format = "station-timeseries" ;
		:metoceanviewer_format_version = "1.0" ;
data:

 station_id = "8761724", "8760922" ;

 station_name = "Grand Isle, LA", "Pilots Station East, SW Pass" ;

 station_provider = "noaa_coops", "noaa_coops" ;

 lat = 29.2633, 28.9322 ;

 lon = -89.9567, -89.4067 ;

 time =
  1700000000000, 1700000360000, 1700000720000, _, _,
  1700000000000, 1700000900000, 1700001800000, 1700002700000, 1700003600000 ;

 obs_count = 3, 5 ;

 water_level =
  0.5, 0.6, 0.7, _, _,
  1, _, 1.2, 1.3, 1.4 ;               // station 2 sample 2 is dry: value missing, status 0

 water_level_status =
  1, 1, 1, _, _,
  1, 0, 1, 1, 1 ;

 water_temperature =
  20, 21, 22, _, _,
  _, _, _, _, _ ;                     // station 2 has no temperature sensor
}
```

Note: `crs:crs_wkt` is a single line in the file. The header above is what `ncdump -h` (netCDF-C 4.9.2) prints for the v5
writer's file of this table (WP10a, 2026-10-07); ncdump prints double attributes with 15 significant digits, so the fill
value `9.969209968386869e+36` appears as `9.96920996838687e+36`. The full headers of both files are committed as
`tests/fixtures/io/station_netcdf/*.cdl` and checked by the golden tests and the `format-compliance` job (§14).

### 9.2 Orthogonal layout (L1): two stations sharing four times

Differences from 9.1 only (all other variables, attributes and the `crs` variable are identical):

```
netcdf station_timeseries_orthogonal {
dimensions:
	station = 2 ;
	station_id_len = 7 ;
	station_name_len = 28 ;
	station_provider_len = 10 ;
	time = 4 ;                                  // `obs` is absent
variables:
	/* station_id ... crs: as in 9.1 */
	double time(time) ;                         // coordinate variable, no _FillValue, no obs_count
		time:standard_name = "time" ;
		time:long_name = "time" ;
		time:units = "milliseconds since 1970-01-01 00:00:00" ;
		time:calendar = "proleptic_gregorian" ;
		time:axis = "T" ;
	double water_level(station, time) ;         // attributes as in 9.1
	byte water_level_status(station, time) ;    // as in 9.1
	double water_temperature(station, time) ;   // as in 9.1
// global attributes: as in 9.1
data:
 time = 1700000000000, 1700000360000, 1700000720000, 1700001080000 ;
 water_level =
  0.5, 0.6, 0.7, 0.8,
  1, _, 1.2, 1.3 ;
 water_level_status =
  1, 1, 1, 1,
  1, 0, 1, 1 ;
 water_temperature =
  20, 21, 22, 23.5,
  _, _, _, _ ;
}
```

Prototype results (verified): `xarray.open_dataset` decodes both files; L1 gives `time` as a `datetime64` index, L2 a 2-D
`time(station, obs)`; strings come back as `str`, fills as NaN, `station_id`, `station_name`, `lat`, `lon`, `time` become
coordinates. xarray promotes the byte status flag to float32 because of its `_FillValue` (usual xarray behavior).

---

## 10. CRS, vertical datum, elevation

### 10.1 Horizontal CRS

| Item | Spec |
|---|---|
| Written coordinates | `lon`/`lat` in EPSG:4326 (WGS 84), degrees. A station whose native CRS is projected (legacy `HorizontalProjectionEPSG != 4326`, e.g. 26915) is projected to 4326 by the reader that read it, in `io` (PROJ is a private dependency of `mov_io`; `core` has no PROJ), which keeps the native point beside the WGS 84 `Location`; the writer writes the `Location` and does **not** retain the native coordinates and EPSG (owner question Q4; `native_position_dropped`). |
| `crs` variable | scalar `int`, no data. `grid_mapping_name="latitude_longitude"` (CF App. F), `longitude_of_prime_meridian=0.0`, `semi_major_axis=6378137.0`, `inverse_flattening=298.257223563` (single-property attributes, which CF §5.6.1 says should accompany `crs_wkt`), `crs_wkt` (constant WKT2 string shown in §9.1; CF §5.6.1), `epsg_code="EPSG:4326"` (our non-CF attribute: CF 1.11 has no EPSG attribute; the code lives inside `crs_wkt` as `ID["EPSG",4326]` and `epsg_code` saves the reader a WKT parse). |
| Data variable link | `grid_mapping = "crs"`, the single-word form (CF §5.6). The extended form `"crs: lat lon"` is legal CF (§5.6, needed only to fix axis order for `crs_wkt`), but the IOOS checker 6.1.0 reports `grid mapping variable crs: must exist in this dataset` for it (verified), so we avoid it. Axis order for consumers is `lon, lat` by CF convention. |
| Reader | Resolve the CRS from, in order: `crs`-variable (the one named by `grid_mapping`) `epsg_code` (`^EPSG:[0-9]+$`); else `grid_mapping_name="latitude_longitude"` with WGS 84 ellipsoid parameters => 4326; else no `grid_mapping` at all => assume 4326 and emit warning `W-CRS-ASSUMED`; else `UnsupportedCrs`. Any *geographic* EPSG with `epsg_code` is projected to WGS 84 by the reader (`io`, PROJ), the native point kept; projected `grid_mapping_name`s (CF App. F) => `UnsupportedCrs` in 1.0. |
| Range | `lat` in [-90, 90], `lon` in [-180, 360]; reader normalizes to (-180, 180]. NaN/fill => `MalformedFile`. |

### 10.2 Vertical datum

`vertical_datum` (variable attribute, `water_level*` and generic `value` quantities) holds the `VerticalDatum` enum token, upper case:
`MLLW`, `MLW`, `MSL`, `MTL`, `MHW`, `MHHW`, `NGVD29`, `NAVD88`, `IGLD85`, `STND` (station/gauge datum). The enum in `core` (`VerticalDatum`) is authoritative; no datum is
represented by **omitting** the attribute, never by `"none"`. Unknown token on read => warning `W-DATUM-UNKNOWN`, datum treated as unspecified (no guessing; the text is kept in
diagnostics). Datum shifts are applied in `core` (decision 18), never in the reader/writer. The attribute is not CF; CF's own channel for a vertical datum is a compound `crs_wkt`
or `geopotential_datum_name` (App. F), which cannot express tidal datums and would imply a geoid relation; we keep one explicit attribute instead.

### 10.3 Elevation

Optional `elevation(station)` as in §4.2. v4 has no station elevation in time-series data (HWM ground elevation is a different
file type, legacy-formats.md §6); the variable exists so the format does not need a major bump when one appears.

---

## 11. Legacy dialect mapping (reader side, legacy-formats.md §5)

"NNNN" is the 1-based, zero-padded station number (`%04i` in A/B, `%06i` in C; both widths accepted by the reader; widening beyond 9999 is handled by
lookup, never by assuming a width). All legacy values become the v5 in-memory model first; only the v5 writer ever produces the new layout. Legacy fills are masked per §8.1
(no `-99999` heuristic, [B9]).

| New field | A: `Hmdf::writeNetcdf` | B: `NetcdfTimeseries` reader | C: CRMS | Conversion |
|---|---|---|---|---|
| Detection | variable `time_station_0001` | same | `time_station_000001` | Order: `metoceanviewer_format` attribute (new) -> A/B -> C |
| `station` dim | `numStations` | `numStations` (required) | `nstation` | station i = number NNNN minus 1 |
| `station_id` | `stationId` char `(numStations, 200)` | not read | none (use NNNNNN) | trimmed (trailing NUL/space); missing => `station_name`, else the decimal index (`station_id_substituted` when the file has `stationId`); duplicates get `#2`, `#3` suffixes (legacy files are lenient, new files are not, §12.4) |
| `station_name` | `stationName` char | `stationName` | attribute `station_name` on data/time var | row stride = actual `stationNameLen` [B7] (A hard-codes 200), trim NUL/space; B keeps legacy `simplified()` whitespace collapse (legacy-formats.md A8) |
| `lat`, `lon` | `stationYCoordinate`, `stationXCoordinate` double `(numStations)` | same | none in file | in the file CRS; EPSG from attribute `HorizontalProjectionEPSG` on `stationXCoordinate` (A writes 4326), absent => 4326 + warning (A7); projected to 4326 by the reader (`io`, PROJ) |
| `time` (per station) | `time_station_NNNN` int64 `(stationLength_NNNN)` seconds since `referenceDate` | same | same name, 6 digits, absolute epoch seconds (`reference` attribute ignored) | `ms = (ref_seconds + value) * 1000`; `referenceDate` sized from `nc_inq_attlen` (not 80 bytes) [B8], 19-char `yyyy-MM-dd hh:mm:ss`, UTC, absent => 1970-01-01; A truncates `ms/1000` toward zero, so sub-second parts are lost in legacy files |
| `timezone` (attr on time var) | `"utc"` | ignored | n/a | present and not `utc`/`gmt` (case-insens.) => warning `W-TZ-ASSUMED-UTC`, treated as UTC like v4. Text after the 19 characters of `referenceDate` (`Z`, `+02:00`, `local`) is said the same way, with the text as the subject; blanks, NULs and `utc`/`gmt` after the date say nothing |
| samples | `data_station_NNNN` double `(stationLength_NNNN)` | same (float or double, typed read [B4]) | `data_station_NNNNNN` float `(numParam, stationLength_NNNNNN)` | quantity `value`; C: one data variable per `sensors` row, token = sanitized sensor name, quantity `value` |
| `units` | data-var attribute `units` | not read | none | `units` (A text such as `m`); empty => omitted |
| `vertical_datum` | data-var attribute `datum` (`MLLW`, `NAVD88`, ..., `none`) | not read | none | token via `VerticalDatum::from_string` (case-insensitive, includes `MHW`, fixes [N8]); `none`/empty => omitted |
| fill | none written; unwritten elements = default `NC_FILL_DOUBLE` | `nc_inq_var_fill`, with a bogus `-99999` fallback [B9] | `-9999.0f`, reader keeps `v > -9999` | typed `_FillValue` attribute else type default; NaN also missing |
| `elevation` | n/a | n/a | n/a | not produced |
| per-station length | `stationLength_NNNN` dim | same | same | valid count; layout re-decided by §2.2 on write |
| dropped | `StationName`/`StationID` data-var attrs, `source`, `creation_date`, `created_by`, `host`, `netCDF_version`, `fileformat="20180123"`, `numParam`, `stringsize`, `minimum`/`maximum`, `HorizontalProjectionName` | | | not carried over; reader may log them in diagnostics |

A legacy file's origin (`LegacyOrigin`) records what the file has, not a label: the global `fileformat` text if any, whether it has a `stationId` variable, and the digits of its
station numbers (four or six). The v4 writer and the v4 reader's files read the same way, so there is no A/B distinction in the reader. The `HorizontalProjectionEPSG` attribute
is any signed integer type (byte, short, int, int64; v4 writes int); text, floating-point and unsigned types are `NcError type_mismatch`, never a code [B10]. A series whose times
are not strictly increasing is put in order as in §12.5 (`times_reordered`, `duplicate_times_dropped`, `conflicting_duplicate_times`; decision 30.3). The errors and
the order of the warnings are in §12.9.

Dialect C has no coordinates (they came from the embedded CRMS station CSV, legacy-formats.md §8) and CRMS is removed (decision 7), so a
C file can only be opened if the caller supplies coordinates; see Q5.

Model-output sources (not dialects, each with its own reader) map into this format as: ADCIRC `zeta` -> `water_level` + `water_level_status`, `u-vel`/`v-vel` ->
`current_u`/`current_v`, `pressure` -> `air_pressure`, `windx`/`windy` -> `wind_u`/`wind_v`; D-Flow his `waterlevel` etc. likewise; ADCIRC `x`/`y`
-> `lon`/`lat`; ADCIRC/D-Flow `station_name` -> `station_name` (`station_id` = decimal 0-based index, legacy-formats.md §3.2/§4).

Session files (`.mvs`, legacy-formats.md §10) reference data files by `timeseries_filetype`: legacy `2 (NETCDF_GENERIC)` opens through the A/B reader; the v5 JSON
session names the new kind `station_netcdf`, detected by `metoceanviewer_format`.

---

## 12. Reader requirements and validation

Reader entry points (`station_netcdf.hpp`): `read_station_netcdf(path, which, ctx[, options]) -> std::expected<Read<StationFile>, Error>` and
`inspect_station_netcdf(path, ctx) -> std::expected<Read<StationNcCatalog>, Error>`. Each opens the file once and closes it on every path. A failure is an `io::Error`
(a `FormatError` with the codes of §12.9, a `ParseError`, an `NcError` with the library status and the variable, a `FileError`, or `Cancelled`); the warnings come with a
successful result (`Read`).

### 12.1 Detection and version gate

1. File opens as netCDF (else `NotNetcdf`).
2. The kinds are tried in this order (`detect_file`, WP10b; the station reader dispatches on the same rule, `classify_netcdf`, and a foreign file's CF version is the one the rule found):
   global `metoceanviewer_format == "station-timeseries"`: this spec; a file that names any other `metoceanviewer_format` is none of ours (`other_format_netcdf`, with its name; it is not guessed to be foreign CF);
   global `model == "ADCIRC"` or the D-Flow FM station coordinate variables: the model-output readers, even when the file carries CF attributes;
   `featureType` is `timeSeries` (case-insensitive) and `Conventions` has a `CF-1.N` token with N >= 6: generic
   CF-DSG file, same rules as below with `W-FOREIGN-CF` and every "written by us" requirement relaxed to the *Foreign* column;
   variable `time_station_0001` (or `time_station_000001` with `numStations` and `stationXCoordinate`): the legacy dialects (legacy-formats.md §1.1, §5).
   Anything else, CRMS (dialect C) included, is `unsupported_netcdf` to `detect_file` and `not_this_format` to the station reader (its subject names the attribute that decides:
   `model`, `station_x_coordinate`, `:metoceanviewer_format`, `:featureType`, `:Conventions`). A v5 file whose `metoceanviewer_format` attribute was removed is therefore read as foreign CF.
   Text files are `unrecognized_text` when none of the text readers recognizes them. netCDF-4 is recognized behind an HDF5 user block (the signature at byte 512 * 2^k).
   An IMEDS file is recognized by a first line that is a `%` comment naming IMEDS, or by its station block after the three header lines; ADCIRC ASCII output comes with its parsed
   header (`AdcircAsciiDetected`).
3. `Conventions` is tokenized on blanks/commas (CF §2.6.1) and must contain `CF-1.6` or later. Later CF versions do not invalidate earlier usage (CF §2.6.1), so `CF-1.12` is accepted.
4. `metoceanviewer_format_version`: see §13.

### 12.2 Required structure (error code on failure)

| Rule | v5 file | Foreign CF file |
|---|---|---|
| Dimension `station` exists, size >= 1, fixed | required (`MissingStructure`) | the dimension of the variable with `cf_role=timeseries_id`; if none, the leading dim of the data variables |
| Exactly one variable has `cf_role="timeseries_id"`, values unique and non-empty | required (`NoStationId`, `DuplicateStationId{id}`) | two such variables: `AmbiguousStationId` naming both; duplicates uniquified with warning; an empty, NULL or masked id is the station's decimal index (`W-STATION-ID-SUBSTITUTED`); float ids are `BadEncoding` |
| `lat`/`lon` identified by `units` string match (CF §4.1/§4.2) or `standard_name`, dim `(station)`, finite, in range | required (`BadCoordinates{station}`) | same |
| `time` identified by units (CF §4.4); L1 = 1-D coordinate; L2 = 2-D `(station, obs)` | required | the best ranked variable with time units (neither a scalar nor a vector over the stations): the coordinate variable of its dimension (its name is its dimension's), then one named in a `coordinates` attribute, then one with `standard_name` `time` or `axis` T, then one with only time units; two of the same rank are `UnsupportedLayout` naming both |
| `station_name` | required | a text variable over the station dimension with `standard_name="platform_name"`, else one called `station_name`, else none (the names stay empty); one that does not fit the station dimension is skipped (`W-SKIPPED-VARIABLE`) |
| `featureType`, `Conventions`, format attributes (§5) | required | `featureType` required, others optional |
| >= 1 data variable: numeric, dims exactly `(station, T)` in that order (either order accepted from foreign files; a transposed variable is transposed on read) | required (`NoDataVariables`) | same; variables with other dims, and those whose masking attributes cannot be read (`W-SKIPPED-VARIABLE`, subject `<variable>:<attribute>`), are skipped; `NoDataVariables` when none is left |
| every `ancillary_variables` target that exists has the same dims | required (`BadAncillary`) | same |

### 12.3 Layouts

| Layout | Recognition | Support |
|---|---|---|
| L1 orthogonal | `time` is 1-D with the time dimension, data `(station, time)` or `(time, station)` | MUST |
| L2 incomplete | `time` is 2-D `(station, obs)` (or transposed) | MUST |
| L3 contiguous ragged | integer variable with `sample_dimension` attribute whose only dim is the instance dim (CF §9.3.3); samples on that dim; the counts must be non-negative, unmasked and add up to the sample dimension (`bad_row_size`) | MUST (WP10b; the writer never emits it in 1.0) |
| L4 indexed ragged | variable over the sample dimension with an `instance_dimension` attribute naming the instance dimension (CF §9.3.4); every index must be in `[0, stations)` and unmasked (`bad_ragged_index`) | MUST (WP10b; was `UnsupportedLayout` in the first draft) |
| Scalar single station (instance dim omitted, CF §9.2) | no `station` dimension, scalar `lat`/`lon`, the id (if any) a scalar string or one char row | MUST (WP10b; treated as one station; no id means the id `0`) |

### 12.4 Value validation

- L1: `time` strictly increasing, no missing (`NonMonotonicTime`). Strictness mirrors CF Table 9.1. (v5 files only: a foreign or legacy series that is not increasing is put in order, §12.5.)
- L2: for each station s, n_s = `obs_count[s]` (v5 file; must satisfy 0 <= n_s <= `obs`, else `BadObsCount`) or, if absent (foreign), the number of leading non-missing times. `time[s, 0..n_s)` strictly increasing and finite; if n_s < `obs` then `time[s, n_s]` MUST be missing, and in a v5 file all later entries
  of `time` and of every data/status variable MUST be missing (`PaddingNotMissing{station}`). `PaddingCheck::boundary` (the default) checks what the read goes through anyway: a
  group of stations is read as far as its longest selected station's samples plus one element, and everything read past a station's own samples is checked, so the first padding
  element of every station is; `PaddingCheck::whole` (tests, files of unknown origin) checks all of it and costs selected stations x `obs` against `ReadLimits`. A foreign file with
  `obs_count` follows the same option, the matrix in either order (a (time, station) matrix is read as many time steps as the stations' samples reach, never the whole dimension).
  A foreign file without `obs_count` has its samples counted as the leading non-missing times of each station, which looks at every element: a non-missing time after a missing one
  is `PaddingNotMissing`, and there is nothing left for the option to check.
- Strings: valid UTF-8, no embedded NUL (trailing NULs trimmed) else `BadEncoding{variable}` for v5 files; foreign/legacy files are decoded leniently (invalid bytes replaced with U+FFFD, warning).
  `NC_STRING` variables are accepted from foreign files; `char` arrays are required of v5 files.
- Wet/dry: `WetDryInconsistent` per §8.2. Flag value outside `flag_values` => `BadFlag`.
- Units: unit text is mapped through the core unit table; an unrecognized string is returned verbatim as a generic unit, never guessed or converted.
- Data variable dtype must be `float`/`double`/integer; `char`/`string` data variables are skipped.

### 12.5 Tolerated in foreign files (never written by v5)

`scale_factor`/`add_offset` (applied after the missing test, CF §2.5.1 and §8.1, one test), `missing_value`, `valid_min`/`valid_max`/`valid_range`, float32 data, `int` times, other time units (§7),
`NC_STRING` strings, extra `coordinates` entries, extra global attributes, extra variables, `bounds` variables (ignored). Beyond that, decision 30 of the plan:

- **Quality flags.** The integer `ancillary_variables` targets of a data variable (same dimensions) are flags, not series. `flag_values` with `flag_meanings` of the same number: a meaning
  that contains `bad`, `fail` or `missing` (any case) masks the samples with that flag (they become Missing, `W-FLAGGED-SAMPLES-MASKED`, count); one that contains `suspect` keeps them and counts them
  (`W-SUSPECT-SAMPLES-KEPT`). `flag_values` of the QARTOD set (1, 2, 3, 4, 9) without meanings: 4 and 9 mask, 3 is suspect. Anything else (no `flag_values`, floating-point or text
  `flag_values`, a meaning count that does not match, other numbers without meanings) is an unknown scheme: ignored with `W-QUALITY-FLAGS-IGNORED`, once per flag variable. A scheme that
  masks and counts nothing is read and says nothing. An ancillary target that is not whole numbers is skipped (`W-SKIPPED-VARIABLE`).
- **Datum.** `vertical_datum` of the variable, else its `geopotential_datum_name`, else the `geopotential_datum_name` of its `grid_mapping` variable. The text is a token (`NAVD88`, `MLLW`, ...), an
  alias or a long name (`North American Vertical Datum of 1988`, `mean lower low water`, `mean sea level`, `National Geodetic Vertical Datum of 1929`, `International Great Lakes Datum 1985`, `station
  datum`). Any other text is no datum, with `W-DATUM-UNKNOWN`; other attributes (`comment`) are never parsed.
- **Out-of-order and repeated times.** A series is stably sorted by time and, of equal times, the first row is kept (`W-TIMES-REORDERED`: descents, `W-DUPLICATE-TIMES-DROPPED`,
  `W-CONFLICTING-DUPLICATE-TIMES`: dropped rows that differ), as IMEDS does. The shared axis of an orthogonal file is sorted for all stations together. A catalog (`inspect`) counts
  what the file holds; a read can return fewer samples.
- **Quantities.** A standard name that is a registry quantity's, with a unit that converts to the canonical one (Kelvin is a unit of core), gives that quantity. The two water levels share
  `water_surface_height_above_reference_datum`: a variable whose name or `long_name` contains `predict`, `tide`, `astronomical` or `harmonic` is `water_level_prediction`, any other
  `water_level`; a variable whose quantity is taken (or whose unit does not convert) is generic with `W-UNKNOWN-QUANTITY`. Generic tokens are the variable names where those are columns the
  writer would write (not a name the format uses, nor `<token>_status` of a column, nor 250 bytes or more); the others get substitutes (`W-VARIABLE-RENAMED`, subject: the variable's name),
  made after every variable that can keep its name has kept it.

### 12.6 Hard errors never downgraded to warnings

Missing `time` units/`since`; unparsable reference date; unsupported calendar; unknown major format version; zero stations; lat/lon missing; duplicate station ids in a v5 file; dimension/variable mismatch of any data
variable; any libnetcdf error (returned with its code and variable name).

### 12.7 Warnings (non-fatal, surfaced in the log/diagnostics)

`W-FOREIGN-CF`, `W-CRS-ASSUMED`, `W-DATUM-UNKNOWN`, `W-TZ-ASSUMED-UTC`, `W-SKIPPED-VARIABLE`, `W-MINOR-NEWER` (§13), `W-UNKNOWN-PROVIDER`, `W-UNKNOWN-QUANTITY`, `W-LEGACY-DIALECT`,
`W-VARIABLE-RENAMED`, `W-STATION-ID-SUBSTITUTED`, `W-QUALITY-FLAGS-IGNORED`, `W-FLAGGED-SAMPLES-MASKED`, `W-SUSPECT-SAMPLES-KEPT`, `W-TIMES-REORDERED`, `W-DUPLICATE-TIMES-DROPPED`,
`W-CONFLICTING-DUPLICATE-TIMES`.
The order of each kind's warnings is in §12.9; a test pins each order (`foreign: the warnings come in the order SN 12 documents`, `legacy: the warnings come in
the order SN 11 documents`).

### 12.8 Writer preconditions (all return `expected` errors, never throw, never write a partial file)

Most of this list is ruled out by the types the writer takes: a `core::StationTable` has unique non-empty UTF-8 ids, strictly increasing axes within +/-(2^53 - 1) ms, finite
samples, valid `Location`s and columns as long as their axis (core-design.md §2.8). What remains is checked by `validate_station_netcdf` before any file is created, as `FormatErrc`
values: `empty_collection` (0 stations), `no_data_variables` (no columns), `no_samples` (no station has a sample, because a dimension of length 0 cannot be defined),
`too_many_samples` (a station with more samples than the `int` `obs_count` can hold), `noncanonical_unit` (a registry quantity other than `difference` without a unit, or with one
that does not convert to its canonical unit), `invalid_variable_name` (a generic token that is a name the format uses, `<token>_status` of any column, or longer than 249 bytes),
`bad_option` (a global attribute text that is not UTF-8, holds a NUL, is longer than 64 KiB, or an empty `title`); then the I/O errors of the atomic write (`FileError`, `NcError`
with the library status and the variable). A single station with zero samples in a multi-station set is legal (L2, `obs_count = 0`, an all-fill row).

### 12.9 The reader's errors and warnings, kind by kind

`read_station_netcdf` and `inspect_station_netcdf` (`station_netcdf.hpp`) report a failure as an `io::Error` and what they noticed as `io::Warning`s. This section
names both by their tokens (`FormatErrc`, `WarningCode`) and is the reference the header points to; the `W-…` and CamelCase names above are this document's older
spellings of the same codes. `inspect` reports what `read` reports, except what concerns samples (a foreign incomplete layout without `obs_count` reads its times to count
each station's samples, so those are checked by `inspect` too). Subjects taken from the file are cut to 120 bytes on a UTF-8 boundary. Every kind can also fail with
`station_count_mismatch` (a selection made for another station count), an `NcError` (any library failure, with its status and variable; `too_large` when the samples a read
would hold exceed `ReadLimits`: the selected stations' samples, or selected stations × `obs` with `PaddingCheck::whole`) and `Cancelled`. A station without a name keeps an
empty one in every kind; only the v5 writer substitutes `"Station <id>"`.

**v5.** Every rule is a hard error; none is downgraded.

- Header: global `metoceanviewer_format` = `"station-timeseries"` (else `not_this_format`: another kind of file); `metoceanviewer_format_version` present and
  `"<major>.<minor>"` (`bad_version`), major 1 (`unsupported_version`); `Conventions` with a `CF-1.N` token, N ≥ 6 (`missing_attribute`, `unsupported_version`; a CF-2 or
  later token too); `featureType` timeSeries, any case (`missing_attribute`, `unsupported_layout`).
- Structure: dimension `station` (`missing_dimension`); no unlimited dimension (`unsupported_layout`, subject: the dimension); exactly one variable with `cf_role`
  timeseries_id (`no_station_id`), char over (station, n) (`bad_encoding`, `dimension_mismatch`); `station_name`, `lat`, `lon` (`missing_variable`); `time` with `units`
  (a CF time unit, else a `ParseError`) and a supported `calendar` (`unsupported_calendar`); `time(time)` for the orthogonal layout, `time(station, obs)` plus
  `obs_count(station)` for the incomplete one (`unsupported_layout`, `dimension_mismatch`, `missing_variable`); every variable over the sample dimension is over
  (station, sample) in that order (`dimension_mismatch`); at least one data variable (`no_data_variables`); every `ancillary_variables` target that exists has its data
  variable's dimensions (`bad_ancillary`).
- Data variables: the name is a quantity token, a registry token or a CF name the format does not use itself (else `invalid_variable_name`); a registry quantity other than
  `difference` has `units` that convert to its canonical unit (`noncanonical_unit`, subject: the token); the `<name>_status` target of `ancillary_variables` is the wet/dry
  status and must be byte, with `flag_values` 0, 1, `flag_meanings` "dry wet" and a `_FillValue` other than 0 and 1 (`bad_flag`, subject: the status variable).
- CRS (§10.1): the `grid_mapping` variable's `epsg_code` (EPSG:4326, or another geographic code, projected to WGS 84 with the native point kept), else
  `latitude_longitude` on the WGS 84 ellipsoid (without ellipsoid parameters: WGS 84 with `crs_assumed`); a nonzero `longitude_of_prime_meridian` or anything else is
  `unsupported_crs`; no `grid_mapping`: WGS 84 with `crs_assumed`.
- Values: ids unique (`duplicate_station_id`), non-empty (`no_station_id`), UTF-8 without NUL once trailing NULs are cut (`bad_encoding`), and so the names and the
  providers; `lat`/`lon` finite and in range (`bad_coordinates`, with the station); times present (`time_missing`), in range (`time_out_of_range`), strictly increasing
  (`time_not_increasing`); `obs_count` in 0..`obs` (`bad_obs_count`); the padding of `time`, the data and the status is fill (`padding_not_missing`, with the station and
  index: the first padding element, or all of them with `PaddingCheck::whole`); a status flag is 0, 1 or its fill (`bad_flag`), a dry sample has no value and a wet one has
  one (`wet_dry_inconsistent`).
- Samples: Missing when equal to the variable's `_FillValue` (or the library's default fill), NaN, or masked by `missing_value` or `valid_*`; Dry when the wet/dry status
  is 0. The quantity is the variable name: a registry token, else a generic quantity with the variable's `standard_name`. Labels are `long_name` (the token when there is
  none); units are `units` (`unrecognized_unit` for one outside the unit table and the registry, subject: the unit text as the file has it); datums are `vertical_datum`
  (`datum_unknown` and no datum for an unknown token, subject: the token, or for a datum on a quantity that cannot carry one, subject: `<variable>:vertical_datum`).
- Warnings, in this order: `minor_newer` (subject: the version), `crs_assumed`, `unknown_provider` (a `station_provider` token core does not know: the station has no
  source; one warning per token, counting its stations), `crs_approximate`, per data variable `unrecognized_unit` and `datum_unknown`, and last those of
  `parse_cf_time_units` for `time:units`.

**Foreign CF** (§12.2 *Foreign* column, §12.3, §12.5). The origin is `ForeignCfOrigin`: the layout of the five (CF 9.3.1–9.3.4 and 9.2) and the CF version
`Conventions` names.

- Finding the variables: by `cf_role`, `standard_name`, `units`, `axis`, `coordinates`, `sample_dimension` and `instance_dimension`; where CF has no attribute, by the
  names `station_name` (the names, when no text variable has the standard name `platform_name`; either is used only over the station dimension, else skipped) and
  `obs_count`. The time variable is the best ranked of those that can be one (§12.2); two of the same rank are `unsupported_layout` naming both. Two variables with
  `cf_role` timeseries_id are `ambiguous_station_id`. An id that is missing (an empty or NULL string, a masked integer, no id variable) is the station's index in decimal
  (`station_id_substituted` when the file has an id variable); float ids are `bad_encoding`. Without a name variable the names are empty.
- Accepted: `_FillValue`, `missing_value`, `valid_*`, packing, int and float data and times, `NC_STRING` and integer ids. A data variable whose masking attributes cannot
  be read is skipped (`skipped_variable`, subject `<variable>:<attribute>`); that is an error only when no data variable is left (`no_data_variables`) or for the time,
  position and helper variables.
- Quantities, datums, quality flags and time order: §12.5. The padding of an incomplete layout with `obs_count` is checked as `options.padding` says; without
  `obs_count` the counts are the leading non-missing times.
- Errors: `missing_variable` (latitude, longitude, time), `ambiguous_station_id`, `no_data_variables`, `unsupported_layout`, `dimension_mismatch`, `bad_obs_count`,
  `bad_row_size`, `bad_ragged_index`, `padding_not_missing`, `time_missing`, `time_out_of_range`, `missing_attribute` (`time:units`), `unsupported_calendar`, an
  `NcError` for a `_FillValue` of the wrong type on a time or position variable.
- Warnings, in this order: `foreign_cf`, `crs_assumed`, then the stations' (`station_id_substituted`, `invalid_utf8_replaced`, `duplicate_station_id_renamed`,
  `crs_approximate`), `skipped_variable`, `unknown_quantity`, `variable_renamed`, per data variable `unrecognized_unit` and `datum_unknown`, `quality_flags_ignored`;
  reading adds those of the time units, `times_reordered`, `duplicate_times_dropped`, `conflicting_duplicate_times`, `flagged_samples_masked`, `suspect_samples_kept`.

**Legacy v4** (§11). The origin is `LegacyOrigin`: what the file has (a `fileformat` text, a `stationId` variable, the digits of the station numbers).

- Errors: `missing_dimension` and `missing_variable` (with the station for the per-station variables), `dimension_mismatch`, `inconsistent_metadata` (a station whose
  `units` or `datum` differ from the first's), `unsupported_crs`, a `ParseError` `bad_date` for `referenceDate`, an `NcError` `type_mismatch` for a
  `HorizontalProjectionEPSG` that is not a signed integer.
- Warnings, in this order: `legacy_dialect`, `crs_assumed`, `tz_assumed_utc` (a `timezone` that is not UTC or GMT, or text after the 19 characters of
  `referenceDate`), `epoch_used`, `station_id_substituted`, `invalid_utf8_replaced`, `duplicate_station_id_renamed`, `crs_approximate`, `unrecognized_unit`,
  `datum_unknown`, and from reading `times_reordered`, `duplicate_times_dropped`, `conflicting_duplicate_times` (the series are put in order as §12.5 says).

---

## 13. Versioning and compatibility

`metoceanviewer_format_version = "<major>.<minor>"` (string). CF version evolves independently via `Conventions`.

| Change | Bump | Examples |
|---|---|---|
| Add an optional attribute or variable, a registry token, a layout the reader already must accept (L3 output) | minor | `elevation` becoming written, new quantity token, new flag meaning |
| Change meaning/units/dtype/name/dimension order of an existing item, make an optional item required, change layout rules, change time encoding | major | |
| Move to a newer CF version | minor if the new CF only adds | e.g. `Conventions="CF-1.12"` |

Reader policy: same major, any minor => read; minor newer than the reader => `W-MINOR-NEWER` and unknown attributes/variables are ignored; major newer => `UnsupportedVersion{found, supported}`;
missing/unparsable version in a file that has `metoceanviewer_format` => `BadVersion`. Writers always write the newest version they implement. A major bump keeps reading all earlier majors for as long as the project
supports them (decided at bump time). Files written by 1.x are valid CF-1.11 at every minor.

---

## 14. Test and compliance checklist

### 14.1 Writer/reader tests (Catch2, `io` tier; each bug-class item also gets a regression test, plan §7)

| Area | Cases |
|---|---|
| Round trip | single station; N stations same axis => L1; N stations different lengths => L2; one empty station among others (`obs_count = 0`); pre-1970 time; millisecond times; times at +/-(2^53 - 1) ms exact and beyond rejected; values at +/-DBL_MAX; `-0.0`; denormals |
| Layout rule | identical axes with one value differing by 1 ms => L2; same length different values => L2; reorder of stations keeps L1; determinism (two writes byte-identical apart from `date_created`/`history`) |
| Missing | `nullopt` at start/middle/end; all-missing variable for one station; all-missing for all; `Dry` vs `Missing` vs `Value` tri-state; padding region reads back missing in `time`, data and status |
| Strings | ASCII, non-ASCII UTF-8 (BMP + supplementary), 1-byte name, 1000-byte name, spaces, trailing space preserved, empty id rejected, embedded NUL rejected, `strlen` vs `QString::length` regression [B15], names with `"`/`\` |
| Structure | read back with raw libnetcdf and assert: format is `NC_FORMAT_NETCDF4`, no unlimited dim, `_FillValue` present and typed on every `double` data variable, attribute lengths (`nc_inq_attlen`) equal to byte length [B8], `time` units/calendar text, `Conventions == "CF-1.11"`, chunk shape per §3 (`nc_inq_var_chunking`), deflate on (`nc_inq_var_deflate`) |
| Validation (one fixture per rule in §12) | each error code of §12.2/§12.4/§12.8 triggered by a minimal CDL fixture (generated with `ncgen` at build time or by the Python fixture script) and asserted to produce exactly that error; truncated/corrupt HDF5; non-netCDF file; read-only file; unwritable output dir (no partial/temp file left) [B6]; crash-safety: failure injected before rename leaves the old file byte-identical [B19] |
| Foreign files | CF H.2.1 / H.2.2 / H.2.3 / H.2.4 (contiguous ragged) / H.2.5 (rejected) examples as committed CDL; float32 data with `missing_value`; `scale_factor`/`add_offset`; `NC_STRING` ids; `days since` and `seconds since 2000-01-01 00:00:00 +00:00` time units; transposed `(time, station)`; no `grid_mapping` |
| Legacy dialects | writer-independent fixtures for A (generated by a small C tool that reproduces `Hmdf::writeNetcdf` semantics), B (`stationNameLen` 50 and 300 [B7], float data [B4], default fill [B9], `referenceDate` of 19/20/120 bytes [B8], EPSG 4326/26915/absent), C (`-9999f`); each mapped per §11; > 9999 stations |
| Versioning | 1.0 read; 1.7 read with `W-MINOR-NEWER`; 2.0 => `UnsupportedVersion`; missing version with format attribute => `BadVersion`; `CF-1.6`, `CF-1.12`, `"CF-1.8 ACDD-1.3"`, `"CF-1.11,ACDD-1.3"` Conventions parse |
| Property/fuzz | the structure fuzzer (core-design.md §7.5): bytes -> a bounded netCDF schema written with netCDF-C -> `detect_file`, `inspect_*` and `read_*`; an unspoiled template must read back as written (decision 21). Fuzzing the reader from raw netCDF bytes is deferred (core-design.md §9.3) |
| Cross-tool smoke (Linux CI) | `xarray.open_dataset(decode_cf=True)` on writer output: values, times, strings, fill-to-NaN equal to the C++ model; `ncdump -h` of the two canonical fixtures equals the committed CDL (the blocks in §9) |

### 14.2 CF compliance in CI

Findings from the prototype run (all with the files of §9):

| Checker | Result | Notes |
|---|---|---|
| `cfchecker` 4.1.0 (the official CF checker; latest on PyPI, 2026-10-06) | **fails on `Conventions = "CF-1.11"`**: supports versions up to 1.8 (`WARNING: CF-1.11 is not a valid CF version`, then `ERROR (2.6.1): ... does not appear to contain CF Convention data`). Run on a copy whose `Conventions` is rewritten to `CF-1.8`: **0 errors, 0 warnings, 2 INFO** (`cf_role` non-standard use, `crs_wkt` syntax not verified). Rejects `NC_STRING` (`ERROR (2.2)`), the reason for D7. | needs `libudunits2`, `UDUNITS2_XML_PATH`, and the standard-name table passed with `-s`; pin table v95 in-repo for determinism |
| IOOS `compliance-checker` 6.1.0, `-t cf:1.11` | **0 errors, 1 warning**: `time` has `calendar=proleptic_gregorian` and "it is recommended that `units_metadata` ... `leap_seconds: ...`". That recommendation is not in the CF-1.11 text (searched; it is a later CF addition) and CF-1.11 forbids `units_metadata` on non-temperature variables (§3.1.2), so the warning is allow-listed. Flags the extended `grid_mapping` syntax (§10.1). | same udunits requirement; supports 1.6-1.11 natively |
| xarray | opens both layouts with CF decoding (§9.2) | |
| Panoply, ncview, NCO | not verified in this environment; manual check before release: Panoply plots both layouts, ncview opens the `(station, time)` variable, `ncks -d station,0` slices | |

CI policy: add a **separate Linux job `format-compliance`** (not part of the OS matrix) that builds the `station_nc_fixtures` test executable, writes canonical L1, L2 and wet/dry-flagged files into a temp directory, then runs
`tools/check_cf.py`, which (a) runs IOOS `compliance-checker -t cf:1.11` and fails on any error or any warning not in `tools/cf_waivers.txt`; (b) runs `cfchecks -v 1.8 -s <pinned table>` on a copy with `Conventions` rewritten to `CF-1.8`
(`netCDF4`, 3 lines) and fails on any ERROR/WARN; (c) runs the xarray smoke test. Pin `cfchecker==4.1.0`, `compliance-checker==6.1.0`, `netCDF4`, `xarray`, and `apt install libudunits2-0 udunits-bin`. The job is **required** on `v5`
(runtime is seconds; the files are < 100 kB) because format drift is silent otherwise. When `cfchecker` gains CF-1.11 support, drop the downgrade copy.

---

## 15. Resolved questions

Each row below was resolved on 2026-10-06 by adopting its default. The decisions follow
from plan §6 (decision 7 retires dialect C with CRMS). Q6 makes strictly increasing
times a `TimeSeries` invariant: text readers sort and drop duplicates, with a warning.

| # | Question | Resolution |
|---|---|---|
| Q1 | Decision 14 says "one format". Is "one format with two deterministic layouts (L1 when axes are identical, else L2)" acceptable, or must it be exactly one layout (then L2 always, losing the `time` index and doubling time storage for model output)? | two deterministic layouts |
| Q2 | Strings as `char` (cfchecker-clean, §3) rather than `NC_STRING` (the session format already uses `NC_STRING`). | `char` |
| Q3 | Time as `double` milliseconds (exact, widely readable) rather than `int64` seconds (legacy, loses sub-second) or `int64` ms. | `double` ms |
| Q4 | Stations from projected legacy files are reprojected to WGS 84 and their native EPSG is dropped. Keep the native coordinates (extra `x`/`y` + second grid mapping, extended `grid_mapping` syntax) instead? | drop; later minor bump if wanted |
| Q5 | Keep a reader for legacy dialect C (CRMS) given decision 7 removed CRMS and the file has no coordinates? | drop it from the 5.0 scope (A and B only) unless you object |
| Q6 | Does the core `TimeSeries` invariant require strictly increasing times? The format does (CF Table 9.1); v4 IMEDS may contain duplicates or out-of-order rows. Proposal: the IMEDS/user readers sort and de-duplicate with a warning, the writer rejects the rest. | writer rejects |
| Q7 | Mixed-provider files and id collisions across providers: `station_id` must be unique per file; for mixed sets the exporter must qualify ids itself. Acceptable? | yes |
| Q8 | `double` samples only. Offer `float` for very large model exports (halves size, loses exact round trip)? | `double` only |
| Q9 | One `vertical_datum` per data variable, uniform `water_surface_height_above_reference_datum` name (§6) rather than switching to `sea_surface_height_above_mean_sea_level` for MSL. | uniform |
| Q10 | Required CI job `format-compliance` with the `CF-1.8` downgrade workaround for `cfchecker` and one allow-listed IOOS warning (§14.2). | required |
