# Legacy file-format specification (MetOceanViewer v4.5.1)

Purpose: behavioral spec for test-first (Catch2) Qt-free C++23 readers/writers (plan Phase 2).
Legacy code is reference only; **where it is buggy this doc states the intended behavior** and tags
the legacy bug as `[B#]` (plan §1.2 numbering) or `[N#]` (new, found while writing this doc, §14).
`file:line` refs are current HEAD; they matched `e5a4e0af` when checked.

Conventions: "MUST" = behavior tests should pin. "Legacy" = what v4 does. "Intended" = what v5 does.
`null` = `std::nullopt`/mask in v5 (never an in-band sentinel, plan §2.2). Times are
`sys_time<milliseconds>` (UTC) unless noted. Paths relative to repo root.

Host tooling note: no `ncdump`, `ncgen`, python `netCDF4`/`h5py`/`scipy` on this host. libnetcdf 4.9.0 +
gcc exist; header and value inspection of all `.nc`/`.mvs` fixtures was done with two throw-away C
programs (not committed; `nc_inq_*` dump). Fixture facts in §12 are therefore exact, not guessed.

---

## 1. Shared concepts

| Concept | Legacy | Intended |
|---|---|---|
| Null data value | `HmdfStation::nullDataValue() = -DBL_MAX` (`hmdfstation.h:39`); per-station `m_nullValue` (default same) overridden by netCDF fill (`netcdftimeseries.cpp:125,173`). Consumers test `abs(v - nullValue) > 1e-4` (`usertimeseries.cpp:279`) | `optional<double>` / mask. Readers convert file fill to null at read time. |
| Null date | `-INT64_MAX` | not representable |
| Station | `HmdfStation`: name, id (default `"noname"`/`"noid"`), lon/lat, `stationIndex`, vectors `date` (ms UTC), `data`, `isNull` (default **true**, only CRMS sets false) | value type; `isNull` derived from `data.empty()` [N17] |
| `Hmdf` | header1..3, `datum`, `units`, stations | `TimeSeriesSet` |
| Date resolution | ms internally; IMEDS/netCDF files second resolution | keep ms; file formats are seconds |
| Fill/dry in ADCIRC output | `-99999` | null |
| ID types | file type enum persisted in sessions: `NETCDF_ADCIRC=0, NETCDF_DFLOW=1, NETCDF_GENERIC=2, ASCII_ADCIRC=3, ASCII_IMEDS=4, FILETYPE_ERROR=5` (`metoceanviewer.h:7-14`) | `enum class FileType` (`src/io/include/mov/io/file_type.hpp`, detected by content, never by suffix); session importer maps ints and the legacy strings (§10) |

### 1.1 File-type detection (`MetOceanViewer/src/filetypes.cpp:24-137`)
Order: generic-netCDF, ADCIRC-netCDF, DFlow-netCDF, IMEDS, ASCII-ADCIRC (the *string* variant swaps the last two; harmless).

| Kind | Test |
|---|---|
| NETCDF_GENERIC | file opens with netCDF and has variable `time_station_0001` (`:133`) |
| NETCDF_ADCIRC | global attribute `model` == `"ADCIRC"` (`:52-82`; reads `attlen` bytes, fine) |
| NETCDF_DFLOW | variables `station_x_coordinate` and `station_y_coordinate` exist (`:84-112`) |
| ASCII_IMEDS | suffix `.imeds` case-insensitive (`:122`) |
| ASCII_ADCIRC | suffix exactly `61`/`62`/`71`/`72` (case-sensitive, `:114`) |
| CRMS netCDF | **not detected here**; has `time_station_000001` (6 digits) so is not generic |

Tests: each fixture classifies correctly; CRMS file is *not* generic; `fort.61` with upper/lower case; a netCDF that is none of these -> error.

---

## 2. IMEDS (read + write)

Sources: `libraries/libmetocean/hmdf.cpp:104-165` (read), `:213-250` (write), `hmdfasciiparser.cpp:95-132`, `stringutil.cpp:71-76`. `FileFormat.md` is the **HWM** format, not IMEDS (name is misleading; see §6).

### 2.1 Layout (read)
```
line 1   free text header1          (conventionally "% IMEDS generic format ...")
line 2   free text header2          ("% year month day hour min sec value")
line 3   header3                    ("<source>  <tz>  <datum>  [<units>]")  e.g. "NOAA    UTC    MLLW"
line 4+  station block, repeated:
           <name> <lat> <lon>                    whitespace separated, name has NO spaces
           <yyyy> <mm> <dd> <hh> <mi> <ss> <value>   zero or more rows; seconds field optional
```
| Rule | Detail |
|---|---|
| Whitespace | each line trimmed, `\r` removed (`sanitizeString`); fields split on **space only** (tabs are NOT separators in legacy; `split(" ", SkipEmptyParts)`). Intended: split on any ASCII whitespace. |
| Header lines | read verbatim as 3 lines; not interpreted. Legacy leaves `datum`/`units` empty after read. Intended: header3 tokens `[source, tz, datum, units?]` exposed as optional metadata; raw header lines preserved for round-trip. **Ambiguity A1**: tz token is always "UTC" in fixtures; non-UTC tz is not honored by legacy (all times treated UTC). Keep: treat as UTC, expose tz string. |
| Station line | name = token 0, **latitude = token 1, longitude = token 2** (`hmdf.cpp:131-133`). Order lat-then-lon is opposite of most other files in this repo. |
| Data row | first successfully parsed 7-number row *or* 6-number row (no seconds -> sec=0). First row that fails to parse ends the station; that line is then re-interpreted as the next station header. |
| Time | `QDate(y,m,d)+QTime(h,mi,s)` UTC -> ms. Invalid calendar values (month 13) silently produce an invalid QDateTime (ms = garbage). Intended: row rejected with `ParseError{line}`. |
| Value | `double`; any string accepted by Spirit `double_`. No fill-value masking in legacy (fixtures contain none). Intended: parse `-99999`/`-9999` as **data**, not null, unless caller supplies a null token (A2: owner decision; the writer never emits one). |
| EOF | file without trailing newline MUST still yield the last row (`mllw.imeds` has none; `msl.imeds` has one). |
| Station id | not in file; legacy `id="noid"`; intended `id = name`. |
| Empty file / <3 header lines | legacy: UB/`templist.at(0)` throws. Intended: `ParseError`. |
| Blank line mid-file | legacy: blank ends station then `templist.at(0)` on empty list -> `QList::at` assert/UB [N13]. Intended: skip blank lines. |
| Station with 0 rows | allowed (header followed by header). |
| Success flag | `readImeds` returns 0 but never calls `setSuccess`; caller does (`usertimeseries.cpp:372`). |

**[B3]** `std::fstream fid(path)` opens in|out; read-only file -> open fails, `fid.bad()` is false, returns 0 with **no stations**. Intended: open `std::ifstream`, return `IoError{Open}` if `!is_open()`. Test: copy `mllw.imeds` to a `0444` file (as non-root) -> must read 2 stations; nonexistent path -> error (not empty success).

**[N3]** Row parser (`hmdfasciiparser.cpp:107`): tries 7-field grammar `int×6 double` first. A 6-field row whose value is fractional (`2015 07 01 00 00 2.605`) is mis-parsed as sec=2, value=0.605 because `int_` consumes `2` and `double_` accepts `.605`. Intended: tokenise the row, require exactly 6 or 7 whitespace tokens, first 5 (or 6) integers. Test: the 6-field row above -> value 2.605, sec 0. Also `phrase_parse` does not require end-of-input: trailing garbage accepted (`... 2.6 abc`). Intended: reject.

### 2.2 Write (`writeImeds`)
```
% IMEDS generic format
% year month day hour min sec value
MetOceanViewer    UTC    <datum>   <units>
<name>   <lat>   <lon>                      ← QString::number() default 'g', 6 significant digits
yyyy    MM    dd    hh    mm    ss    <%10.4e>      ← 4 spaces between fields; value in C "%10.4e" e.g. " 2.6050e+00"
```
- Name sanitised: `' '` -> `_`, `','` -> `_`, then `"__"` -> `_` once (`hmdf.cpp:226-228`; "a, b" -> "a__b" -> "a_b").
- Rows with invalid date skipped. Line endings `\n`. No trailing blank line.
- Legacy loses precision: coordinates 6 sig digits (`29.987793` -> `29.9878`), values 5 sig digits. **[N18]** and `-DBL_MAX` null would be printed as `-1.7977e+308`. Intended: coordinates `%.8g`-or-shortest-roundtrip (`std::format("{}")`), values shortest round-trip; nulls **omitted**. **A3 (owner)**: keep legacy `%10.4e` for byte-compat with downstream tools, or round-trip precision? Recommend round-trip precision; test reads back equal.
- Write of an empty set: header only.
- Unwritable path -> `IoError`, not silent.

Tests: golden read of both fixtures (counts below §12); write->read round trip; write byte-golden for a 2-station synthetic; name sanitising; 6-field/7-field rows; CRLF file; tab-separated file (intended accepted); no trailing newline; read-only file [B3]; empty station block; mid-file blank line.

### 2.3 Other `Hmdf` outputs used by the CLI/GUI
`writeCsv` (`hmdf.cpp:185-211`): per station `Station: <name>\nDatum: <d>\nUnits: <u>\n\n` then rows `MM/dd/yyyy,hh:mm,%10.4e\n`, then `\n\n\n`. Dispatch by suffix `.imeds`/`.csv`/`.nc` (case-insensitive), else return 1 (`:445-455`). `Hmdf::dataBounds` **[B13]**: init `dateMax=+max, dateMin=-max` is inverted, so bounds never update; intended `dateMin=+max, dateMax=-max`, and skip stations with no data (not `isNull` flag [N17]). `HmdfStation::dataBounds` **[B14]** (`hmdfstation.cpp:133-150`) dereferences `end()` of empty vectors; intended `optional<Bounds>` = nullopt for empty/all-null. Its min logic (`upper_bound(sorted, -DBL_MAX)`) = "min over non-null"; **note** it only excludes `-DBL_MAX`, not a station's netCDF fill; intended: min over unmasked values. Tests: empty station -> nullopt; two stations with different ranges -> true min/max; all-null -> nullopt.

---

## 3. ADCIRC station output

Sources: `MetOceanViewer/src/adcircstationoutput.cpp`. Output kinds by file suffix/variable: fort.61 elevation (1 col), fort.62 velocity (2 col u,v), fort.71 pressure (1), fort.72 wind (2: x,y).

### 3.1 ASCII (`readAscii`, `:53-133`) + station file
```
line 1   run description (not parsed; fixture: "  riverspinup   padcirc 49_14   Pontchartrain Test")
line 2   NSnaps NStations DT NSPOOL NCOLS [FileFmtVersion: n]     simplified+split(" "): [0]=nSnaps [1]=nStations [4]=nColumns
then NSnaps records, each:
   <time_seconds> <timestep>          (record header; only token 0 used, as double seconds)
   NStations lines: <station_index> <v1> [<v2>]      1-based index IGNORED; row order = station order
```
| Rule | Detail |
|---|---|
| nColumns | `1` scalar, `2` vector. Anything else treated as scalar (legacy: only `==2` is special). Intended: 1 or 2 else error. |
| Fill | `v1 < -900` -> null (`:93`). Fixture fill = `-99999` (`-9.9999000000E+004`). Keep legacy strict **`< -900`**. For 2-col files the test is on v1 only; v2 fill is ignored [N7]. Intended: null if **either** component `< -900`. |
| **[B1]** Vector value | legacy `pow(pow(u,2)+pow(v,2), 2)` = (u²+v²)². Intended `sqrt(u²+v²)` (magnitude). Direction is **not** produced by the ADCIRC reader at all (legacy: magnitude only). Intended: reader exposes `u`,`v`; magnitude and direction derived. Direction convention (if added) = DFlow's: `atan2(v,u)` degrees, math convention (CCW from +x/east), range (-180,180], same as `dflow.cpp:106`. Test: fort.62.nc/ASCII station 1 step 1: u=0.049298094833, v=0.012227184139 -> 0.0507918... (= sqrt); legacy would give (u²+v²)² = 6.66e-6. |
| Short/blank lines | `TempList.value(i)` returns empty -> `toDouble()=0.0` silently (truncated file yields zeros). Intended: `ParseError{truncated}`. |
| Time | `time[i]` seconds (double) since model cold start. Converted `coldStart.addSecs(time)` (`:333`; `addSecs(qint64)` truncates fractional seconds). Intended: round to ms, `cold_start + duration<double>`. |
| Cold start | **user-supplied** (not in file), string `yyyy-MM-dd hh:mm:ss`. Legacy parses it **without a time spec -> local time** (`usertimeseries.cpp:366,383`; `QDateTime::fromString`) [N5]. Intended: UTC. Test: result independent of TZ env (`TZ=America/Chicago` vs `UTC`). |

Station file (`readAscii :106-130`), fixture `function_tests/ReadADCIRC/ASCII/stations.csv`:
```
3                          ← first token = station count; MUST equal header NStations else WRONG_NUMBER_OF_STATIONS
-90.0127,29.987793         ← lon , lat [, name...]   separators: comma or space (QRegExp ",| "); line is simplified() first
-90.5,28.0
-91.0,25.0
```
| Rule | Detail |
|---|---|
| Order | **lon first, lat second** (opposite of IMEDS). |
| Name | tokens 2.. joined, each prefixed by a space (`" " + tok`) -> **leading space** [N6]. If no name: `"Station_" + i` (0-based). netCDF default name is `"Station " + i` (space, 0-based) (`:318`). Intended: join with single space, no leading space; default `"Station N"`; decide 0- vs 1-based (A4; keep 0-based to match legacy sessions). |
| Count mismatch | returns error; but note `readAscii` opens/validates the station file *after* reading all data. |
| Station-file open failure | `CANNOT_OPEN_FILE`; the output file remains open on early return (leak, harmless with ifstream RAII). |
| Pairing | one station file per output file; the same file serves 61/62/71/72. Count in file must equal header count. |

Tests: parse 4 ASCII fixtures (3 stations; 144 snaps for 61, 48 for 62/71/72); fill row (`fort.61` step 1 station 1 = -99999 -> null); [B1]; count mismatch; station file with names, with space separators, with CRLF, with blank trailing line; truncated data; header with nColumns=3 -> error.

### 3.2 netCDF (`readNetCDF`, `:135-321`)
| Item | Spec |
|---|---|
| Dims | `time` (unlimited), `station`, (`namelen` unused). |
| Coord vars | `x` = lon, `y` = lat, both `(station)`; read with `nc_get_var_double` (type-converting; OK for float or double). |
| `time` | `(time)` double seconds; **`units` ("seconds since Met") and `base_date` attributes are ignored**; same user cold start as ASCII. Intended: still ignore (fixtures carry junk text), cold start required. |
| Data var search order | `zeta`, `u-vel`, `v-vel`, `pressure`, `windx`, `windy`; first present wins. `u-vel` => vector with partner `v-vel` (required, else NETCDF error); `windx` => vector with `windy`. Quirk: a file with `v-vel` but no `u-vel` is read as scalar `v-vel`. Dims `(time, station)`. None found -> `NO_VARIABLE_FOUND` (and ncid leaks [B5]). |
| Vector value | `sqrt(a²+b²)` here (correct). null if `a == fill` (partner not tested [N7]; intended either). |
| **[B4]** fill | `nc_inq_var_fill(..., &double)` writes only 4 bytes for NC_FLOAT var -> garbage upper half; `nc_get_vara` into `double*` for a float var reads wrong data. All 4 fixtures are `double`, which hides it. Intended: `nc_inq_vartype`, typed `nc_get_vara_double` (netCDF converts), fill from `_FillValue` attr via `nc_get_att_double` (or default fill for the type if absent), compare after converting to the variable's own type (float fill `-99999f` vs `-99999.0` double: `(double)(float)-99999 == -99999.0` exactly, but 9.96921e36f != 9.96921e36). Also mask NaN. |
| Fill in fixtures | var attr `_FillValue = -99999.0` (double) plus identical global `_FillValue`, `dry_Value`. ADCIRC writes `-99999` for dry. |
| Global attrs | `model="ADCIRC"` required for detection only; `Conventions="UGRID-0.9.0"`, `dt`, `ics`, etc. ignored. |
| Name | `"Station " + i`; `station_name` `(station,namelen)` char var **exists but is ignored** ("Station One"...). Intended: use it when present (trim trailing blanks/NULs), fall back to default. A5: owner confirm (changes names shown vs v4). |
| Errors | each `nc_*` failure returns generic `NETCDF`; intended `NcError{code, var}`. All early returns leak `ncid` [B5]; use RAII. |
| Layout | read per-station column `start={0,i}, count={nt,1}` (strided, slow). Intended: one hyperslab read. |

Tests: 4 `.nc` fixtures (3 stations; time = 600,1200,... for 61; 144/48/48/48 steps); **float-typed copies of all four** [B4] with `_FillValue=-99999f` and values identical to double within `float` epsilon; fill value present at (t0,station1) in fort.61 (check ASCII/NetCDF agree); no `_FillValue` attribute (default fill 9.96921e36); missing `time` var; missing `v-vel` for u-vel; read-only file; file that is not netCDF.

Cross-check test: ASCII fort.61/62/71/72 and netCDF fort.61/62/71/72.nc hold the same run (3 stations, same times); values must agree to ~1e-9 (verify at fixture-inspection time; if not, record the delta).

---

## 4. DFlow-FM his netCDF (`MetOceanViewer/src/dflow.cpp`)

No DFlow fixture exists in the repo. Phase 2 must add one (§13).

| Item | Spec |
|---|---|
| Required dims | `time`, `stations`, `name_len` (`:451`). Optional: `laydim` (layer centres), `laydimw` (layer interfaces). |
| Required vars | `station_x_coordinate`, `station_y_coordinate` (double `(stations)`; legacy `nc_get_var` into `double*` untyped -> wrong for float [B4-class]), `station_name` char `(stations, name_len)`, `time`. |
| Station names | row-major `name_len` bytes per station. Legacy hard-codes stride **200** [B11] (`:478`); real D-Flow FM files use `name_len=64`. Intended: stride = `name_len`; trim trailing space/NUL; `simplified()` (collapse internal whitespace — keep). Id = decimal station index (0-based) (`:246`). |
| Init status | **[B2]** `dflow.cpp:23-29` sets `_isInitialized=true`/`_readError=false` when `isError()`: inverted. Intended: `expected<DflowFile, Error>`; test: opening a non-DFlow file reports failure; DFlow file reports success. |
| Time | `time(time)`, attribute `units` string `"<unit> since <date>"`. Legacy `substr(14,19)` [B11] assumes the prefix is exactly `"seconds since "` (14 chars) and the date exactly `yyyy-MM-dd hh:mm:ss`; anything else (`"minutes since"`, `"2000-01-01T00:00:00"`, trailing `" +00:00"`, 1-digit fields) mis-parses silently; parse failure yields an invalid QDateTime and garbage times. Intended parser: unit in {seconds,minutes,hours,days} (singular/plural), `since`, date `yyyy-mm-dd`, optional `[ T]hh:mm[:ss[.fff]]`, optional zone `Z`/`+hh[:mm]`/` UTC`. Convert `ref + round(time*unitSeconds)`. Legacy rounds to whole seconds (`qRound64`). Unparsable units -> error. Time var read as double. Typical D-FM value: `"seconds since 2000-01-01 00:00:00 +00:00"`? (plausible: A6 verify against a real his file before pinning; the +00:00 suffix would be dropped by legacy `substr(14,19)`, which only works because `19` chars stops before it.) |
| Plot variables | `nd==2` with dims `(time, stations)`, or `nd==3` with `(time, stations, laydim)`. Variables on `laydimw` excluded (commented out `:399-402`). Dim/var id maps are `QMap::operator[]`, which silently yields **0** for missing names [B12] (so a file with no `time` dim "matches" any var whose dim 0 is id 0). Intended: lookup returns `optional`/error. |
| 3D | `is3d` = file has dim `laydimw`. Layer count = `laydim` length; **[B12]** `_get3d` checks `laydimw` but reads `laydim` (`:277,291`), and via the 0-default map this can read dimension 0. Intended: `nLayers` = `laydim` length if present. Layer argument is **1-based** (`start[2]=layer-1`, `:628`); layer 0 or >nLayers -> intended error (legacy: `size_t(-1)` underflow, also for wind vars which pass layer 0 `:153,181` [N16]). Variable selection applies layer to 3D vars only; 2D vars ignore it. |
| Fill | hard-coded `-999.0` with `==` [B12] (`:592,641`). Intended: per-variable `_FillValue` (typically `-999` in D-FM); mask `NaN` too; typed read. |
| Data layout | `(time, stations[, laydim])` row-major; reads whole block `count={nt,ns[,1]}`. |
| Derived variables (names exposed in the plot-var list) | `2D_current_speed` = `sqrt(x_velocity²+y_velocity²)` (needs `x_velocity`,`y_velocity`); `2D_current_direction` = `atan2(y,x)*180/π` (degrees, math convention, not compass; range (-180,180]); `3D_current_speed` = `sqrt(x²+y²+z²)` (only if `is3d` and all three plot vars exist); `wind_speed`, `wind_direction` from `windx`/`windy` same formulas. Null if either/any input null. |
| Hmdf produced | header1..3 = `"DFlowFM"`, datum `"dflowfm_datum"`, `success=true`, station name from file. |
| Errors | `DFLOW_NOXVELOCITY` etc. -> intended typed `MissingVariable{name}`. `_getTime` never closes ncid on error returns [B5]. |

Tests: synthetic his (CDL in §13): name_len 64 and non-200 (e.g. 20); units `seconds since 2000-01-01 00:00:00` and `minutes since 2001-02-03 04:05:06 +00:00`; 2D + 3D (nlayers=3) variables; layer 1 vs 3 data differ; derived speed/direction numbers (u=3,v=4 -> 5, 53.1301°; u=0,v=-1 -> -90); fill -999 via `_FillValue`; float-typed vars; missing `time`; non-DFlow input; wind variables on 3-D file.

---

## 5. Generic "station netCDF" (three dialects)

All dialects: one pair of variables per station, per-station length dim, plus coordinate vars. Station numbering is 1-based, zero-padded; **padding width differs**.

| | **A: Hmdf::writeNetcdf** (`hmdf.cpp:261-432`) | **B: NetcdfTimeseries reader** (`netcdftimeseries.cpp:50-155`) | **C: CRMS** (`ProcessCrmsDatabase` writer / `crmsdata.cpp` reader) |
|---|---|---|---|
| Name pattern | `%s%4.4i` -> `_0001` | `%04i` -> `_0001` (same as A for N<10000; both widen beyond) | `%06i` -> `_000001` |
| Station-count dim | `numStations` | `numStations` (required) | `nstation` (added last, at close) |
| String-length dim | `stationNameLen`=200 (fixed) | `stationNameLen` (read; **ignored** for stride) | `stringsize`=200 |
| Param dim | none (1-D data) | none | `numParam` |
| Per-station dim | `stationLength_NNNN` | `stationLength_NNNN` | `stationLength_NNNNNN` |
| Time var | `time_station_NNNN` int64 `(stationLength_N)` | same | `time_station_NNNNNN` int64 `(stationLength_N)` |
| Data var | `data_station_NNNN` **double** `(stationLength_N)` | same | `data_station_NNNNNN` **float** `(numParam, stationLength_N)`, `_FillValue=-9999f`, contiguous, deflate L2 shuffle |
| Coord vars | `stationXCoordinate`, `stationYCoordinate` double `(numStations)`, attrs `HorizontalProjectionName="WGS84"`, `HorizontalProjectionEPSG=4326 (int)` | read same; EPSG read from **`stationXCoordinate`** attr (required; reader fails if absent) | none (coords from embedded CSV, §8) |
| Name vars | `stationName`, `stationId` char `(numStations, 200)` | reads `stationName` only | var `sensors` char `(numParam, 200)` (parameter names); station name is **attribute** `station_name` on data+time var |
| Time encoding | seconds since **1970** (writer: `date_ms / 1000`, int64, truncating toward 0) + attrs on time var: `referenceDate="1970-01-01 00:00:00\0"` (20 bytes incl. NUL), `timezone="utc"`, `units` **meant** `"second since referenceDate"`, written length 3 => `"sec"` [B15] | reads `referenceDate` (80-byte buffer, first 19 chars, `yyyy-MM-dd hh:mm:ss`, UTC), time = `ref + int64 seconds`; `units` ignored | time var attr `reference="seconds since YYYY/MM/DD hh:mm:ss UTC"` (written: `"seconds since 1970/01/01 00:00:00 UTC"`); **reader ignores it**, reads absolute epoch seconds; attrs `minimum`/`maximum` on time var = first/last date `yyyy/MM/dd hh:mm:ss` UTC |
| Fill | none defined (default `NC_FILL_DOUBLE` 9.96921e36 for unwritten) | `nc_inq_var_fill` -> if `== NC_FILL_DOUBLE` use `-99999` as "null value" [B9]: a masked value is never recognised | `-9999.0f`; reader keeps `v > -9999.0f` |
| Attr on data var | `StationName`,`StationID`,`units`,`datum` | none read | `station_name` |
| Global attrs | `source="MetOceanViewer"`, `creation_date`, `created_by` (`$USER`/`$USERNAME`), `host`, `netCDF_version`, `fileformat="20180123"` | none | none |
| Format | NC_NETCDF4, deflate(shuffle,L2) on time+data | any | NC_NETCDF4 |
| Detect | `time_station_0001` exists | | `time_station_000001` |

### 5.1 Reader semantics (dialect A/B) — intended
- Required: dims `numStations`,`stationNameLen`; vars `stationXCoordinate`,`stationYCoordinate`,`stationName`; attr `HorizontalProjectionEPSG` on X var **or default 4326 if absent** (A7: legacy hard-fails; intended lenient + warn).
- Station name bytes: row stride = `stationNameLen` actual value **[B7]** (legacy `substr(200*i,200)` over a buffer sized `(len+1)*n` -> overrun/misalignment whenever len != 200). Trim trailing NUL/space; `simplified()` (legacy) — keep trim-only? A8: legacy collapses internal whitespace; keep for parity.
- Station id = name (legacy ignores `stationId`). Intended: use `stationId` if present else name.
- Per-station lookup by constructed name `stationLength_%04i`, `time_station_%04i`, `data_station_%04i`; accept also `%06i` (dialect C naming without `numParam`) and, for robustness, `%i`? (A9; plan §2.2: readers accept all three dialects).
- `referenceDate` att: size from `nc_inq_attlen`, never a fixed 80-byte buffer **[B8]**; trim NUL; parse `yyyy-MM-dd[ T]hh:mm:ss`. Missing attr -> assume 1970-01-01 (A10).
- Time read as int64 seconds; data via typed read (**float or double var** [B4-class]).
- Fill [B9]: mask value == `_FillValue` attr (typed compare) **or** default fill of the var type if no attr, and NaN; **no** magic `-99999`.
- EPSG getter [B10]: `getEpsg(file)` returns netCDF error codes as EPSG values; intended `expected<int, Error>`.
- Coordinates in file CRS; legacy GUI reprojects to 4326 via ezproj when epsg != 4326 (`usertimeseries.cpp:696`). the io reader projects to WGS 84 at the boundary and keeps the native point (PROJ is a private dependency of `mov_io`; `core` has no PROJ).
- `nc_open` failure macro **[B6]**: `NCCHECK` calls `nc_close(ncid)` on an uninitialised ncid when `nc_open` fails (`netcdftimeseries.cpp:58`, `hmdf.cpp:265`). RAII fixes; test: nonexistent file, unwritable output dir for the writer.

### 5.2 Writer — intended
Emit **one** new format, `station-timeseries` (station-netcdf.md; decision 14 superseded A11's dialect-A naming) with: stationName/stationId padded/truncated to `stationNameLen` using **actual** byte lengths (UTF-8 bytes, not `QString::length()`) **[B15]**; `units` attr full text (`"seconds since 1970-01-01 00:00:00"`); attribute lengths = byte length; name char array written with count = padded buffer; `_FillValue` defined on data vars; EPSG attr on both coordinate vars; time in seconds (write floor not truncate for pre-1970); deflate retained. Empty stations (0 samples) must not define a zero-length unlimited dim problem (netCDF dim of 0 is *unlimited* in classic; use explicit handling) **[N19-edge, test]**.

Tests: reader on writer output (round trip incl. non-ASCII, 150-char and 5-char names, names with spaces); `stationNameLen` = 50 and 300 files; float-typed data var; `_FillValue` = -9999 and default fill; value == 9.96921e36 masked; EPSG 4326/26915/absent; `referenceDate` with 19- and 20-byte (NUL) forms and a 120-byte value; `units` attr round-trip; 0-sample station; >9999 stations name widening; legacy-written fixture (generate with a small C tool compiled against legacy semantic) to pin that old files still read.

---

## 6. HWM file and statistics

### 6.1 File (`highwatermarks.cpp:77-103`, doc `FileFormat.md`)
Plain text, **no header**, comma-separated, one HWM per line:

| col | name | legacy use |
|---|---|---|
| 0 | Longitude (deg E, WGS84) | `lon` |
| 1 | Latitude | `lat` |
| 2 | Ground (topo) elevation | `topoElevation` ("bathy") |
| 3 | Station **Measurement** (observed HWM) | `observedElevation` |
| 4 | **Modeled** elevation | `modeledElevation` |
| 5 | Difference = Modeled - Measured | **ignored on read**; recomputed (`modeledError()`) |

- Line handling: `simplified()` (whitespace collapsed; so spaces after commas OK); `split(",")`; `toDouble()` with failure -> 0.0 silently; missing columns -> 0.0. Blank line mid-file creates a (0,0,0,0,0) HWM [N20]. Intended: skip blank lines, `ParseError{line,col}` on non-numeric; accept header line if first token is non-numeric (A12).
- Units: file is unit-agnostic (no unit field); GUI combo "Feet"/"Meters" selects default class breaks + legend label only; **no conversion** (`uihwmtab.cpp:57-80`). Intended: `Units` is metadata passed in.
- **Dry flag**: modeled elevation sentinel (ADCIRC writes `-99999`). Legacy thresholds are inconsistent **[N2]**: stats use `> -999` (`highwatermarks.cpp:118,170`), SStot uses `> -9999` (`:146`), map uses `< -999 => class -1` (`hwm.cpp:81`), scatter/axes use `> -900` (`hwm.cpp:183,193...`), `HwmData::isValid()` uses `> -900` (unused). Intended single rule (A13, owner to confirm): dry iff **`modeled <= -999.0`**; matches the stats and map behavior users saw. Model dry is `optional<double>=nullopt` after read.
- Return codes: 1 filename empty / open failure, 2 no rows.

### 6.2 Statistics (`calculateStats`, `:105-176`) — pin exactly
Let wet set `W = {i : modeled_i > -999}` (dry excluded), `n₂=|W|`, `x=observed`, `y=modeled`, `e=y-x`.
`Σx, Σy, Σxy, Σx², Σy²` over W; `Σe` over W.

| Mode | slope m | intercept b | R² |
|---|---|---|---|
| Through zero (`-z`) | `Σxy / Σx²` | `0` | `1 - SSE/SStot`; `SSE = Σy² - m²·Σx²` (= `Σy² - mΣxy`, algebraically the same as the residual sum), `SStot = Σ(y-ȳ)²`, `ȳ=Σy/n₂` (centred about mean of y even though the fit is uncentred; can be **negative**) |
| Free | `(n₂Σxy - ΣxΣy)/(n₂Σx² - (Σx)²)` | `(ΣyΣx² - ΣxΣxy)/(n₂Σx² - (Σx)²)` | `[(n₂Σxy - ΣxΣy)/√((n₂Σx² - (Σx)²)(n₂Σy² - (Σy)²))]²` (Pearson r²) |

**Standard deviation of error**: `ē = Σe/n₂`; `σ = √( Σ(e-ē)² / n₂ )` over W (**population**, divisor n₂ not n₂-1). A14 (owner): keep population σ; tests pin it.
Regression axes: x = observed, y = modeled. Constructor default `m_r2=-1`, `m_slope=0`.
Degenerate: `n₂=0` or zero denominators -> legacy NaN/inf. Intended `Result<HwmStats>` error `InsufficientData`; `n₂=1` free mode -> error. `nValid() = n₂`; CLI prints `Processed n HWMs [Ignored n-n₂ dry locations]`.

**[N1]** `Hwm::readHWMData` (`hwm.cpp:339`) passes the `QCheckBox*` to `setRegressionThroughZero(bool)` -> always `true` in the GUI. Intended: honor the checkbox. Test: same data, both modes produce different slope/intercept.

Golden numbers: generate from an independent numpy script committed beside the fixtures (A15); do not hand-derive.

### 6.3 Error classification (map colors, `hwm.cpp:62-67`, `uihwmtab.cpp:57-80`)
`classify(e)`: for i in 0..6, if `e < c[i]` return i; else 7. Class `-1` = dry (white). Strict `<`: `e == c[i]` goes to the **upper** class.

| Units | c0..c6 (default) |
|---|---|
| Feet | -5.0, -3.5, -1.5, 0.0, 1.5, 3.5, 5.0 |
| Meters | -1.5, -1.0, -0.5, 0.0, 0.5, 1.0, 1.5 |
| Manual (checkbox) | user spin boxes `spin_class0..6`; must be strictly increasing else "Your classifications are invalid." (`hwm.cpp:297-326`) |

| class | range | map marker | scatter color |
|---|---|---|---|
| -1 | dry | `mm_20_white` | n/a (plotted on 1:1 line at (obs,obs), series 0, grey `#B8B8B8`) |
| 0 | e < c0 | purple | `#B8B8B8` |
| 1 | c0 ≤ e < c1 | darkblue | `#FF00FF` |
| 2 | c1 ≤ e < c2 | lightblue | `#8282CD` |
| 3 | c2 ≤ e < c3 | darkgreen | `#006600` |
| 4 | c3 ≤ e < c4 | lightgreen | `#00CC66` |
| 5 | c4 ≤ e < c5 | yellow | `#CCCC00` |
| 6 | c5 ≤ e < c6 | darkorange | `#FF9933` |
| 7 | e ≥ c6 | red | `#FF0000` |

(Marker order: `MovMapItem.qml:36-43`; legend text `MapLegend.qml:31-39` labels last bucket "> c6". Scatter palette only when "color dots" is on; else all wet classes use the user HWM color, default RGB(11,84,255) `mainwindow.cpp:691`. Map and scatter palettes differ — legacy inconsistency; v5 chooses one; classification itself is the contract.) Dry points in scatter: classified from `modeledError()` of the sentinel (hugely negative -> class 0). Bounding lines: `y = x ± k·σ` with k from spin box (`hwm.cpp:140-141,245-258`); 1:1 line `y=x`; regression line `y=m x+b`; label strings `"y = %0.2fx"` / `"y = %0.2fx + %0.2f"`, R² and σ `%0.2f`.

Tests: stats golden (both modes, with dry rows, `-99999` and `-999` and `-998` rows to pin threshold); classification boundaries at exactly `c_i` for each i; manual class validation; ft/m defaults; parse errors; trailing blank line; header line; all dry; one wet.

---

## 7. CRMS

### 7.1 Source CSV ingested by `ProcessCrmsDatabase` (`crmsdatabase.cpp`)
Large CRMS hourly export, comma separated (**no quoted-field handling**: a comma inside a value shifts columns), CRLF tolerated (`\r` stripped from header only; data lines parsed by `lexical_cast<float>` which tolerates trailing `\r`? it does not — row-end `\r` on the last column makes `lexical_cast` throw -> caught -> fill [N21]; strip `\r` from every line).
Header row (`readHeader :234-254`): any header text **not in** the exclusion list `{Station ID, Date (mm/dd/yyyy), Time (hh:mm:ss), Time Zone, Sensor Environment, Geoid, Organization Name, Comments, Latitude, Longitude}` is a **data parameter** column (order preserved = `numParam` index). `Geoid` index remembered, otherwise unused.
Data row:

| col (hard-coded position) | meaning |
|---|---|
| 0 | Station ID (e.g. `CRMS0589-H01`); rows MUST be grouped contiguously per station (no sort) |
| 1 | date `m/d/yyyy` (`%d/%d/%d`, no zero padding required) |
| 2 | time `h:m:s` |
| 3 | time zone: `CST` => add **21600 s** (UTC = local + 6 h); `CDT` => add **18000 s** (+5 h); anything else (e.g. `UTC`, `GMT`, blank) => offset 0 [N15 treat unknown as error/assume UTC? A16] |
| param cols | float; empty or unparsable -> fill `-9999.0f` |

Rows with `line.size() < 10` terminate station reading (`getNextStation :305`). Per-station time = `CDate::toSeconds()` (int64 epoch seconds UTC).
Output netCDF = dialect C (§5). `numParam` = number of param columns; `sensors(numParam,200)` holds header names (written with count = string length: **trailing bytes are default fill NUL**; reader `trim_right` + strip at first NUL).
Station counting bugs **[B26]**: (a) `prereadCrmsFile` only pushes a station on name *change*, so a file **without trailing newline drops the last station**; with trailing newline an empty final record makes `nstation` = N+1 while only N station vars exist (`parse :94-112`, `closeOutputFile`), so readers fail on var N+1 (`generateStationMapping` then uses an uninitialised varid); (b) `max_element` on empty vector when file has header only (UB); (c) `split[0..3]` unchecked on short rows; (d) strings >200 chars overflow `sensors`/`station_name`. Intended: `nstation` = number of distinct stations; empty file -> error; short rows -> `ParseError`.
Tests (tiny CSV in repo, ~10 rows, 3 stations, 3 params): LF/CRLF, with and without trailing newline, blank cells, non-numeric cell, CST/CDT/other tz, header-only file, ungrouped rows (intended error), param named like an excluded column, name >200.

### 7.2 Reading the CRMS netCDF (`crmsdata.cpp`)
| Function | Behavior |
|---|---|
| `generateStationMapping` `:109-135` | dims `nstation`,`stringsize`; for i in 0..n-1 var `data_station_%06i`, attr `station_name` (buffer `stringsize`=200, `trim_right`, cut at NUL) -> map name -> index. Return `ierr==0`. `nc_open` failure ignored; continues and returns garbage [B16]. |
| `readHeader` `:137-161` | `numParam`, `stringsize`, var `sensors`; per row `start={i,0},count={1,stringlen}`; trimmed. |
| `readStationList` `:163-255` | reads embedded `crms_stations.csv` (§8: `lon,lat,name`, **no header**) into name->(lat,lon); for each netCDF station finds coordinates by `station_name`; stations absent from CSV are **skipped**; start/end from time var attrs `minimum`/`maximum` parsed `yyyy/MM/dd hh:mm:ss` UTC; station `id` = position among **kept**... (`readCrmsMarkers :238-243`: `id = i` over the kept list, not the netCDF index — can diverge from the mapping index if any station was skipped) [N22]. Blank/short CSV line -> `sl[1]`,`sl[2]` out-of-bounds UB [B16]. |
| `retrieveData` `:38-107` | by station name -> index; reads time `int64 (stationLength_N)`, data float rows `(param i, :)`; keeps samples with `v > -9999.0f` **and** `minTime <= t <= maxTime` (seconds); **<5 kept samples -> parameter skipped**; each remaining parameter becomes one `HmdfStation` named `header[i]`, id = param index string, lat/lon = station's; `datum` argument ignored. `nc_open` result ignored, always returns 0 [B16]; unknown station returns 1. |
| `inquireCrmsStatus` | file exists. |

Intended io API: `CrmsFile::open -> expected`, `stations() -> vector<{name, start, end}>`, `read(station, range) -> map<param, TimeSeries>` with the same `>-9999` mask (as optional), min-5-samples rule left to caller. Tests: round trip with generator output; station absent from CSV skipped; <5 samples; range filtering inclusive both ends; float var typed read.

---

## 8. Station-list CSVs (`libraries/libmetocean/data/*`, embedded via `resource_files.qrc`; readers `stationlocations.cpp`)

All parsed as: line -> `simplified()` (collapses whitespace runs, strips CR) -> split. `QFile::readLine` w/o `Text` mode; CRMS file has CRLF.

| File | Rows (header incl.) | Delim | Header | Columns (0-based) | Lat/Lon order | Notes |
|---|---|---|---|---|---|---|
| `noaa_stations.csv` | 2663 data | `;` | **none** | 0 id, 1 name, **2 lon, 3 lat**, 4 start `MMM dd, yyyy`, 5 end or `present`, 6 MLLW, 7 MLW, 8 MSL(=0.0, ignored), 9 MHW, 10 MHHW, 11 NGVD29, 12 NAVD88 | lon,lat | offsets `-999999` = unknown. 331 rows `present`. Names contain commas (hence `;`). |
| `usgs_stations.csv` | 1+10503 | `;` | yes | 0 SiteNumber, 1 SiteName, **2 lat, 3 lon**, 4 `USGS`+number | lat,lon | 5 cols all rows. |
| `xtide_stations.csv` | 1+4166 | `;` | yes | **0 lat, 1 lon**, 2 Type (`Ref`/`Sub`), 3 StationID `xTide_NNNN`, 4 name, 5 MLLW (always 0.0, ignored), 6 MLW, 7 MSL, 8 MHW, 9 MHHW, 10 NGVD, 11 NAVD | lat,lon | UTF-8 (e.g. `Bahía`); names ending `Current` are current stations. 12 cols all rows. |
| `ndbc_stations.csv` | 1+357 | `,` | yes | 0 StationID, **1 lon, 2 lat** | lon,lat | `Station name = "NDBC_" + id`. |
| `crms_stations.csv` | 23029 | `,` | **none** | **0 lon, 1 lat**, 2 name (`CRMS0001-H01`...) | lon,lat | CRLF endings; 23029 unique names; used only to give coordinates to netCDF stations. |

Rules:
- **NOAA `present`** end date => legacy stores `QDate(2050,1,1)` and `active=true` (`stationlocations.cpp:68-78`). Intended: `ValidRange{start, optional end=nullopt}` (plan §2.2); test: row with `present` -> end nullopt/active; other rows -> exact date. Date format `MMM dd, yyyy` is locale-dependent in `QDateTime::fromString` (English month names only). Intended: parse English month abbreviations explicitly (`std::chrono::parse` is locale-neutral for `%b` in "C").
- **Empty start date** exists (e.g. `9751309;...;;Apr 09, 2011;...`); legacy keeps the station via `startDate.isValid() || endDate.isValid()` (`:74`). Intended: start optional.
- **[N9]** 31 NOAA rows (28 with 14 fields, 3 with 15) contain HTML entities in the name (`&rsquo;`, `&quot;`); the entity's `;` splits the field, shifting every later column (lon/lat/dates/offsets wrong). Test: all 2663 rows parse to exactly 13 fields after entity-aware handling, or the asset generator (`tools/`) decodes entities to Unicode and the delimiter never appears in a name. Recommend: fix in the generated v5 asset, and make the reader reject rows with the wrong field count.
- Offsets: `< -900` => unknown (`-9999.0` internal `Station::nullOffset`, compared `|x-(-9999)|<1e-4`). Intended `optional<double>`.
- NDBC longitude convention: values are signed east-positive in this file (30 rows have lon>0 and lat>0 e.g. `21413,152.132,30.533`), range [-180,180].
- Index-0 header skipped by counter (`index > 1`); NOAA has none -> first row is data. Blank lines: `list.value(i)` empty -> `0.0` coords (phantom station at 0,0). Intended: skip blank, error on wrong field count.
- Datum columns semantic: see §9 (shifts to add).
- `stations.txt`/`parse.sh`/`assemble*.sh` under `data/` are generator scripts (HTML scrape of NOAA inventory; NOAA datum API); out of io scope, inputs for the v5 `tools/` asset generator.
- Other embedded assets: `harmonics.tcd` (libtcd binary, 1.8 MB; consumed by XTide, out of scope here), `timezones.csv` (`abbr,name,region,offsetHours`; 8330 B; replaced by IANA per plan).

Tests: row counts above; per-file column mapping with one hand-checked row each; lat/lon order table (a wrong order puts stations in the ocean: assert NOAA 8731952 at lon -87.735 lat 30.30333333; USGS 01010070 lat 46.89388889 lon -69.7516667; NDBC 44006 etc.; CRMS first row lon -91.27923501 lat 29.42058380); `present` count 331; all-lat in [-90,90]/lon in [-180,180] property test; HTML-entity rows.

---

## 9. Datum and units

### 9.1 Datum IDs (`libraries/libmetocean/datum.h`)
`enum VDatum { NullDatum, MLLW, MLW, MSL, MHW, MHHW, NGVD29, NAVD88 }` (values 0..7). Names: `none, MLLW, MLW, MSL, MHW, MHHW, NGVD29, NAVD88`. `datumID(string)` accepts the same names **except `"MHW"` is missing** [N8] (falls to `NullDatum`); unknown -> `NullDatum`. GUI lists: NOAA native `{MHHW,MHW,MTL,MSL,MLW,MLLW,NAVD,LWI,HWI,IGLD,STND}` (`noaaDatumList`), VDatum `{MHHW,MHW,MSL,MLW,MLLW,NGVD29,NAVD88}` (`vDatumList`). Intended: `enum class VerticalDatum`, `from_string` includes MHW, case-insensitive; separate `NoaaNativeDatum`.

### 9.2 Offsets semantic (`hmdfstation.cpp:158-184`, `station.cpp:170`)
`data += shift` where `shift = station.<target>Offset()`; Offsets are "value to add to data **in the provider's native datum** to express it in the target datum".
- **NOAA CO-OPS** with "use VDatum" (`m_useVdatum`): request `&datum=MSL` (`noaacoops.cpp:128-134`), then add the station's offset for the chosen datum; native datum is therefore **MSL** (MSL offset = 0.0 stored, ignored). Non-VDatum path requests `&datum=<name>` directly and does no shift. `Stnd` is requested without datum param.
- **XTide** predictions native datum is **MLLW** (MLLW offset stored 0.0); other offsets are tabulated shifts (e.g. MSL = -2.199 for station xTide_0001: data in MSL = data in MLLW - 2.199).
- Unknown offset (`-9999` after load) => `applyDatumCorrection` returns 1 and **data are left unshifted** for that station; the overload over a whole `Hmdf` returns `false` and does *not* update the `datum` label. `NullDatum` target is a no-op returning success. No unit handling (meters assumed throughout; tables in meters).
- Intended: `shift(series, from, to, DatumTable) -> expected<TimeSeries, MissingOffset{station,datum}>`; `from` explicit; offset arithmetic `to - from` through MSL pivot: `v_to = v_from + (off[to] - off[from])` where each `off` is relative to the same pivot (NOAA table pivot MSL; XTide pivot MLLW) — A17: v4 only ever shifts from the provider's native datum, so v5 MUST keep the native-datum semantics; arbitrary from/to is an extension.
- The offset *tables* are in station-list CSVs (§8) and are the golden data.

Tests: each of the 7 targets on a NOAA row and an XTide row with hand-checked arithmetic; unknown offset -> error and data untouched; `NullDatum` no-op; `MHW` round trip through string; Hmdf label updated only on success.

### 9.3 Units conversions in the codebase (all GUI multipliers; **no unit logic in file readers**)
- Time-series add dialog (`addtimeseriesdialog.cpp:225-295`) fills the per-series **multiplier** `unit` (applied as `y' = y·unit + yshift`; x shift in **hours** `x' = t + xshift·3.6e6 ms`, `usertimeseries.cpp:228-232`):

| Length | factor | Speed | factor | Pressure | factor |
|---|---|---|---|---|---|
| m->ft | 3.28084 | m/s->mph | 2.23694 | mH2O->mb | 98.07 |
| ft->m | 0.3048 | mph->m/s | 0.44704 | mb->mH2O | 0.010197 |
| m->in | 39.3701 | mph->ft/s | 1.46667 | mH2O->Pa | 9806.38 |
| ft->in | 12.0 | m/s->kt | 1.94384 | Pa->mH2O | 0.00010197442889221 |
| mi->m | 1609.34 | kt->m/s | 0.514444 | mb->Pa | 100.0 |
| mi->ft | 5280.0 | m/s->ft/s | 3.28084 | Pa->mb | 0.01 |
| km->mi | 0.621371 | ft/s->m/s | 0.3048 | | |
| mi->km | 1.60934 | ft/s->kt | 0.592484 | | |
| | | kt->ft/s | 1.68781 | | |
Rounded 5-6 sig digits in legacy; v5 should use exact definitions (0.3048 exact; 1 kt = 1852/3600 m/s exact; mi = 1609.344 m exact). Tests assert exact-factor results within 1e-9 and **document** the legacy rounded values as non-goals.
- XTide tab ft multiplier `3.28084` (`xtide.cpp:129`). Session stores the chosen multiplier in `timeseries_units` (a double, not a unit name).
- Time zones: out of io scope (plan §2.7, IANA).

---

## 10. Session `.mvs` (`MetOceanViewer/src/session.cpp`)

netCDF4 (NC_NETCDF4, enhanced model; **NC_STRING variables**). No version attribute, no global attributes. Written whole, never appended.

### 10.1 Variables (writer `:149-231`; all written)
Dims: `ntimeseries` = number of table rows (can be 0), `one` = 1.

| Variable | Type | Dims | Meaning / table column | Required on open? |
|---|---|---|---|---|
| `timeseries_filename` | string | ntimeseries | path of data file, **relative to session dir** (`relativeFilePath(col 6)`) | **required** `:416` |
| `timeseries_colors` | string | n | `#rrggbb` (col 2) | required |
| `timeseries_names` | string | n | series name (col 1) | required |
| `timeseries_filetype` | **int** | n | enum §1 (col 8) | required (see B22) |
| `timeseries_coldstartdate` | string | n | `yyyy-MM-dd hh:mm:ss` (col 7); ADCIRC cold start | required |
| `timeseries_stationfile` | string | n | ASCII-ADCIRC station file, relative path (col **10**) | required |
| `timeseries_xshift` | double | n | hours (col 4) | required |
| `timeseries_yshift` | double | n | col 5 | required |
| `timeseries_units` | double | n | multiplier (col 3) | required |
| `timeseries_checkState` | int | n | 1 checked / 0 | **optional**: absent -> Checked (`:495-499,618`) |
| `timeseries_epsg` | int | n | col 11 | **required `:478`** (written since newer versions) |
| `timeseries_dflowvar` | string | n | col 12 | **required `:481`** |
| `timeseries_layer` | int | n | col 13 (1-based) | **required `:484`** |
| `timeseries_linestyle` | int | n | col 14: 0 none,1 solid,2 dash,3 dashdot,4 dashdotdot, other solid (`usertimeseries.cpp:196-211`) | **optional** `:487-493`; default 1 |
| `timeseries_plottitle` | string | one | | required |
| `timeseries_xlabel` | string | one | | required |
| `timeseries_ylabel` | string | one | | required |
| `timeseries_precision` | int | one | always written `3`; read but unused | required (inq only) |
| `timeseries_startdate` | string | one | `yyyy-MM-dd hh:mm:ss`; only the date part used | required |
| `timeseries_enddate` | string | one | same | required |
| `timeseries_ymin` | double | one | | required |
| `timeseries_ymax` | double | one | | required |
| `timeseries_autodate` | int | one | "use all data" check (0/other) | required |
| `timeseries_autoy` | int | one | y-auto check | required |

Table columns (`session.cpp:130-146,770-800`): 0 file base name (+checkbox), 1 name, 2 color, 3 unit mult, 4 xshift, 5 yshift, 6 full file path, 7 cold start, 8 filetype int, 9 station-file base name, 10 station-file full path, 11 epsg, 12 dflowvar, 13 layer, 14 linestyle.

### 10.2 Open semantics — intended
- Treat as **versionless, all-optional per-row variables** (plan §2.6): missing `epsg` -> 4326, `dflowvar` -> "", `layer` -> 1, `linestyle` -> 1, `checkState` -> 1, `stationfile` -> "", `coldstart` -> "" (A18: which are truly required? recommend only `ntimeseries` dim + `filename`).
- `timeseries_filetype` may be **int** (current) **or NC_STRING** (legacy fixtures, values `"IMEDS"`); intended: inspect `nc_inq_vartype`; map strings `IMEDS`->ASCII_IMEDS(4); unknown strings (`ADCIRC`? `NETCDF`? not observable from repo, A19) -> ERROR kind, row skipped with warning.
- Strings: read with `nc_get_var1_string`/`nc_get_vara_string`, **always `nc_free_string`** [B21], never `QString(char*)` of a possibly-null pointer (empty NC_STRING can be NULL).
- Paths: stored relative to the session file directory; open must resolve `abs(session_dir / rel)`; **absolute stored paths must stay absolute** [B20] (legacy always prefixes `currentDirectory + "/"`, `:630`; also `relativeFilePath` across drives on Windows returns absolute).
- Missing `linestyle` must not show an "Error Saving" dialog while opening [B20] (`NETCDF_ERR` called unconditionally `:487`).
- Missing referenced files: skipped rows (UI asks for alternate folder: UI concern); io returns per-row status.
- Rows whose `ntimeseries` read count differs from var length -> error.
- Dates: only the date of start/end used (time part dropped); round-trip keep full string.

### 10.3 Save — intended
Write to temp file in same dir then atomic rename **[B19]** (legacy deletes the old file first, `:101`); on any error leave the old file intact, remove temp; RAII `ncid` [B5]; write all variables of §10.1 incl. int `filetype`; precision stays `3` unless owner changes; relative paths via `std::filesystem::relative` with fallback to absolute when no relative path exists; plot-title etc. possibly empty strings.
Note legacy save/open asymmetry: save writes column **10** (full) path as stationfile, open fills col 9 (base name) and col 10 (resolved path).

### 10.4 Why the committed fixtures fail to load — **[B22]**
`MetOceanViewer/function_tests/SessionFiles/session_read_test_{new,old}Format.mvs` (see §12): both lack `timeseries_epsg`, `timeseries_dflowvar`, `timeseries_layer`, `timeseries_linestyle`; the old file also lacks `timeseries_checkState`. `Session::open` aborts at the first **required** missing var: `nc_inq_varid(..."timeseries_epsg")` (`session.cpp:478`) returns `NC_ENOTVAR` -> `NETCDF_ERR` shows a modal "Error Saving File" dialog and returns 1 (before the `checkState`/`linestyle` optional logic is even reached, and before any row is read). Additionally `timeseries_filetype` is NC_STRING in both; the untyped `nc_get_var1(…, &mydataint)` (`:570`) would copy an 8-byte `char*` into an `int[1]` (stack overwrite). Expected result with intended behavior: 2 rows each; new format: checkState {1,0}, old: {1,1}; both: names {MLLW, MSL}, colors {#00ff00,#ff0000}, xshift {6,5}, yshift {2,3}, units {3.28084,1}, coldstart {"2015-08-02 00:15:32","2015-08-02 00:15:59"}, filetype IMEDS, files `../ReadIMEDS/{mllw,msl}.imeds`, title "IMEDS Read Test", x label "Date (GMT)", y label "Water Surface Elevation (ft)", precision 3, start 2015-07-01 00:00:00, end 2015-08-02 23:59:59 (new) / 2015-08-02 00:00:00 (old), ymin=ymax=0, autodate=autoy=1.

Tests: both fixtures load with exact values above; synthetic session with all variables; session lacking only `linestyle`; int-typed filetype; save/open round trip; save failure (read-only dir) leaves old file byte-identical [B19]; absolute path round trip [B20]; leak test under ASan/LSan for string reads [B21]; zero-row session; 0-length strings.

---

## 11. Regression-test index for §1.2 bugs in io scope

| Bug | Regression test (name suggestion) |
|---|---|
| B1 | `adcirc_ascii.vector_magnitude_is_sqrt` (fort.62 step1 st1 = 0.050793...) and netCDF parity |
| B2 | `dflow.open_reports_failure_for_non_dflow_and_success_for_dflow` |
| B3 | `imeds.reads_read_only_file`, `imeds.missing_file_is_error` |
| B4 | `adcirc_nc.float_variables_match_double`, `dflow.float_vars`, `generic_nc.float_data` (float fixtures) |
| B5 | `nc.no_fd_leak_on_error_paths` (loop 1000 failing opens/reads, assert `/proc/self/fd` count stable; LSan) for each reader/writer |
| B6 | `generic_nc.open_nonexistent_does_not_close_garbage` (ASan/UBSan; writer to unwritable dir) |
| B7 | `generic_nc.name_len_not_200` (len 50 and 300) ; same for `dflow.name_len_64` [B11] |
| B8 | `generic_nc.long_reference_date_attr` (attr length 120 -> no overflow; ASan) |
| B9 | `generic_nc.default_fill_value_is_masked`, `_FillValue` explicit, NaN |
| B10 | `generic_nc.epsg_error_is_not_epsg` (missing file/att -> error object, never an int) |
| B11 | `dflow.name_len_64`, `dflow.time_units_variants` |
| B12 | `dflow.missing_dim_or_var_is_error`, `dflow.layer_dims_checked`, `dflow.fill_from_attr` (-999 vs 9.96e36 vs NaN) |
| B13 | `hmdf.data_bounds_two_stations` |
| B14 | `hmdf_station.bounds_empty_is_nullopt` |
| B15 | `generic_nc.writer_utf8_and_short_names`, `generic_nc.writer_units_attr_full` |
| B16 | `crms.open_failure_reported`, `crms.station_csv_blank_line`, `crms.long_attr` |
| B19 | `session.failed_save_preserves_old_file` |
| B20 | `session.open_without_linestyle_no_error`, `session.absolute_paths_preserved` |
| B21 | `session.read_strings_leak_free` (LSan) |
| B22 | `session.legacy_fixtures_load` (both) |
| B25 (io part) | `station_lists.parse_once` (asset loaded once; no per-line re-parse; row counts §8) |
| B26 | `crms_csv.last_station_without_newline`, `crms_csv.trailing_newline_station_count`, `crms_csv.header_only`, `crms_csv.short_row`, `crms_csv.long_string` |
| out of io scope | B17, B18 (GUI/print), B23 (update dialog), B24 (CLI menu; XTide label), B24-USGS index (provider/CLI phase) |

---

## 12. Fixture inventory (all sizes from `ls -l`)

Existing test data in repo (`MetOceanViewer/function_tests/`; no other test data exists: no HWM, DFlow, CRMS, generic-netCDF fixtures; no Catch2 tests yet):

| File | Format | Size B | Exercises | Data type / facts |
|---|---|---|---|---|
| `ReadIMEDS/mllw.imeds` | IMEDS ASCII | 674,371 | 3 header lines, 2 stations (`NOAA_8413320` 44.3917,-68.205; `NOAA_8531680` 40.4669,-74.0094), 6-minute rows 2015-07-01.., 7-field rows, header3 `NOAA UTC MLLW`, values e.g. 2.605..4.027 | **no trailing newline**; LF; 15,363 non-header data lines total (~7.68 k/station); no fill values |
| `ReadIMEDS/msl.imeds` | IMEDS ASCII | 681,173 | same layout, stations `NOAA_8534720` (39.355,-74.4183) and `NOAA_8413320`; header3 `... MSL` | trailing newline; values negative allowed (min -0.993); station order differs from mllw (tests station matching) |
| `ReadADCIRC/ASCII/fort.61` | ADCIRC ASCII elev | 19,892 | 144 snaps x 3 stations, DT 600, NCOLS 1, fill `-99999` at snap 1 st 1 (see `-9.9999000000E+004`), header line `FileFmtVersion: 1050624` | text |
| `.../fort.62` | ASCII velocity | 9,908 | 48 x 3, DT 1800, NCOLS 2 (B1) | text |
| `.../fort.71` | ASCII pressure | 6,743 | 48 x 3, NCOLS 1 | text |
| `.../fort.72` | ASCII wind | 9,911 | 48 x 3, NCOLS 2 | text |
| `.../stations.csv` | ADCIRC station file | 57 | `3` then `lon,lat ` x3 (-90.0127,29.987793; -90.5,28.0; -91.0,25.0), trailing spaces, no names | text, no trailing newline? ends `-91.0,25.0` (padded) |
| `ReadADCIRC/netCDF/fort.61.nc` | netCDF4-classic (HDF5; `nc_inq_format`=4) | 4,230,686 | dims time(unlim)=144, station=3, namelen=50; vars `time` double, `station_name` char(3,50) ("Station One/Two/Three"), `x`,`y` double, `zeta` **double** `_FillValue=-99999`; global `model="ADCIRC"`, `_FillValue`, `dry_Value`, ~65 attrs (units attr text is the junk string "seconds since Met") | **double**; x=-90.0127,-90.5,-91; y=29.9878,28,25; time 600,1200,... |
| `.../fort.62.nc` | same | 4,220,772 | time=48; `u-vel`,`v-vel` **double** `_FillValue -99999` | double |
| `.../fort.71.nc` | same | 4,215,695 | `pressure` **double** | double |
| `.../fort.72.nc` | same | 4,220,648 | `windx`,`windy` **double** | double |
| `SessionFiles/session_read_test_newFormat.mvs` | netCDF4 (format 3), 2 rows | 14,960 | lacks epsg/dflowvar/layer/linestyle; has checkState {1,0}; filetype NC_STRING | strings; doubles; ints (§10.4 values) |
| `SessionFiles/session_read_test_oldFormat.mvs` | netCDF4 | 14,668 | additionally lacks checkState; enddate `2015-08-02 00:00:00` | as above |
| `libraries/libmetocean/data/{noaa,usgs,xtide,ndbc,crms}_stations.csv` | station CSV | 365,847 / 862,930 / 563,638 / 7,639 / 875,699 | §8 (real data, use as golden row counts) | text (xtide UTF-8; crms CRLF) |
| `libraries/libmetocean/data/noaa/stations.txt` | HTML scrape | 440,654 | generator input only | not a runtime format |
| `libraries/libmetocean/harmonics.tcd` (+ `thirdparty/xtide-2.15.1/harmonics.tcd`) | libtcd binary | 1,798,472 | XTide; not io-scope | binary |
| `libraries/libmetocean/timezones.csv` | CSV | 8,330 | replaced by IANA | text |

Observations: all four ADCIRC netCDF files are ~4.2 MB for ~1-3 kB of data (HDF5 chunking of the unlimited dim and attribute headers); consider shrinking/regenerating for the repo (git-lfs unnecessary). All netCDF fixtures hide B4 (double only). The IMEDS fixtures hide B3 (read-write perms) and, because the last line of `mllw.imeds` has no `\n`, do exercise EOF-without-newline.

### 12.1 Proposed new fixtures (as built: text under `tests/fixtures/{core,io}/`, netCDF generated at test time by `tests/io/support/`; core-design.md §7.3)
| ID | Fixture | Purpose |
|---|---|---|
| F1 | `adcirc/fort.61_float.nc`, `fort.62_float.nc`, `fort.72_float.nc` (float-typed `zeta`/`u-vel`/`windx`, `_FillValue=-99999f`, plus one no-`_FillValue` file with default fill) | B4 |
| F2 | `adcirc/fort.61_small.nc` (3 stations x 5 steps, handwritten values incl. one fill) | fast golden tests, no 4 MB blobs |
| F3 | `adcirc/fort.62_vfill.nc` (u valid, v fill) | N7 |
| F4 | `adcirc/ascii_badheader`, `fort.61` truncated, station file with names / spaces / CRLF / count mismatch | error paths, N6 |
| F5 | `dflow/his_2d.nc`, `his_3d.nc` (3 layers, `laydim`,`laydimw`), `his_float.nc`, `his_namelen64.nc`, `his_namelen20.nc`, `his_units_minutes.nc` (`minutes since 2001-02-03 04:05:06 +00:00`), `his_nofill.nc`, `his_notime.nc` | whole §4; no DFlow fixture exists today |
| F6 | `generic/stations_A.nc` (written by legacy-compatible writer; 3 stations, names 5/50/150 chars, `stationNameLen`=200), `stations_len50.nc`, `stations_len300.nc`, `stations_float.nc`, `stations_fill_default.nc` (9.96921e36 entries), `stations_fill_explicit.nc`, `stations_epsg26915.nc`, `stations_noepsg.nc`, `stations_longref.nc` (120-byte `referenceDate`), `stations_empty_station.nc`, `stations_utf8.nc` | §5, B7-B10, B15 |
| F7 | `crms/crms_small.csv` (3 stations, 3 params, CST/CDT/UTC rows, blanks, non-numeric, LF), `crms_crlf.csv`, `crms_no_trailing_newline.csv`, `crms_header_only.csv`, and expected `crms_small.nc` (generated) + `crms_stations_small.csv` | §7, B26 |
| F8 | `hwm/hwm_basic.csv` (hand-computable, 8 rows: wet, 2 dry at -99999, one at exactly a class boundary), `hwm_ft.csv`, `hwm_header.csv`, `hwm_blank_line.csv`, `hwm_allDry.csv`, `hwm_one_wet.csv`, with golden stats produced by a committed Python (numpy) script | §6, N1, N2 |
| F9 | `imeds/readonly.imeds` (chmod 0444 at test time, copy of small file), `imeds/crlf.imeds`, `imeds/tabs.imeds`, `imeds/no_seconds.imeds` (6-field integer and fractional values, N3), `imeds/empty_station.imeds`, `imeds/blank_line.imeds`, `imeds/empty.imeds`, `imeds/bad_date.imeds` | §2, B3, N3, N13 |
| F10 | `session/legacy_*.mvs` (existing two, copied), `session/full.mvs` (all vars, int filetype), `session/no_linestyle.mvs`, `session/abs_paths.mvs`, `session/zero_rows.mvs`, `session/string_filetype_unknown.mvs` | §10 |
| F11 | `stations/*_small.csv` excerpts incl. NOAA `&rsquo;` rows, empty start-date row, `present` row; plus whole-file row-count test on the real assets | §8, N9 |
| F12 | `datum/offsets.csv` (hand-written table for shift arithmetic) | §9 |

Fixture generation: `tools/` gets a C++ (or `ncgen` CDL via vcpkg `netcdf-c` tools) generator executed at CMake configure or committed as binaries; no python `netCDF4` available on this host.

---

## 13. Resolved/open items (owner input needed, flagged `A#` above)

A1 IMEDS tz token; A2 null token for IMEDS data; A3 IMEDS write precision (`%10.4e` vs round-trip); A4 default station name base (0/1); A5 use ADCIRC netCDF `station_name`; A6 verify real D-Flow FM `time:units` strings; A7 generic EPSG default when absent; A8 collapse internal whitespace in station names; A9 accepted station-numbering patterns; A10 missing `referenceDate`; A11 single new generic-netCDF dialect definition; A12 HWM header line tolerance; A13 dry threshold (-999 chosen); A14 population vs sample σ (keep population); A15 golden HWM numbers via independent script; A16 CRMS unknown time zone handling; A17 datum shift semantics (native-datum only); A18 session required set; A19 legacy string filetype vocabulary.

---

## 14. New bugs found (not in plan §1.2) — add to the plan table

| # | Location | Bug |
|---|---|---|
| N1 | `MetOceanViewer/src/hwm.cpp:339` | `setRegressionThroughZero(this->m_checkForceZero)` passes a `QCheckBox*` -> always true; GUI "force through zero" checkbox has no effect on stats (the regression plot string uses the checkbox correctly, mismatching the fitted numbers). |
| N2 | `highwatermarks.cpp:118,146,170`; `hwm.cpp:81,183-202`; `hwmdata.cpp:59` | Inconsistent dry thresholds (-999 / -9999 / -900). Points with modeled in (-9999,-999] are excluded from fit but included in SStot; map/scatter disagree. |
| N3 | `hmdfasciiparser.cpp:107-131` | 6-field row with fractional value parsed as 7-field (sec=int part, value=fraction); no end-of-input check. |
| N5 | `usertimeseries.cpp:366-367,383-384` | ADCIRC cold start parsed as local time (no `Qt::UTC`) -> DST/offset dependent shifted series. |
| N6 | `adcircstationoutput.cpp:123-128,318` | ASCII station names get a leading space; default names inconsistent (`Station_i` vs `Station i`); netCDF `station_name` ignored. |
| N7 | `adcircstationoutput.cpp:93,289,298` | Fill test applied to first component only for vector outputs. |
| N8 | `datum.h:40-57` | `datumID("MHW")` returns `NullDatum`; `hmdf.cpp:485` calls unqualified `datumName`. |
| N9 | `libraries/libmetocean/data/noaa_stations.csv` | 31 rows contain `&rsquo;`/`&quot;` whose `;` breaks the `;` delimiter (columns shift). |
| N13 | `hmdf.cpp:121-133` | Blank line / missing header lines -> `QList::at` out of range. |
| N15 | `ProcessCrmsDatabase/src/crmsdatabase.cpp:266-271` | Only `CST`/`CDT` recognised; all other zones silently treated as UTC; column positions 0-3 hard-coded though header drives param columns. |
| N16 | `dflow.cpp:153,181,628` | Wind variables on 3-D files request layer 0 -> `start[2] = size_t(-1)`. |
| N17 | `hmdfstation.cpp:26`, `hmdf.cpp:467` | `isNull` defaults true and only CRMS clears it, so `Hmdf::dataBounds` skips every IMEDS/ADCIRC/DFlow/generic station (masked by B13). |
| N18 | `hmdf.cpp:229-244` | IMEDS/CSV writers truncate precision and emit `-DBL_MAX` for nulls. |
| N19 | `hmdfstation.cpp:60,68,76,81` | bounds assert uses `\|\|` (always true) so the out-of-range fallback never triggers; plus `HmdfStation::date(int)` on negative index. |
| N20 | `highwatermarks.cpp:85-95` | Blank/non-numeric lines create zero-valued HWMs silently. |
| N21 | `crmsdatabase.cpp:280` | CRLF files: `\r` is stripped from header only; last data column `lexical_cast<float>` fails -> fill. |
| N22 | `stationlocations.cpp:238-243` | CRMS station id = index into the *filtered* list, not netCDF station index; stations missing from the CSV shift ids. |
