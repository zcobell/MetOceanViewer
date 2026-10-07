# Packaging

How MetOceanViewer v5 becomes the release packages of plan §4: a DMG for
macOS 14+ on Apple Silicon, an installer and a portable zip for Windows 10
22H2+/11 on x64, and an AppImage for x86-64 Linux with glibc 2.35 or newer
(plan §6.24).

| Platform | Package | Made by | Signing |
|---|---|---|---|
| macOS (arm64) | `MetOceanViewer-<v>-macos-arm64.dmg` | macdeployqt, CPack DragNDrop | Developer ID, hardened runtime, notarized and stapled (app and DMG) |
| Windows (x64) | `MetOceanViewer-<v>-windows-x64.exe` (Inno Setup), `...-windows-x64-portable.zip` | windeployqt, CPack INNOSETUP and ZIP | Azure Artifact Signing (executables, DLLs, installer) |
| Linux (x86-64) | `MetOceanViewer-<v>-linux-x86_64.AppImage` | linuxdeploy and its Qt plugin, through CPack External | none |

No signing account exists yet (plan §6.1). Every signing step is in place and
runs only when its secrets are set; without them it is skipped with a notice
and the packages are unsigned. See [Signing](#signing).

## Pieces

| What | Where |
|---|---|
| Version, the single source | `project(VERSION ...)` in `CMakeLists.txt` |
| Identity (plan §6.19), install layout, install rules | `cmake/Packaging.cmake` |
| CPack configuration | `packaging/CMakeLists.txt`, `packaging/cpack-project-config.cmake` |
| Qt deployment, macOS and Windows | `packaging/deploy-qt.cmake` (run at install time) |
| AppImage | `packaging/linux/appimage.cmake`, `packaging/linux/apprun-hooks/` |
| Info.plist, entitlements, DMG layout, signing | `packaging/macos/` |
| VERSIONINFO, manifest, file association | `packaging/windows/` |
| Icons, DMG background | `packaging/icons/`, `packaging/macos/dmg-background.png`, made by `tools/make_icons.py` |
| Smoke test | `packaging/smoke-test.sh`, `metoceanviewer --self-test` |
| CI | `.github/workflows/package.yml` |
| AppImage build image | `tools/dev/appimage/Dockerfile` |

`project(VERSION)` feeds `mov::core::version()`, the Info.plist
(`CFBundleShortVersionString`, `CFBundleVersion`), the Windows `VERSIONINFO`
and manifest `assemblyIdentity`, and the CPack package version and file names.

## Building packages locally

Each platform has a workflow preset that configures a Release build without
tests, builds it and runs `cpack`. Packages land in
`build/package-<platform>/packages/`.

**Linux (AppImage).** Build it in the Ubuntu 22.04 image, not the dev image:
the AppImage must not need a newer glibc than 2.35, and a binary needs at
least the glibc it was linked against.

```sh
MOV_DEV_IMAGE=appimage tools/dev/run.sh cmake --workflow --preset package-linux
tools/dev/run.sh packaging/smoke-test.sh build/package-linux/packages/MetOceanViewer-5.0.0-linux-x86_64.AppImage
```

The first run builds the image (a few minutes) and, in it, the vcpkg ports
and MapLibre (Ubuntu 22.04's GCC is a different compiler, so the dev image's
binary cache does not apply). The CPack step downloads linuxdeploy and its Qt
plugin to `build/package-linux/_packaging-tools/` and checks their SHA-256.

**macOS (DMG).** On a Mac with Xcode 16.2+ and the native setup of
`tools/dev/README.md` (vcpkg, Qt in `QT_ROOT_DIR`):

```sh
cmake --workflow --preset package-macos
packaging/smoke-test.sh build/package-macos/packages/MetOceanViewer-5.0.0-macos-arm64.dmg
```

CPack runs the DMG layout script through Finder, so the first run asks
for permission to control Finder (System Settings, Privacy & Security,
Automation). Set the `APPLE_*` variables of [macOS signing](#macos) to sign
and notarize locally; without them the app is ad-hoc signed.

**Windows (installer and zip).** From a Visual Studio 2022 developer prompt
with Inno Setup 6 installed, and Git Bash for the smoke test:

```sh
cmake --workflow --preset package-windows
packaging/smoke-test.sh build/package-windows/packages/MetOceanViewer-5.0.0-windows-x64-portable.zip
packaging/smoke-test.sh build/package-windows/packages/MetOceanViewer-5.0.0-windows-x64.exe
```

`cmake --install build/package-<platform> --prefix <dir>` gives the install
tree without packing it. On macOS and Windows it is the complete application;
on Linux it lacks Qt, which only linuxdeploy adds.

## The smoke test

`metoceanviewer --self-test` checks, without a window or the network, that a
package contains what the application loads at run time, and exits non-zero
otherwise:

- Qt Location loads the `maplibre` geoservices plugin and its mapping engine
  (the plugin, the QMapLibre libraries and the Qt modules they need are
  deployed);
- the main window's QML compiles (every QML import is deployed);
- the window icon renders (Qt's SVG image plugin is deployed);
- PROJ opens its database (`proj.db` is shipped and found);
- the netCDF-C version.

It needs no GL, so it runs on the offscreen platform. ctest runs it from the
build tree (`metoceanviewer_self_test`, label `qt`), and the build stages the
plugin and `proj.db` the same way the packages do.
`packaging/smoke-test.sh <package>` installs or unpacks a package the way a
user would and runs it: the AppImage under Xvfb and offscreen, the DMG
mounted read-only (it also verifies the bundle signature), the zip unpacked,
and the installer run silently for the current user and uninstalled.

## Layout

| | Linux (AppDir `usr/`) | Windows (install dir) | macOS (`MetOceanViewer.app/Contents/`) |
|---|---|---|---|
| Executable | `bin/metoceanviewer` | `bin/metoceanviewer.exe` | `MacOS/MetOceanViewer` |
| Qt, QMapLibre | `lib/` | `bin/` | `Frameworks/` |
| Qt plugins, `geoservices/` (maplibre) | `plugins/` | `plugins/` | `PlugIns/` |
| QML modules | `qml/` | `qml/` | `Resources/qml/` |
| `proj.db` | `share/metoceanviewer/proj/` | `share/metoceanviewer/proj/` | `Resources/proj/` |
| Licenses | `share/doc/metoceanviewer/` | `share/doc/metoceanviewer/` | `Resources/licenses/` |

**PROJ data.** At startup `mov::ui::configure_projection_data()` hands
`mov::io::set_projection_data_dir` the directory `MOV_APP_PROJ_DATA_DIR`,
relative to the executable (`../share/metoceanviewer/proj`, or
`../Resources/proj` in the bundle), when it holds a `proj.db`. One CMake
variable sets both that path and the install destination. The environment
variable `MOV_PROJ_DATA` still overrides it. The build tree gets the same
layout, so the build behaves like a package.

**Notices.** The packages carry the GPL-3.0 `LICENSE` and, under
`third-party/`, the license file vcpkg installed for every shipped port
(`<port>.txt`; the `maplibre-native-qt` one covers MapLibre's vendored code).

**Runtime libraries.** netCDF-C, HDF5, PROJ and SQLite are static on Linux and
macOS (vcpkg's default triplets) and DLLs on Windows, where the install step
collects them with `install(RUNTIME_DEPENDENCY_SET)`. The MSVC runtime is
installed app-local (`InstallRequiredSystemLibraries`), so the installer does
not run `vc_redist`. Qt's SQL drivers other than SQLite (MapLibre's tile cache)
are dropped: their client libraries are not shipped.

## Linux

**The glibc floor: 2.35.** The AppImage runs where glibc is 2.35 or newer:
Ubuntu 22.04+, Debian 12+, RHEL/Rocky/Alma 10, Fedora 36+ (RHEL 9 has 2.34 and
is not covered). C++23 with GCC 14 is fixed; the floor was negotiable (owner,
2026-10-07: a newer Ubuntu is acceptable if 22.04 proves fragile). The build
runs in an Ubuntu 22.04 image with GCC 14 from the `ubuntu-toolchain-r/test`
PPA (`tools/dev/appimage/Dockerfile`), because a binary needs at least the
glibc it was linked against and 22.04's own GCC is 12. Qt 6.11's Linux
binaries need glibc 2.34, so Qt does not raise the floor. The packaging script
fails the build if any binary in the AppDir needs more than 2.35
(`CPACK_MOV_GLIBC_MAX`).

Why not build on 24.04: it would need no libstdc++ handling, but would raise the
floor to glibc 2.39 (Ubuntu 24.04, Debian 13, Fedora 40) and drop Ubuntu 22.04
and Debian 12. The 22.04 route is verified (below), so it is kept. If it ever
breaks, switching means running `package-linux` in the dev image instead of
`MOV_DEV_IMAGE=appimage` and setting `CPACK_MOV_GLIBC_MAX` to 2.39; the hook
then never fires (the system libstdc++ is always new enough).

Verified on 2026-10-07 with the AppImage built here: `--self-test` passes under
Xvfb (xcb) and offscreen on bare `ubuntu:22.04` (glibc 2.35, libstdc++
`3.4.30`, the bundled libstdc++ used), `ubuntu:24.04` and `fedora:latest`
(the system's used), and the hook chooses the bundled copy on `debian:12`.
The CI job repeats the 24.04 and bare 22.04 runs on every package build.

**libstdc++.** The binaries GCC 14 makes need `GLIBCXX_3.4.32` and
`CXXABI_1.3.15`, which Ubuntu 22.04's libstdc++ (GCC 12, `3.4.30`) lacks.
Static libstdc++ is not an option: QMapLibre and the executable are
separate C++ shared objects that exchange standard-library types. The AppImage carries the build image's
libstdc++ and libgcc_s in `usr/optional/libstdc++/`. An AppRun hook
(`apprun-hooks/libstdcxx.sh`) puts that directory first on `LD_LIBRARY_PATH`
only when the system's libstdc++ lacks one of the symbol versions the
application needs. It never does it the other way round: a bundled
libstdc++ older than the system's would break system libraries loaded into
the process, such as Mesa's drivers. The packaging script reads the
versions the binaries need and writes them into the hook.

**linuxdeploy.** `packaging/linux/appimage.cmake` is CPack's External
generator script. CPack stages the install tree with the prefix `/usr`, and
that staging directory is the AppDir. The script then:

1. downloads linuxdeploy, its Qt plugin and the AppImage runtime (release
   tags and SHA-256 in `tools/versions.env`);
2. gives the Qt plugin a view of Qt (a `qmake` wrapper with its own `qt.conf`)
   without the plugins whose libraries are not installed: the SQL drivers
   other than SQLite, and the NMEA position plugin (Qt Serial Port). The
   plugin otherwise copies every plugin of a type and fails on those;
3. bundles libstdc++ and configures the hook;
4. runs linuxdeploy with the Qt plugin (`EXTRA_QT_MODULES=svg` for the SVG
   icon, the `offscreen` platform besides `xcb`), which bundles Qt, the QML
   imports and the system libraries outside its exclude list, writes AppRun
   and makes the AppImage;
5. fails if any binary in the AppDir needs a glibc above 2.35, or a newer
   libstdc++ than the hook checks.

**What the system provides.** linuxdeploy leaves the graphics driver stack
(libGL, libEGL, libGLdispatch), X11/xcb, fontconfig, freetype and a few
others to the system, as its exclude list says. MapLibre therefore links
`libGL.so.1`, which every system with OpenGL has, rather than GLVND's
`libOpenGL.so.0` (`OpenGL_GL_PREFERENCE=LEGACY` in the port); the bare
Ubuntu 22.04 smoke test found that dependency. That test installs only
`xvfb xauth libgl1 libegl1 libfontconfig1 libxkbcommon-x11-0 libdbus-1-3`.

The desktop entry, the icons and the `.mvs` MIME type
(`application/x-metoceanviewer-session`) are installed under `usr/share`, all
named after the application id. Desktop integrators (appimaged, Gear Lever)
read them from there.

To update linuxdeploy, its Qt plugin or the AppImage runtime
(`type2-runtime`, which appimagetool would otherwise download from the moving
`continuous` release), change `LINUXDEPLOY_*` in `tools/versions.env` to a
release tag and the SHA-256 of its `x86_64` file. GitHub shows the digest on
the release page, or run `sha256sum` on the download.

## macOS

- **Info.plist** (`packaging/macos/Info.plist.in`): bundle id
  `io.github.zcobell.metoceanviewer`, name `MetOceanViewer`, version, icon,
  `LSMinimumSystemVersion` 14.0, and the session document type. `.mvs` files
  are exported as `io.github.zcobell.metoceanviewer.session`, conforming to
  `public.data` (JSON in v5, netCDF in v4).
- **Deployment.** The install step puts the QMapLibre frameworks in
  `Contents/Frameworks` and the plugin in `Contents/PlugIns/geoservices`,
  then runs macdeployqt over the bundle with those as extra binaries, so the
  Qt they need (Qt Sql, Qt Location's private API) is deployed too. The map
  renders with Metal.
- **DMG.** CPack DragNDrop with an `/Applications` link, the background
  `dmg-background.png` (made from its SVG) and the icon positions of
  `dmg-setup.applescript`, which runs through Finder.
- **Signing** (`packaging/macos/sign.sh`): CPack signs the staged app before
  it makes the image (`CPACK_PRE_BUILD_SCRIPTS`), and the image after
  (`CPACK_POST_BUILD_SCRIPTS`). Every Mach-O file is signed inside out with
  the hardened runtime and a secure timestamp, then the app with
  `entitlements.plist`. Its only entitlement is `allow-jit`, for the QML
  JavaScript JIT. With notary credentials, the app is zipped, submitted with
  `notarytool submit --wait`, and stapled; then the DMG is signed, notarized,
  stapled and checked with `spctl`. A rejected submission prints Apple's log.
  Without an identity, the app gets an ad-hoc signature and the DMG stays
  unsigned. Apple Silicon runs nothing unsigned, and a consistent ad-hoc
  signature lets Gatekeeper offer "Open Anyway" instead of calling the app
  damaged.

## Windows

- **Executable metadata** (`packaging/windows/`): `VERSIONINFO` and the icon
  (`metoceanviewer.rc.in`), and an application manifest that MSVC merges into
  the embedded one. The manifest sets `activeCodePage` UTF-8, which netCDF-C
  4.9.3 needs for non-ASCII paths (`docs/wp-notes/WP6.md`), `longPathAware`
  (it also needs the system's `LongPathsEnabled` policy), `dpiAwareness`
  PerMonitorV2, and `supportedOS` Windows 10/11.
- **Deployment.** windeployqt runs over the installed executable and the
  QMapLibre DLLs. It keeps `opengl32sw.dll`, Qt's software OpenGL: MapLibre
  draws with OpenGL on Windows, and machines without an OpenGL 2 driver need
  the fallback. Never pass `--no-opengl-sw`. The install step warns if Qt has
  no `opengl32sw.dll`, and the zip smoke test requires it.
- **Installer** (CPack INNOSETUP; Inno Setup 6 is preinstalled on GitHub's
  Windows runners): it installs to Program Files by default, or per user
  from the dialog or `/CURRENTUSER`. A fixed `AppId` makes a new version
  upgrade the old one in place. It requires Windows 10 build 19045 (22H2)
  and offers to start the application. It adds a Start menu entry and
  registers `.mvs` with an `OpenWithProgids` entry and the ProgID
  `MetOceanViewer.Session` (`file-association.iss`) without taking the
  extension over.
- **Portable zip**: the same tree, unpacked anywhere.
- **Signing** (CI only, `azure/artifact-signing-action`): before CPack runs,
  the executable, the DLLs beside it and the vcpkg DLLs and plugins are signed
  in place, so the installer and the zip carry signed binaries. After CPack
  runs, the installer is signed. Qt's and Microsoft's DLLs carry their
  publishers' signatures. Inno's uninstaller is not signed (that would need
  Inno's `SignTool` hook with a local signing tool).

## CI

`.github/workflows/package.yml` runs on tag pushes (`v*`) and on demand
(Actions, Package, Run workflow). Each platform job builds, packages,
smoke-tests and uploads its packages as a workflow artifact:

- **linux** builds in the Ubuntu 22.04 image through `tools/dev/run.sh`. It
  smoke-tests the AppImage twice: on the Ubuntu 24.04 runner, which uses the
  system libstdc++, and in a bare `ubuntu:22.04` container with a few
  desktop libraries, which tests the glibc floor and the bundled libstdc++.
- **macos** (macos-15, Xcode 16.2) and **windows** (windows-2022) use the
  CI toolchains and share the build-test jobs' vcpkg caches.
- **release** (tag pushes only) collects the packages, writes
  `SHA256SUMS.txt`, and creates a **draft** GitHub Release for the tag, or
  updates its files on a rerun. The owner publishes it.

GitHub restores caches only from the run's own ref and the default branch
(`master`), so a tag build starts with cold vcpkg caches and builds MapLibre
on every platform (allow 30-60 minutes per job). A manual run on `v5` reuses
`v5`'s caches.

## Signing

### Secrets

Add them under Settings, Secrets and variables, Actions. Any one missing
disables the step that needs it, with a notice in the run summary.

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

The macOS signing setup creates a temporary keychain for the certificate and
removes it at the end of the job. The app registration needs the
Certificate Profile Signer role on the certificate profile ("Trusted
Signing Certificate Profile Signer" before the service was renamed).

### Local signing (macOS)

`packaging/macos/sign.sh` reads the same values from the environment:
`APPLE_SIGNING_IDENTITY` (and optionally `APPLE_KEYCHAIN`), and for
notarization `APPLE_API_KEY_PATH` (the `.p8` file), `APPLE_API_KEY_ID` and
`APPLE_API_ISSUER_ID`. Export them before `cmake --workflow --preset
package-macos`.

### What the owner must do

1. **Apple**: join the Apple Developer Program, create a Developer ID
   Application certificate and an App Store Connect API key, and add the six
   `APPLE_*` secrets.
2. **Windows**: create an Azure subscription with an Artifact Signing account
   (formerly Trusted Signing). Pass identity validation for a Public Trust
   certificate profile, create an app registration with the signer role, and
   add the six `AZURE_*` secrets.
3. Push a `v5.0.0`-style tag (or run the workflow by hand), check the draft
   release, and publish it.

## Icons

`tools/make_icons.py` renders the placeholder icon `src/ui/qml/images/app-icon.svg`
into `packaging/icons/metoceanviewer.icns` (macOS), `metoceanviewer.ico`
(Windows) and `hicolor/<n>x<n>.png` (Linux), and
`packaging/macos/dmg-background.svg` into its PNG. It uses rsvg-convert from
the dev image and writes the ICNS and ICO containers itself, so the output is
reproducible. The outputs are committed (about 150 kB). After changing an
SVG:

```sh
tools/dev/run.sh python3 tools/make_icons.py          # rewrite
tools/dev/run.sh python3 tools/make_icons.py --check  # verify they are current
```

## Known gaps

- Only the Linux pipeline has run end to end, in this repository's dev
  environment. The macOS and Windows steps follow the documented tools and
  have not run yet; the first `package` workflow run is their test.
- No AppStream metainfo file in the AppImage (`appimagetool` warns). Add one
  when there is a description and screenshots to publish.
- The DMG background is a single 1x image; a Retina copy needs a
  multi-resolution TIFF (`tiffutil -cathidpicheck`).
- Qt's own license texts are not collected into the packages; the shipped
  GPL-3.0 text covers the terms Qt is used under here. How corresponding
  source is offered for the bundled libraries (GPL-3.0 §6) is the owner's
  call.
- The application does not open `.mvs` files yet (plan Phase 6); the
  associations already pass the file as an argument.
