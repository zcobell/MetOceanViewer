# Development environment

The v5 build needs GCC 14 or Clang 19+ (`<format>`, `<expected>`), which the
Debian 12 host does not have, so builds run in an Ubuntu 24.04 image that
mirrors the `ubuntu-24.04` CI jobs.

| Tool | Version |
|---|---|
| GCC (default `cc`/`c++`) | `GCC_VERSION` in `tools/versions.env` |
| clang, clang-tidy, clang-format, llvm-cov, libFuzzer/sanitizer runtimes | `LLVM_VERSION` |
| clang + libc++ (`clang++-libcxx`, the `dev-libcxx` preset) | `LIBCXX_VERSION` |
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

In a git worktree (`.git` is a file), `run.sh` also mounts the main
repository's git directory read-only and the worktree's own gitdir read-write,
at their host paths, so `git` and `pre-commit` work in the container.

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

## Locales

The image generates `de_DE.UTF-8`. The locale-independence tests of `mov::io`
(`parse_double` under a comma-decimal locale) assert that the locale exists
and fail, rather than skip, when it does not: every preset sets
`MOV_REQUIRE_LOCALES=ON`, and CI generates the locale on the Linux jobs
(`.github/actions/setup-locales`). On a native machine without it, either
`locale-gen de_DE.UTF-8` (Debian/Ubuntu) or configure with
`-DMOV_REQUIRE_LOCALES=OFF` to turn the failure into a skip.

## libc++ build (macOS stand-in)

`cmake --workflow --preset dev-libcxx` builds and tests with Clang and libc++
(`LIBCXX_VERSION`, 18) instead of libstdc++: the nearest thing on Linux to
Apple Clang. It reproduces the standard-library gaps of the macOS job
(`variant<..., std::string>` is not constexpr, no `std::stop_token` without
`-fexperimental-library`, no floating `from_chars`, no `ranges::fold_left`),
and CI runs it as the `libcxx` job. C++ vcpkg ports are built with libc++ too
(overlay triplet `x64-linux-libcxx`), because libstdc++ and libc++ `std::string`
are different types. Findings and the per-version feature table are in
`docs/core-design.md` §1.

## Windows/MSVC cross-check (clang-cl + xwin)

A local stand-in for the `windows-2022` job, so MSVC STL, UCRT and `/W4` problems
show up before a push:

```sh
MOV_DEV_IMAGE=msvc tools/dev/run.sh cmake --workflow --preset dev-msvc-xwin
```

`MOV_DEV_IMAGE=msvc` selects `tools/dev/msvc/Dockerfile` (image
`metoceanviewer-msvc:<hash>`, about 2.3 GB, built on first use in a few minutes:
it downloads about 85 MiB from Microsoft). It holds `clang-cl`, `lld-link`,
`llvm-rc` and `llvm-mt` from `LLVM_VERSION`; the MSVC CRT and STL
(`XWIN_CRT_VERSION`) and the Windows SDK (`XWIN_SDK_VERSION`), x86-64 and release
CRT only, laid out by [xwin](https://github.com/Jake-Shadle/xwin)
(`XWIN_VERSION`, SHA-256-checked) under `/opt/xwin`; vcpkg; and Wine to run the
result. Building the image downloads Microsoft's CRT and SDK with
`--accept-license`, which the repository owner has accepted. The pinned
versions are those of the `windows-2022` runner image on 2026-10-07 (VS 2022
17.14, MSVC toolset 14.44, Windows SDK 10.0.26100 among its SDKs). xwin reads
Microsoft's live VS 17 channel manifest, so if Microsoft retires 14.44 the image
build fails until `XWIN_CRT_VERSION` moves.

**Level reached: full build and test.** `dev-msvc-xwin` is Qt-free (`src/core`,
`src/io`, `tests/`, no `MOV_ENABLE_QT`). Nothing is compile-only:

- `cmake/toolchains/clang-cl-xwin.cmake` selects `clang-cl --target=x86_64-pc-windows-msvc`
  with the MSVC and SDK headers as system headers (`/imsvc`), `/MD`, `lld-link` and `llvm-rc`.
  The project's own MSVC branch applies unchanged: `/W4 /WX /permissive- /Zc:preprocessor`,
  the `/w14xxx` list, `_CRT_SECURE_NO_WARNINGS`, `_MSVC_STL_HARDENING=1`.
  `-Wno-unused-command-line-argument` keeps `/WX` from failing on switches that clang-cl
  ignores (`/Zc:preprocessor`).
- vcpkg builds catch2, netcdf-c, HDF5, PROJ (and zlib, libaec, sqlite3, tinyxml2)
  for Windows with the same toolchain: overlay triplet `x64-windows-xwin`
  (static libraries, `/MD`, release only: xwin has no debug CRT, so the preset
  is RelWithDebInfo like `ci-windows`). PROJ's SQLite tool is built for the host.
  Two workarounds are in the toolchain: an empty `InstallRequiredSystemLibraries`
  stand-in (CMake's looks for a Visual Studio and fails on a Linux host, in HDF5),
  and `CMAKE_TRY_COMPILE_CONFIGURATION=Release` (ports with an old
  `cmake_minimum_required` would link the debug CRT). The triplet must not set
  `VCPKG_CMAKE_SYSTEM_NAME`: that is how vcpkg recognises a Windows target.
- `tools/dev/msvc/wine-run` runs the test executables under Wine 9 (it rewrites
  absolute Unix paths to `Z:\`, which Catch2 would read as options). CMake's
  `CMAKE_CROSSCOMPILING_EMULATOR` makes `ctest` and Catch2's test discovery use it.

Cost on a 21-core host: image build about 10 minutes once; a cold configure builds
the ports in 3 to 8 minutes (then cached by vcpkg); building 139 targets about
1.5 minutes without ccache; `ctest` (1028 tests, 8 jobs, one Wine process per test
case) about 50 seconds. With warm vcpkg and ccache caches the whole workflow from an
empty build tree takes about a minute. The vcpkg binary cache key includes the
toolchain file, so editing it rebuilds the ports.

**What it catches.** Anything the MSVC STL/UCRT headers do differently from
libstdc++/libc++: missing or changed headers and overloads, `std::format` and
`<chrono>` behaviour, the CRT's `C4996` deprecations (`getenv`, `fopen`, ...;
clang-cl reports them as `-Wdeprecated-declarations`), POSIX-only code (`<unistd.h>`,
`strcasecmp`, `ssize_t`), Windows-only `#if` branches, link errors against the
real import libraries, `/W4`-class warnings that Clang's `-Wall -Wextra` also
raises (`CompilerWarnings.cmake` adds `-Wshadow -Wconversion -Wunreachable-code` for
clang-cl for the C4456-C4459, C4244/C4267 and C4702 families), and run-time
differences of the Windows CRT and file system semantics (Wine implements them
closely but not exactly). It found one test that would fail on Windows:
`the new file keeps the permission bits of the one it replaces` expects 0660 where
Windows reports 0777 (now skipped on `_WIN32`).

**What only real MSVC catches.** This is Clang's front end, not `cl.exe`'s.
- MSVC-only front-end bugs and limits: for example, `AttTarget`'s `consteval`
  constructor calling `NcNameRef`'s `consteval` constructor (the error fixed in
  `99518daf`) compiles with clang-cl; checked against the parent's headers.
- `cl`'s `C47xx` warnings that have no Clang counterpart: C4702 after a fully covered
  enum `switch`, C4127, most of the `/w14xxx` list (clang-cl ignores them).
  Clang warnings that `cl` lacks can fail this build instead.
- MSVC's `constexpr` evaluator (step and nesting limits), template instantiation and
  `requires` details, ABI of `long double`, and `/analyze`.
- Qt, MapLibre and the packaging steps (no Qt in this image).

Known Wine 9 gaps (`wine-run` turns them into ctest "skipped", with Wine's message in
the test output): `ucrtbase.dll.feholdexcept` (HDF5 calls it, so 47 netCDF tests
cannot run) and `msvcp140_2.dll.__std_smf_hypot3` (`std::hypot` of three arguments,
2 tests). `layering_guard_rejects_violations` is excluded: it configures nested
projects with `clang-cl` and no cross toolchain. The full `ctest` run is therefore
"1028 passed, 65 skipped" here (49 for the Wine gaps, 16 that skip themselves, for
example on a directory Wine does not protect), not a statement about those tests on
Windows.

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
(`MetOceanViewer.app` on macOS). The build copies the MapLibre geoservices
plugin next to it (`geoservices/`) and `proj.db` to where a package keeps it
(`../share/metoceanviewer/proj`, `Contents/Resources/proj` in the bundle), so
it runs from the build tree without environment variables.
`metoceanviewer --self-test` checks that without a window (ctest:
`metoceanviewer_self_test`). Packages: `docs/packaging.md`.

- Headless host: the GUI test above is the way to look at it.
  `tools/dev/run.sh xvfb-run -a build/dev-qt/src/ui/metoceanviewer` also runs
  it, invisibly.
- Linux desktop, from the container (X11; allow it with `xhost +local:` first):
  `MOV_DOCKER_ARGS="--env DISPLAY --volume /tmp/.X11-unix:/tmp/.X11-unix" tools/dev/run.sh build/dev-qt/src/ui/metoceanviewer`
- macOS 14+ and Windows: build natively (below) with Qt, as CI does:
  `cmake --preset ci-macos -DMOV_ENABLE_QT=ON "-DCMAKE_PREFIX_PATH=$QT_ROOT_DIR"`
  then `cmake --build --preset ci-macos` (`ci-windows` likewise, from a Visual
  Studio developer prompt). Run `open build/ci-macos/src/ui/MetOceanViewer.app`,
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
  Quick's default there and MapLibre's only Apple backend. On Linux it links
  `libGL.so.1` (`OpenGL_GL_PREFERENCE=LEGACY`), not GLVND's `libOpenGL.so.0`,
  which many systems lack and the AppImage cannot bundle.
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
- macOS: upstream builds the three libraries as frameworks
  (`lib/QMapLibre.framework`, ...). The port turns vcpkg's Mach-O fix-up off
  (`VCPKG_FIXUP_MACHO_RPATH`), which would otherwise rename their install
  names to the unloadable `@rpath/QMapLibre`; build-tree executables find the
  frameworks through the rpaths CMake gives them (vcpkg `lib`, Qt `lib`), and
  the packaging step must copy the frameworks into the bundle.
- Notices: the port's copyright file carries the bindings' licenses, the
  core's `LICENSE.md` and `LICENSES.core.md` (its vendored libraries) and the
  ICU, nunicode and MapLibre Tile licenses; the packages ship it
  (`share/doc/metoceanviewer/third-party/maplibre-native-qt.txt`).
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

Packaging (`docs/packaging.md`) installs `MOV_MAPLIBRE_RUNTIME_LIBRARIES` and
the plugin itself (`cmake/Packaging.cmake`) and has the deploy tools scan
them; on Windows it keeps Qt's `opengl32sw.dll` (never pass `--no-opengl-sw`
to windeployqt): Mesa's software OpenGL, the fallback where the GPU driver
lacks OpenGL 2+, which MapLibre needs on Windows.

Planned, not done:

- A source tarball. The port depends on GitHub and about 30 shallow fetches
  staying available. The plan: a GitHub release asset holding the pinned
  maplibre-native-qt tree with its submodules, which the port downloads with
  `vcpkg_download_distfile` and a SHA-512 (so vcpkg's asset cache works too).
  Publishing release assets is an owner action, so this waits for the owner.

## AppImage build image

`MOV_DEV_IMAGE=appimage tools/dev/run.sh <cmd>` runs `<cmd>` in a second
image, `tools/dev/appimage/Dockerfile`: Ubuntu 22.04 (`APPIMAGE_UBUNTU_IMAGE`)
with a GCC 14 release built from source in the image (`APPIMAGE_GCC_VERSION`,
`docs/packaging.md`), the same CMake,
vcpkg and Qt, so the AppImage keeps the glibc 2.35 floor of plan §6.24. It
shares the caches and `~/Qt` with the dev image; its vcpkg binaries are its
own (another compiler). Use it only for `--preset package-linux`
(`docs/packaging.md`).

## Native build (without Docker)

Any machine with a C++23 toolchain works the same way the CI jobs do:

1. A compiler with `<format>` and `<expected>`: GCC 14+, Clang 19+ with
   libstdc++ 14 or libc++ 18+, Xcode 16+, or Visual Studio 2022 17.10+. Nothing may
   rely on more than libc++ 18 provides (`docs/core-design.md` §1).
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
  `cmake-<version>-SHA-256.txt`), `UBUNTU_IMAGE`'s and
  `APPIMAGE_UBUNTU_IMAGE`'s digests when moving a base image, and each
  `LINUXDEPLOY_*_SHA256` with its tag.
- The Qt system packages of `tools/dev/appimage/Dockerfile` (Ubuntu 22.04
  names) follow the dev image's.
- The committed icons (`packaging/icons/`): `tools/make_icons.py`
  after editing their SVG sources (the pre-commit hook `icons` checks), and
  Qt's license texts (`packaging/licenses/qt/`): `tools/fetch_qt_licenses.py`
  after a Qt bump.
- The packaging values listed in `docs/packaging.md`, "Keep in sync".
- `XWIN_SHA256` whenever `XWIN_VERSION` changes (the release's
  `xwin-<version>-x86_64-unknown-linux-musl.tar.gz`), and `XWIN_CRT_VERSION` /
  `XWIN_SDK_VERSION` when the `windows-2022` image moves to another Visual Studio or SDK
  (actions/runner-images `Windows2022-Readme.md`; `xwin list` shows what Microsoft offers).
- `cmake_minimum_required` in `CMakeLists.txt` and `cmakeMinimumRequired` in
  `CMakePresets.json` follow `CMAKE_VERSION` (the only version CI exercises).
- `actions/*` versions in the workflow and composite actions (Dependabot).
- `mov_qt_version` in `cmake/vcpkg-ports/maplibre-native-qt/portfile.cmake`
  equals `QT_VERSION` (the port and the configure both check it).
- The Qt system packages in `.github/actions/setup-qt` mirror the Dockerfile's.
- The Qt version in prose: CLAUDE.md (the `dev-qt` line) and
  `docs/rearchitecture-plan.md` §3; on a minor bump also
  `MOV_QT_MINIMUM_VERSION` in `CMakeLists.txt`.
- The aqtinstall git pin in `requirements.txt` equals `aqtsource` in
  `.github/actions/setup-qt/action.yml`. Return both to a released version once
  aqtinstall > 3.3.0 ships Qt 6.11 Windows support (PR #1000).
