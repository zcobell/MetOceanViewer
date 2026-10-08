# MetOceanViewer

This repository is being re-architected from the legacy Qt 5 / qmake app (v4.x)
into v5: Qt 6 + QML + MapLibre Native Qt, with a Qt-free, strongly typed C++23 core.

**Before doing any work, read `docs/rearchitecture-plan.md` in full.** It holds the
review findings (with `file:line` references to commit `e5a4e0af`), the target
architecture, the phase order and the open decisions.

## Ground rules

- Follow the phase order in the plan, §5. Do not start GUI work before the core,
  I/O and providers are built and tested. Each phase ends green in CI.
- The open decisions in §6 belong to the repo owner. Ask; do not assume.
- Do not port legacy Qt 5 widget code (`MetOceanViewer/src`, `ui/`, `qml/`) to Qt 6.
  Use it only as a reference for behavior. Be aware that it contains known bugs
  (plan §1.2), so do not copy them.
- Legacy code stays untouched and buildable until v5 reaches feature parity, unless
  the owner approves v4 hotfixes.
- Follow the engineering rules in plan §7:
  - No Qt in `core`/`io`.
  - No sentinels, raw owning pointers or blocking GUI-thread I/O.
  - Every parser and every bug fix gets a test.
- Verify external API details (especially the USGS Water Data API migration) against
  current official documentation before implementing a provider.

## Scratch space

The host's root filesystem is small. Put temporary files, probes and scratch
builds under `/home` (the repo's `build/` or `~/.cache/metoceanviewer-dev/tmp`),
never in `/tmp` on the host. Inside the dev container `/tmp` is fine: it lives
on the Docker disk. Delete scratch output when done.

## House rules for reviewers

The reviewer agents in `.claude/agents/` were written for another codebase
(Cocoa: Kokkos/nvcc, GPU kernels, C++20, Sphinx). In this repository, the rules
below **replace** every Cocoa-specific item in their "truce with reality" sections.
Those items include device-code bans, nvcc lambda/ranges limits, the C++20 cap,
Kokkos mappings, float-storage/double-compute rules and "do not prescribe strong types".

- There is no device code. All code is host C++23 (GCC 14, Apple Clang, MSVC).
  `std::expected`, `std::variant`, `std::optional` (monadic), `std::ranges` and views,
  `std::format` and concepts are all available and preferred.
- Strong types are policy here (plan §2.2): `StationId<Provider>`, `TimeRange::make`,
  unit-carrying quantities and per-domain error variants. Push for them where a
  misuse is possible. Restraint still applies to one-implementation seams.
- No in-band sentinels in domain types. Legacy sentinels (`-99999`, `NC_FILL_*`, `MM`)
  are converted to `optional`/masks at the parser boundary.
- Multi-field aggregates use designated initializers. Logical operators use the
  alternative tokens (`and`, `or`, `not`), as enforced by `.clang-tidy`.
- Concurrency lives in `providers`/`app` only: `QFuture`/`QtConcurrent`, with
  cancellation and timeouts. There are no nested event loops and no `processEvents`.
  `core`/`io` are single-threaded pure functions over values.
- Errors are values (`expected`) everywhere below the UI. They are formatted for
  humans only at the UI or CLI edge.
- Documentation is Markdown in `docs/` plus Doxygen-style `///` comments on public
  headers. There is no Sphinx.

## Review roster

Each phase's work is reviewed before it is committed. Every reviewer reports
findings only, except documentation-reviewer and prose-editor, which also edit docs.
The main session triages the findings, and the implementing agent fixes them.

| Phase / area | Reviewers |
|---|---|
| 1 Build, CI, tooling, skeleton app, packaging | jason-turner (warnings, sanitizers, CMake), neckbeard-nate (scripts, CI failure modes), architecture-clarity-reviewer (layout, dependency direction) |
| 2 Core domain types | ben-deane (lead), sean-parent (value semantics, regular types), jason-turner (constexpr, hidden costs) |
| 2 I/O, netCDF wrapper, parsers | neckbeard-nate (lead: UB, error paths, buffers, the §1.2 bug classes), sean-parent, conor-hoekstra (parsing pipelines, HWM statistics) |
| 3 Providers, async | bryce-lelbach (lead: cancellation, forward progress, data races, QFuture chains), ben-deane (request/product variants, error types), neckbeard-nate |
| 3 CLI and tools | uncle-bob-martin, neckbeard-nate |
| 4–6 App, view-models, QML | architecture-clarity-reviewer (lead), uncle-bob-martin, bryce-lelbach (GUI thread vs. off-thread work) |
| 5 Chart decimation, export | conor-hoekstra, neckbeard-nate (performance claims must be measured) |
| 6 Sessions, legacy importer | neckbeard-nate, ben-deane |
| Every phase end | architecture-clarity-reviewer on the whole phase diff, then documentation-reviewer and prose-editor on `docs/` and public headers |

## Build (v5)

All builds run in the dev container (host GCC 12 lacks `<format>`); see
`tools/dev/README.md`, which also describes the native (non-Docker) route.
`tools/dev/run.sh <cmd>` runs `<cmd>` in it, building the image when its
inputs change. CMake presets plus a vcpkg manifest (`vcpkg.json`, baseline
pinned in `vcpkg-configuration.json`); each preset builds into
`build/<preset>/`. Toolchain versions live in `tools/versions.env`.

```sh
tools/dev/run.sh cmake --workflow --preset dev        # GCC 14 debug: configure, build, test
tools/dev/run.sh cmake --workflow --preset dev-clang  # same with Clang 20
tools/dev/run.sh cmake --workflow --preset dev-qt     # dev + Qt app + GUI test (installs Qt 6.11.3 into
                                                      # ~/Qt on first use; vcpkg builds MapLibre once, then caches it)
tools/dev/run.sh cmake --workflow --preset asan       # ASan + UBSan
tools/dev/run.sh cmake --workflow --preset fuzz       # Clang libFuzzer targets, MOV_FUZZ_SECONDS each
tools/dev/run.sh cmake --workflow --preset release    # as shipped (no stdlib hardening)
tools/dev/run.sh cmake --workflow --preset coverage   # report in build/coverage/coverage-report/;
                                                      # fails < 80% lines overall or < 90% in src/core, src/io
tools/dev/run.sh ctest --preset dev -R <regex>        # rerun selected tests

# The app (Qt layers). This host is headless: the GUI test runs it under Xvfb
# and writes build/dev-qt/tests/ui/screenshots/main-window.png.
tools/dev/run.sh ctest --preset dev-qt -L 'qt|gui'   # qt: offscreen, no GL; gui: Xvfb + Mesa
tools/dev/run.sh cmake --build --preset dev-qt --target all_qmllint   # QML type check (CI runs it)
# On a machine with a display: build natively and run build/<preset>/src/ui/metoceanviewer
# (macOS: metoceanviewer.app); see "Running the app" in tools/dev/README.md.

# clang-tidy gate (CI runs the same; the tidy preset includes the Qt layers):
tools/dev/run.sh cmake --preset tidy
tools/dev/run.sh python3 tools/clang_tidy_gate.py -p build/tidy

# Station netCDF format compliance (the format-compliance CI job, locally): IOOS
# compliance-checker, cfchecks, xarray and ncdump in their own pinned image.
tools/check_station_netcdf.sh

# pre-commit (formatting, codespell, license header, lizard). It checks only
# files git tracks and reports fixer edits only on tracked files: stage first.
tools/dev/run.sh pre-commit run --all-files
```

- Layers: `cmake/Layering.cmake` declares the order (core, io, providers, app,
  ui; cli beside them) once. Add a library layer with
  `mov_add_module(<layer> SOURCES ... PUBLIC_LINK ... PRIVATE_LINK ...)` in
  `src/<layer>/CMakeLists.txt`; headers go in `src/<layer>/include/mov/<layer>/`.
  The configure fails if a layer links upward or a Qt-free layer (core, io)
  links Qt; the `qt_free_sources` test fails on a Qt `#include` there.
- Tests: one Catch2 executable per module under `tests/<module>/`, added with
  `mov_add_test(<name> SOURCES ... CONSTEXPR_SOURCES ... LIBRARIES ...)`;
  `CONSTEXPR_SOURCES` hold `STATIC_REQUIRE` tests (build-time, plus a
  `_relaxed_constexpr` runtime twin). Fixtures: `tests/fixtures/<module>/`, via
  `mov::test::fixture("<module>/...")`. Parsers get a libFuzzer target with
  `mov_add_fuzz_test(<name> SOURCES ... LIBRARIES ... CORPUS <module>/<dir>)`.
- `MOV_ENABLE_QT` (the `-qt` presets and `tidy`) finds Qt, selects the vcpkg
  feature `gui` (MapLibre Native Qt, overlay port `cmake/vcpkg-ports/`) and adds
  `src/ui` (QML module, `metoceanviewer` executable) and `tests/ui`. Qt tests use
  `mov_add_test(<name> QT ...)` (label `qt`, offscreen, every CI OS) or
  `mov_add_test(<name> GUI ...)` (label `gui`, renders; `xvfb-run` on Linux).
- New files need the two-line `SPDX-License-Identifier: GPL-3.0-or-later` /
  `Copyright (c) <year> Zach Cobell` header (`tools/check_license_header.py`).
