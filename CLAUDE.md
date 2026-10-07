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

## Build (v5, once Phase 1 lands)

CMake presets plus a vcpkg manifest; Qt 6 installed separately (e.g. `aqtinstall`).
Update this section with exact commands when the build exists.
