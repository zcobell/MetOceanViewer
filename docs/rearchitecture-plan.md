# MetOceanViewer v5 — Re-architecture Plan

Status: **proposed** (review completed 2026-10-06 against commit `e5a4e0af`, v4.5.1).
All `file:line` references below are relative to that commit.

## 0. Summary

- **Do not port to Python.** Rewrite in place on **Qt 6 + QML + MapLibre Native Qt**
  with a new, Qt-free, strongly typed C++23 core.
- The GUI is rewritten from scratch, the data layer is heavily restructured (most
  logic is salvageable in *intent*, little is salvageable as *code*), and the build
  and distribution are replaced entirely.
- Order of work: build/CI/packaging first, then headless core + tests, then
  providers + CLI, then the GUI.
- The legacy v4 code stays buildable on `master` (or a `legacy` branch) until v5
  reaches feature parity.

### Why not Python

1. **The map is the hard requirement.** No Python GUI toolkit has a native GPU
   vector map. A Python port ends up embedding MapLibre GL JS in a webview, i.e. a
   web app with Python plumbing.
2. **Distribution gets harder, not easier.** Shipping a notarized macOS DMG and a
   Windows installer that bundle a Python runtime, netCDF/HDF5 and a binding to
   XTide (C++) is more fragile than `macdeployqt`/`windeployqt` on one binary.
3. **The domain logic is small.** About 3–4k lines of real logic (parsers,
   fetchers, datum shifts, HWM statistics) map directly onto a typed C++ core.

The only credible alternative considered was a Tauri-style app (MapLibre GL JS +
uPlot). It was rejected because netCDF and XTide would need a separate native
backend anyway.

---

## 1. Current state: findings

The codebase is about 14k hand-written lines, plus a 5.4k-line `mainwindow.ui`.
The vendored `thirdparty/` tree is excluded from that count.

### 1.1 Architecture problems

- **`MainWindow` god object.** It has about 100 `on_*` slots (`MetOceanViewer/src/mainwindow.h`).
  The same per-source tab code is copy-pasted seven times: NOAA, USGS, NDBC, XTide,
  CRMS, user time series and HWM, each with its own `ui*tab.cpp`.
- **Controllers are coupled to widgets.** For example, `Noaa` takes about 12 raw
  widget pointers in its constructor (`MetOceanViewer/src/noaa.cpp:30`). Business logic
  reads directly from `QComboBox::currentIndex()`.
- **Blocking network on the UI thread.** Requests are made with nested
  `QEventLoop::exec()`:
  - `libraries/libmetocean/noaacoops.cpp:141`
  - `libraries/libmetocean/usgswaterdata.cpp:78`
  - `libraries/libmetocean/ndbcdata.cpp:96`
  - `MetOceanViewer/src/updatedialog.cpp:123`

  There is also a busy-wait, `Generic::delay()` with `processEvents`
  (`libraries/libmetocean/generic.cpp:31`).
- **Error handling.**
  - Functions return `int` codes, often accumulated with `ierr +=`.
  - Errors also come back as string-ly typed `errorString()`.
  - A single global enum (`MetOceanViewer/src/metoceanviewer.h`) mixes every subsystem.
- **Ownership is raw `new`/`delete` through `QObject` parents.** `HmdfStation*`
  instances are shared between two `Hmdf` parents
  (`MetOceanViewer/src/usertimeseries.cpp:659-661`).
- **Map performance is limited by the design.**
  - Every station is a QML `MapQuickItem` delegate (`MetOceanViewer/qml/MovMapItem.qml`).
  - Selection is a JS loop over `mapItemView.children` (`MetOceanViewer/qml/MapViewer.qml`).
  - That is why viewport filtering, `MAX_NUM_DISPLAYED_STATIONS 250`
    (`MetOceanViewer/src/errors.h`) and the four debounce timers exist.
- **Time zones.** `libraries/libmetocean/timezone.cpp` (1221 lines) is 238
  generated `insert` calls mapping abbreviations to fixed offsets. It has these problems:
  - No DST handling.
  - Ambiguous abbreviations (AST, ACT, AMST) resolve to the first hit.
  - `offsetFromUtc` ignores its `location` argument (`:127`).
  - The table is rebuilt every time a `Timezone` is constructed.
- **Station lists.** Five CSVs are embedded via `resource_files.qrc` (about 2.7 MB,
  40.7k lines). Their delimiters (`;` vs `,`), headers and lat/lon column order are
  inconsistent. NOAA "present" is stored as 2050 (`stationlocations.cpp:69`). The
  lists are re-parsed on every `readMarkers()` call.
- **Three incompatible "station netCDF" dialects:**
  - CRMS: `%06i` names, plus `station_name`/`reference` attributes.
  - `Hmdf::writeNetcdf`: `%4.4i` names, plus `referenceDate`.
  - `NetcdfTimeseries`: `%04i` names.
- **Startup requires internet.** It does a blocking GET to `http://www.google.com`
  with no timeout and exits if offline (`MetOceanViewer/src/main.cpp:38`, `generic.cpp:67-77`).

### 1.2 Confirmed correctness bugs in shipped v4.5.1

The first three bugs were verified by reading the source directly.

| # | Location | Bug |
|---|----------|-----|
| 1 | `MetOceanViewer/src/adcircstationoutput.cpp:98-100` | ASCII vector magnitude computes `pow(u²+v², 2)` instead of `sqrt`. |
| 2 | `MetOceanViewer/src/dflow.cpp:23-29` | Init status is inverted: on error it sets `_isInitialized = true`. |
| 3 | `libraries/libmetocean/hmdf.cpp:105-106` | `readImeds` opens with `std::fstream` (in\|out mode), so read-only files fail to open. `fid.bad()` does not catch this, so it returns success with no data. |
| 4 | `adcircstationoutput.cpp:238,275,283` | Untyped `nc_get_vara` / `nc_inq_var_fill` into `double` misreads `float` variables. The test fixtures are all `double`, which hides this. |
| 5 | `adcircstationoutput.cpp:162-305`, `dflow.cpp:450-551`, `session.cpp` (about 40 returns), `crmsdata.cpp` | `ncid` leaks on error paths. `dflow::_getTime` never closes the file. |
| 6 | `libraries/libmetocean/netcdftimeseries.cpp:58`, `hmdf.cpp:265` | The `NCCHECK` macro calls `nc_close` on an uninitialised `ncid` when `nc_open` fails. |
| 7 | `netcdftimeseries.cpp:90` | `substr(200*i, 200)` overruns the buffer whenever the name length is not 200. |
| 8 | `netcdftimeseries.cpp:117` | `nc_get_att_text` writes into a fixed 80-byte buffer without checking `attlen` (buffer overflow). |
| 9 | `netcdftimeseries.cpp:124` | Data equal to `NC_FILL_DOUBLE` is never masked. |
| 10 | `netcdftimeseries.cpp:184-188` | `getEpsg` returns netCDF error codes as if they were EPSG codes. |
| 11 | `dflow.cpp:478,530` | Name stride is hard-coded to 200 instead of `name_len`. The reference date is parsed with a hard-coded `substr(14,19)`. |
| 12 | `dflow.cpp:291,391,494` | `QMap::operator[]` silently returns id 0 for missing dim/var names. `_get3d` checks `laydimw` but reads `laydim`. Fill value is hard-coded as `-999.0` and compared with `==`. |
| 13 | `hmdf.cpp:459-460` | `dataBounds` initialises min/max inverted, so the date bounds never update. |
| 14 | `hmdfstation.cpp:135` | `dataBounds` dereferences `end()` on empty data. |
| 15 | `hmdf.cpp:321,329,378,416` | `QString::length()` used as the UTF-8 byte length. `units` written with length 3. `nc_put_vara_text` with count 200 over-reads shorter strings. |
| 16 | `crmsdata.cpp:41-106,127,179-182,228` | `nc_open` result ignored and the function always returns 0. 200-byte attribute buffers unchecked. Blank CSV lines index `sl[1]`/`sl[2]`, which is UB. |
| 17 | `MetOceanViewer/src/usertimeseries.cpp:151` | Compares a `QString*` with `QString()`, so the empty check never fires. |
| 18 | `usertimeseries.cpp:100-102` | `QPainter` is constructed on the printer and then `begin()` is called again. |
| 19 | `MetOceanViewer/src/session.cpp:101` | The existing session file is deleted before the new one is written, so a failed save loses data. |
| 20 | `session.cpp:486,630,700` | A missing `linestyle` shows an "Error Saving" dialog while *opening*. Paths are always prefixed with the current directory, which breaks absolute paths. |
| 21 | `session.cpp` | `nc_free_string` is never called, so every string read leaks. |
| 22 | `MetOceanViewer/function_tests/SessionFiles/*.mvs` | The fixtures no longer load. They store `filetype` as a string, so `Session::open` aborts at `epsg` (`session.cpp:478`). The untyped read of `filetype` would also write 8 bytes into an `int[1]`. |
| 23 | `MetOceanViewer/src/updatedialog.cpp:44,75,129` | `operator<` and `operator>` order Dev/Rev inconsistently. The reply is never deleted. The remote URL is injected into rich text unescaped. |
| 24 | `MetOceanData/metoceandata.cpp:335-345,407,548,564,584,593` | CLI off-by-one bugs in menu bounds. XTide output is always labelled "MLLW". USGS indexes `station(i)` after stations have been skipped. |
| 25 | `MetOceanData/options.cpp:240,288` | `checkIntegerString` can never return -1. The whole qrc CSV is re-parsed for every station line. |
| 26 | `ProcessCrmsDatabase/src/crmsdatabase.cpp:137-157,261-266` | `max_element` on an empty vector. Unchecked `split[0..3]`. The last station is dropped when the file lacks a trailing newline. Strings over 200 chars overflow. |

### 1.3 Build and distribution problems

- **Installer scripts hard-code one developer's machine.** Each targets a different Qt version:
  - `MetOceanInstaller/buildInstaller_Unix.sh` — 5.12.1 static, IFW 3.0
  - `buildInstaller_Windows.sh` — 5.13.1, MSVC2017
  - `buildInstaller_Windows_mingw.sh` — 5.14.2, MinGW under cygwin
  - `buildDmg_MacOSX.sh` — 5.14.1
- **Windows deploy check never runs.** The scripts test `$windDeployQtBinary`, but the
  variable is `winDeployQtBinary` (`buildInstaller_Windows.sh:19`, `_mingw.sh:12`).
- **The MinGW script mixes ABIs.** It links against the MSVC netCDF import libraries.
- **Unix bundling parses `ldd` output.** It only copies netCDF/HDF5 and misses curl,
  zlib, proj, ssl and libstdc++.
- **Prebuilt Windows binaries are committed.** These include netCDF 4.3.3.1 (2015),
  HDF5 1.8.14, libcurl 7.35.0 (2014), zlib 1.2.8 and OpenSSL 1.1.1c (end of life).
  All have known CVEs.
- **`thirdparty/boost_1_67_0` (150 MB, most of the 135 MiB git pack)** is used only for:
  - `algorithm/string`, `format`, `lexical_cast`
  - `progress.hpp`
  - Spirit Qi/Phoenix, in `hmdfasciiparser.cpp`, `noaacoops.cpp` and `cdate.cpp`
- **Version numbers disagree across files:**
  - `version.h` says 4.5.1.
  - `global.pri` and the IFW packages say 4.4.0.
  - `config.xml` says 4.0.0.
  - The netCDF package claims 4.4.1, but the DLL is actually 4.3.3.1.
- **Platform metadata is thin.** There is no Info.plist and no Windows VERSIONINFO.
- **`.travis.yml` is dead.** It targets Xenial, travis-ci.org and the abandoned
  `beineri` PPA, and runs no tests.
- **`ProcessCrmsDatabase/CMakeLists.txt` is broken.** It has no `project()` and
  hand-rolls `-std=c++11`.
- **There are no automated tests.** `MetOceanViewer/function_tests` contains fixtures only.

### 1.4 External endpoints in use

- NOAA CO-OPS: `api.tidesandcurrents.noaa.gov/api/prod/datagetter` (`noaacoops.cpp:120`)
- NDBC: `ndbc.noaa.gov/view_text_file.php` (`ndbcdata.cpp:82`)
- USGS: `nwis.waterdata.usgs.gov/usa/nwis/uv` and `waterservices.usgs.gov/nwis/{iv,dv}`
  (`usgswaterdata.cpp:62-69`). **These are legacy NWIS endpoints that USGS is
  retiring in favor of its new Water Data APIs. Verify the current status and
  timeline before implementing the USGS provider; write against the new API.**
- CRMS: `metoceanviewer.s3.amazonaws.com/crms.nc`, produced by `ProcessCrmsDatabase`
- Update check: `raw.githubusercontent.com/.../mov_release_revision.txt`
- Tiles: OSM, Mapbox (user key) and Esri

---

## 2. Target architecture

### 2.1 Repository layout

```
CMakeLists.txt  CMakePresets.json  vcpkg.json
cmake/
src/core/        # C++23, NO Qt: domain types, units, datums, stats, time series
src/io/          # NO Qt: RAII netCDF wrapper; IMEDS, ADCIRC (ascii/nc), DFlow, CSV, HWM, generic nc
src/providers/   # NOAA CO-OPS, USGS, NDBC, XTide; Qt Network only at the edge
src/app/         # view-models (QObject / QML_ELEMENT), AppState, commands, settings (arrives in Phase 4)
src/ui/          # startup code (mov_ui), main.cpp and the metoceanviewer executable
src/ui/qml/      # QML module MetOceanViewer: map shell, panels, chart, theme
src/cli/         # metocean-data CLI (+ hwm-stats subcommand)
tools/           # station-list builder
tests/           # Catch2; recorded API fixtures; golden files
packaging/       # macos/, windows/, linux/ — CPack config, icons, DMG background, Info.plist
docs/
```

Dependencies may only point in one direction: `core ← io ← providers ← app ← ui`.
Enforce this with CMake target dependencies, so that `core` and `io` cannot link Qt.
The CLI depends on `providers` and below, never on `app` or `ui`.

### 2.2 Type-driven domain model (Ben Deane style: make illegal states unrepresentable)

| Today | Becomes |
|---|---|
| `QString id` for every provider | `StationId<Provider>` (tagged strong type). Passing a USGS id to the NOAA fetcher does not compile. |
| `m_productIndex == 0`, `"wind:speed"` split on `:` | `using CoopsProduct = std::variant<WaterLevel, Predictions, Wind<Component>, AirTemp, …>`. Each alternative provides `units()`, `label()` and `query_params()`. |
| `-std::numeric_limits<double>::max()` nulls, `modeled < -900` means "Dry" | `std::optional<Length>` or an explicit `enum class WetDry`. No in-band sentinels anywhere. |
| Valid dates 1900 / 2050 meaning unbounded | `ValidRange{ sys_days start; std::optional<sys_days> end; }` |
| `qint64` ms plus ad-hoc offset arithmetic before plotting | `std::chrono::sys_time<milliseconds>` everywhere. The time zone is a display-only concern (`QTimeZone`, IANA). |
| `"MSL"`, `"Stnd"`, `Datum::datumID(QString)` | `enum class VerticalDatum`, converted with `shift(TimeSeries, from, to, const DatumTable&) -> Result<TimeSeries>` |
| One `Station` class carrying HWM fields | Separate `GaugeStation`, `HighWaterMark` and `ModelStation` types |
| Two `Hmdf*` in a vector (index 0 = observed, 1 = predicted) | `struct ObsVsPred { TimeSeries observed; TimeSeries predicted; };` |
| `int` codes and `errorString()` | `std::expected<T, Error>`, where `Error` is a per-domain variant (`NetError`, `ParseError`, `NcError{code, var}`, …) formatted only at the UI edge |
| `TimeRange` checked at runtime in `Noaa::fetchNOAAData` | `TimeRange::make(a, b) -> expected<TimeRange, …>`. A constructed `TimeRange` is always valid. |
| Units as display strings | Strong `Length`/`Speed`/`Temperature` types carrying their unit, or adopt mp-units if the overhead is acceptable |
| QML `markerMode === 0..3` | A typed layer style per layer |

`TimeSeries` is a plain value type: a struct of arrays (`std::vector<sys_time<ms>>`,
`std::vector<double>`) plus metadata. It is not a `QObject` and is cheap to move.

Parsers are pure functions, for example
`parse_coops_json(std::string_view) -> expected<TimeSeries, ParseError>`.

Replace Boost with `std::format`, `<charconv>`/`std::from_chars` and `std::ranges`/`std::views::split`.

**netCDF:** write one small RAII wrapper, used by every reader and writer:
- `NcFile` closes on destruction.
- Typed `get<T>`/`put<T>` calls check `nc_inq_vartype`.
- Every call returns `expected`.
- String attributes are sized from `attlen`.
- Fill values are read from attributes and turned into an `optional`/mask.

This fixes bug classes 4–16 structurally.

**One station netCDF dialect**, documented in `docs/`. Readers accept the three
legacy dialects; the writer emits only the new one.

### 2.3 Async model

- Providers satisfy a concept like `fetch(const Request&) -> QFuture<expected<TimeSeries, Error>>`.
  Use Qt 6 `QFuture::then`, or QCoro if coroutines are preferred; pick one and use it consistently.
- Run CPU-heavy work on `QtConcurrent::run`: netCDF reads, parsing, datum shifts and
  station list loading.
- Every operation must be cancellable, with a timeout and visible progress.
  A new selection cancels the in-flight fetch.
- No `QEventLoop::exec()` and no `processEvents()` anywhere.
- Startup does no blocking network I/O. Being offline is a normal state, not a fatal one.

### 2.4 Map: MapLibre Native Qt

- Qt 6's Qt Location dropped the `mapboxgl` plugin. MapLibre Native Qt provides the
  Qt 6 replacement with the same GPU vector rendering that made v4's Mapbox GL path fast.
- Stations are **GeoJSON sources plus symbol/circle layers styled from data**:
  - Provider icon.
  - Active/inactive state.
  - HWM error category.
  - Built-in clustering.
- Feed sources from C++. Do not create per-marker QML items.
- The design target is all ~40k stations loaded at once at interactive frame rates.
  This removes viewport filtering, the 250-marker cap and the debounce timers.
- Basemaps:
  - **Default to a free, keyless vector style (decision pending, see §6).**
  - Support Mapbox styles via a user token.
  - Support Esri basemaps as raster sources.

### 2.5 Charts

- Use **Qt Graphs** (`GraphsView`, `LineSeries`, `DateTimeAxis`). Qt Charts is being
  phased out in favor of Qt Graphs.
- Add C++-side **M4 decimation** (first/min/max/last per pixel column, which is
  pixel-exact for line rasterization; refined from min/max on 2026-10-07, see
  `docs/wp-notes/WP3.md`), so multi-year USGS or NDBC
  series stay smooth when zooming.
- Hide the chart behind a thin `ChartModel` boundary. If Qt Graphs is inadequate for
  crosshair, tooltip or performance, a custom `QQuickItem` line renderer
  (`QSGGeometry` line strips) can replace it without touching the rest of the app.
- Export to PNG and PDF (PDF via `QPdfWriter`).

### 2.6 UI layout

A screenshot of a web app (`screenshot.png` at repo root) is the reference for
*organization*, not pixel-exact look.

- **Full-bleed map.** NOAA, USGS, NDBC and XTide become **toggleable layers on
  one map**, each with a distinct icon. This replaces the per-source tabs.
- **Left floating panel:** time range, product, datum, units and display time zone.
  It adapts to the provider of the selected station. It is collapsible.
- **Right "Details" panel:**
  - The selected station's name and id.
  - Headline statistics (peak observed, peak predicted/modeled, etc.).
  - The chart, with download, expand and close actions.
  - The legend.
- **Projects:** model output (ADCIRC, DFlow, IMEDS, generic netCDF) and HWM files are
  loaded as project layers.
  - Their stations appear on the same map.
  - The Details panel overlays model output against the observed series for the
    selected location.
  - HWM projects add the scatter/regression view and color-coded HWM markers.
  - This replaces the "Plot Time Series Stations" and "Plot High Water Marks" tabs.
- **Theming:** dark and light themes built on Qt Quick Controls (Basic style) with
  centrally defined theme tokens.
- **Sessions** use a versioned JSON schema, with a one-time importer for legacy `.mvs`
  files. The legacy format is netCDF4 with `timeseries_*` variables over the
  `ntimeseries` dimension, has no version attribute, and its variables are optional.
  It must tolerate the old fixtures.
- **Settings** use `QSettings`. Consider QtKeychain for API tokens.
- **Updates:** check the GitHub Releases API for the latest tag, asynchronously and
  never at startup in a way that blocks.

### 2.7 Other replacements

| Old | New |
|---|---|
| `timezone.cpp` table | `QTimeZone` (IANA) |
| ezproj submodule (used in `addtimeseriesdialog.cpp`, `usertimeseries.cpp`) | PROJ via vcpkg |
| Vendored netCDF/HDF5/curl/zlib/OpenSSL | vcpkg manifest. Qt 6 uses Schannel on Windows and the native backend on macOS, so OpenSSL does not need to be shipped. |
| Embedded inconsistent CSVs | One normalized station asset generated at build time by `tools/`, loaded once off-thread, plus an optional "refresh station list from APIs" action |
| XTide 2.15.1 compiled via a hand-listed `.pro` | Keep vendored, but build it as its own CMake target with warnings isolated. Consider upgrading to the latest XTide/libtcd and the latest `harmonics.tcd`. |
| `ProcessCrmsDatabase` (Unix-only, shares no code) | Deleted with the CRMS feature (§6, decision 7) |

---

## 3. Qt 6

- Use **Qt 6.8 LTS at minimum**. Move to the current Qt 6 release if it contains
  Qt Graphs or Qt Location features you need.
- **Pinned: Qt 6.11.3** (decided 2026-10-06). MapLibre Native Qt has no tagged
  release newer than v3.0.0 (2024, Qt ≤ 6.7). Its `main` branch CI targets Qt 6.11.2,
  so build MapLibre Native Qt from source at a pinned commit.
- **Development environment:** the Docker image in `tools/dev/` (Ubuntu 24.04, GCC 14,
  LLVM 20 tooling, vcpkg, Qt via aqtinstall). It mirrors the `ubuntu-24.04` CI job.
  The host's Debian 12 GCC 12 lacks `<format>`.
- **Branching:** v5 is developed on the long-lived `v5` branch; `master` stays legacy v4.
- Build with CMake using `qt_standard_project_setup()` and `qt_add_qml_module`.
  QML is compiled and type-checked; enable `qmllint` in CI.
- Use C++23 (`std::expected`, `std::ranges`, `std::format`). Confirm that the minimum
  MSVC, Apple Clang and GCC versions in CI all provide `<expected>` and `<format>`.
- Qt modules: Core, Gui, Network, Quick, QuickControls2, Positioning, Location,
  Graphs, Concurrent, PrintSupport (if needed).
- **Do not port the v4 Qt 5 widget code to Qt 6.** It is being replaced, not migrated.
- Licensing: Qt Graphs and Qt Charts are GPL/commercial, and the app is GPL-3, so this
  is compatible. Ship Qt dynamically linked via the deploy tools.

---

## 4. Distribution

- **CI:** GitHub Actions matrix.

  | Runner | Toolchain |
  |---|---|
  | `windows-2022` | MSVC |
  | `macos-14`+ | arm64 |
  | `ubuntu-24.04` | GCC |

  - Install Qt with `aqtinstall`.
  - Use vcpkg manifest mode with binary caching, plus ccache/sccache.
  - Every PR builds and runs tests.
  - Tag pushes produce signed release artifacts on a GitHub Release.
- **Versioning:** single source of truth is `project(VERSION …)` in CMake. Generate
  `version.h`, Info.plist, the Windows VERSIONINFO `.rc` and the CPack metadata from it.
- **macOS (Apple Silicon, arm64 only):**
  1. `macdeployqt`.
  2. Codesign with Developer ID Application and the hardened runtime.
  3. `xcrun notarytool submit --wait`.
  4. `xcrun stapler staple`.
  5. Build a **classic drag-to-Applications DMG** (background image, app icon plus an
     `/Applications` symlink, fixed icon positions) using `create-dmg` or CPack
     `DragNDrop` with `CPACK_DMG_BACKGROUND_IMAGE` and `CPACK_DMG_DS_STORE_SETUP_SCRIPT`.
  6. Sign and notarize the DMG itself as well.
  7. Provide a proper Info.plist with bundle id, version, icon, and document types
     for session files.
- **Windows (x64):**
  - `windeployqt`.
  - **Inno Setup** installer (CPack has an `INNOSETUP` generator), plus a portable zip.
  - Embed VERSIONINFO.
  - Ship an application manifest with `activeCodePage=UTF-8` and
    `longPathAware=true`: netCDF-C 4.9.3 treats Windows paths as text in the
    active code page, so non-ASCII and long paths need both (see
    `docs/wp-notes/WP6.md`).
  - Code signing: **Azure Trusted Signing** (decision pending, see §6).
  - The installer registers the session file association.
- **Linux:** a single **AppImage** built with `linuxdeploy` and its Qt plugin, if Linux
  is shipped at all (decision pending). No distro packages; Flatpak only if requested later.
- **Delete:**
  - `MetOceanInstaller/` (Qt IFW)
  - `.travis.yml`, `.codacy.yml`
  - all `*.pro`/`*.pri`
  - `thirdparty/{boost_1_67_0,netcdf,openssl}`
  - `mov_release_revision.txt`, once the Releases API check exists
- Consider starting a fresh history, or a filtered history, for v5 to drop the
  135 MiB Boost pack.

---

## 5. Phases

Each phase ends with working, tested, CI-green software. Do not start GUI work
before Phase 3 is done.

1. **Foundation**
   - CMake, presets, vcpkg manifest and the GitHub Actions matrix on all three OSes.
   - A skeleton QML app that opens a MapLibre map.
   - The complete packaging pipeline, signing included as soon as accounts exist.
     The skeleton must produce a signed and notarized DMG and a Windows installer
     before Phase 2 starts.
2. **Core and I/O (headless)**
   - Domain types, the RAII netCDF wrapper and all file readers/writers: IMEDS,
     ADCIRC ASCII/netCDF, DFlow, generic netCDF, HWM, CSV.
   - HWM statistics.
   - Catch2 golden-file tests built from `MetOceanViewer/function_tests`.
   - **Add float-typed and fill-value fixtures**, because the current ones hide
     bugs 4 and 9.
   - Every bug in §1.2 that falls in scope gets a regression test.
3. **Providers and CLI**
   - Async NOAA CO-OPS, USGS (**new Water Data API**), NDBC and XTide.
   - Recorded-response fixture tests, plus a separate opt-in live-API test job
     (nightly, non-blocking).
   - Rebuild `metocean-data` on top of the providers: fully flag-driven with no
     `std::cin` prompts, and an `hwm-stats` subcommand that replaces MetOceanHWMStats.
4. **Map shell**
   - AppState and view-models.
   - Unified station layers and selection.
   - Left and right panels, theming, settings.
5. **Charts and export**
   - Qt Graphs integration, decimation, crosshair and value readout.
   - Image/PDF export and data export (CSV/IMEDS/netCDF).
6. **Projects**
   - Model-vs-observed comparison and the HWM map/scatter/regression views.
   - JSON sessions plus the legacy `.mvs` importer.
7. **Release v5.0**
   - Reach feature parity with the README feature list, retire the legacy build, and
     publish release notes.

### Feature parity checklist (from v4 README)
- [ ] NOAA CO-OPS station data (all products incl. wind components, obs vs. predicted)
- [ ] USGS station data (instantaneous, daily, historic)
- [ ] NDBC archive data
- [ ] XTide predictions
- ~~CRMS database~~ (removed in v5, §6 decision 7)
- [ ] ADCIRC fort.61 ASCII (with station file) and netCDF (fort.61/62/71/72)
- [ ] DFlow-FM his files (incl. 3D variables)
- [ ] IMEDS read/write
- [ ] Generic netCDF read/write
- [ ] HWM comparison map + statistics + regression
- [ ] Vertical datum conversion (VDatum offsets)
- [ ] Session save/load
- [ ] Image/PDF and data export

### v5.0 extras (approved 2026-10-06; optional, never on the parity path)
See `docs/v5-extras.md` for costs and the cut order if parity slips.
- [ ] Quick stats in the Details panel (min, max, mean, count, peak timing)
- [ ] Station search by name or id, with fly-to
- [ ] Favorites and recent stations
- [ ] Observed-minus-predicted residual (surge)
- [ ] Extra CO-OPS scalar products (conductivity, salinity, visibility, air_gap,
      one_minute_water_level) and `--format json` in the CLI

---

## 6. Open decisions (owner: Zach)

Items marked **Decided** were resolved by the owner on 2026-10-06. The rest are still
open. Do not guess; ask before proceeding past the phase that needs them.

1. **Signing accounts. Decided: none exist yet.** Build the complete packaging
   pipeline with signing and notarization steps gated on CI secrets (skipped when
   absent). Unsigned artifacts do not block Phase 2.
2. **Linux. Decided: ship an AppImage** (linuxdeploy + Qt plugin).
3. **Default basemap. Decided: OpenFreeMap** (keyless OSM vector styles). Mapbox
   (user token) and Esri raster remain selectable in Settings.
4. **v4 hotfixes.** Should bugs 1–3 in §1.2 be patched on the legacy v4 code for
   current users before v5 lands? **Decided: no v4 changes at all**, including the
   USGS endpoint shutdown on 2027-02-22. v4 is frozen.

### Provider decisions (2026-10-06; see `docs/provider-apis.md`)

5. **USGS API key:** optional, entered per user in Settings and stored with QtKeychain.
   The project ships no key.
6. **NOAA station list:** active stations only, from the CO-OPS metadata API.
   Retired stations are dropped.
7. **CRMS: Decided: removed as a feature in v5.** No supported CRMS API exists. The
   only per-station path is scripting the CIMS ASP.NET download form, which has no
   published contract (investigation in `docs/provider-apis.md`). There is no CRMS
   provider, no S3 netCDF, no `crms-to-netcdf` tool and no `ProcessCrmsDatabase`
   successor. Bugs 16 and 26 are retired with it. If CPRA publishes a real API,
   revisit; the recorded time-zone finding (CST, UTC-6 year-round) still applies.
8. **NDBC:** cover the current year using `realtime2` (45 days) plus the monthly files,
   in addition to the yearly historical files.
9. **XTide:** upgrade to XTide 2.16, libtcd 2.2.7-r3 and the
   `harmonics-dwf-20251228-free` constants. Ship only the "free" harmonics file.
10. **Scope:** v4 feature parity is the baseline for v5.0. Cheap additions the new
    APIs make available may be proposed, but must not delay parity.

### Engineering decisions (2026-10-06)

11. **Process:** work lands as reviewed commits directly on `v5`, which is pushed to
    `origin` freely (never force-pushed; `master` is never touched). Phase 2 may run in
    parallel with the rest of Phase 1, because `core`/`io` do not depend on the GUI skeleton.
12. **Async:** `QFuture::then` + `QPromise` only. No QCoro.
13. **Units:** small hand-written strong types (`Length`, `Speed`, `Temperature`, ...),
    constexpr and dependency-free. No mp-units.
14. **Station-netCDF output:** CF-1.11 Discrete Sampling Geometries, `featureType =
    timeSeries`, specified in `docs/station-netcdf.md`. Readers still accept the legacy dialects.
15. **IMEDS write precision:** fixed, column-aligned precision (not shortest round-trip,
    not v4's format). Exact widths are pinned in the IMEDS writer tests.
16. **Dry model values:** `value <= -999` is dry. The rule is applied once at the reader
    boundary and becomes an explicit `WetDry`, never a number.
17. **HWM standard deviation:** fix math errors rather than preserve them. The error
    standard deviation is a sample estimate, so use divisor `n - 1`, and document the
    difference from v4.
18. **Datum shifts:** any-to-any through an MSL pivot. A missing offset is an error, never zero.
19. **App identity:** bundle id `io.github.zcobell.metoceanviewer`, display name
    `MetOceanViewer`. Sessions keep the `.mvs` extension; v5 writes JSON and detects
    JSON vs. legacy netCDF on open.
20. **Hardening:** standard-library assertions (`_GLIBCXX_ASSERTIONS`, libc++ hardening,
    MSVC STL hardening) in all dev, sanitizer, coverage and CI builds. Shipped release
    builds do not use them for now.
21. **Extra gates:** libFuzzer fuzzing of every parser, the expanded clang-tidy set and a
    constexpr (`STATIC_REQUIRE`) test harness. UBSan float checks are not enabled.
22. **Coverage:** at least 90% line coverage for `core`/`io` and 80% overall.
23. **Station lists:** a normalized asset built by a `tools/` script from the provider
    APIs is committed as a snapshot. The app can refresh it at runtime in the background.
24. **Minimum platforms:** macOS 14 (arm64), Windows 10 22H2 / 11 (x64), and an AppImage
    built on Ubuntu 22.04 (glibc 2.35).
25. **R² of a through-origin HWM fit:** uncentred (`1 − SSres/Σy²`), as R and statsmodels
    report for no-intercept models. It differs from v4's centred value.
26. **D-Flow FM:** no real `_his.nc` is available. Tests use synthetic fixtures built from
    the D-Flow FM documentation, to be checked against a real file when one turns up.
27. **CSV export:** long format, one row per sample (station id/name, ISO-8601 UTC time,
    quantity, value, units, datum).
28. **Vector components of model output (D-Flow FM, ADCIRC netCDF and ASCII):** what they
    are depends on the CRS the caller states for the file.
    (1) If it is geographic (a PROJ geographic 2D or 3D CRS, whatever the datum, not just
    EPSG:4326) the components are eastward and northward: `current_u`/`current_v` and
    `wind_u`/`wind_v`.
    (2) If it is projected they point along the grid's axes: they are generic quantities with
    CF's grid names `sea_water_x_velocity`, `sea_water_y_velocity`, `x_wind` and `y_wind`,
    paired with `VectorSeries::assume_components` (speed is the same), and the direction's
    label says "grid-relative".
    (3) Rotating grid-relative components by the meridian convergence is deferred.
    For ADCIRC netCDF the global `ics` is checked against the CRS (`crs_mismatch` warning);
    the ASCII reader takes the CRS of its station file.

## 7. Engineering rules for v5

- `core` and `io` never include Qt headers.
- No raw owning pointers. No `new` outside Qt parent/child UI object creation.
- No sentinel values in domain types; use `optional`, `variant` or `expected`.
- No blocking calls on the GUI thread. No nested event loops. No `processEvents`.
- Every parser and reader has fixture tests. Every fixed bug gets a regression test.
- Warnings as errors in CI for first-party code (`-Wall -Wextra -Wpedantic` / `/W4`),
  with sanitizers (ASan/UBSan) in a Linux debug job. Run clang-tidy with
  modernize/bugprone/performance checks.
- clang-format enforced. Keep the GPL-3 header on source files.
