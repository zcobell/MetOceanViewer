# MetOceanViewer v5 — Phase 3 design: providers and the CLI

Status: **proposed, revision 2** (2026-10-09). Revision 2 applies the owner's answers
(plan §6 decisions 32–35) and the design review (bryce-lelbach, ben-deane,
neckbeard-nate); Appendix A maps each finding to its resolution. Built: H0 (§2.2, the
layers, gates and build plumbing); nothing from P1 on. Phase 2 (`core`, `io`) is as built;
the tide engine work packages H1–H9 of `docs/harmonics-engine.md` have not started. Where a header written later disagrees with
this page, the header wins and this page gets fixed.

Inputs:
- `docs/rearchitecture-plan.md` (plan; decisions D1–D35, the Phase 2 open items "OI#")
- `docs/provider-apis.md` (PA; verified 2026-10-06, corrected 2026-10-09; fixtures in
  `docs/provider-apis/`)
- `docs/core-design.md` (CD), `docs/station-netcdf.md` (SN), `docs/v5-extras.md` (EX)
- `docs/harmonics-engine.md` (HE), `docs/harmonics-json.md` (HJ)
- Legacy v4 for behavior only: `libraries/libmetocean/{noaacoops,usgswaterdata,ndbcdata,stationlocations}.cpp`,
  `MetOceanData/`, `MetOceanHWMStats/`

Tags: `B#` plan §1.2 bug, `L#` legacy provider bug (§11.2), `D#` plan decision, `P#` this
page's decisions, `E#` HE decisions. Review findings: `Br#` bryce-lelbach, `Bd#`
ben-deane, `N#` neckbeard-nate (Appendix A).

---

## 0. Decisions (summary)

| # | Topic | Decision |
|---|---|---|
| P1 | Layering | One new **Qt-free layer `fetch`** between `io` and `providers`: stages, units and their continuation, retry and origin policies, the sans-IO machine, merging, and the provider error type. Request **vocabulary** goes in **core**; wire formats in both directions go in **io**. `providers` is the Qt Network edge and nothing else. §2. |
| P2 | JSON | nlohmann/json (vcpkg, H0) in `io` only, private, behind an include gate. Every network body passes a nesting-depth pre-scan before the parser sees it; large documents are decoded with the parser-callback pattern of HE §6.2. |
| P3 | Illegal requests | Each provider's request is a variant whose alternatives carry only legal fields: no datum on a met product; no interval on one-minute water level; a half-open **day** range for USGS daily values, whose statistic lives in the daily selector. Max spans are constexpr functions of the alternative. §3. |
| P4 | Wire policy | UTC on the wire. CO-OPS always `units=metric`; USGS and NDBC have no choice. Series leave the provider in the unit and datum delivered (D13). CO-OPS datums are requested natively; only datums CO-OPS serves are offered (D32). §3.6. |
| P5 | USGS daily | Daily values are a `core::DailySeries` (days, samples, meta, statistic), never a `TimeSeries` (D34). SN writes them with `time_basis` and `cell_methods` (minor bump); CSV writes a `date` column in place of `time_utc`; IMEDS refuses them. §3.4, §4.7. |
| P6 | Stages and units | A fetch is one or two **stages** composed in one job (USGS: resolve the series, then fetch it). Within a stage each **unit** (window, NDBC year) is a chain advanced by `next(unit, outcome)`: the next page, the next NDBC fallback, or done. §5.2. |
| P7 | Merging | A unit's parsed result is a `Part`: a map from column key to points, with the empty part as identity and a left-biased union as the combine, folded in plan order. Units differing within a column are converted exactly to the first part's unit or the merge fails. §5.4. |
| P8 | Async | `fetch(Request) -> QFuture<expected<Fetched<Value>, Failure>>`; one `QPromise` per fetch, owned by a `Job` on the one `Network`'s thread. `QFuture::then` with a context and `QPromise` only (D12). One continuation per future; a future is never shared between consumers. §6. |
| P9 | Cancellation | `cancelChain()` on the future a caller holds reaches the job (verified on Qt 6.11.3, §1.5); the job aborts its exchanges, and parse and merge tasks stop through `core::StopToken`. A per-fetch deadline lives in the machine. §6.4. |
| P10 | Transport | Exchanges are move-only handles owned by the job; destroying one aborts it; each attempt has a fresh id; completions are always delivered queued, never re-entrantly and never after an abort. Redirects only to the same `https` origin, at most 3. §6.2. |
| P11 | Retries and limits | Retry idempotent GETs on 429, 500, 502, 503, 504, inactivity or throughput timeout, refused or closed connections; 4 attempts; `Retry-After` honored; full-jitter backoff. Per-origin concurrency and spacing, a shared cool-down after a 429, interactive work before background work. §5.5. |
| P12 | Partial failure | Strict by default. Lenient (CLI `--partial`, background catalog refresh): a failed unit, a parse error included, becomes a gap and a `UnitFailed` warning. `NoData` is decided only at the end, when no unit failed; a lenient run in which every unit failed is a failure. §5.6. |
| P13 | Station lists | One versioned JSON catalog per provider, snapshots compiled in as a Qt resource, refreshes in the shared app-data directory. The builder is the CLI (`stations build`). §7. |
| P14 | Startup | No network at startup. The catalog is loaded off-thread; a background refresh runs at most once per launch when the catalog is older than 30 days. |
| P15 | Secrets | `ApiKey` has no format or stream operator. `Network` holds it and attaches it as `X-Api-Key` to USGS-origin requests only. App: QtKeychain, or a plain-text file in app-local data where no keychain backend exists (D35). CLI: `--api-key-file` or `MOV_USGS_API_KEY`. §8. |
| P16 | CLI | Flag-driven, names not indices, ISO 8601 UTC with exclusive end. Exit code from an exhaustive join: 130 > 1 > 4 > 3 > 0. Output is committed only on success. §9. |
| P17 | Tests | Pure tiers driven by events with virtual time; a Qt tier with a `FakeTransport` replaying cassettes (no timing assertions); the real stack against a local server; one allowed top-level loop, `mov::test::drive`. §10. |
| P18 | Tides | H9 is HE §9.2 with `core::StopToken`, `QtConcurrent::run(QPromise&)` and `fetch::Failure`; `tides predict` goes through it, so H8's `tides predict` lands after H9. §6.10. |

---

## 1. External facts verified for this page (2026-10-09)

PA is the reference; the rows here are new or re-checked, and PA now carries the
corrections. "Live" means a request from the dev host that day; "probe" means the
program of §1.5 built against the pinned Qt 6.11.3.

### 1.1 NOAA CO-OPS

| Fact | Source |
|---|---|
| Data length per request: 1-minute 4 days; 6-minute 1 month; hourly 1 year; high/low 1 year; daily means and daily max/min 10 years; monthly means 200 years. Predictions: `hilo` 10 years, any other interval 1 year. | https://api.tidesandcurrents.noaa.gov/api/prod/ |
| Datums `CRD, IGLD, LWD, MHHW, MHW, MTL, MSL, MLW, MLLW, NAVD, STND`; tide prediction intervals `h, 1, 5, 6, 10, 15, 30, 60, hilo`; met `h` or the 6-minute default; throttling exists, no number published. | same |
| Live: `hourly_height` over 13 months fails "Range Limit Exceeded"; 6-minute predictions over 2 months, hourly over 2 years and `hilo` over 6 years succeed; `predictions` with `datum=NAVD` succeeds. The planner uses the documented limits. | live |
| `stations.json?type=waterlevels&expand=details,sensors`: 302 stations, `details.established`/`removed`, sensors (`name`, `status`, `sensorID`), 1.3 MB. `type=met`: 315. `expand=datums` does not expand; `products` are website links. `datums.json` values are relative to `STND`; it names `NAVD88`, `DTL`, `GT`, `MN`, `DHQ`, `DLQ` besides the tidal datums; `LAT`/`HAT` are top-level members. | live |

### 1.2 USGS Water Data

| Fact | Source |
|---|---|
| Key as `api_key` or `X-Api-Key`; a key allows "more requests per hour"; example `X-RateLimit-Limit: 1000`, `X-RateLimit-Remaining: 998`; over the limit is 429; no keyless numbers, no `Retry-After` documented. | https://api.waterdata.usgs.gov/docs/ogcapi/keys/ |
| Paging by `next` links; `limit` sets the page size. | https://api.waterdata.usgs.gov/docs/ogcapi/ |
| WaterServices "decommissioned in the first quarter of 2027"; post updated 2026-10-02. v4 is frozen (D4). | https://waterdata.usgs.gov/blog/api-waterservices-decom/ |
| Live: `continuous` with `limit=3` returns `next`, no `numberMatched`. `daily` over 36 years returns 13,150 items on one page. No `X-RateLimit-*` on keyless responses; `cache-control: public, max-age=3600`. | live |
| Live: `f=csv` carries no next link (body or `Link` header): paging needs `f=json`. | live |
| Live: `combined-metadata` returns geometry, `id` (32-hex series id), `monitoring_location_id`, `monitoring_location_name`, `parameter_code`, `parameter_name`, `statistic_id`, `computation_identifier`, `begin`, `end`, `primary`, `unit_of_measure`, `time_zone_abbreviation`, `uses_daylight_savings`; `properties=` rejects `time_series_id`. | live; `/collections/combined-metadata/queryables` |

### 1.3 NDBC

| Fact | Source |
|---|---|
| Finalized months: `/data/stdmet/{Mon}/{id}{m}{yyyy}.txt.gz`, lower-case id, `m` the month digit (`Jan/4100212026.txt.gz`); seen Jan–Jul 2026. | live listings |
| Latest unfinalized month: `/data/stdmet/{Mon}/{id}.txt`, uncompressed, lower-case id, realtime units (`nmi`), 9-filled missing values. `Sep/` empty on 2026-10-09; `Oct/`–`Dec/` empty. | live |
| **Unverified:** the month code for October–December (believed `a`/`b`/`c`). | — |
| `realtime2/{ID}.txt`: upper-case id only (lower-case and `.TXT` are 404), newest first, about 45 days, `MM` for missing. Historical `h2025` exists, `h2026` is 404. | live |
| `.txt.gz` files are served `Content-Type: application/x-gzip` with no `Content-Encoding`; three files (2005, 2025, Jan 2026) are each one gzip member, ratios 4.5–6.2, the 2025 year 3.2 MB decompressed. | live |
| `activestations.xml`: 1,354 stations, attributes `id lat lon elev name owner pgm type met currents waterquality dart`; 905 with `met="y"`. | live |

### 1.4 Qt documentation (pages are for 6.12; every function used exists in 6.11)

| Fact | Source (https://doc.qt.io/qt-6/) |
|---|---|
| `QNetworkAccessManager` is used only from its own thread. "6 requests are executed in parallel for one host/port" applies to HTTP/1.1; `Http2AllowedAttribute` defaults to true, so HTTPS negotiates HTTP/2 and multiplexes over one connection: the origin gate (§5.5), not Qt, bounds concurrency. Default redirect policy `NoLessSafeRedirectPolicy`. `setTransferTimeout` aborts after a period with no bytes; default 0. | `qnetworkaccessmanager.html`, `qnetworkrequest.html` |
| `cancel()` cancels continuations attached to the future but does not travel upstream. `then(context, f)` runs `f` in the context's thread and cancels if the context dies. `cancelChain()` (6.10) cancels the whole chain but not a nested computation already started. | `qfuture.html` |
| `QPromise::isCanceled()` is a poll; after cancellation `addResult` adds nothing. | `qpromise.html` |
| `QRestAccessManager` adds callbacks and JSON helpers only: not used. | `qrestaccessmanager.html` |
| `setDecompressedSafetyCheckThreshold` (6.2), `setMaximumRedirectsAllowed`, `UserVerifiedRedirectPolicy` exist on `QNetworkRequest`. | `qnetworkrequest.html` |
| vcpkg `qtkeychain-qt6` 0.14.3 depends on vcpkg `qtbase` (and `libsecret` on Linux): an overlay port against `$QT_ROOT_DIR` is needed. | `microsoft/vcpkg` `ports/qtkeychain-qt6/vcpkg.json` |

### 1.5 Qt 6.11.3 probe (measured)

A throw-away program against `~/Qt/6.11.3/gcc_64` with local `QTcpServer`s:

| Question | Result | Consequence |
|---|---|---|
| Does `cancelChain()` on the end of `promise.future().then(…).then(…)` reach the promise? | **Yes**: the head future and `QPromise::isCanceled()` are true and a `QFutureWatcher` on the head emits `canceled()`. Plain `cancel()` on the continuation does not reach the head. | P9: callers cancel with `cancelChain()` (§6.4). |
| Continuation of a promise that is `finish()`ed **without** a result and not cancelled | The continuation calls `result()` on an empty future: **segfault** inside `QFutureInterface::resultReference`. | The job always `addResult`s before `finish()` unless the promise is cancelled; asserted (§6.3). |
| Do raw headers follow redirects? | **Yes, including to another origin** under the default policy: a 302 from one port to another delivered `X-Api-Key` to the second server. `SameOriginRedirectPolicy` refuses (`InsecureRedirectError`, 11). | P10: `UserVerifiedRedirectPolicy`, same `https` origin only (§6.2). |
| Content-Encoding bomb (32 MiB of zeros, 32 KiB deflated) | Default threshold 10 MiB: refused with error 299 ("Decompression failed: … exceeds the limits specified by QNetworkRequest::decompressedSafetyCheckThreshold()"), empty body; threshold 1 MiB: same; −1: the whole 32 MiB delivered. | The threshold is set to the body cap and error 299 on a reply with `Content-Encoding` maps to `body_too_large`; decompressed bytes are also counted (§6.2). |

---

## 2. Layering

### 2.1 The layers

```
core   <- io   <- fetch   <- providers   <- app   <- ui
                               ^
cli -----------------------------  (cli may link core, io, fetch, providers)
Qt-free: core, io, fetch
```

| Layer | Phase 3 content | Why here |
|---|---|---|
| `core` | Provider vocabulary (`coops::Product`, `usgs::Request`, `ndbc::StdMet`, codes and ids, max spans), `split`, `DayRange`, `DailySeries`, the USGS parameter-to-quantity table, station capabilities and `offered` | Domain values with invariants, constexpr, `STATIC_REQUIRE`-tested. Plan §2.2 puts `CoopsProduct` in the domain model; io's parsers need the product, so it sits below io. |
| `io` | Wire formats: request targets, response parsers, the station catalog format, the JSON series writer, `gunzip`, the daily outputs | Reuses io's text primitives (`detail::parse_double`, `LineCursor`, `ParseError::make`, `Read<T>`, `ReadContext`), its JSON and gzip, its fuzz harness and 90 % gate. Serializing a request is a format writer like IMEDS. |
| `fetch` (new) | Stages, units and `next`, `Part` merging, `RetryPolicy`, origin policies, the sans-IO `Machine`, `fetch::Error`, `Failure`, `Fetched<T>` | The orchestration rules are where provider code goes wrong. As pure functions over values they are tested exhaustively with virtual time and no event loop, under the 90 % gate. They are neither byte formats nor domain values. |
| `providers` | `Network`, `Transport`, `Job`, the providers, catalogs, the snapshot resource, the update check, the result cache, `describe()` | The Qt edge. |
| `cli` | `metocean-data` | The same providers; `QCoreApplication::exec()` once. |

Rejected: everything Qt-free in `io` (io's contract is pure functions over bytes; the
machine has time, retries and multi-request state); the parsers in `fetch` (io's
`detail` helpers would have to become public); the vocabulary in `providers` (untestable
without Qt, and io could not name a product).

### 2.2 CMake and gates (H0, built)

- `cmake/Layering.cmake`: `MOV_LAYERS core io fetch providers app ui`;
  `MOV_QT_FREE_LAYERS core io fetch`; `MOV_LAYER_cli_MAY_LINK core io fetch providers`.
  The coverage gate and `qt_free_sources` follow `mov_qt_free_source_dirs`. A layer
  declared without `SOURCES` is header-only (an `INTERFACE` library): `fetch` is one until
  P6 adds its first source file; H0 gave it `TransportErrc`, `Verdict` and
  `classify(TransportErrc)` (§5.1, §5.3) in `mov/fetch/policy.hpp`. `tests/cmake/layering`
  covers the new edges.
- `vcpkg.json`: `nlohmann-json`, `zlib` (HE's H0; done once), linked `PRIVATE` into
  `mov_io`.
- `find_package(Qt6 COMPONENTS Core Concurrent Network …)` under `MOV_ENABLE_QT`;
  `providers` builds in the Qt presets (`PUBLIC` Qt Core, `PRIVATE` Qt Network and Qt
  Concurrent), the CLI joins it in P9a; `dev` keeps building core, io and fetch.
- `coverage-qt` (configure, build, test and workflow presets) is `coverage` with the Qt
  layers, so `providers` and `cli` count toward the 80 % overall; the CI coverage job
  still runs `coverage` (switching it is P7's, when providers has code to measure).
- **`no_blocking_calls` gate** (`cmake/CheckBlockingCalls.cmake`, with a
  `_rejects_violations` twin over `tests/cmake/blocking_calls/`) over `src/` and
  `tests/`, every layer, comments ignored:
  - nested event loops: `QEventLoop`, `processEvents`, `sendPostedEvents`;
  - synchronous waits: `waitFor[A-Z]…` (`waitForFinished`, `waitForDone`,
    `waitForReadyRead`, `waitForBytesWritten`, `waitForConnected`, …), QTest's `qWait…`,
    `qSleep` and `QTRY_…`; `.wait(`, `.wait_for(`, `.wait_until(`, `.arrive_and_wait(`,
    `.acquire(`, `.try_acquire_for(`, `.try_acquire_until(`;
  - sleeps: `QThread::sleep`/`msleep`/`usleep`, `std::this_thread::sleep_for`/
    `sleep_until`, POSIX and Win32 sleeps;
  - future reads: `.result()`, `.results()`, `.resultAt(`, `.takeResult()`;
  - `QFutureSynchronizer`, `QSemaphore`, `QtConcurrent::blocking…`,
    `QNetworkRequest::SynchronousRequestAttribute`, `Qt::BlockingQueuedConnection`;
  - the std types: `std::future`, `shared_future`, `async`, `promise`, `packaged_task`,
    and as types `std::thread`, `jthread`, `latch`, `barrier`, the semaphores and the
    condition variables;
  - `exec(` outside `src/<layer>/main.cpp` (it also hits `QSqlQuery::exec`).

  Allowed: everything in `tests/support/include/mov/test/qt_drive.hpp`, the one test
  driver (`mov::test::drive_until(predicate, timeout)`, which rethrows the predicate's
  exceptions after the loop and refuses to nest; §10.2 adds a future-driven variant
  beside it); the line of io's Windows atomic rename marked `// gate: bounded-retry`
  (CD §4.4); `std::thread` in io's projection test. The GUI render test uses
  `drive_until` instead of `QTest::qWait…`.
- **`no_ignore_ssl_errors` gate** (`cmake/CheckIgnoreSslErrors.cmake`, with a twin):
  `ignoreSslErrors`, `VerifyNone` and `QueryPeer` anywhere under `src/` and `tests/`, no
  exception (the local-server tests use plain HTTP on loopback).
- **`third_party_include_gate`** (`cmake/CheckThirdPartyIncludes.cmake`, with a twin):
  `#include` or `#include_next` of `<nlohmann/…>` only under `src/io/json/`, of
  `<zlib.h>`/`<zconf.h>` only in `src/io/gzip.cpp`. Tests may include them. Unlike the
  netCDF gate it does not require a use, since none exists before the readers of §4.2
  and HE §6.3. `mov_io` compiles with `JSON_USE_IMPLICIT_CONVERSIONS=0`.
- The three gates take `-DMOV_REPO=<root>` and share one line scanner
  (`cmake/SourceScan.cmake`): it masks string and character literals before it removes
  comments, and keeps `;`, brackets and backslashes from joining or splitting lines.
- **`live` label:** `mov_add_test(<name> LIVE [QT] …)` registers one ctest test labelled
  `live` with Catch2's skip exit code (4); every test preset and the coverage run exclude
  the label; `mov::test::require_live_api()` (`mov/test/live.hpp`) skips a test case
  unless `MOV_LIVE_API=1`. A preset's label filter also applies to `-L`, so the live
  tests run by build directory: `MOV_LIVE_API=1 ctest --test-dir build/<preset> -L live`.
  The label's guard test, `live_api_opt_in_guard`, fails without the opt-in, so a run
  that forgot it is red. `tests/live/` checks the plumbing in every preset on the
  `[live-plumbing]` test case only (`live_tests_skip_without_opt_in`: exit code 4 and
  the reason; `live_tests_run_with_opt_in`); the provider checks and `live-api.yml` are
  §10.5's.
- **`StopToken`** moved from io to core (`mov/core/stop_token.hpp`, as `Cancelled` did),
  so the tide engine's prediction can take it; io keeps the name through a
  using-declaration.

---

## 3. core: provider vocabulary

New headers in `src/core/include/mov/core/provider/` (`coops.hpp`, `usgs.hpp`,
`ndbc.hpp`, `capabilities.hpp`) and `src/core/include/mov/core/` (`windows.hpp`,
`daily_series.hpp`). Constexpr where the types allow, regular, designated initializers.

### 3.1 Windows and days

```cpp
namespace mov::core {
/// Consecutive half-open windows [b, b+span), ..., the last cut at r.end(). Precondition
/// (asserted): span >= 1 ms. Result non-empty, disjoint, ordered, covering r exactly.
[[nodiscard]] std::vector<TimeRange> split(TimeRange r, std::chrono::milliseconds span);

/// r widened outward to whole multiples of `grain` (floor the begin, ceil the end), so
/// windows split from it by a span that is a multiple of `grain` start and end on the
/// grain. nullopt only on overflow.
[[nodiscard]] constexpr std::optional<TimeRange> aligned(TimeRange r, std::chrono::milliseconds grain) noexcept;

/// Half-open calendar days [first, end): first < end; no default (Bd SF13).
class DayRange {
 public:
  [[nodiscard]] static constexpr std::expected<DayRange, TimeRangeError> make(
      std::chrono::sys_days first, std::chrono::sys_days end) noexcept;
  /// The days of r, only when both bounds are midnights; otherwise not_whole_days.
  [[nodiscard]] static constexpr std::expected<DayRange, DayRangeError> covering(TimeRange r) noexcept;
  [[nodiscard]] constexpr std::chrono::sys_days first() const noexcept;
  [[nodiscard]] constexpr std::chrono::sys_days end() const noexcept;
  [[nodiscard]] constexpr std::chrono::days size() const noexcept;
  friend constexpr bool operator==(const DayRange&, const DayRange&) = default;
};
enum class DayRangeError : std::uint8_t { empty_or_inverted, not_whole_days };
}
```

Laws (tested): the windows concatenate to `r`; all but the last have length `span`;
`split(aligned(r, 1 min), 31 d)` windows begin and end on minutes; `covering` round-trips
`[first 00:00, end 00:00)` and refuses any other instant.

### 3.2 CO-OPS — `coops.hpp`

```cpp
namespace mov::core::coops {
/// Datums the data API accepts that core can name (D32: only these are offered).
/// CRD and LWD (rivers) are not offered (v4 never did); IGLD is IGLD85.
enum class Datum : std::uint8_t { mhhw, mhw, mtl, msl, mlw, mllw, navd88, stnd, igld85 };
[[nodiscard]] constexpr VerticalDatum to_vertical(Datum) noexcept;
[[nodiscard]] constexpr std::optional<Datum> from_vertical(VerticalDatum) noexcept;
[[nodiscard]] constexpr std::string_view wire_token(Datum) noexcept;          // "NAVD", "IGLD", ...

enum class Observed : std::uint8_t { six_minute /*water_level*/, one_minute /*one_minute_water_level*/,
                                     hourly /*hourly_height*/ };
enum class PredictionInterval : std::uint8_t { one, five, six, ten, fifteen, thirty, sixty };  // hilo: v5.x (EX #7)
enum class PairedGrid : std::uint8_t { six_minute, hourly };   // observed with predictions at 6 or "h"

struct ObservedLevel { Observed series; };
struct PredictedLevel { PredictionInterval interval; };
struct ObservedVsPredicted { PairedGrid grid; };
using LevelKind = std::variant<ObservedLevel, PredictedLevel, ObservedVsPredicted>;
struct WaterLevel { LevelKind kind; Datum datum; };          // datum mandatory (verified)

enum class Met : std::uint8_t {
  air_temperature, water_temperature, wind, air_pressure, humidity,   // v4 parity
  conductivity, salinity, visibility, air_gap                          // EX #1, droppable
};
enum class MetInterval : std::uint8_t { six_minute, hourly };
struct Meteorological { Met product; MetInterval interval; };
using Product = std::variant<WaterLevel, Meteorological>;

struct Request { StationId<provider::Coops> station; TimeRange range; Product product; /* == */ };

/// Documented maxima (§1.1); a paired product takes the smaller: 1-minute 4 d;
/// 6-minute 31 d (the live error text, PA §2.1); hourly and non-hilo predictions 365 d.
[[nodiscard]] constexpr std::chrono::days max_window(const Product&) noexcept;
/// The grain windows are aligned to: 1 min for every product (S8).
inline constexpr std::chrono::minutes grain{1};

enum class Shape : std::uint8_t { series, observed_vs_predicted, wind };
[[nodiscard]] constexpr Shape shape(const Product&) noexcept;
}
```

`air_gap`'s datum handling and `salinity` in JSON are confirmed with recorded fixtures in
P2 before those alternatives ship. `daily_mean`, `monthly_mean`, `high_low` and `hilo`
are not alternatives (EX #7); each would be one enumerator, one span row and a fixture.

### 3.3 Capabilities and `offered` (Bd SF12, SF14)

```cpp
namespace mov::core {
template <Provider P> struct Capabilities;

template <> struct Capabilities<provider::Coops> {
  std::vector<coops::Observed> observed;           // from the water-level sensor
  std::vector<coops::Met> met;                     // from sensors with status 1
  bool predictions;                                // the station's `tidal` member
  std::optional<DatumTable> datums;                // nullopt: datums.json failed or had no MSL
  std::vector<coops::Datum> datums_offered;        // datums.json names ∩ coops::Datum, plus STND
};
template <> struct Capabilities<provider::Usgs> {
  std::vector<usgs::SeriesInfo> series;            // the map filter's series (D33)
};
template <> struct Capabilities<provider::Ndbc> {
  std::string owner, program, platform_type;
  std::optional<Length> elevation;
};

enum class NotOffered : std::uint8_t { product, interval, datum, predictions, parameter };
/// Whether the station's catalog entry offers the request: one check for the CLI and
/// the app's picker.
[[nodiscard]] std::expected<void, NotOffered> offered(const Capabilities<provider::Coops>&, const coops::Product&);
[[nodiscard]] std::expected<void, NotOffered> offered(const Capabilities<provider::Usgs>&, const usgs::Request&);
}
```

- `great_lakes` is not stored: what it gated (IGLD and no tidal predictions) is stated
  directly by `datums_offered` and `predictions`.
- `GaugeStation<P>` loses its `datums` member (P1): a USGS or NDBC station has no datum
  table, and the default table ("knows only MSL") claimed knowledge it did not have. The
  CO-OPS table lives in `Capabilities<Coops>` as an `optional`.

### 3.4 USGS — `usgs.hpp`, `daily_series.hpp`

```cpp
namespace mov::core::usgs {
class ParameterCode { /* 5 ASCII digits, make(); view(); <=> */ };
class StatisticId   { /* 5 ASCII digits; static constexpr instantaneous(), mean(), minimum(), maximum() */ };
class TimeSeriesId  { /* 32 lower-case hex */ };

struct ByParameter      { ParameterCode parameter; };                        // continuous: statistic 00011
struct DailyByParameter { ParameterCode parameter; StatisticId statistic; };
struct ByTimeSeries     { TimeSeriesId id; };

struct Continuous { StationId<provider::Usgs> station; std::variant<ByParameter, ByTimeSeries> series; TimeRange range; };
struct Daily      { StationId<provider::Usgs> station; std::variant<DailyByParameter, ByTimeSeries> series; DayRange days; };
using Request = std::variant<Continuous, Daily>;

/// What resolution (stage 1, §5.2) proves about the series: it exists at the station,
/// belongs to the request's collection, and is the Primary one when chosen by parameter.
struct ResolvedSeries {
  TimeSeriesId id; ParameterCode parameter; StatisticId statistic;
  std::string parameter_name; std::optional<Unit> unit;
  std::optional<QuantityMapping> quantity;      // registry_quantity(parameter)
};
/// Catalog row (Capabilities<Usgs>) and picker row.
struct SeriesInfo { TimeSeriesId id; ParameterCode parameter; StatisticId statistic; bool primary;
                    std::optional<Unit> unit; ValidRange validity; };

/// continuous: 1000 d (the server refuses > 1100 d); daily: nullopt (36 years seen).
[[nodiscard]] constexpr std::optional<std::chrono::days> max_window(const Request&) noexcept;
inline constexpr std::chrono::seconds grain{1};

/// 00060 discharge; 00065 water_level, datum stnd (the gage datum); 62620 water_level
/// navd88; 62619 water_level ngvd29. Others: GenericQuantity "usgs_<code>". Further rows
/// (00010, 00020, 00035, 00036) only after P3 checks them against parameter-codes.
struct QuantityMapping { Quantity quantity; std::optional<VerticalDatum> datum; };
[[nodiscard]] constexpr std::optional<QuantityMapping> registry_quantity(const ParameterCode&) noexcept;
}

namespace mov::core {
/// Values of a station's local calendar days (D34): not instants, so not a TimeSeries.
/// Days strictly increasing; one sample per day; no zone conversion ever applies.
class DailySeries {
 public:
  [[nodiscard]] static std::expected<DailySeries, ConstructionError> make(
      SeriesMeta meta, std::vector<std::chrono::sys_days> days, std::vector<Sample> samples,
      usgs::StatisticId statistic);
  // meta(), days(), samples(), statistic() — const& views, && deleted; ==
};
}
```

- Resolution **always** runs (Bd B2), also for `ByTimeSeries`: it supplies the unit,
  name and statistic, and proves the id belongs to the station and the collection.
  `ByParameter` with no Primary series is `NoData`, with several `AmbiguousSeries`.
- The statistic of a continuous request is `00011` by construction; a daily request by
  parameter must name its statistic.

### 3.5 NDBC — `ndbc.hpp`

```cpp
namespace mov::core::ndbc {
struct StdMet { StationId<provider::Ndbc> station; TimeRange range; };   // every column; the caller picks
/// Which file a series came from, in precedence order (§5.4).
enum class FileKind : std::uint8_t { historical, monthly, monthly_latest, realtime };
inline constexpr std::chrono::minutes grain{1};
}
```

### 3.6 Units, datums, time

- **Units (D13).** Parsers set `SeriesMeta::unit` from what the server says: CO-OPS
  metric; USGS `unit_of_measure` through an alias table in io (`ft^3/s` → cfs, `ft` →
  foot, `deg C` → celsius; otherwise `parse_unit`, else an `OtherUnit` with
  `unrecognized_unit`); NDBC from the header's units row (`mi` historical, `nmi`
  realtime and the latest monthly file; `TIDE` feet). No provider converts; English
  output is `core::convert` at the edge. The affine `Temperature` value type stays
  deferred (no single-temperature consumer in Phase 3).
- **Datums.** CO-OPS: requested natively; the offered set is `datums_offered` (D32: no
  NGVD29, no client-side shift). USGS: `registry_quantity`. NDBC `TIDE`: MLLW per NDBC's
  measurement descriptions, confirmed in P4. Harmonics: HE §5.
- **OI 12** (a `DatumTable` that carries its station) **stays open for Phase 6**:
  Phase 3 shifts no provider series client-side, and the model-vs-observed work is
  where a station-bound table may be needed.

---

## 4. io: wire formats

New headers in `src/io/include/mov/io/wire/` (`origin.hpp`, `coops.hpp`, `usgs.hpp`,
`ndbc.hpp`, `github.hpp`); `station_catalog.hpp`, `series_json.hpp` and `gzip.hpp`
(HE §6.5's `gunzip`, public, taking a `StopToken`) beside the other formats.

### 4.1 Origins and targets

```cpp
namespace mov::io::wire {
/// A scheme-host-port triple the app talks to; all are https.
enum class Origin : std::uint8_t { coops_data, coops_metadata, usgs, ndbc, github };
[[nodiscard]] constexpr std::string_view base(Origin) noexcept;   // "https://api.tidesandcurrents.noaa.gov"

struct QueryParam { std::string name; std::string value; /* == */ };
/// Path and parameters, unencoded (providers encode with QUrlQuery), in a fixed order so
/// cassettes match byte for byte.
struct Composed { Origin origin; std::string path; std::vector<QueryParam> query; };
/// A link the server handed out (USGS `next`), sent verbatim. Verbatim::make refuses any
/// scheme, host or port but `base(origin)`, and any path but `pinned_path` (N B1): a page
/// cannot send the client elsewhere, nor to another collection.
class Verbatim {
 public:
  [[nodiscard]] static std::expected<Verbatim, FormatError> make(Origin, std::string_view pinned_path, std::string_view url);
};
using Target = std::variant<Composed, Verbatim>;
}
```

| Function | Output |
|---|---|
| `coops::targets(request, window)` | `datagetter`: `product`, `application=MetOceanViewer`, `station`, `begin_date`/`end_date` as `yyyyMMdd HH:mm` (`window` is minute-aligned; end sent as `window.end() − 1 min`), `time_zone=gmt`, `units=metric`, `format=json`, `datum` only for `WaterLevel`, `interval` only when not the default. Two targets for `ObservedVsPredicted`. |
| `coops::catalog_targets()`, `coops::datums_target(id)` | `stations.json?type=waterlevels&expand=details,sensors`, `type=met&…`; `stations/{id}/datums.json?units=metric` |
| `usgs::resolve_target(request)` | `combined-metadata/items?monitoring_location_id&(parameter_code&statistic_id | id)&properties=id,primary,parameter_code,parameter_name,statistic_id,unit_of_measure,computation_identifier,begin,end&skipGeometry=true&f=json` |
| `usgs::data_target(resolved, window)` | `continuous` or `daily` items: `time_series_id`, `datetime=a/b` (continuous: second-aligned instants, `b` = window end − 1 s; daily: dates, `b` = end − 1 day), `properties=time,value,approval_status,qualifier`, `skipGeometry=true`, `sortby=time` (confirmed for `daily` in P3; else the parser reverses a strictly decreasing page), `limit=50000`, `f=json`. Pinned path for `next`: the same collection's `/items`. |
| `usgs::catalog_target(parameters)` | `combined-metadata/items?parameter_code=00060,00065,62620,62619&statistic_id=00011&properties=…&limit=50000&f=json` (geometry kept) |
| `ndbc::file_target(source)` | per §5.2; historical and monthly ids **lower-case**, `realtime2` **upper-case** |

### 4.2 Body checks before any parser

- **Depth pre-scan** (N S3): one pass over a JSON body counting `[`/`{` nesting outside
  strings; deeper than `max_depth` (16; the deepest real document is 5) is a
  `ParseError{too_large}` before nlohmann sees it. nlohmann's DOM is recursive to
  destroy, and a parser callback does not stop the lexer from descending.
- **Content sniff** (N nit): a body whose first non-space byte is `<` where JSON or text
  is expected (a maintenance or captive-portal page) is
  `ServiceError{unexpected_content}` carrying the `Content-Type` header.
- **gzip by magic** (N S5): NDBC bodies are inflated when they start with `1f 8b`,
  whatever the URL suffix or `Content-Type` says; anything else is read as text.
  `gunzip` stays strict (one member, no trailing bytes); P8 counts members over a
  sample of files before that is final (three files: one member each).

### 4.3 Parsers

Every parser follows CD §5 (`parse_x(body, …, ReadContext) -> expected<Read<T>,
io::Error>`, stop polled every 4096 records, limits before allocation). JSON uses the
parser-callback pattern (decode at `object_end`, discard).

| Parser | Output | Notes |
|---|---|---|
| `coops::classify_body(body)` | `variant<Payload, NoData, ServiceError>` | Errors are HTTP 200 with `{"error":{"message":…}}`. "No data was found" → `NoData`; "Range Limit Exceeded" → `range_exceeded` (a planner bug); "The station is not a valid station or there is system error." → **`station_or_system`** (ambiguous by design: retried once, §5.5); "Wrong Datum" / "supported Datum values" → `datum_unavailable`; else `unknown`. Messages are cut to 120 B on a UTF-8 boundary (`detail::truncate_utf8`). Each mapping pinned by a recorded body. |
| `coops::parse_data(body, kind, ctx)` | `Read<Part>` | `t` via `parse_utc_datetime`; `v` (or `s`/`d`/`g`) via `detail::parse_double`; `""` → Missing; non-zero `f` flags kept and counted (`suspect_samples_kept`, subject `coops:f`); `q == "p"` counted (`provisional_samples`) |
| `coops::parse_predictions(body, ctx)` | `Read<Part>` | the `{"predictions": […]}` envelope |
| `coops::parse_stations(body, ctx)` | `Read<vector<CoopsStationRecord>>` | id via `StationId::make`; a record whose id, position or dates fail is **skipped and counted** (`station_skipped`, subject the raw id cut to 120 B); `established`/`removed` → `ValidRange` (`""` ongoing; no 2050, L9); sensors with `status == 1` → products by a pinned name table; unknown names counted (`unknown_sensor`) |
| `coops::parse_datums(body, ctx)` | `Read<DatumTable>` | `from_heights(stnd, …)` over known names; range names (`GT`, `MN`, `DHQ`, `DLQ`) skipped; no MSL → error, and the catalog step records the station without a table (§7) |
| `usgs::parse_items(body, ctx)` | `Read<UsgsPage>{Part, optional<Verbatim> next, optional<Time> first, last}` | `time` via the new `io::parse_rfc3339` (`Z` or an offset; built on core's `DateTimeCursor`); daily `time` a date; `value` string or `null`; `approval_status == "Provisional"` counted; `qualifier` counted by text (`qualified_samples`) |
| `usgs::classify_error(status, body)` | `ServiceError` | OGC `{code, description}` and api.data.gov `{error:{code,message}}`; `API_KEY_INVALID` → `api_key_invalid`; the time-envelope text → `range_exceeded` |
| `usgs::parse_series(body, ctx)` | `Read<vector<SeriesInfo>>` + next | as above, `station_skipped` for bad rows |
| `ndbc::parse_stdmet(text, kind, ctx)` | `Read<Part>` | §4.4 |
| `ndbc::parse_active_stations(text, ctx)` | `Read<vector<NdbcStationRecord>>` | §4.5; bad rows skipped and counted |
| `github::parse_latest_release(body, ctx)` | `Read<ReleaseInfo>` | `tag_name` through core's `Version` (B23); `body` raw for the UI to show as plain text |

`Part` is fetch's merge unit (§5.4); io builds it.

### 4.4 NDBC standard met

- **Header** by token, never position (L13): one line (pre-2007, `YY MM DD hh [mm] WD
  WSPD …`) or two (`#YY …`, `#yr mo dy hr mn degT …`). Aliases `WD`→`WDIR`, `BAR`→`PRES`;
  two-digit years are 19YY (four digits from 1999). Each file by its own header (L14).
- **Units** from the units row; without one, historical defaults (`mi` for `VIS`). A row
  disagreeing with the column's family is a `ParseError`.
- **Missing:** `MM` anywhere; otherwise per column, exact values: `WDIR`/`MWD` 999;
  `WSPD`/`GST`/`VIS` 99.0; `WVHT`/`DPD`/`APD`/`TIDE` 99.0 and 99.00; `ATMP`/`WTMP`/`DEWP`
  999.0; `PRES` 9999.0 only (999.0 hPa is a real pressure; L12); `PTDY` none. Counted
  (`legacy_sentinel_masked`, subject the column). Any other non-number is a `ParseError`
  (v4 read `MM` as 0.0, L12).
- **Quantities:** SN §6 rows; `VIS` visibility; `TIDE` water_level with datum MLLW;
  `PTDY` generic `pressure_tendency`.
- **Order:** realtime2 is newest first; a strictly decreasing file is reversed in one
  pass, anything else goes through `normalize` with its warnings.
- **File kind** (`core::ndbc::FileKind`) is an argument; it sets the unit defaults and
  warning subjects.

### 4.5 `activestations.xml`

A strict subset scanner, not an XML parser: an XML declaration, comments, one
`<stations …>` element of self-closing `<station …/>` elements with quoted attributes,
the five predefined entities and numeric character references. Anything else (DTD,
CDATA, nesting, unknown entities) is a `ParseError`, so a format change fails closed and
the live job notices. Chosen so the parser stays in io, Qt-free and fuzzed (D21); a `qt`
test runs `QXmlStreamReader` over the fuzz corpus as the reference (§10.4).

### 4.6 Station catalog — `station_catalog.hpp`

```json
{"format":"metoceanviewer-stations","version":"1.0","provider":"noaa_coops",
 "generated":"2026-10-09T07:00:00Z",
 "sources":[{"url":"https://api.tidesandcurrents.noaa.gov/mdapi/prod/webapi/stations.json?type=waterlevels&expand=details,sensors","retrieved":"2026-10-09T07:00:01Z"}],
 "stations":[
{"id":"8729840","name":"Pensacola","lon":-87.2112,"lat":30.4044,"first":"1923-04-30","coops":{"observed":["six_minute","hourly"],"met":["air_temperature","water_temperature","wind","air_pressure"],"predictions":true,"datums":{"reference":"STND","heights_m":{"MHHW":2.952,"MSL":2.757,"MLLW":2.569,"NAVD88":2.667}}}},
…]}
```

- One station per line, sorted by id, deterministic numbers (lat/lon 1e-6°, heights
  1e-4 m). Absent facts are absent members (`last` absent while ongoing; `datums`
  absent when the station has no table).
- `read_station_catalog(bytes, ctx) -> expected<Read<StationCatalog>, Error>`,
  `StationCatalog = variant<Catalog<Coops>, Catalog<Usgs>, Catalog<Ndbc>>`,
  `CatalogEntry<P>{GaugeStation<P> station; Capabilities<P> caps;}`. Version gate as SN
  §13. In our own format a bad id or a duplicate id is an error (the source parsers
  skip bad rows; the catalog never contains one). A `generated` more than one day in
  the future is `FormatError{future_dated}` (a new `FormatErrc`; N S7). Writer atomic.
- `docs/schemas/stations-v1.schema.json` and `docs/station-catalog.md` (P5), checked in
  `format-compliance`.

### 4.7 Series outputs

- **JSON** (`series_json.hpp`, EX #6, droppable): `{"format":"metoceanviewer-series",
  "version":"1.0","stations":[{id, name, provider, lon, lat, "series":[{quantity, label,
  unit, datum, "time":[…], "value":[…|null], "status":[…]?}]}]}`; `status` only when a
  sample is Dry. A daily series has `"date":[…]` and `"statistic"` instead of `time`.
  Written with `std::format` and an escaper, streamed station by station.
- **Daily (D34):** `write_station_netcdf(path, span<const AtStation<FileStation,
  DailySeries>>, options)` writes SN 1.1: `time` at 00:00 of each date with
  `time:time_basis = "local_calendar_day"` and the data variable's `cell_methods =
  "time: <mean|minimum|maximum|point> (interval: 1 day)"`; the reader returns such a
  file as `DailySeries`, never as instants. The exact layout is written into
  `docs/station-netcdf.md` by P5b. CSV: `format_csv` gains a `DailySeries` overload whose
  time cell is `yyyy-mm-dd` (§13.2 asks about the column name). IMEDS has no overload; the
  CLI reports `unsupported_cadence` (a new `FormatErrc`).
- io `WarningCode` gains `provisional_samples`, `qualified_samples`, `unknown_sensor`,
  `station_skipped`; `Error` gains HE's `JsonError` (H5).

---

## 5. fetch: stages, units, policies, machine

### 5.1 Errors, labels, results (Bd SF8)

```cpp
namespace mov::fetch {
using io::wire::Origin;
enum class TransportErrc : std::uint8_t {
  inactivity_timeout, too_slow, connection_refused, connection_closed, host_not_found,
  tls, redirect_refused, body_too_large, other };
struct TransportError { TransportErrc code; Origin origin; };
struct HttpError { int status; Origin origin; };
struct RateLimited { Origin origin; std::optional<std::chrono::seconds> retry_after; };
enum class ServiceErrc : std::uint8_t { station_or_system, datum_unavailable, range_exceeded,
  invalid_parameter, api_key_invalid, unexpected_content, unknown };
struct ServiceError { Origin origin; ServiceErrc code; std::string text; };  // ≤ 120 B, UTF-8 boundary, raw
struct NoData {};
struct AmbiguousSeries { std::vector<core::usgs::TimeSeriesId> candidates; };
struct DeadlineExceeded {};
struct UnitMismatch { core::QuantityId column; core::Unit first, other; };
struct MetaMismatch { core::QuantityId column; };

/// One error for fetch and providers. Every io::Error alternative is one here too
/// (static_assert), so lift_io is a total visit and there is one Cancelled.
using Error = std::variant<TransportError, HttpError, RateLimited, ServiceError, NoData,
  AmbiguousSeries, DeadlineExceeded, UnitMismatch, MetaMismatch,
  io::FileError, io::ParseError, io::NcError, io::FormatError, io::JsonError, core::Cancelled>;
[[nodiscard]] Error lift_io(io::Error) noexcept;

/// Which unit something happened to. Values, formatted only at the edge.
struct Window { core::TimeRange range; };
struct DayWindow { core::DayRange days; };
struct Page { core::TimeRange window; std::uint32_t index; };
struct NdbcFile { core::ndbc::FileKind kind; std::chrono::year year; std::optional<std::chrono::month> month; };
struct Resolution {};
struct CatalogPart { std::uint32_t index; };
using UnitLabel = std::variant<Window, DayWindow, Page, NdbcFile, Resolution, CatalogPart>;

struct Failure { Error error; std::optional<UnitLabel> unit; };

struct UnitFailed { UnitLabel unit; Error error; };
struct Retried { UnitLabel unit; std::uint8_t attempts; };
struct CooledDown { Origin origin; std::chrono::seconds for_; };
struct FellBack { NdbcFile from, to; };
using FetchWarning = std::variant<UnitFailed, Retried, CooledDown, FellBack>;
using Warning = std::variant<io::Warning, FetchWarning>;

struct Stats { std::uint32_t exchanges, retries; std::uint64_t bytes; };
template <class T> struct Fetched { T value; std::vector<Warning> warnings; Stats stats; };
}
```

### 5.2 Stages and units (Bd B1)

A **plan is not a list**: what to fetch next depends on what came back. Two
compositions, each used for one kind of dependency:

- **Between stages, a bind:** USGS runs stage 1 (resolve: `Request → ResolvedSeries`)
  and then stage 2 (data: `(Request, ResolvedSeries) → DailySeries | TimeSeries`), two
  one-stage machines composed in one `Job` (`Job` constructs the second from the first's
  value). Stage 1's value is a different type that the second stage's plan needs as a
  whole, which is exactly `and_then`; folding it into the unit chain would make every
  data unit carry an "unresolved" state that no data unit can have.
- **Within a stage, a chain per unit:** the stage's initial units are a list
  (`initial(request, now) -> vector<Unit>`), and each unit advances with
  `next(const Unit&, const UnitOutcome&) -> optional<Unit>`, where `UnitOutcome =
  variant<Parsed<Part>, Absent>`. The USGS `next` page and the NDBC fallback are both
  `next`; everything else returns nullopt.

| Provider | Initial units | `next` |
|---|---|---|
| CO-OPS | windows of `split(aligned(range, 1 min), max_window)`; one exchange per window, two for a paired product (one unit) | none |
| USGS stage 1 | one `Resolution` | `next` page of `combined-metadata` (bounded as below) |
| USGS stage 2 | continuous: windows of `split(aligned(range, 1 s), 1000 d)`; daily: one `DayWindow` | the page's `next`, accepted only when: the page was non-empty, the link was not seen before in this window, the page's first time is after the previous page's last, and fewer than `max_pages(window) = ceil(window / 1 min / 50000) + 2` pages were taken (N B1). Otherwise the unit fails with `ParseError{corrupt_record}`. |
| NDBC | one unit per **source** in precedence order, from `range` and `now` | `Absent` (404) → the next spelling or source: historical `h{y}`, then for `y == year(now) − 1` the twelve monthly files; for the current year each past month `{id}{m}{y}.txt.gz` → `{id}.txt` → absent; month codes 10–12 try `a`/`b`/`c` and then decimal until §1.3 is verified; plus `realtime2` when the range ends within 45 days of `now` |
| catalogs | the provider's list exchanges; CO-OPS then one `datums.json` unit per station | the list's next page (USGS) |

### 5.3 Policies (Bd SF9)

```cpp
namespace mov::fetch {
struct RetryPolicy {
  int max_attempts = 4;
  std::chrono::milliseconds base = std::chrono::seconds{1}, cap = std::chrono::seconds{30};
  std::chrono::seconds max_retry_after = std::chrono::seconds{120};   // longer: RateLimited, no wait
};
enum class Verdict : std::uint8_t { accept, retry, absent /*NDBC 404*/, cool_down /*429*/, fail };
[[nodiscard]] constexpr Verdict classify(Origin, int status) noexcept;
[[nodiscard]] constexpr Verdict classify(TransportErrc) noexcept;
/// Retry-After as delta-seconds, or an HTTP-date taken relative to the response's own
/// Date header (never the local clock); clamped to [1 s, max_retry_after].
[[nodiscard]] std::optional<std::chrono::seconds> retry_after(std::string_view value, std::optional<std::string_view> date) noexcept;
[[nodiscard]] std::chrono::milliseconds backoff(const RetryPolicy&, int attempt,
    std::optional<std::chrono::seconds> retry_after, double unit_random) noexcept;   // full jitter

enum class KeyPresence : std::uint8_t { none, present };
enum class Priority : std::uint8_t { interactive, background };
struct OriginPolicy { std::uint8_t max_in_flight; std::chrono::milliseconds min_spacing; };
[[nodiscard]] constexpr OriginPolicy policy(Origin, KeyPresence) noexcept;
//   coops_*: {2, 200 ms}; usgs: {2, 0} none, {4, 0} present; ndbc: {1, 250 ms}; github: {1, 0}

struct ExchangeLimits {
  std::chrono::seconds inactivity{30};          // QNetworkRequest::setTransferTimeout
  std::uint64_t min_bytes_per_window = 64 << 10; // throughput: at least 64 KiB ...
  std::chrono::seconds throughput_window{60};    // ... in every 60 s after the first byte
  std::uint64_t max_body_bytes = std::uint64_t{64} << 20;   // also the decompression threshold
};
}
```

- **429** is accepted as a response, then the origin cools down (N nit): `cool_down`
  sets the shared `cool_down_until` from `Retry-After`, or from `backoff` without one;
  `X-RateLimit-Remaining: 0` is a 429 without `Retry-After`. Queued exchanges to that
  origin wait; the exchange that got the 429 is retried after the cool-down.
- **`station_or_system`** is retried once, then fails.
- **`api_key_invalid`** fails at once; there is no silent retry without the key (N nit).
- **Exchange limits** (N S14, Br): no fixed per-exchange deadline (a large page on a slow
  link is legitimate); an exchange fails `inactivity_timeout` after 30 s without a byte
  and `too_slow` when fewer than 64 KiB arrive in any 60 s after the first byte. The
  clocks start at `Transport::start`, not when the exchange was queued.
- **Fetch deadline** (Br): `Machine` takes an optional deadline (`Time`); every event
  carries `now`; past the deadline the machine aborts everything and finishes
  `DeadlineExceeded`. The app sets none (the user cancels); the CLI's `--timeout` sets
  it, and it exits 1, not 130.
- **Priority** (Br nit): catalog refreshes and the update check are `background`; an
  origin gate admits background exchanges only when no interactive one is queued.

### 5.4 Parts and merging (Bd SF4, SF5, SF6)

```cpp
namespace mov::fetch {
/// One unit's data, by column. Identity: the empty part. combine(a, b): per key, the
/// union of rows by time where a's row wins on equal times (left-biased), b's unit
/// converted exactly to a's first. Associative; folded in plan order.
struct Column { core::SeriesMeta meta; std::vector<core::Point> rows; };
struct Part { std::map<std::string, Column, std::less<>> columns; };   // key: quantity token
[[nodiscard]] std::expected<Part, Error> combine(Part a, Part b);
}
```

- Units: a later part whose column unit differs is converted with `core::convert` to the
  first part's unit (NDBC `VIS` in `mi` and `nmi` across eras); an incompatible unit is
  `UnitMismatch`. The **metas-agree law**: after conversion, quantity, label and datum of
  a column agree across parts, else `MetaMismatch` (tested as a property over generated
  parts).
- Left bias is the precedence rule: NDBC sources are folded historical, monthly,
  realtime, so the quality-controlled row wins; dropped rows are counted
  (`duplicate_times_dropped`, subject `ndbc:<file kind>`). CO-OPS and USGS windows are
  disjoint, so the union is concatenation (tested: zero `duplicate_times_dropped`, N S8).
- The fold ends in `S::finish(Part) -> expected<Read<Value>, Error>` building the
  `TimeSeries` / `DailySeries` / `StationTable` through `normalize` (a strict check that
  never reorders a merged part).
- Merging is a `Merge` step whose inputs are **moved out of the machine by value**; it
  runs on the pool with a `StopToken` and returns through an `on_merged` event. No
  sentinel unit stands for it (Bd SF6).

### 5.5 The machine (sans-IO; Bd SF7)

```cpp
namespace mov::fetch {
struct ExchangeId { std::uint32_t value; /* <=> */ };   // fresh for every attempt
struct Response { int status; std::optional<std::chrono::seconds> retry_after;
                  std::optional<std::int64_t> rate_remaining; };   // the body stays with the job
using Outcome = std::variant<Response, TransportErrc>;

namespace event {
struct Completed  { ExchangeId id; Outcome outcome; };
struct WaitElapsed{ ExchangeId id; };
template <class Part> struct Parsed { ExchangeId id; std::expected<io::Read<Part>, Error> part; };
template <class V> struct Merged { std::expected<io::Read<V>, Error> value; };
struct Cancel {};
}
template <class S> using Event = std::variant<event::Completed, event::WaitElapsed,
    event::Parsed<typename S::Part>, event::Merged<typename S::Value>, event::Cancel>;

struct Send     { ExchangeId id; io::wire::Target target; Priority priority; };
struct Wait     { ExchangeId retry_of; std::chrono::milliseconds delay; };
struct CoolDown { Origin origin; std::chrono::milliseconds for_; };
struct Parse    { ExchangeId id; };                                     // the body of `id`, on the pool
template <class S> struct Merge { std::vector<typename S::Part> in_plan_order; };   // moved out
struct Abort    { std::vector<ExchangeId> ids; };
struct Progress { std::uint32_t done, total; };
template <class V> struct Finish { std::expected<Fetched<V>, Failure> result; };
template <class S> using Step = std::variant<Send, Wait, CoolDown, Parse, Merge<S>, Abort, Progress,
                                             Finish<typename S::Value>>;

template <FetchSpec S>
class Machine {
 public:
  struct Started;   // {Machine machine; std::vector<Step<S>> steps;}
  [[nodiscard]] static Started start(typename S::Request, Strictness, RetryPolicy,
                                     std::optional<core::Time> deadline, core::Time now);
  [[nodiscard]] std::vector<Step<S>> on(Event<S>, core::Time now, double unit_random);
  [[nodiscard]] bool finished() const noexcept;
};
}
```

- **Total:** an event naming an unknown id, an aborted id or arriving after `Finish`
  returns no steps. Every attempt gets a fresh id, so a late completion of an aborted
  attempt can never be taken for its retry. No bytes, clock or threads inside.
- **Invariants** (property tests over generated event scripts, §10.1): each `Send` gets
  one `Completed` or one `Abort`; attempts per unit ≤ `max_attempts`; after a 429 with
  `Retry-After: n` no `Send` to that origin before n s; after `Cancel`, or past the
  deadline, no `Send`; exactly one `Finish`, and nothing after it; `Finish` holds the
  fold of exactly the accepted parts in plan order; `Progress.done` is monotone and
  reaches `total` (which grows when `next` adds a unit).

### 5.6 Partial results (Bd SF4, Br)

| Unit outcome | Strict (default) | Lenient (CLI `--partial`, background catalog refresh) |
|---|---|---|
| data | kept | kept |
| no data (CO-OPS "No data was found", an empty USGS page with no `next`, NDBC absent after its fallbacks) | gap | gap |
| failure after retries (transport, HTTP, service, **parse**) | `Finish{Failure{error, unit}}`, `Abort` the rest | gap + `UnitFailed{unit, error}` |
| cancelled / deadline | `Finish{Failure{Cancelled / DeadlineExceeded}}` | same |

- `NoData` is decided only at `Finish`, and only when **no** unit failed and no unit
  had data.
- A lenient run in which every unit failed is a `Failure` (the first unit's error), not
  `NoData`.
- **Which failure is reported** in strict mode is the first to arrive; when two units
  fail concurrently that depends on completion order, so tests assert only that the
  reported unit is one of the failed ones (Br nit). v4 overwrote earlier errors with
  later ones and ignored failing chunks (L2, L3).

---

## 6. providers: the Qt edge

### 6.1 Objects

```cpp
namespace mov::providers {
struct NetworkOptions { fetch::ExchangeLimits limits{}; fetch::RetryPolicy retry{};
                        QString user_agent; /* "MetOceanViewer/<version> (+https://github.com/zcobell/MetOceanViewer)" */
                        std::size_t cache_bytes = std::size_t{256} << 20; };

/// One exchange in flight. Move-only; destroying it aborts the exchange, and after an
/// abort its completion is never delivered (Br B1).
class Exchange { public: Exchange(Exchange&&) noexcept; ~Exchange(); void abort() noexcept; };

/// The seam the tests replace. Completions are always posted to `receiver`'s thread
/// (queued), never invoked from inside start() or abort().
class Transport {
 public:
  virtual ~Transport() = default;
  using Done = std::function<void(fetch::Outcome, QByteArray body)>;
  [[nodiscard]] virtual Exchange start(const fetch::Send&, const RequestHeaders&, QObject* receiver, Done) = 0;
};
class QnamTransport final : public Transport { /* owns QNetworkAccessManager */ };

/// **One per process**, on the thread with the event loop (GUI thread in the app, main
/// thread in the CLI). Owns, in destruction order: the jobs (cancelled and destroyed
/// first), the result cache, the origin gates, the API key, the transport (last).
class Network : public QObject {
 public:
  Network(NetworkOptions, std::unique_ptr<Transport>, QObject* parent = nullptr);
  ~Network() override;
  void set_usgs_key(std::optional<ApiKey>);   // attached only to Origin::usgs (Bd SF10)
};

template <class P>
concept Fetcher = requires(P& p, typename P::Request r) {
  { p.fetch(std::move(r)) } -> std::same_as<QFuture<std::expected<fetch::Fetched<typename P::Value>, fetch::Failure>>>;
};

class CoopsProvider {      // UsgsProvider, NdbcProvider, CatalogProvider, UpdateCheck alike
 public:
  using Request = core::coops::Request;
  using Value = core::AtStation<core::StationId<core::provider::Coops>, CoopsResult>;
  explicit CoopsProvider(Network&);
  [[nodiscard]] QFuture<std::expected<fetch::Fetched<Value>, fetch::Failure>> fetch(Request, fetch::Priority = fetch::Priority::interactive);
};
}
```

- `CoopsResult = variant<TimeSeries, ObsVsPred, WindSeries>` mirrors `coops::shape`
  (the `TideResult` pattern of HE §9.2). `UsgsProvider::Value` holds
  `variant<TimeSeries, DailySeries>` (Bd B3); `NdbcProvider::Value` a one-station
  `StationTable`.
- `fetch()` asserts it runs on `Network`'s thread.

### 6.2 `QnamTransport`

| Concern | Rule |
|---|---|
| Ownership | `QNetworkReply` held by the `Exchange` through `unique_ptr<QNetworkReply, DeleteLater>`; no reply is parented elsewhere (B23). |
| Redirects (N B2) | `UserVerifiedRedirectPolicy`; the `redirected` handler allows only `https` with the request's own host and port, else aborts with `redirect_refused`; `setMaximumRedirectsAllowed(3)`. The probe (§1.5) showed raw headers, the key included, follow redirects to any origin under the default policy. |
| Timeouts | `setTransferTimeout(inactivity)`; a throughput check on `downloadProgress` per `ExchangeLimits`; both from `start`. |
| Body cap (N S4) | `setDecompressedSafetyCheckThreshold(max_body_bytes)`; the transport also counts the bytes it reads (decompressed) and aborts with `body_too_large` past the cap; error 299 on a reply with `Content-Encoding` maps to `body_too_large`. P8 measures the real compression ratios of CO-OPS and USGS responses. |
| Headers | `User-Agent`; `X-Api-Key` only when `Network` supplies it, which it does only for `Origin::usgs`; never a URL parameter. |
| TLS | system trust store; no `ignoreSslErrors` (gate, §2.2). |
| HTTP/2 | left enabled (Qt's default for HTTPS); concurrency is the gate's. |

### 6.3 One fetch

```
fetch(req) [Network thread]
  Job (QObject, child of Network): QPromise<Result> p; p.start(); f = p.future()
  QFutureWatcher on f: canceled() → enqueue event::Cancel
  steps = Machine::start(...).steps → step queue
  run queue (never re-entrant: a step that produces events enqueues them):
    Send     → OriginGate (round-robin across jobs, interactive first, spacing, cool-down)
                → Transport::start → Exchange kept in a map by id
                → Done posted to the job → enqueue event::Completed (body kept by id)
    Wait     → QTimer::singleShot(delay, job, …) → event::WaitElapsed
    CoolDown → OriginGate::cool_down(origin, for)
    Parse    → QtConcurrent::run(pool, [body = std::move(body), stop]{ return S::parse(body, ctx); })
                 .then(job, …) → event::Parsed
    Merge    → QtConcurrent::run(pool, [parts = std::move(m.in_plan_order), stop]{ fold, finish })
                 .then(job, …) → event::Merged
    Abort    → erase the Exchanges (their destructors abort)
    Progress → p.setProgressRange / setProgressValueAndText
    Finish   → cache.insert (success only) ; if not p.isCanceled(): p.addResult(result) ;
               p.finish() ; job->deleteLater()
```

- **Result rule:** the job calls `addResult` before `finish()` unless the promise is
  cancelled (asserted): a finished promise without a result crashes its continuation
  (§1.5).
- **Stop token:** `core::StopToken{[f]{ return f.isCanceled() or f.isFinished(); }}`
  (Br nit): a task that outlives its job's `Finish` stops too.
- **Threads:** gate, machine, transport callbacks, timers on `Network`'s thread; HTTP in
  Qt's internal thread; parse, gunzip, merge on the global pool; delivery through the
  caller's `then(context, …)`; netCDF writes (CLI) on the serial pool of CD C11. What
  crosses threads: the body `QByteArray` (moved; io reads a `string_view` of it), parts
  and values (moved), `expected` results. No QObject pointer or job reference crosses.

### 6.4 Cancellation, staleness, consumers (Br)

- **Rule:** a caller cancels with `cancelChain()` on the future it holds (the end of its
  continuation chain). On Qt 6.11.3 that reaches the job's promise (§1.5); `cancel()` on
  a continuation does not, and is not used. A test pins it (§10.2), so a Qt update that
  changes it fails CI.
- **One continuation per future;** a future is never handed to two consumers. Cached
  results (below) give each consumer its own ready future.
- **Stale results:** a view model tags each selection with a generation number; its
  continuation drops a result whose generation is not current. A view model that is
  destroyed calls `cancelChain()` on its pending futures in its destructor (Phase 4).
- **Destruction:** `~Network` cancels every job and destroys it before the transport;
  context-object continuations of a destroyed job are cancelled by Qt.

### 6.5 Result cache

On `Network`'s thread only. Keys are requests (`Request` is regular). Values are
`std::shared_ptr<const Fetched<Value>>`, immutable once inserted. Filled **inside** the
job just before `Finish`; a hit returns
`QtFuture::makeReadyValueFuture(expected{*value})` (a copy), so no future is shared.
Bounded: LRU, 256 MiB by estimated size (24 B per sample) and 64 entries. Session only,
no disk (EX #14). A request whose range ends within 2 hours of now is not cached (the
data is still arriving).

### 6.6 USGS site metadata

On selection, `UsgsProvider::series(StationId)` fetches every continuous and daily
series of the site (`combined-metadata` by `monitoring_location_id`) for the product
picker, cached like results.

### 6.7 Station catalogs

`CatalogProvider::fetch(DataSource, Priority::background)` runs the catalog plan
leniently; `CatalogStore` loads and saves (§7).

### 6.8 Update check

`UpdateCheck::fetch()` asks `releases/latest` with `If-None-Match`; 304, 403, 404, 429
and network failure are "check skipped" values. Version order is core's `Version`
(B23). The release notes are shown as plain text in Phase 4 (`Text.PlainText`), never as
rich text (B23; N nit: every server string in the UI is plain text).

### 6.9 `describe()`

Errors carry no text below the edge (CD §2.12). `describe(const X&) -> std::string` for
core, io and fetch errors, labels and warnings lives in `providers` (`describe.hpp`), the
lowest layer both edges link. English only. It escapes control characters in server text
(`\xNN`) and never prints an `ApiKey`.

### 6.10 Tides provider (H9)

HE §9.2 with: `core::StopToken` instead of `std::stop_token`; `fetch::Failure` instead of
`ProviderError`; `Value = AtStation<StationId<Harmonics>, TideResponse>`; the work runs
as `QtConcurrent::run(pool, [file, req](QPromise<Result>& p){ … })`, so the promise is
the task's own and `p.isCanceled()` is the stop token; no `Network`, no gate. The file is
a `std::shared_ptr<const core::tide::HarmonicsFile>`, immutable.

---

## 7. Station lists

| Provider | Source | Kept | Count (2026-10-09) |
|---|---|---|---|
| NOAA CO-OPS | `type=waterlevels` ∪ `type=met` with `expand=details,sensors`, then `datums.json` per station | active only (D6) | 302 and 315, overlapping |
| USGS | `combined-metadata`, `statistic_id=00011`, `parameter_code=00060,00065,62620,62619` (D33), paged | sites with ≥ 1 such series, ended series included | measured in P3 (v4: 10,503 sites) |
| NDBC | `activestations.xml`, `met="y"` | active, with met data | 905 of 1,354 (v4: 357) |
| Harmonics | the user's file | HE §9.2 | — |

- **Snapshot:** `data/stations/{noaa_coops,usgs,ndbc}.json` from
  `tools/update_station_snapshot.sh` (runs `metocean-data stations build` per provider),
  refreshed per release, compiled into `mov_providers` with `qt_add_resources`.
- **Cache:** `QStandardPaths::AppLocalDataLocation/stations/<provider>.json`. The CLI
  sets the same organization and application names as the app, so both use one
  directory (N nit).
- **Startup:** off-thread, the valid one of cache and snapshot with the newer
  `generated`; an invalid cache (unparsable, future-dated, wrong provider) is reported
  once and ignored. Loaded once into an id-indexed table, never re-parsed per lookup
  (B25).
- **Refresh** (lenient, background): at most once per launch when the loaded catalog is
  older than 30 days, or on the manual action. A failed `datums.json` for one station is
  a warning and the station is kept without a table (N S7). The new catalog replaces
  the old only if it is valid and has **at least 50 %** of the old station count;
  otherwise it is discarded with a warning (an upstream outage must not empty the map).
  Written with `write_file_atomic`.
- **Offline:** the snapshot always exists; fetches fail inline.

---

## 8. The USGS API key and settings

- `ApiKey::make(std::string) -> expected<ApiKey, ApiKeyError>`: trimmed, 1–128 printable
  ASCII. No `operator<<`, no `std::formatter`; `reveal()` is called in one place (the
  transport setting the header); `describe` gives `"<redacted>"`.
- **App (Phase 4, W-K; D5, D35):** QtKeychain (service `io.github.zcobell.metoceanviewer`,
  account `usgs-api-key`), read asynchronously at startup. Where no keychain backend
  exists (Linux without a Secret Service), the key is stored in plain text in
  `AppLocalDataLocation/usgs-api-key` (owner-only permissions on POSIX), as v4 did; the
  Settings page says which storage is in use. The session-only design of revision 1 is
  withdrawn. An `api_key_invalid` response is shown inline; the stored key is not
  deleted automatically.
- **QtKeychain build:** an overlay port in `cmake/vcpkg-ports/qtkeychain` against
  `$QT_ROOT_DIR` (§1.4); Linux needs `libsecret` at build time and bundles it in the
  AppImage.
- **CLI:** `--api-key-file PATH` wins over `MOV_USGS_API_KEY` (N nit). The file is opened
  once, then `fstat`ed on the open handle: a regular file, at most 4 KiB, and on POSIX not
  group- or world-readable; otherwise a usage error. Never `--api-key VALUE`. The CLI
  does not read the app's keychain or key file.
- **Settings:** non-secret settings use `QSettings` in `app`; providers take
  `NetworkOptions` values and never read settings. The harmonics path is
  `tides/harmonicsFile` (HE §9.2).
- **Fixtures and logs:** the cassette recorder drops `X-Api-Key` and `api_key`; a test
  greps every cassette for both names; a test captures all Qt messages during a fetch
  (`qInstallMessageHandler`) and asserts no URL or key appears (L11, N S10).

---

## 9. CLI: `metocean-data`

### 9.1 Commands

```
metocean-data fetch coops   SELECTION --product NAME [--interval 6m|1h|1m|5m|10m|15m|30m]
                            [--datum NAME] --start T --end T [-o FILE|-] [--format netcdf|imeds|csv|json]
                            [--units metric|english]
metocean-data fetch usgs    SELECTION --parameter CODE [--daily --statistic mean|min|max|CODE] …
metocean-data fetch ndbc    SELECTION [--column WSPD,WDIR,…] …
metocean-data stations list   --provider P [SELECTION] [--format csv|json]
metocean-data stations show   --provider P --station ID
metocean-data stations build  --provider P --out FILE
metocean-data tides predict   (HE §7.8, H8; through the tides provider)
metocean-data harmonics validate|info|subset|add-computed-datums   (HE §7.8, H8)
metocean-data hwm-stats FILE [--units ft|m] [--through-origin] [--breaks a,b,c] [--format text|json]
metocean-data convert IN -o OUT [--format …] [--crs EPSG:n] [--stations all|0,3,7] [--cold-start T]

SELECTION := --station ID… | --stations-file F | --bbox W,S,E,N | --near LON,LAT[,KM]
```

Common: `--catalog FILE`, `--partial`, `--timeout SECONDS`, `--dry-run` (planned URLs,
no network, key redacted), `--diagnostics text|json`, `--quiet`, `--progress`,
`--api-key-file`, `--max-samples N`, `--help`, `--version`.

**CO-OPS product rule table** (Bd SF12; one spelling per observed series):

| `--product` | `--interval` (default first) | `Product` |
|---|---|---|
| `water_level` | `6m`, `1h`, `1m` | `WaterLevel{ObservedLevel{six_minute | hourly | one_minute}, datum}` |
| `predictions` | `6m`, `1h`, `1m`, `5m`, `10m`, `15m`, `30m` | `WaterLevel{PredictedLevel{…}, datum}` |
| `observed_vs_predicted` | `6m`, `1h` | `WaterLevel{ObservedVsPredicted{six_minute | hourly}, datum}` |
| `air_temperature`, `water_temperature`, `air_pressure`, `humidity`, EX #1 names | `6m`, `1h` | `Meteorological{…}` |
| `wind_speed`, `wind_direction`, `wind_gust` | `6m`, `1h` | `Meteorological{wind, …}` plus `WindComponent` selecting one column |

Any other pair is a usage error naming the allowed intervals. `--datum` is **required**
for the water-level rows (the API requires one and there is no neutral default; a silent
MLLW would mislabel scripts that expected MSL) and refused for the others.

**Selections and `offered`** (Bd SF12): `--station` and `--stations-file` name stations
on purpose, so a station that is not in the catalog, or whose capabilities do not offer
the request (`core::offered`), is a usage error before any network (exit 2). `--bbox`
and `--near` select **among stations that offer the request**: the others are skipped
with one summary warning; `--near` takes the nearest offering station; an empty
selection is exit 3.

- **Times:** `parse_utc_datetime` forms, end exclusive; v4's `yyyyMMddhhmmss` is refused
  with a hint (v4 read it as local time). USGS `--daily` needs whole days
  (`DayRange::covering`).
- **Output formats:** from the extension or `--format`; a mismatch is a usage error;
  `-o -` for CSV, IMEDS and JSON. A daily series to IMEDS is `unsupported_cadence`
  (exit 2, checked before the network).

### 9.2 Execution, output and memory (N S11, S12)

- Stations run with at most 4 in flight (origin gates bound exchanges below that).
- **Memory:** before any network the CLI estimates the samples from the plan (window
  length over the product's cadence, times stations); above `--max-samples` (default
  2^25, about 800 MB at 24 B per sample) it is a usage error suggesting a shorter range
  or fewer stations. CSV, JSON and IMEDS stream station by station into the output;
  station netCDF holds the table until it is written.
- **Commit:** files are written with the atomic writers; in strict mode any failure
  abandons the output (the target stays untouched). `-o -` is buffered whole and written
  only on success. With `--partial` the output holds what succeeded.
- **EPIPE:** `SIGPIPE` is ignored; a write that fails with `EPIPE` ends the program
  quietly with the exit code it would have had.
- **SIGINT** (Br): the handler only sets a flag and writes a byte to a self-pipe (POSIX)
  or, on Windows, the console control handler (which the system runs on its own thread)
  posts a queued call to the main thread. The main thread then `cancelChain()`s every
  original future and waits, without blocking, on `QtFuture::whenAll` over them and over
  the futures of the serial netCDF pool's pending writes, then exits **130 because the
  flag is set** (not because a result says `Cancelled`). A second SIGINT calls
  `_exit(130)` from the handler.
- `--timeout` exceeded: `DeadlineExceeded`, exit 1.

### 9.3 Outcome and exit code (Bd SF11, N S11)

Per station the outcome is one of `ok`, `ok_partial`, `no_data`, `failed`; the run adds
`interrupted` and `usage`. The run's outcome is a join, and `exit_code` is an exhaustive
`switch` over it:

| Run outcome (first matching row) | Exit |
|---|---|
| usage error (before any network) | 2 |
| interrupted | 130 |
| any station `failed` | 1 |
| any station `ok_partial` (only with `--partial`) | 4 |
| every station `no_data` | 3 |
| otherwise (all `ok`, or `ok` mixed with `no_data`) | 0 |

Mixed data and no-data is success: the output holds the stations with data and a
`no_data` warning names each of the others. In strict mode a run with a failed station
writes nothing (above). Rendering: `metocean-data: error: 8729840 window
[2026-09-01, 2026-10-01): <describe>` and `metocean-data: warning: …` on stderr, or one
JSON object per line with `--diagnostics json`.

Structure: `main()` builds `QCoreApplication`, calls `mov::cli::run(args, Services&)`,
and `exec()`s once. `run` parses with `QCommandLineParser` into a typed `Command`
(`to_command(ParsedArgs) -> expected<Command, UsageError>`, Qt-free and tested), then
executes it. Tests call `run` in-process with a `FakeTransport`; the binary has no
switch that replaces its transport.

### 9.4 v4 → v5

| v4 (`MetOceanData`, `MetOceanHWMStats`) | v5 |
|---|---|
| `-s/--service NOAA\|USGS\|NDBC\|XTIDE` | `fetch coops\|usgs\|ndbc`; XTIDE → `tides predict` |
| `--station ID` | `--station ID` (validated by `StationId<P>::make`, checked against the catalog) |
| `--boundingbox x1,y1,x2,y2` | `--bbox W,S,E,N` |
| `--nearest x,y` | `--near LON,LAT[,KM]` |
| `--list FILE` | `--stations-file FILE` |
| `--show` | `stations list` |
| `-b/-e yyyyMMddhhmmss` (local time) | `--start/--end` ISO 8601 UTC, end exclusive |
| `-p INDEX` or a `std::cin` menu | `--product NAME` (+ `--interval`) (B24, B25) |
| `--parameter CODE` | `--parameter CODE` [`--daily --statistic`] |
| `-d INDEX` (incl. LWI/HWI) | `--datum NAME`, required for water level, only offered datums |
| `--vdatum` | removed (D32) |
| `-o FILE` (`.imeds`/`.nc`) | `-o FILE` + `--format` (also CSV, JSON) |
| XTide output labelled MLLW whatever happened (B24) | the series meta carries the real datum |
| `MetOceanHWMStats -f FILE`, `-z` | `hwm-stats FILE`, `--through-origin` (σ with n − 1, D17; uncentred origin R², D25) |

---

## 10. Testing

### 10.1 Pure tiers (core, io, fetch)

- **core:** `split`/`aligned` laws; `DayRange::make`/`covering`; every `coops::Product`'s
  `max_window`, `shape`, datum token in `STATIC_REQUIRE` tables; code and id grammars;
  `registry_quantity`; `offered` truth tables; `DailySeries::make`.
- **io:** every parser against recorded fixtures (PA samples promoted to
  `tests/fixtures/io/{coops,usgs,ndbc,github}/` plus P8's recordings); the depth
  pre-scan; the content sniff; gzip by magic; targets byte-exact (minute and second
  alignment); `Verbatim::make` refusing another origin, `http:` and another path; the
  catalog round trip and schema, bad and future-dated catalogs; the JSON writer golden;
  the daily SN, CSV and IMEDS behavior.
- **fetch:** `initial`/`next` tables (NDBC for given `now`s, month spellings; USGS page
  bounds: empty-with-next, repeated next, non-advancing time, page cap); `combine`
  identity, associativity, left bias, unit conversion, `UnitMismatch`, the metas-agree
  law; `retry_after` (seconds, HTTP-date against `Date`, clamping); `policy` table; the
  machine over scripted scripts and a seeded generator (2,000 scripts per run: statuses,
  transport errors, delays, late completions of aborted ids, cancellation and deadline
  points), checking §5.5's invariants and §5.6's table. Virtual time only.

### 10.2 Qt tier with `FakeTransport` (label `qt`)

- **Cassettes:** `tests/fixtures/providers/cassettes/<name>/exchanges.json` (method, URL
  with the key removed, status, the headers that matter, body file). An unmatched
  request fails the test naming the URL. Scripted variants: 0–50 ms delays, 429/503
  sequences, hangs. Recorded with `metocean-data … --record-cassette DIR` (hidden), then
  trimmed by hand.
- **`mov::test::drive(QFuture<T>, timeout) -> expected<T, TimedOut>`** (Br): a
  top-level `QEventLoop` (Catch2's main runs none), quit by `QFutureWatcher::finished`.
  On timeout it `cancelChain()`s the future and keeps running until it finishes. Before
  returning it flushes `DeferredDelete` events and asserts that no `Job` is alive (a
  test-visible instance counter). It is the only allowed loop (§2.2).
- **No timing assertions** in this tier: delays only order events; no test asserts how
  long anything took.
- Cases: happy paths per provider; out-of-order chunk completion with the result in plan
  order; cancel before send, during send, during parse, during merge, after finish;
  **`cancelChain()` on a two-level continuation reaches the job and aborts its exchange;
  `cancel()` on the continuation does not** (pins §1.5); a destroyed `Network` cancels
  its jobs before its transport; two jobs on one origin share the cool-down and
  round-robin; background waits for interactive; a cache hit gives an independent ready
  future; `--partial` with a parse error; `AmbiguousSeries`; NDBC fallbacks; catalog
  store precedence, the 50 % rule, a future-dated cache.
- **B23 (N S9):** after an update check and a fetch finish and `drive` returns, the
  transport's `QNetworkAccessManager` has no `QNetworkReply` children and no job is
  alive; under ASan/LSan in the `asan-qt` job.

### 10.3 Real transport against a local server (label `qt`)

`tests/support/local_http_server.{hpp,cpp}`: a `QTcpServer` speaking scripted HTTP/1.1
on loopback. Cases: inactivity timeout (silent after headers); `too_slow` (a drip under
the throughput floor); abort on cancel (the server sees the close); `Retry-After` in
seconds and as a date with a skewed `Date`; the body cap, raw and `Content-Encoding`
deflate (error 299 → `body_too_large`); a redirect to another port, to `http`, and a
fourth redirect, all refused, and the second server never sees `X-Api-Key`; `User-Agent`
present; the key never in a URL.

### 10.4 Fuzzing (D21)

| Target | Reference law |
|---|---|
| `fuzz_coops_json` (1-byte product prefix) | no crash or UB; accepted output strictly increasing; Missing only where the text had `""` |
| `fuzz_coops_metadata` | locations valid; tables only through `from_heights`; skipped rows counted |
| `fuzz_usgs_items` | `next` absent or a `Verbatim` of the USGS origin and pinned path; Missing only for `null`/`""` |
| `fuzz_usgs_paging` (bytes → a sequence of pages) | the unit chain ends within `max_pages`; never follows a repeated link or a page that does not advance (N B1) |
| `fuzz_usgs_catalog` | ids pass `StationId<Usgs>`; `ValidRange` ordered |
| `fuzz_ndbc_stdmet` (1-byte file-kind prefix) | columns equal the recognized header tokens; masked counts bound Missing |
| `fuzz_ndbc_active_stations` | the scanner accepts only the subset; a `qt` test checks the corpus against `QXmlStreamReader` |
| `fuzz_json_depth` | the pre-scan's verdict equals a reference depth count; nothing deeper reaches nlohmann |
| `fuzz_station_catalog` | `parse(format(x)) == x` |
| `fuzz_rfc3339` | error column inside the text; canonical form re-parses |
| `fuzz_fetch_machine` (bytes → an event script) | §5.5's invariants; no step after `Finish` |

### 10.5 Live job (opt-in, nightly, non-blocking; N S13)

`.github/workflows/live-api.yml`: nightly, `continue-on-error`, never required; `ctest -L
live` with `MOV_LIVE_API=1` and the optional secret `USGS_API_KEY`. One small request per
provider asserting **structure only** (CO-OPS keys and error envelope, mdapi members,
USGS paging and both error shapes, NDBC header tokens and monthly naming, the XML subset,
GitHub fields). Each check retries once. An unreachable origin (DNS, connect, 5xx after
the retry) is a `SKIP`, not a failure; a shape change fails. On failure the workflow
opens or updates **one** issue labelled `live-api` (deduplicated by title) with the log
link.

### 10.6 Coverage

`core`, `io`, `fetch`: the 90 % gate. `providers` and `cli`: 85 % target under the Qt
coverage variant, within the 80 % overall gate.

---

## 11. Bugs in scope

### 11.1 Plan §1.2 and §1.1

| Bug | Prevention | Test |
|---|---|---|
| B23 `updatedialog.cpp`: inconsistent version order, reply never deleted, unescaped rich text | core `Version`; replies owned by `Exchange`; plain text in the UI (Phase 4) | `[regression][B23]` order table; no reply children and no live job after `drive` (§10.2); Phase 4 plain-text test |
| B24 CLI menu bounds; XTide labelled MLLW; `station(i)` after skips | names; datum from series meta (H8); results keyed by `StationId` | product rule table; a 3-station run whose middle station fails keeps ids aligned; tides datum label |
| B25 `checkIntegerString`; the station CSV re-parsed per line | typed `to_command`; catalog loaded once | `--product 0`, `-1`, `99` are usage errors; one catalog parse for 100 stations |
| §1.1 nested `QEventLoop::exec()` (`noaacoops.cpp:141`, `usgswaterdata.cpp:78`, `ndbcdata.cpp:96`, `updatedialog.cpp:123`), `Generic::delay()` | futures and timers | `no_blocking_calls` gate |
| §1.1 startup GET to google.com | no startup network (P14) | Phase 4 startup test with no network |

### 11.2 Legacy provider bugs

| # | v4 behavior | v5 | Test |
|---|---|---|---|
| L1 | CO-OPS windows share a boundary sample | minute-aligned half-open windows, end − 1 min | targets byte-exact; zero `duplicate_times_dropped` |
| L2 | a later chunk's error overwrites an earlier one | the first failure is reported with its unit | machine scripts |
| L3 | a failing chunk ignored | strict failure or `UnitFailed` | machine scripts |
| L4 | ≤ 3-point series dropped | kept | 1-point fixture |
| L5 | products by combo index; `"wind:speed"` split | `coops::Product`, rule table | `STATIC_REQUIRE`, CLI table |
| L6 | USGS end date + 1 day | the range as asked | targets |
| L7 | USGS zones via the abbreviation table | UTC instants; daily as `DailySeries` | DST fixture |
| L8 | USGS only increasing timestamps kept | left-biased union, `normalize` | out-of-order page |
| L9 | NOAA "present" = 2050; `-999999` datums | `ValidRange`, optional table | catalog fixture |
| L10 | predictions reuse the observed product's datum handling | each `WaterLevel` carries its datum | targets |
| L11 | URLs logged with `qDebug()` | no URL logging; key never in a URL | message-handler capture test |
| L12 | NDBC sentinels: 4 literals, 9999.0 kept, `MM` → 0.0 | exact per-column values, `MM`, else `ParseError` | all-sentinel fixture |
| L13 | minute column by `d[4] == "mm"` | header tokens | pre-2005 fixture |
| L14 | the first file's header for every year | each file's own header | two-era pair |
| L15 | `view_text_file.php` and its 200 "Unable to access data file" | direct files; 404 is absent | wrapper body is a `ParseError` |
| L16 | current year unobtainable | monthly and realtime2 (D8) | NDBC plan table |

---

## 12. Work packages

Sizes in working days. Every package ends green in CI; the phase ends with
architecture-clarity-reviewer on the whole diff, then documentation-reviewer and
prose-editor.

| WP | Scope | Depends on | Size | Reviewers |
|---|---|---|---|---|
| **H0** | HE §10's H0 plus: the `fetch` layer, Qt Network/Concurrent, the `no_blocking_calls` and `no_ignore_ssl_errors` gates, JSON/zlib include gates, the `live` label, the Qt coverage variant, the doc sync items of §13.3 | — | S (2) | jason-turner, neckbeard-nate, architecture-clarity-reviewer |
| P1 | core: `split`, `aligned`, `DayRange`, `coops`, `usgs` (selectors, `ResolvedSeries`, `SeriesInfo`), `ndbc`, `Capabilities`, `offered`, `DailySeries`, `registry_quantity`, `GaugeStation::datums` removed | H0 | M (4) | ben-deane (lead), sean-parent, jason-turner |
| P2 | io CO-OPS: targets, body checks, `classify_body`, data/predictions/stations/datums parsers, fuzz | P1 | M (3) | neckbeard-nate (lead), sean-parent, conor-hoekstra |
| P3 | io USGS: targets, `Verbatim`, `parse_rfc3339`, page/series/catalog parsers, unit aliases, error shapes, fuzz (incl. paging); measures the catalog size | P1 | M (3.5) | same |
| P4 | io NDBC: stdmet parser, sentinel table, XML subset scanner, public `gunzip`, gzip sniff, fuzz | P1 | M (3) | same |
| P5a | io station catalog + schema + `docs/station-catalog.md`; JSON series writer (droppable) | P1 | S (2) | same |
| P5b | io daily outputs (D34): SN 1.1 writer and reader for `DailySeries`, `docs/station-netcdf.md` minor bump, CSV overload, `unsupported_cadence` | P1 | M (3) | neckbeard-nate (lead), sean-parent, ben-deane |
| P6 | fetch: errors and labels, `initial`/`next` per provider, `Part`/`combine`, policies, `retry_after`, `Machine`, property tests, `fuzz_fetch_machine` | P1 (P2–P4 for the provider specs, which can land last) | L (6) | bryce-lelbach (lead), ben-deane, neckbeard-nate |
| P7 | providers: `Exchange`, `QnamTransport` (redirects, limits, body cap), `Network` (gates, key, cache, destruction order), `Job`, the providers, catalogs, snapshot resource, update check, `describe`, `FakeTransport`, cassettes, local server, `drive` | P2–P6 | L (7) | bryce-lelbach (lead), ben-deane, neckbeard-nate |
| P8 | fixtures and live job: missing fixtures (curl first, cassettes after P7), compression ratios, gzip member counts, NDBC Oct–Dec codes, `live-api.yml` with the issue step | P2–P4; P7 | M (2.5) | neckbeard-nate, jason-turner |
| P9a | CLI skeleton: `run`, `to_command`, rendering, outcome join, exit codes, output commit and EPIPE, SIGINT, `convert`, `hwm-stats` | H0, P5a | M (3.5) | uncle-bob-martin, neckbeard-nate |
| P9b | CLI `fetch`, `stations`, rule table, `offered` selections, memory bound, `--partial`, `--dry-run`, `--record-cassette`, key file | P7, P9a | M (3.5) | uncle-bob-martin, neckbeard-nate |
| P10 | station snapshots committed | P9b | S (1) | neckbeard-nate |
| H1–H7 | as HE §10 | as HE | ≈ 26 | as HE §10 |
| H9 | tides provider (§6.10) | H4, H5, P7 | M (2.5) | bryce-lelbach (lead), ben-deane, neckbeard-nate |
| H8 | CLI `harmonics …` (after P9a) and `tides predict` (after H9) | H4, H5, P9a, H9 | M (2.5) | uncle-bob-martin, neckbeard-nate |
| W-K | Phase 4: QtKeychain overlay port, keychain/plain-text storage (D35); a port build spike may run in Phase 3 | P7 | S (1.5) | jason-turner, neckbeard-nate |

Waves (∥ parallel): 1 H0; 2 P1 ∥ H1 ∥ P8 (curl fixtures); 3 P2 ∥ P3 ∥ P4 ∥ P5a ∥ P5b ∥ P6
∥ H2; 4 P7 ∥ P9a ∥ H3 ∥ H5; 5 P9b ∥ H4 ∥ H6 ∥ H7 ∥ P8 (cassettes, live job); 6 H9 ∥ P10;
7 H8, then the phase-end reviews.

Totals: provider track (H0, P1–P10) about 44 days of work, tide track (H1–H9) about 31.
Critical paths: H0 → P1 → P6 → P7 → P9b → P10, about 24 working days; H0 → H1 → H2 → H3
→ H4 → H9 → H8, about 21 (H9 also waits for P7, which ends near day 19). Phase 3 ends
when both are done.

---

## 13. Owner decisions, open items and sync items

### 13.1 Recorded (plan §6)

- **D32** NGVD29 at CO-OPS stations dropped; only datums CO-OPS serves are offered.
- **D33** USGS map: instantaneous 00060, 00065, 62620 or 62619, ended series included.
- **D34** USGS daily values: `DailySeries` in memory; SN `time_basis` + `cell_methods`
  (minor bump); CSV writes a `date` column (`yyyy-mm-dd`) in place of `time_utc`;
  IMEDS refuses.
- **D35** USGS key: QtKeychain where a backend exists, else plain text in app-local
  data, as v4.
- **OI 12** stays open for Phase 6 (§3.6).

### 13.2 New questions

None open. The daily CSV column question was decided on 2026-10-09: a daily file names
its column `date`, so a reader cannot take a local day for a UTC instant (D34).

### 13.3 Sync items for other documents (applied in H0)

| Document | Change |
|---|---|
| `docs/harmonics-engine.md` §3, §4, §6.1, §6.5, §9.2 | `std::stop_token` → `core::StopToken`, moved from io in H0 (CD §1.1 rules `std::stop_token` out); `ProviderError` → `fetch::Failure`; `gunzip` is public `io/gzip.hpp`; the tides provider uses `QtConcurrent::run(QPromise&)` (§6.10) |
| `docs/harmonics-engine.md` §6.3 | the JSON depth pre-scan of §4.2 before nlohmann (its `max_depth = 16` is otherwise only checked in the callback, after the lexer descended) |
| `docs/harmonics-engine.md` §7.8, §10 | `tides predict` goes through the tides provider: H8's `tides predict` after H9 |
| `docs/core-design.md` §9.3 | `TimeRange::split` and chunk merging, the USGS daily cadence: resolved here (`split`, `Part`, `DailySeries`); `Temperature` stays deferred; OI 12 deferred to Phase 6; `GaugeStation::datums` removed (§3.3) |
| `docs/station-netcdf.md` | 1.1: `time_basis`, `cell_methods` for daily series (P5b) |
| `docs/rearchitecture-plan.md` §2.1 | the `fetch` layer in the layout and the dependency line |
| `CLAUDE.md` | the layer list in "Build (v5)" gains `fetch` |

---

## Appendix A. Review resolution (revision 2)

**F** fixed as asked; **P** partly, with the reason; **X** not adopted, with the reason.

### bryce-lelbach

| Finding | Status | Where |
|---|---|---|
| B1 move-only exchange handles owned by the job, abort on destruction, fresh id per attempt, completions queued and never after abort, steps from a queue, `~Network` order | F | §6.1, §6.3, §5.5 |
| `Merge` by value, `StopToken`, `on_merged` | F | §5.4, §5.5, §6.3 |
| Fetch deadline in the machine, `now` with every event, exchange clocks from `start`, `--timeout` → 1 | F (reconciled with N S14: no per-exchange deadline, a throughput floor instead) | §5.3, §9.2 |
| One continuation per future; cache filled in the job; ready futures for hits; no shared futures; cache thread, immutability, bound | F | §6.4, §6.5 |
| Stale-result contract: generations; destroyed view model cancels | F | §6.4 |
| CLI SIGINT: `whenAll` over original futures, flag decides 130, serial pool drained, second SIGINT `_exit(130)` | F | §9.2 |
| Lenient: `ParseError` → `UnitFailed`; background refresh lenient | F | §5.6, §7 |
| `drive()` hygiene: cancel on timeout, flush `DeferredDelete`, zero live jobs, no timing assertions | F | §10.2 |
| Nits: stop token with `isFinished`; `promise.start()`; extended ban list; HTTP/2 wording; `CoolDown` step and HTTP-date relative to `Date`; first-failure nondeterminism; one `Network` per process; background priority; Windows handler wording; tides via `QtConcurrent::run(QPromise&)` | F | §6.3, §2.2, §1.4, §5.3, §5.6, §6.1, §9.2, §6.10 |
| Question: does `cancelChain` reach a `QPromise` head on 6.11? | F: **yes**, measured; rule "cancel with `cancelChain()` on the held future", pinned by a test | §1.5, §6.4, §10.2 |

### ben-deane

| Finding | Status | Where |
|---|---|---|
| B1 a plan is a bind | F: stages composed as a bind (USGS), units chained by `next(unit, outcome)` within a stage; reasons given | §5.2 |
| B2 resolution always runs, `ResolvedSeries`, statistic in the daily by-parameter selector | F | §3.4, §5.2 |
| B3 `DailySeries`; `UsgsProvider::Value = variant<TimeSeries, DailySeries>`; SN overload; no IMEDS overload | F | §3.4, §4.7, §6.1 |
| SF4 `Part` monoid, left-biased union in plan order; `NoData` only at `Finish` without failures; all-failed lenient is a failure | F | §5.4, §5.6 |
| SF5 unit rule and metas-agree law | F | §5.4, §10.1 |
| SF6 no sentinel merge unit; `on_merged` | F | §5.4 |
| SF7 totality, fresh ids, factory, one `Event` variant | F (the `Event` variant adopted) | §5.5 |
| SF8 `UnitLabel`, `Failure{Error, optional<UnitLabel>}`, `FetchWarning` variant without text | F | §5.1 |
| SF9 gate keyed by origin; `constexpr policy(Origin, KeyPresence)` | F | §5.3 |
| SF10 `Network` owns the key, USGS origin only | F | §6.1, §6.2 |
| SF11 `RunOutcome` join, exhaustive `exit_code`, order reconciled with N (130 > 1 > 4 > 3 > 0; usage 2 first, before any network), mixed data/no-data is 0 | F | §9.3 |
| SF12 rule table (name, interval) → `Product` + `WindComponent`; one spelling for observed; `--datum` default stated (required); shared `offered`; bbox behavior | F | §9.1, §3.3 |
| SF13 half-open `DayRange`, `covering` refusing non-midnight | F | §3.1 |
| SF14 optional table in capabilities; `great_lakes` replaced by the facts it gated | F | §3.3 |
| Nits | F | throughout (designated aggregates, `Origin` naming, text-free warnings) |

### neckbeard-nate

| Finding | Status | Where |
|---|---|---|
| B1 USGS page bounds, path-pinned `Verbatim`, empty+next, repeated next, non-advancing time, fuzz invariant | F | §4.1, §5.2, §10.4 |
| B2 user-verified redirects, https same origin, max 3, `ignoreSslErrors` gate; do raw headers follow redirects? | F: **yes, to any origin** (probe) | §1.5, §6.2, §2.2 |
| S3 JSON depth pre-scan; also the harmonics reader | F; harmonics as a sync item | §4.2, §13.3 |
| S4 decompression threshold = body cap, 299 → `body_too_large`, decompressed bytes counted, P8 measures ratios | F (NDBC gz ratios 4.5–6.2 already measured) | §6.2, §1.3, P8 |
| S5 gzip by magic; P8 counts members | F (3 files: one member each) | §4.2, P8 |
| S6 `station_or_system`, retried once | F | §4.3, §5.3 |
| S7 refresh: datums error a warning, < 50 % refused, future-dated cache invalid | F | §7, §4.6 |
| S8 minute/second alignment; targets and zero duplicates asserted | F | §3.1, §4.1, §10.1 |
| S9 real B23 test | F | §10.2 |
| S10 L11 message-handler test | F | §8, §11.2 |
| S11 strict output uncommitted; stdout buffered; EPIPE clean; exit order | F | §9.2, §9.3 |
| S12 station concurrency, memory bound or streaming | F (4 stations; `--max-samples`; text formats stream) | §9.2 |
| S13 live job: one deduplicated issue, one retry, unreachable → skip, shape change → fail | F | §10.5 |
| S14 throughput rule instead of the 120 s deadline | F (reconciled with Br's fetch deadline) | §5.3 |
| Nits: accept-then-cool-down; `unexpected_content` with `Content-Type`; UTF-8-safe truncation; catalog bad-id policy; NDBC case per target; key file `fstat`, 4 KiB, precedence; no keyless retry; one `AppLocalDataLocation`; plain text in Phase 4 | F | §5.3, §4.2, §4.3, §4.6, §4.1, §8, §7, §6.8 |
| PA corrections (predictions span, NDBC monthly pattern, CSV paging, other §1 facts) | F: applied to `docs/provider-apis.md` with "Corrected 2026-10-09" marks | PA §1.1, §1.2, §1.5, §1.6, §2.1, §2.3, §3.1, §8 |

### Owner (2026-10-09)

| Question | Answer | Where |
|---|---|---|
| Q1 NGVD29 at CO-OPS | dropped (D32) | §3.2, §3.6 |
| Q2 USGS map filter | 00060, 00065, 62620, 62619, ended included (D33) | §7 |
| Q3 daily values | (a), with `DailySeries` (D34) | §3.4, §4.7 |
| Q4 key without a keychain | plain text in app-local data, as v4 (D35); session-only withdrawn | §8 |
| Q5 OI 12 | not asked; stays open for Phase 6 | §3.6 |
