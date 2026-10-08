#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Smoke test of a package: unpacks or installs it the way a user would, then
# runs the packaged application's self-tests (src/ui/self_test.cpp):
# --self-test (the maplibre plugin, QML imports, the SVG icon plugin, TLS,
# SQLite, the packaged proj.db, netCDF; no window or GL) and
# --self-test=render (one map frame drawn with GL; MOV_SMOKE_RENDER=0 skips it
# where there is no GL).
#
#   packaging/smoke-test.sh MetOceanViewer-<v>-linux-x86_64.AppImage
#       on the xcb platform (under xvfb-run when there is no display; fails
#       if there is neither) and on the offscreen platform
#   packaging/smoke-test.sh MetOceanViewer-<v>-macos-arm64.dmg
#       mounted read-only; also verifies the bundle's code signature
#   packaging/smoke-test.sh MetOceanViewer-<v>-windows-x64-portable.zip
#   packaging/smoke-test.sh MetOceanViewer-<v>-windows-x64.exe
#       the installer, run silently for the current user into a temporary
#       directory; checks the .mvs ProgID, then uninstalls and checks that the
#       files and the ProgID are gone (Git Bash)
#
# The macOS and Windows cases are UNVERIFIED until the package workflow runs
# them (docs/packaging.md).

set -euo pipefail

if [[ $# -ne 1 || ! -f "$1" ]]; then
  echo "usage: $0 <package>" >&2
  exit 2
fi
package="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
render="${MOV_SMOKE_RENDER:-1}"
work="$(mktemp -d)"
cleanup_steps=()
cleanup() {
  local step
  for step in "${cleanup_steps[@]+"${cleanup_steps[@]}"}"; do
    eval "${step}" || true
  done
  rm -rf "${work}"
}
trap cleanup EXIT

fail() {
  echo "smoke-test: $*" >&2
  exit 1
}

# Runs <command...> --self-test[=render]; the output goes through a file
# because a Windows GUI-subsystem program has no console of its own.
run_self_test() {
  local mode="$1" status=0
  shift
  echo "--- $* ${mode}"
  "$@" "${mode}" >"${work}/self-test.log" 2>&1 || status=$?
  cat "${work}/self-test.log"
  [[ ${status} -eq 0 ]] || fail "${mode} failed (exit status ${status})"
}

# Both self-tests of <command...>.
self_tests() {
  run_self_test --self-test "$@"
  if [[ "${render}" == 1 ]]; then
    run_self_test --self-test=render "$@"
  fi
}

# The .mvs ProgID of packaging/windows/installer.iss, per-user install.
progid_key='HKCU\Software\Classes\MetOceanViewer.Session'

case "${package}" in
  *.AppImage)
    chmod +x "${package}"
    # Without FUSE (containers, CI) the AppImage runtime extracts itself to a
    # temporary directory instead of mounting.
    export APPIMAGE_EXTRACT_AND_RUN=1
    if [[ -n "${DISPLAY:-}" ]]; then
      self_tests "${package}"
    elif command -v xvfb-run >/dev/null; then
      self_tests xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" "${package}"
    else
      fail "no display and no xvfb-run: cannot test the xcb platform"
    fi
    QT_QPA_PLATFORM=offscreen run_self_test --self-test "${package}"
    ;;
  *.dmg)
    mkdir "${work}/volume"
    hdiutil attach -nobrowse -readonly -mountpoint "${work}/volume" "${package}" >/dev/null
    cleanup_steps+=("hdiutil detach '${work}/volume' >/dev/null")
    app="${work}/volume/MetOceanViewer.app"
    [[ -L "${work}/volume/Applications" ]] || fail "the DMG has no Applications link"
    codesign --verify --deep --strict --verbose=2 "${app}"
    self_tests "${app}/Contents/MacOS/MetOceanViewer"
    ;;
  *.zip)
    unzip -q "${package}" -d "${work}/portable"
    exe="$(find "${work}/portable" -name metoceanviewer.exe -print -quit)"
    [[ -n "${exe}" ]] || fail "no metoceanviewer.exe in the zip"
    # MapLibre draws with OpenGL; this is Qt's software fallback, which the
    # render test uses (runners have no OpenGL driver).
    [[ -f "$(dirname "${exe}")/opengl32sw.dll" ]] || fail "no opengl32sw.dll beside the executable"
    QT_OPENGL=software self_tests "${exe}"
    ;;
  *.exe)
    # Git Bash would rewrite the /SWITCHES as paths.
    export MSYS_NO_PATHCONV=1
    installed="${work}/installed"
    "${package}" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER "/DIR=$(cygpath -w "${installed}")"
    cleanup_steps+=("'${installed}/unins000.exe' /VERYSILENT /SUPPRESSMSGBOXES /NORESTART")
    reg query "${progid_key}" >/dev/null || fail "the installer did not register ${progid_key}"
    QT_OPENGL=software self_tests "${installed}/bin/metoceanviewer.exe"

    # Uninstall. The uninstaller copies itself to a temporary file and returns
    # at once, so wait until it has removed its own executable.
    cleanup_steps=()
    "${installed}/unins000.exe" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART
    for _ in $(seq 120); do
      [[ -e "${installed}/unins000.exe" ]] || break
      sleep 1
    done
    [[ ! -e "${installed}/unins000.exe" ]] || fail "the uninstaller did not finish within 120 s"
    [[ ! -e "${installed}/bin/metoceanviewer.exe" ]] || fail "the uninstaller left bin/metoceanviewer.exe"
    if reg query "${progid_key}" >/dev/null 2>&1; then
      fail "the uninstaller left ${progid_key}"
    fi
    ;;
  *)
    echo "smoke-test: unknown package type: ${package}" >&2
    exit 2
    ;;
esac
echo "smoke-test: $(basename "${package}") passed"
