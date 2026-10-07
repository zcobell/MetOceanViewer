# Development environment

The v5 build needs GCC 14 or Clang 19+ (`<format>`, `<expected>`), which the
Debian 12 host does not have, so builds run in an Ubuntu 24.04 image that
mirrors the `ubuntu-24.04` CI jobs.

| Tool | Version |
|---|---|
| GCC (default `cc`/`c++`) | `GCC_VERSION` in `tools/versions.env` |
| clang, clang-tidy, clang-format, llvm-cov, libFuzzer/sanitizer runtimes | `LLVM_VERSION` |
| CMake (upstream release, SHA-256 verified) | `CMAKE_VERSION` |
| vcpkg | the `baseline` in `vcpkg-configuration.json` |
| Ninja, ccache, lcov; gcovr, pre-commit, aqtinstall, lizard (`requirements.txt`) | |
| Xvfb, Mesa, xcb/xkb/EGL libraries | for Qt GUI tests |

## Usage

```sh
tools/dev/run.sh cmake --workflow --preset dev   # one command in the container
tools/dev/run.sh                                 # interactive shell
MOV_DEV_QT=1 tools/dev/run.sh                    # shell, with Qt installed first
```

`run.sh` runs the command as your uid/gid with the repository mounted at its
host path, so build trees and `compile_commands.json` paths match on both
sides. The compiler is chosen by the preset (`g++` or `clang++`), never by
`CC`/`CXX`.

The image is tagged `metoceanviewer-dev:<hash>`, a hash of the Dockerfile,
`requirements.txt`, `tools/versions.env` and the vcpkg baseline; `run.sh`
rebuilds it (about 3 minutes) whenever one of them changes, and also tags it
`latest`. Old tags can be removed with `docker image prune`.

Qt (`QT_VERSION`, `QT_MODULES`) is installed with aqtinstall on demand: when
`MOV_DEV_QT=1` or an argument names a Qt preset (one ending in `-qt`, such
as `dev-qt`, or `tidy`). A stamp file in the Qt prefix records version, arch
and modules; a change in
`tools/versions.env` reinstalls, and a lock serializes concurrent installs.
`run.sh` exports `QT_ROOT_DIR`, which the Qt presets put on `CMAKE_PREFIX_PATH`.

Persistent state stays on the host:

| Host path | Contents |
|---|---|
| `~/Qt/<version>/gcc_64` | Qt |
| `~/.cache/metoceanviewer-dev/ccache` | ccache |
| `~/.cache/metoceanviewer-dev/vcpkg` | vcpkg downloads and binary cache |
| `~/.cache/metoceanviewer-dev/home` | `$HOME` in the container (pre-commit and pip caches) |

Overrides: `MOV_QT_ROOT`, `MOV_DEV_CACHE`, and `MOV_DOCKER_ARGS` for extra
`docker run` arguments.

## Qt and GUI tests

Qt test executables (`mov_add_test(<name> QT|GUI ...)`) have their own
`main()` with one `QGuiApplication`, and run as one ctest test each:

- Label `qt` runs on Qt's `offscreen` platform and needs no display or GL, so
  CI runs it on all three OSes: `mov_ui_qt_tests` (app identity and version;
  Qt Location loads the "maplibre" plugin and creates its mapping engine) and
  `qml_format` (qmlformat-clean QML, checked here because the pre-commit job
  has no Qt).
- Label `gui` renders, so it runs under Xvfb rather than `offscreen`: MapLibre
  draws through OpenGL, which Mesa provides in software. On Linux the test's
  `TEST_LAUNCHER` is `xvfb-run` (24-bit screen), so a plain `ctest` works in
  the container and in CI; `RESOURCE_LOCK xvfb` serializes GUI tests.
  `ctest -LE gui` skips them where there is no display or GL (CI's macOS and
  Windows jobs, for now).

`mov_ui_tests` points the map at a local background-only style
(`tests/fixtures/ui/background-style.json`) and checks that a map-area pixel
takes its colour, with no network involved. A second case, tagged
`[screenshot]`, loads the default OpenFreeMap basemap, waits (at most 20 s)
until the frames stop changing and saves a screenshot for humans; it never
fails on the basemap.

```sh
tools/dev/run.sh ctest --preset dev-qt -L 'qt|gui' --verbose
# -> build/dev-qt/tests/ui/screenshots/main-window.png
# Offline, with no MapLibre tile cache (empty HOME): passes; the screenshot's map is empty.
MOV_DOCKER_ARGS="--network none --env HOME=/tmp/empty" tools/dev/run.sh ctest --preset dev-qt -L gui
```

## Running the app

The executable is `build/<preset>/src/ui/metoceanviewer`
(`metoceanviewer.app` on macOS). The build copies the MapLibre geoservices
plugin next to it (`geoservices/`), so it runs from the build tree without
environment variables.

- Headless host: the GUI test above is the way to look at it.
  `tools/dev/run.sh xvfb-run -a build/dev-qt/src/ui/metoceanviewer` also runs
  it, invisibly.
- Linux desktop, from the container (X11; allow it with `xhost +local:` first):
  `MOV_DOCKER_ARGS="--env DISPLAY --volume /tmp/.X11-unix:/tmp/.X11-unix" tools/dev/run.sh build/dev-qt/src/ui/metoceanviewer`
- macOS 14+ and Windows: build natively (below) with Qt, as CI does:
  `cmake --preset ci-macos -DMOV_ENABLE_QT=ON "-DCMAKE_PREFIX_PATH=$QT_ROOT_DIR"`
  then `cmake --build --preset ci-macos` (`ci-windows` likewise, from a Visual
  Studio developer prompt). Run `open build/ci-macos/src/ui/metoceanviewer.app`,
  or `build\ci-windows\src\ui\metoceanviewer.exe` with Qt's `bin` on `PATH`.
  On Windows use a release-type configuration (`ci-windows` is RelWithDebInfo):
  MapLibre is built in release only, and a Debug app would load Qt's debug DLLs
  beside the release ones MapLibre links.

## MapLibre Native Qt

The map is MapLibre Native Qt's Qt Location plugin (`Plugin { name:
"maplibre" }` in QML). No release supports Qt 6.11, so vcpkg builds it from a
pinned commit of `main` through the overlay port
`cmake/vcpkg-ports/maplibre-native-qt` (manifest feature `gui`, selected by
`MOV_ENABLE_QT`). vcpkg stays the only dependency mechanism, and its binary
cache (`~/.cache/metoceanviewer-dev/vcpkg/archives` here, `actions/cache` in
CI) means it is built once per port version, compiler and triplet:

| Configure of a fresh Qt build tree, 20-thread host | Time |
|---|---|
| Cold cache: fetch the sources and build MapLibre (two runs; the git fetch took 1–5 min of it) | 5–12 min |
| Cached: MapLibre restored from the binary cache (~2 s, a 6 MB archive) | 14 s |

On 3–4-core CI runners a cold build is estimated at 25–40 min (not yet
measured there); the build-test and clang-tidy jobs allow 120 min.

- Qt is external to vcpkg (`$QT_ROOT_DIR`) and the plugin uses Qt private API,
  so the port pins the Qt version (`mov_qt_version`) and refuses another one;
  `cmake/MapLibre.cmake` fails the configure if a cached build was made for a
  different Qt than the one found.
- Renderer: OpenGL on Linux and Windows; Metal on macOS, which is both Qt
  Quick's default there and MapLibre's only Apple backend.
  `mov::ui::select_graphics_api` makes Qt Quick use the same API.
- Release build only, shared libraries, bundled ICU on Linux, upstream
  `-Werror` off. Our warning flags never reach it.
- Sources: the GitHub archives lack submodules, so the port fetches with git
  (shallow, pinned commits): the bindings, the core submodule and the core
  submodules the portfile lists. This runs only on a binary-cache miss and is
  not in vcpkg's download cache.
- The port passes `Qt6_DIR` explicitly, reads back the Qt the build actually
  resolved (failing if it is not under `$QT_ROOT_DIR`) and records that
  version.
- Notices: the port's copyright file carries the bindings' licenses, the
  core's `LICENSE.md` and `LICENSES.core.md` (its vendored libraries) and the
  ICU, nunicode and MapLibre Tile licenses; packaging must ship it.
- To bump: change `mln_qt_ref`, compare the submodule lists with the new
  `vendor/maplibre-native/.gitmodules`, update `version-date` in the port's
  `vcpkg.json`, and rebuild.
- CI: on a cold cache the Linux build-test and clang-tidy jobs both build
  MapLibre, in parallel, under the same cache key; the first to save wins.
- From the build tree, `mov_stage_maplibre_runtime` (`cmake/MapLibre.cmake`)
  stages the plugin (and on Windows the QMapLibre DLLs, listed once in
  `MOV_MAPLIBRE_RUNTIME_LIBRARIES`) and documents the Linux RUNPATH
  invariant. MSVC Debug builds of the Qt layers are rejected at configure:
  MapLibre is release-only, and the app would load debug and release Qt side
  by side.

Planned, not done:

- Packaging (Windows): keep Qt's `opengl32sw.dll` (do not pass
  `--no-opengl-sw` to windeployqt). It is Mesa's software OpenGL, the fallback
  where the GPU driver lacks OpenGL 2+, and MapLibre renders through OpenGL on
  Windows.
- A source tarball. The port depends on GitHub and about 30 shallow fetches
  staying available. The plan: a GitHub release asset holding the pinned
  maplibre-native-qt tree with its submodules, which the port downloads with
  `vcpkg_download_distfile` and a SHA-512 (so vcpkg's asset cache works too).
  Publishing release assets is an owner action, so this waits for the owner.

## Native build (without Docker)

Any machine with a C++23 toolchain works the same way the CI jobs do:

1. A compiler with `<format>` and `<expected>`: GCC 14+, Clang 19+ with
   libstdc++ 14 or libc++ 18+, Xcode 16+, or Visual Studio 2022 17.10+.
2. CMake `CMAKE_VERSION` or newer and Ninja (e.g. `pip install cmake ninja`).
3. vcpkg checked out at the baseline commit and bootstrapped, with `VCPKG_ROOT`
   pointing at it:
   `git clone https://github.com/microsoft/vcpkg && git -C vcpkg checkout <baseline> && vcpkg/bootstrap-vcpkg.sh`.
4. For the Qt presets, Qt `QT_VERSION` (aqtinstall: `aqt install-qt <host> desktop <version> <arch> -m <QT_MODULES>`)
   and `QT_ROOT_DIR` set to its prefix.
5. `pip install -r tools/dev/requirements.txt` for gcovr, pre-commit and lizard.

Then `cmake --workflow --preset dev` (the Linux presets name `g++`/`clang++`;
on macOS and Windows use `ci-macos` / `ci-windows`, the latter from a Visual
Studio developer prompt).

## Versions and what is still synced by hand

`tools/versions.env` is read by `run.sh` (image build args) and by CI
(`.github/actions/load-versions`); the vcpkg baseline is read from
`vcpkg-configuration.json` by both; CI installs Python tools from
`requirements.txt`. These still have to be changed by hand:

- `rev:` of each hook in `.pre-commit-config.yaml` (`pre-commit autoupdate`).
  The `mirrors-clang-format` major must equal `LLVM_VERSION`, and the lizard
  pin in its hooks must equal `requirements.txt`.
- The `lukka/get-cmake` action pin in `.github/workflows/ci.yml` (Dependabot
  updates the SHA; the CMake version itself comes from `CMAKE_VERSION`).
- `CMAKE_SHA256` whenever `CMAKE_VERSION` changes (from Kitware's
  `cmake-<version>-SHA-256.txt`), and `UBUNTU_IMAGE`'s digest when moving the
  base image.
- `cmake_minimum_required` in `CMakeLists.txt` and `cmakeMinimumRequired` in
  `CMakePresets.json` follow `CMAKE_VERSION` (the only version CI exercises).
- `actions/*` versions in the workflow and composite actions (Dependabot).
- `mov_qt_version` in `cmake/vcpkg-ports/maplibre-native-qt/portfile.cmake`
  equals `QT_VERSION` (the port and the configure both check it).
- The Qt system packages in `.github/actions/setup-qt` mirror the Dockerfile's.
- The Qt version in prose: CLAUDE.md (the `dev-qt` line) and
  `docs/rearchitecture-plan.md` §3; on a minor bump also
  `MOV_QT_MINIMUM_VERSION` in `CMakeLists.txt`.
