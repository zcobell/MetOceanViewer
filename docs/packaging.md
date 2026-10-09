# Packaging

**Status (2026-10-07):** the Linux AppImage is built and verified end to end
(in the dev environment: package build, tests, smoke tests on bare Ubuntu
22.04, Ubuntu 24.04, Fedora). The macOS DMG and the Windows installer and zip
are implemented but **have never run**; the first run of the package workflow
is their test. Nothing is signed yet: no signing account exists (plan §6.1).

How MetOceanViewer v5 becomes the release packages of plan §4: a DMG for
macOS 14+ on Apple Silicon, an installer and a portable zip for Windows 10
22H2+/11 on x64, and an AppImage for x86-64 Linux with glibc 2.35 or newer
(plan §6.24).

| Platform | Package | Made by | Signing |
|---|---|---|---|
| macOS (arm64) | `MetOceanViewer-<v>-macos-arm64.dmg` | macdeployqt, CPack DragNDrop | Developer ID, hardened runtime; the DMG notarized and stapled |
| Windows (x64) | `MetOceanViewer-<v>-windows-x64.exe` (Inno Setup), `...-windows-x64-portable.zip` | windeployqt, CPack INNOSETUP and ZIP | Azure Artifact Signing (executables, DLLs, installer) |
| Linux (x86-64) | `MetOceanViewer-<v>-linux-x86_64.AppImage` | linuxdeploy and its Qt plugin, through CPack External | none |

Every signing step is in place and runs only when its secrets are set; without
them it is skipped with a notice and the packages are unsigned (see
[Signing](#signing) and [What the owner must do](#what-the-owner-must-do)).

## Pieces

| What | Where |
|---|---|
| Version, the single source | `project(VERSION ...)` in `CMakeLists.txt` |
| Identity (plan §6.19), install layout, install rules | `cmake/Packaging.cmake` |
| The same values for the C++ code | `cmake/app_identity.hpp.in` → `mov/ui/app_identity.hpp` |
| CPack configuration | `packaging/CMakeLists.txt`, `packaging/cpack-project-config.cmake` |
| Qt deployment, macOS and Windows | `packaging/deploy-qt.cmake` (run at install time) |
| AppImage | `packaging/linux/appimage.cmake`, `packaging/linux/apprun-hooks/` |
| Info.plist, entitlements, DMG layout, signing | `packaging/macos/` |
| VERSIONINFO, manifest, installer sections | `packaging/windows/` |
| Icons, DMG background | `packaging/icons/`, `packaging/macos/dmg-background.png`, made by `tools/make_icons.py` |
| Qt's license texts | `packaging/licenses/qt/`, fetched by `tools/fetch_qt_licenses.py` |
| Self-test, smoke test | `src/ui/self_test.cpp` (`--self-test`), `packaging/smoke-test.sh` |
| CI | `.github/workflows/package.yml` |
| AppImage build image | `tools/dev/appimage/Dockerfile` |

`project(VERSION)` feeds `mov::core::version()`, the Info.plist
(`CFBundleShortVersionString`, `CFBundleVersion`), the Windows `VERSIONINFO`
and manifest `assemblyIdentity`, the CPack package version and file names,
and the release job's check that the tag is `v<version>`.

## Building packages locally

Each platform has a workflow preset: configure a RelWithDebInfo build with
the tests, build, run `ctest -LE gui`, and run `cpack`. Packages land in
`build/package-<platform>/packages/`; the split debug symbols stay in the
build tree (`src/ui/metoceanviewer.debug`, `MetOceanViewer.app.dSYM`,
`metoceanviewer.pdb`) and the shipped binaries are stripped.

**Linux (AppImage).** Build it in the Ubuntu 22.04 image, not the dev image:
a binary needs at least the glibc it was linked against.

```sh
MOV_DEV_IMAGE=appimage tools/dev/run.sh cmake --workflow --preset package-linux
tools/dev/run.sh packaging/smoke-test.sh build/package-linux/packages/MetOceanViewer-5.0.0-linux-x86_64.AppImage
```

The first run builds the image (a few minutes) and, in it, the vcpkg ports
and MapLibre (another compiler, so the dev image's binary cache does not
apply). The CPack step downloads linuxdeploy, its Qt plugin and the AppImage
runtime to `build/package-linux/_packaging-tools/` and checks their SHA-256.

**macOS (DMG).** *Unverified.* On a Mac with Xcode 16.2+ and the native setup
of `tools/dev/README.md` (vcpkg, Qt in `QT_ROOT_DIR`):

```sh
cmake --workflow --preset package-macos
packaging/smoke-test.sh build/package-macos/packages/MetOceanViewer-5.0.0-macos-arm64.dmg
```

CPack runs the DMG layout script through Finder, so the first run asks for
permission to control Finder (System Settings, Privacy & Security,
Automation). Set the `APPLE_*` variables of [Local signing](#local-signing-macos)
to sign and notarize; without them the app is ad-hoc signed.

**Windows (installer and zip).** *Unverified.* From a Visual Studio 2022
developer prompt with Inno Setup 6 installed, and Git Bash for the smoke test:

```sh
cmake --workflow --preset package-windows
packaging/smoke-test.sh build/package-windows/packages/MetOceanViewer-5.0.0-windows-x64-portable.zip
packaging/smoke-test.sh build/package-windows/packages/MetOceanViewer-5.0.0-windows-x64.exe
```

`cmake --install build/package-<platform> --prefix <dir>` gives the install
tree without packing it. On macOS and Windows it is the complete application;
on Linux it lacks Qt, which only linuxdeploy adds.

## The self-test and the smoke test

`metoceanviewer --self-test` checks, without a window, GL or the network,
that a package contains what the application loads at run time, prints one
line per check and exits non-zero if one fails:

- Qt Location loads the `maplibre` geoservices plugin (its path is printed)
  and its mapping engine: the plugin, the QMapLibre libraries and the Qt
  modules they need are deployed;
- the main window's QML compiles: every QML import is deployed;
- the window icon renders: Qt's SVG image plugin is deployed;
- Qt's TLS backend works (the basemap is HTTPS) and Qt Sql has its SQLite
  driver (MapLibre's tile cache);
- Windows: the active code page is UTF-8, so the manifest is embedded;
- PROJ opened the packaged `proj.db` (its path is printed), or the one
  `MOV_PROJ_DATA` names (and it says so), and converts EPSG:26915. Any other
  database fails the check (see [PROJ data](#proj-data));
- the netCDF-C version.

`--self-test=render` runs those checks, then loads the main window with a
local one-colour style and passes when a map frame shows the colour (at most
30 s). It needs a display and GL: Xvfb with Mesa on Linux, the runner's
session on macOS, and Qt's `opengl32sw.dll` on Windows (`QT_OPENGL=software`;
the runners have no OpenGL driver).

On Windows the program has no console of its own (it is a GUI program):
with its output redirected (as the smoke test does) the output goes there;
otherwise `--self-test` attaches to the console of the shell that started it.
`cmd` does not wait for a GUI program, so take the exit status from
`start /wait metoceanviewer --self-test` or from PowerShell's
`Start-Process -Wait -PassThru`.

ctest runs the self-tests from the build tree (`metoceanviewer_self_test`
and `..._detects_missing_proj_db`, label `qt`; `..._render`, label `gui`), and
the build stages the plugin and `proj.db` the way the packages do.
`packaging/smoke-test.sh <package>` installs or unpacks a package the way a
user would and runs both self-tests:

- the AppImage on the xcb platform (under `xvfb-run` when there is no
  display; it fails if there is neither) and on the offscreen platform;
- the DMG mounted read-only, after checking its Applications link and the
  bundle's code signature;
- the zip unpacked, after checking that `opengl32sw.dll` is there;
- the installer run silently for the current user. It checks the `.mvs`
  ProgID, uninstalls, waits for the uninstaller, and checks that the files and
  the ProgID are gone.

`MOV_SMOKE_RENDER=0` skips the render test.

## Layout

| | Linux (AppDir `usr/`) | Windows (install dir) | macOS (`MetOceanViewer.app/Contents/`) |
|---|---|---|---|
| Executable | `bin/metoceanviewer` | `bin/metoceanviewer.exe` | `MacOS/MetOceanViewer` |
| Qt, QMapLibre | `lib/` | `bin/` | `Frameworks/` |
| Qt plugins, `geoservices/` (maplibre) | `plugins/` | `plugins/` | `PlugIns/` |
| QML modules | `qml/` | `qml/` | `Resources/qml/` |
| `proj.db` | `share/metoceanviewer/proj/` | `share/metoceanviewer/proj/` | `Resources/proj/` |
| Licenses | `share/doc/metoceanviewer/` | `share/doc/metoceanviewer/` | `Resources/licenses/` |

### PROJ data

The vcpkg PROJ of the Linux and macOS builds is a static library that
carries a copy of `proj.db` inside it, and PROJ falls back to that copy when
it cannot open the database it is given. A build path is not compiled in
(vcpkg builds PROJ with `EMBED_PROJ_DATA_PATH=OFF`); the absolute paths left
in the binaries are source file names in third-party assertions.

The packages still ship `proj.db` and use exactly that file. The database is
then a plain file in the package: it can be inspected and replaced, and it is
the same on Windows, whose PROJ is a DLL. At startup
`mov::ui::configure_projection_data()` hands `mov::io::set_projection_data_dir`
the directory `app_identity::proj_data_dir`, relative to the executable
(`../share/metoceanviewer/proj`, or `../Resources/proj` in the bundle), when
it holds a `proj.db`. **Behaviour change in `mov::io`:** with a configured
directory (or `MOV_PROJ_DATA`), PROJ opens exactly `<dir>/proj.db`, and a
missing or broken file is `database_unavailable`. Before this change PROJ
could silently use its built-in copy. `mov::io::projection_database_path()`
reports the database in use, which the self-test compares with the packaged
directory. `tests/io/test_projection.cpp` and the ctest
`metoceanviewer_self_test_detects_missing_proj_db` pin the behaviour; removing
`proj.db` from an unpacked AppImage makes `--self-test` fail. One CMake
variable sets both the relative path and the install destination, and the
build tree gets the same layout. (`configure_projection_data` moves to
`src/app` with the application state in Phase 4.)

### Notices

The packages carry the GPL-3.0 `LICENSE` and, under `third-party/`:

- the license file vcpkg installed for every shipped port (`<port>.txt`),
  that is every installed port except those in `MOV_NON_RUNTIME_PORTS`
  (`cmake/Packaging.cmake`: the test framework and build helpers). The
  `maplibre-native-qt` one covers MapLibre's vendored code;
- `qt/`: the license texts of the Qt modules shipped, at `QT_VERSION`
  (`packaging/licenses/qt/`; Qt's binaries carry none; refresh with
  `python3 tools/fetch_qt_licenses.py` after a Qt bump);
- in the AppImage, `usr/share/doc/<package>/copyright` of every Ubuntu
  library linuxdeploy bundled. The packaging script maps each library to its
  package (`dpkg-query --search`), adds missing files and fails on a library
  no package owns.

### Runtime libraries

netCDF-C, HDF5, PROJ and SQLite are static on Linux and macOS (vcpkg's
default triplets) and DLLs on Windows, where the install step collects them
with `install(RUNTIME_DEPENDENCY_SET)`. The MSVC runtime is installed
app-local (`InstallRequiredSystemLibraries`), so the installer does not run
`vc_redist`. Qt's SQL drivers other than SQLite (MapLibre's tile cache) are
dropped: their client libraries are not shipped.

## Linux

**The glibc floor: 2.35.** The AppImage runs where glibc is 2.35 or newer:
Ubuntu 22.04+, Debian 12+, RHEL/Rocky/Alma 10, Fedora 36+. RHEL 9 has 2.34 and
is not covered. C++23 with GCC 14 is fixed; the floor was negotiable (owner,
2026-10-07: a newer Ubuntu is acceptable if 22.04 proves fragile; plan §6.24
records the decision). The build runs in an Ubuntu 22.04 image
(`tools/dev/appimage/Dockerfile`), because a binary needs at least the glibc
it was linked against, with a GCC 14 release, because 22.04's own GCC is 12.
Qt 6.11's Linux binaries need glibc 2.34, so Qt does not raise the floor.

Building on 24.04 instead would need no libstdc++ handling, but would raise
the floor to glibc 2.39 (Ubuntu 24.04, Debian 13, Fedora 40) and drop Ubuntu
22.04 and Debian 12. If the 22.04 route ever breaks, switch by running
`package-linux` in the dev image and setting `APPIMAGE_GLIBC_MAX=2.39`; the
hook then never fires, because the system libstdc++ is always new enough.

**The toolchain**, pinned in `tools/versions.env`, is all released software
fetched by URL and SHA-256, so the image can be rebuilt later:

- The compiler is GCC `APPIMAGE_GCC_VERSION` (14.4.0), built in the image's
  first stage from GNU's release tarball (`APPIMAGE_GCC_SHA256`; its GPG
  signature was checked when the pin was set) by 22.04's GCC 12. It is C and
  C++ only, without multilib or the sanitizer runtimes. Like Ubuntu's GCC it
  defaults to PIE, the strong stack protector, a build id and `--as-needed`
  (a specs edit). `cmake/MapLibre.cmake`'s run-time layout relies on
  `--as-needed`: without it QMapLibre lists Qt libraries it does not use,
  which the loader then cannot find. Unlike Ubuntu's, it does not default to
  `_FORTIFY_SOURCE`, `-fcf-protection` or `-fstack-clash-protection`; add
  them as flags if wanted (plan §6.20 leaves release hardening open). It lives in
  `/opt/gcc`, and its libstdc++ is first in the image's loader cache, so the
  tests run against the runtime the code was compiled for. The
  `ubuntu-toolchain-r/test` PPA was the first choice and was dropped: its
  `gcc-14` needs the PPA's newest runtime packages, at the time a GCC 16
  development snapshot, and the PPA expires superseded packages (Launchpad
  removes their files), so no pinned set survives an update. A source build
  adds about 8 minutes on 20 cores to a cold image build (an estimated
  30-60 minutes on CI's 4-core runners, not yet measured).
- The bundled runtime is conda-forge's `libstdcxx` of the same GCC release
  (`APPIMAGE_LIBSTDCXX_URL` and `APPIMAGE_LIBSTDCXX_SHA256`). It is built for
  glibc 2.17 and provides up to `GLIBCXX_3.4.33`. Its license files (GPL-3.0
  with the GCC Runtime Library Exception) ship in
  `share/doc/metoceanviewer/third-party/libstdc++/`. The CMake configure
  fails if the compiler is newer than this runtime (the URL names the GCC
  version), so the headers are never newer than the runtime. The packaging
  script fails unless the bundled copy provides the `GLIBCXX`/`CXXABI`
  versions the binaries need, needs no glibc above the floor and has no
  absolute RUNPATH. The log names it and what it provides.

**libstdc++.** The binaries GCC 14 makes need `GLIBCXX_3.4.32` and
`CXXABI_1.3.15`, which Ubuntu 22.04's libstdc++ (GCC 12, `3.4.30`) lacks.
Static libstdc++ is not an option: QMapLibre and the executable are separate
C++ shared objects that exchange standard-library types. The AppImage
carries the released libstdc++ above in `usr/optional/libstdc++/`. The AppRun hook
`apprun-hooks/mov-runtime.sh` finds the libstdc++ the loader would pick, in
the loader's order (`LD_LIBRARY_PATH`, then ldconfig's cache, then the usual
directories). It puts the bundled directory first on `LD_LIBRARY_PATH` only
when that copy lacks one of the symbol versions the binaries need. It never
does it the other way round: a bundled libstdc++ older than the system's
would break system libraries loaded into the process, such as Mesa's
drivers. libgcc_s is not bundled: the script checks that nothing needs a
newer `GCC_` symbol version than Ubuntu 22.04's (`APPIMAGE_LIBGCC_S_MAX`).
When the hook changes `LD_LIBRARY_PATH` it exports the caller's value as
`MOV_ORIG_LD_LIBRARY_PATH`. A program the application starts (none yet)
should get that back instead of the bundled path.

The hook also handles the Qt platform: the AppImage has the `xcb` and
`offscreen` platform plugins only. A `QT_QPA_PLATFORM` naming none of them
(`wayland`, set globally on some desktops) becomes `xcb`, which runs through
XWayland, and the original value is kept in `MOV_ORIG_QT_QPA_PLATFORM`.
linuxdeploy's Qt plugin adds a hook that sets the `gtk2` platform theme on
GNOME; Qt 6 has no such theme, so the script removes that hook.

Verified on 2026-10-07 with the AppImage built here. `--self-test` and
`--self-test=render` pass under Xvfb, and `--self-test` passes offscreen, on:

- bare `ubuntu:22.04` (glibc 2.35, libstdc++ `3.4.30`): the bundled
  libstdc++ is used;
- `ubuntu:24.04`, with the system libstdc++, and again with 22.04's
  libstdc++ first on `LD_LIBRARY_PATH`. The hook switches to the bundled
  copy; without the hook the process fails to load;
- `fedora:latest` (glibc 2.43): the system libstdc++ is used.

Also on 24.04, `QT_QPA_PLATFORM=wayland` is rewritten and the self-test passes.
The CI job repeats the 24.04 runs and the bare 22.04 run.

**linuxdeploy.** `packaging/linux/appimage.cmake` is CPack's External
generator script. CPack stages the install tree with the prefix `/usr`, and
that staging directory is the AppDir. The script then:

1. downloads linuxdeploy, its Qt plugin and the AppImage runtime (release
   tags and SHA-256 in `tools/versions.env`; the runtime would otherwise be
   the moving `continuous` build);
2. gives the Qt plugin a view of Qt (a `qmake` wrapper with its own `qt.conf`)
   without the plugins whose libraries are not installed: the SQL drivers
   other than SQLite, and the NMEA position plugin (Qt Serial Port). The
   plugin otherwise copies every plugin of a type and fails on those;
3. deploys with linuxdeploy and its Qt plugin (`EXTRA_QT_MODULES=svg` for the
   SVG icon, the `offscreen` platform besides `xcb`), without making the
   AppImage yet;
4. checks the deployed AppDir. It fails on a binary that needs glibc above
   `APPIMAGE_GLIBC_MAX`, uses `GLIBC_PRIVATE`, has a RUNPATH or RPATH that
   does not start with `$ORIGIN`, or needs more of libgcc_s than Ubuntu 22.04
   has. It also collects the Ubuntu libraries' licenses;
5. bundles libstdc++ and configures the hook with the symbol versions the
   binaries need (the log names the bundled package version);
6. makes the AppImage: linuxdeploy again, which rewrites AppRun to source the
   hooks now present.

**What the system provides.** linuxdeploy leaves the graphics driver stack
(libGL, libEGL, libGLdispatch), X11/xcb, fontconfig, freetype, D-Bus and a few
others to the system, as its exclude list says. MapLibre and the application
therefore link `libGL.so.1`, which every system with OpenGL has, rather than
GLVND's `libOpenGL.so.0` (`OpenGL_GL_PREFERENCE=LEGACY` in the port and the
top-level `CMakeLists.txt`). The bare Ubuntu 22.04 smoke test found that
dependency. It installs only `xvfb xauth libgl1 libegl1 libgl1-mesa-dri
libfontconfig1 libxkbcommon-x11-0 libdbus-1-3 libssl3`.

The desktop entry, the icons and the `.mvs` MIME type
(`application/x-metoceanviewer-session`) are installed under `usr/share`, all
named after the application id. Desktop integrators (appimaged, Gear Lever)
read them from there.

To update linuxdeploy, its Qt plugin or the AppImage runtime, change
`LINUXDEPLOY_*` in `tools/versions.env` to a release tag and the SHA-256 of
its `x86_64` file. GitHub shows the digest on the release page, or run
`sha256sum` on the download.

## macOS

*Unverified: implemented to Apple's and Qt's documentation; not yet run.*

- **Info.plist** (`packaging/macos/Info.plist.in`): bundle id
  `io.github.zcobell.metoceanviewer`, name `MetOceanViewer`, version, icon,
  `LSMinimumSystemVersion` from `CMAKE_OSX_DEPLOYMENT_TARGET` (14.0; the
  configure fails without one), and the session document type. `.mvs` files
  are exported as `io.github.zcobell.metoceanviewer.session`, conforming to
  `public.data` (JSON in v5, netCDF in v4). Opening a document from Finder
  arrives as a `QFileOpenEvent`, not as an argument; Phase 6 handles it.
- **Deployment.** The install step puts the QMapLibre frameworks in
  `Contents/Frameworks` and the plugin in `Contents/PlugIns/geoservices`,
  then runs macdeployqt over the bundle with those as extra binaries, so the
  Qt they need (Qt Sql, Qt Location's private API) is deployed too. The map
  renders with Metal.
- **DMG.** CPack DragNDrop with an `/Applications` link, the background
  `dmg-background.png` (made from its SVG) and the icon positions of
  `dmg-setup.applescript`, which runs through Finder.
- **Checks and signing** (`packaging/macos/sign.sh`). Before CPack makes the
  image (`CPACK_PRE_BUILD_SCRIPTS`), the script fails on a Mach-O file in the
  bundle that needs a newer macOS than the deployment target (`minos` of
  `LC_BUILD_VERSION`) or has an absolute `LC_RPATH`. It then signs every
  Mach-O file inside out with the hardened runtime and a secure timestamp, and
  the app last with `entitlements.plist`. The only entitlement is
  `allow-jit`, for the QML JavaScript JIT. After the image is made
  (`CPACK_POST_BUILD_SCRIPTS`), the DMG is signed, notarized once with
  `notarytool submit --wait` (the submission covers the app inside), stapled
  and assessed with `spctl`. A rejected submission prints Apple's log. The app
  inside the DMG is not stapled; Gatekeeper looks its ticket up online.
- **Without an identity** the app gets an ad-hoc signature without the
  hardened runtime, whose library validation an ad-hoc signature cannot
  satisfy, and the DMG stays unsigned. Apple Silicon runs nothing unsigned,
  and a consistent ad-hoc signature lets Gatekeeper offer "Open Anyway"
  instead of calling the app damaged.

## Windows

*Unverified: implemented to Microsoft's, Qt's and Inno Setup's
documentation; not yet run.*

- **Executable metadata** (`packaging/windows/`): `VERSIONINFO` and the icon
  (`metoceanviewer.rc.in`), and an application manifest that MSVC merges into
  the embedded one. The manifest sets `activeCodePage` UTF-8, which netCDF-C
  4.9.3 needs for non-ASCII paths (`docs/core-design.md` §4.1, Paths; the self-test
  checks it), `longPathAware` (it also needs the system's
  `LongPathsEnabled` policy), `dpiAwareness` PerMonitorV2, and `supportedOS`
  Windows 10/11.
- **Deployment.** windeployqt runs over the installed executable and the
  QMapLibre DLLs. It keeps `opengl32sw.dll`, Qt's software OpenGL: MapLibre
  draws with OpenGL on Windows, and machines without an OpenGL 2 driver need
  the fallback. Never pass `--no-opengl-sw`. The install step warns if Qt has
  no `opengl32sw.dll`, and the zip smoke test requires it. Installing a Debug
  configuration fails: MapLibre is release-only.
- **Installer** (CPack INNOSETUP; Inno Setup 6 is preinstalled on GitHub's
  Windows runners). It installs to Program Files by default, or per user from
  the dialog or `/CURRENTUSER`. A fixed `AppId` makes a new version upgrade
  the old one in place, and `[InstallDelete]` (`installer.iss`) clears
  `bin`, `plugins` and `qml` first, so no file of the old version stays behind.
  It requires Windows 10 build 19045 (22H2) and offers to start the
  application. It adds a Start menu entry and registers `.mvs` with an
  `OpenWithProgids` entry and the ProgID `MetOceanViewer.Session` without
  taking the extension over. The application does not open sessions yet
  (Phase 6); the association already passes the file as an argument.
- **Portable zip**: the same tree, unpacked anywhere.
- **Signing** (CI only, `azure/artifact-signing-action`). Before CPack runs,
  every binary the packages take that has no valid signature yet is signed
  in place: the executable, the DLLs beside it, the vcpkg DLLs and plugins,
  and any unsigned Qt DLL. After CPack runs, the installer is signed. Then
  every `.exe` and `.dll` in the zip, and the installer, must carry a valid
  signature of our publisher, Microsoft or The Qt Company. Inno's
  uninstaller is not signed; that would need Inno's `SignTool` hook with a
  local signing tool.

## CI

`.github/workflows/package.yml` (*the macOS and Windows jobs are unverified*):

| Trigger | What happens |
|---|---|
| tag `v*` | release build: cold, signed when the secrets exist, attested, then a draft GitHub Release |
| manual run (Actions, Package, Run workflow) | the packages as workflow artifacts, attested; no release |
| pull request touching packaging inputs | build, test, package, smoke-test; unsigned, no attestation |
| weekly (Monday) | the same, to catch tool and runner drift |

Each platform job configures, builds and tests (`ctest -LE gui`) before any
secret is in its environment. It then packages, smoke-tests, attests build
provenance (`actions/attest`; tags and manual runs), and uploads the packages
and the split symbols (`symbols-<platform>`) as artifacts:

- **linux** builds in the Ubuntu 22.04 image through `tools/dev/run.sh`. It
  smoke-tests the AppImage on the Ubuntu 24.04 runner (the system
  libstdc++), again with Ubuntu 22.04's libstdc++ first on
  `LD_LIBRARY_PATH`, and in a bare `ubuntu:22.04` container (the glibc
  floor, the bundled libstdc++).
- **macos** (macos-15, Xcode 16.2) and **windows** (windows-2022) use the
  CI toolchains and share the build-test jobs' vcpkg caches.
- **release** (tags only) checks that the tag is `v` + `project(VERSION)`,
  collects the packages, builds the [corresponding source](#corresponding-source)
  archive, writes `SHA256SUMS.txt` over all of them, and creates a **draft**
  GitHub Release, or replaces the files of an existing draft on a rerun. It
  refuses to touch a published release. The owner publishes the draft.

**Release builds are cold.** A tag build restores no cache: vcpkg's binary
cache is off (`VCPKG_BINARY_SOURCES=clear`), ccache and sccache are off, Qt is
downloaded, and the Linux container starts with an empty `HOME`. A release
therefore depends only on pinned sources, not on what an earlier build left
behind. Allow 30-60 minutes per job for MapLibre; the Linux job also builds
GCC in its image on every run (not measured on CI yet). Other runs use the caches;
the Linux job caches Qt along with vcpkg and ccache.

**Provenance.** Check a downloaded package with
`gh attestation verify <file> --repo zcobell/MetOceanViewer`.

**Symbols.** The `symbols-*` artifacts hold what the stripped binaries lack:
`metoceanviewer.debug` (Linux; matched by its build id), the `.dSYM` bundle
and the `.pdb`. Keep the release's ones to read crash reports.

## Corresponding source

GPL-3.0 §6 (plan §6.29): every GitHub Release carries
`MetOceanViewer-<v>-sources.tar.xz`, made by the release job with
`tools/source_archive.py` and covered by `SHA256SUMS.txt`. Its `SOURCES.md`
lists the contents:

| Included | What |
|---|---|
| `metoceanviewer/` | this repository at the tag (`git archive`), without the legacy v4 trees no v5 package contains |
| `vcpkg/downloads/`, `vcpkg/ports/`, `vcpkg/BASELINE` | the source archives and recipes of the vcpkg ports at the pinned baseline (`vcpkg install --only-downloads`): netCDF-C, HDF5, libaec, PROJ, SQLite, zlib, nlohmann-json, tinyxml2 (and Catch2, used only by tests) |
| `maplibre-native-qt/` | MapLibre Native Qt at the overlay port's commit, with the submodules the port builds |
| `qt/` | the source archives of the Qt modules the packages ship (qtbase, qtdeclarative, qtlocation, qtpositioning, qtsvg), from Qt's online repository, checked against `packaging/sources/qt-sources.sha256` |

| Referenced (URL and checksum, or package and version) | Why not copied |
|---|---|
| GCC (the AppImage's bundled libstdc++ and its compiler) | under the GCC Runtime Library Exception; the release tarball's URL and SHA-256 are in `tools/versions.env` |
| ICU 73 (inside Qt's Linux binaries) | permissive (Unicode license) |
| The AppImage runtime | permissive (MIT) |
| The Ubuntu 22.04 packages whose libraries the AppImage bundles | listed with source package and version (the packaging script writes `<AppImage>.ubuntu-sources.md`; the release job appends it to `SOURCES.md`, and it is a release asset too); Ubuntu keeps them at `launchpad.net/ubuntu/+source/...` |
| The MSVC runtime of the Windows packages | Microsoft's redistributable binaries, no source |

Size: about 230 MB. MapLibre's test and render-test data and documents
vendored in it (about 400 MB, mostly images and tile databases) are left out:
the build does not use them, and they are upstream at the pinned commits
(`MAPLIBRE_LEFT_OUT` in the script, listed in `SOURCES.md`).

Locally, with vcpkg at the baseline (the dev image's): `tools/dev/run.sh
python3 tools/source_archive.py --output-dir build/sources --work-dir build`.

## Signing

### Secrets

The signing secrets belong in the GitHub Environment `release`, which only
tag builds use (Settings, Environments; see the owner's steps below). A
missing one disables the step that needs it, with a notice in the run
summary. With the repository variable `MOV_REQUIRE_SIGNING` set to `true`
(Settings, Secrets and variables, Actions, Variables), a tag build fails
instead.

| Secret | What |
|---|---|
| `APPLE_CERTIFICATE_P12_BASE64` | The Developer ID Application certificate with its private key, exported from Keychain Access as `.p12`, base64 (`base64 -i cert.p12`) |
| `APPLE_CERTIFICATE_PASSWORD` | The `.p12` export password |
| `APPLE_SIGNING_IDENTITY` | `Developer ID Application: <name> (<team id>)` (`security find-identity -v -p codesigning`) |
| `APPLE_API_KEY_P8_BASE64` | An App Store Connect API key (Users and Access, Integrations, Team Keys; role Developer), the `.p8` file, base64 |
| `APPLE_API_KEY_ID` | That key's id |
| `APPLE_API_ISSUER_ID` | The issuer id shown above the team keys |
| `AZURE_TENANT_ID` | Tenant of the app registration that may sign |
| `AZURE_CLIENT_ID` | That app registration's client id |
| `AZURE_CLIENT_SECRET` | A client secret of it |
| `AZURE_SIGNING_ENDPOINT` | The signing account's regional endpoint, e.g. `https://eus.codesigning.azure.net/` |
| `AZURE_SIGNING_ACCOUNT` | The Artifact Signing account name |
| `AZURE_CERTIFICATE_PROFILE` | The certificate profile name (Public Trust) |

The macOS signing setup imports the certificate into a temporary keychain,
with the private key not extractable and usable by `codesign` only. It writes
the API key with mode 0600 and removes both after packaging. The Azure app
registration needs the Certificate Profile Signer role on the certificate
profile ("Trusted Signing Certificate Profile Signer" before the service was
renamed).

### Local signing (macOS)

`packaging/macos/sign.sh` reads the same values from the environment:
`APPLE_SIGNING_IDENTITY` (and optionally `APPLE_KEYCHAIN`), and for
notarization `APPLE_API_KEY_PATH` (the `.p8` file), `APPLE_API_KEY_ID` and
`APPLE_API_ISSUER_ID`. Export them before `cmake --workflow --preset
package-macos`.

## What the owner must do

1. **First run.** Run the Package workflow by hand on `v5` (Actions, Package,
   Run workflow). The macOS and Windows jobs have never run; fix what they
   find before the first tag.
2. **Apple.** Join the Apple Developer Program, create a Developer ID
   Application certificate and an App Store Connect API key, and add the six
   `APPLE_*` secrets to the `release` environment.
3. **Windows.** Create an Azure subscription with an Artifact Signing account
   (formerly Trusted Signing), pass identity validation for a Public Trust
   certificate profile, create an app registration with the signer role, and
   add the six `AZURE_*` secrets to the `release` environment.
4. **The `release` environment and tags.** GitHub creates the environment on
   its first use. Protect it (Settings, Environments, `release`): under
   deployment branches and tags allow only tags matching `v*`, and optionally
   require a reviewer. Add a tag ruleset (Settings, Rules, Rulesets) that lets
   only maintainers create, update or delete `v*` tags. Once the signing
   secrets are in, set the repository variable `MOV_REQUIRE_SIGNING=true`.
5. **MapLibre source archive.** Publish the pinned maplibre-native-qt tree with
   its submodules as a release asset, so the vcpkg port no longer depends on
   about 30 shallow git fetches (`tools/dev/README.md`, MapLibre Native Qt).
6. **AppStream metainfo.** Add `usr/share/metainfo/<app id>.metainfo.xml`
   (description, screenshots) when there is something to publish;
   `appimagetool` warns until then.
7. **Releasing.** Push `v<project(VERSION)>` (the release job checks it),
   review the draft release, and publish it.

## Keep in sync

Values repeated across files; change them together.

| Value | Where |
|---|---|
| Application id `io.github.zcobell.metoceanviewer` | `cmake/Packaging.cmake` (`MOV_APP_ID`: Info.plist, manifest, `app_identity.hpp`, Inno `AppId`), the file names `packaging/linux/<id>.desktop` and `<id>.xml`, the `Icon=` line of the `.desktop` file and the `<icon>` of the MIME file |
| Session file type `.mvs` | Info.plist (`UTExportedTypeDeclarations`, `<id>.session`), `packaging/windows/installer.iss` (ProgID `MetOceanViewer.Session`), `packaging/smoke-test.sh` (the ProgID it checks), `packaging/linux/<id>.xml` (`application/x-metoceanviewer-session`) and the `.desktop` `MimeType=` |
| Install layout | `cmake/Packaging.cmake` (`MOV_INSTALL_*`, `MOV_APP_PROJ_DATA_DIR`); repeated in the layout table above, `installer.iss` (`{app}\bin`, `plugins`, `qml`), `appimage.cmake` (`usr/plugins/geoservices`, `usr/share/...`) and `smoke-test.sh` (`bin/metoceanviewer.exe`, `MetOceanViewer.app`) |
| DMG window geometry | `packaging/macos/dmg-setup.applescript` (window bounds, icon positions) and `dmg-background.svg` (660 x 400, arrow between the icons) |
| Bare Ubuntu 22.04 package list | `package.yml` (smoke test) and the list under [Linux](#linux) |
| `lukka/get-cmake` pin | `ci.yml` and `package.yml` |
| Qt license texts | `packaging/licenses/qt/` follows `QT_VERSION` (`tools/fetch_qt_licenses.py`) |
| Qt source archives | `packaging/sources/qt-sources.sha256` follows `QT_VERSION` and the Qt modules the packages ship (Qt's online repository, `qt6_<version>_unix_line_endings_src`) |
| Icons | `packaging/icons/`, `packaging/macos/dmg-background.png` follow their SVGs (`tools/make_icons.py`; the pre-commit hook `icons` checks) |
| AppImage toolchain | `tools/versions.env` (`APPIMAGE_*`, `LINUXDEPLOY_*`), read by `tools/dev/run.sh` and `packaging/CMakeLists.txt`; `APPIMAGE_GCC_VERSION` and the GCC version in `APPIMAGE_LIBSTDCXX_URL` move together (the runtime may be newer, never older) |

## Icons

`tools/make_icons.py` renders the placeholder icon
`src/ui/qml/images/app-icon.svg` into `packaging/icons/metoceanviewer.icns`
(macOS), `metoceanviewer.ico` (Windows) and `hicolor/<n>x<n>.png` (Linux),
and `packaging/macos/dmg-background.svg` into its PNG. It uses rsvg-convert
and writes the ICNS and ICO containers itself, so the output depends only on
librsvg's version: Ubuntu 24.04's, in the dev image and in CI's pre-commit
job, which runs the `icons` hook (`--check`). The outputs are committed
(about 150 kB). After changing an SVG:

```sh
tools/dev/run.sh python3 tools/make_icons.py          # rewrite
tools/dev/run.sh python3 tools/make_icons.py --check  # verify they are current
```

## Known gaps

- macOS and Windows: everything is unverified until the first workflow run
  (above).
- The DMG background is a single 1x image; a Retina copy needs a
  multi-resolution TIFF (`tiffutil -cathidpicheck`).
- The Inno Setup uninstaller is unsigned.
