#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Smoke test of a package: unpacks or installs it the way a user would, then
# runs the packaged application's --self-test (src/ui/self_test.cpp: the
# maplibre plugin, the QML imports, the SVG icon plugin, proj.db, netCDF),
# which shows no window and needs no GL.
#
#   packaging/smoke-test.sh MetOceanViewer-<v>-linux-x86_64.AppImage
#       under xvfb-run (the xcb platform) when there is no display, then on
#       the offscreen platform
#   packaging/smoke-test.sh MetOceanViewer-<v>-macos-arm64.dmg
#       mounted read-only; also verifies the bundle's code signature
#   packaging/smoke-test.sh MetOceanViewer-<v>-windows-x64-portable.zip
#   packaging/smoke-test.sh MetOceanViewer-<v>-windows-x64.exe
#       the installer, run silently into a temporary directory for the
#       current user, then uninstalled (Git Bash)

set -euo pipefail

if [[ $# -ne 1 || ! -f "$1" ]]; then
  echo "usage: $0 <package>" >&2
  exit 2
fi
package="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
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

# Runs the self-test of <executable>; its output goes through a file because
# a Windows GUI-subsystem program has no console of its own.
self_test() {
  local status=0
  echo "--- $* --self-test"
  "$@" --self-test >"${work}/self-test.log" 2>&1 || status=$?
  cat "${work}/self-test.log"
  if [[ ${status} -ne 0 ]]; then
    echo "smoke-test: the self-test failed (exit status ${status})" >&2
    exit 1
  fi
}

case "${package}" in
  *.AppImage)
    chmod +x "${package}"
    # Without FUSE (containers, CI) the AppImage runtime extracts itself to a
    # temporary directory instead of mounting.
    export APPIMAGE_EXTRACT_AND_RUN=1
    if [[ -z "${DISPLAY:-}" ]] && command -v xvfb-run >/dev/null; then
      self_test xvfb-run --auto-servernum --server-args="-screen 0 1280x800x24" "${package}"
    elif [[ -n "${DISPLAY:-}" ]]; then
      self_test "${package}"
    fi
    QT_QPA_PLATFORM=offscreen self_test "${package}"
    ;;
  *.dmg)
    mkdir "${work}/volume"
    hdiutil attach -nobrowse -readonly -mountpoint "${work}/volume" "${package}" >/dev/null
    cleanup_steps+=("hdiutil detach '${work}/volume' >/dev/null")
    app="${work}/volume/MetOceanViewer.app"
    test -L "${work}/volume/Applications"
    codesign --verify --deep --strict --verbose=2 "${app}"
    self_test "${app}/Contents/MacOS/MetOceanViewer"
    ;;
  *.zip)
    unzip -q "${package}" -d "${work}/portable"
    exe="$(find "${work}/portable" -name metoceanviewer.exe -print -quit)"
    test -n "${exe}"
    # MapLibre draws with OpenGL; this is Qt's software fallback.
    test -f "$(dirname "${exe}")/opengl32sw.dll"
    self_test "${exe}"
    ;;
  *.exe)
    # Git Bash would rewrite the /SWITCHES as paths.
    export MSYS_NO_PATHCONV=1
    target="$(cygpath -w "${work}/installed")"
    "${package}" /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER "/DIR=${target}"
    cleanup_steps+=("MSYS_NO_PATHCONV=1 '${work}/installed/unins000.exe' /VERYSILENT /SUPPRESSMSGBOXES")
    self_test "${work}/installed/bin/metoceanviewer.exe"
    ;;
  *)
    echo "smoke-test: unknown package type: ${package}" >&2
    exit 2
    ;;
esac
echo "smoke-test: $(basename "${package}") passed"
