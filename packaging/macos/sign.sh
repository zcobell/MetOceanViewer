#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Checks, code signing and notarization of the macOS package. CPack calls it
# (packaging/macos/cpack-sign-app.cmake and cpack-sign-dmg.cmake):
#
#   sign.sh app <MetOceanViewer.app> <entitlements.plist> <minimum macOS>
#       Checks that no Mach-O file in the bundle needs a newer macOS than
#       <minimum> or searches an absolute LC_RPATH, then signs every Mach-O
#       file inside out and the bundle last, with the entitlements.
#   sign.sh dmg <file.dmg>
#       Signs the disk image, notarizes it (one submission, which covers the
#       app inside) and staples the ticket to it.
#
# UNVERIFIED: written for macOS 14+ and Xcode 16, not yet run (docs/packaging.md).
#
# Environment (docs/packaging.md; CI fills it from the APPLE_* secrets):
#   APPLE_SIGNING_IDENTITY  "Developer ID Application: <name> (<team id>)".
#                           Unset or empty: the app gets an ad-hoc signature
#                           (Apple Silicon runs nothing unsigned, and a
#                           consistent bundle signature lets Gatekeeper offer
#                           "Open Anyway" instead of "damaged"), without the
#                           hardened runtime, whose library validation an
#                           ad-hoc signature cannot satisfy; the DMG stays
#                           unsigned; nothing is notarized.
#   APPLE_KEYCHAIN          keychain holding the identity (optional).
#   APPLE_API_KEY_PATH, APPLE_API_KEY_ID, APPLE_API_ISSUER_ID
#                           App Store Connect API key for notarytool. Any of
#                           them unset: signed but not notarized.

set -euo pipefail

identity="${APPLE_SIGNING_IDENTITY:-}"

notice() { printf 'sign.sh: %s\n' "$*" >&2; }

can_notarize() {
  [[ -n "${identity}" && -n "${APPLE_API_KEY_PATH:-}" && -n "${APPLE_API_KEY_ID:-}" &&
    -n "${APPLE_API_ISSUER_ID:-}" ]]
}

# codesign's options. Bash 3.2 compatible (macOS's /bin/bash): no mapfile,
# and the array is never empty.
if [[ -z "${identity}" ]]; then
  codesign_args=(--force --sign - --timestamp=none)
else
  codesign_args=(--force --sign "${identity}" --timestamp --options runtime)
  if [[ -n "${APPLE_KEYCHAIN:-}" ]]; then
    codesign_args+=(--keychain "${APPLE_KEYCHAIN}")
  fi
fi

# The Mach-O files of a bundle, NUL-separated.
mach_o_files() {
  local file
  while IFS= read -r -d '' file; do
    if file -b "${file}" | grep -q 'Mach-O'; then
      printf '%s\0' "${file}"
    fi
  done < <(find "$1" -type f -print0)
}

# Fails on a Mach-O file whose minimum macOS (LC_BUILD_VERSION minos) is
# above $2, or that has an absolute LC_RPATH (a path outside the bundle).
check_bundle() {
  local app="$1" minimum="$2" file minos rpath failed=0
  while IFS= read -r -d '' file; do
    minos="$(otool -l "${file}" | awk '/LC_BUILD_VERSION/ {b = 1} b && $1 == "minos" {print $2; exit}')"
    if [[ -n "${minos}" ]] &&
      [[ "$(printf '%s\n%s\n' "${minos}" "${minimum}" | sort -V | tail -n 1)" != "${minimum}" ]]; then
      notice "${file#"${app}"/} needs macOS ${minos}, above ${minimum}"
      failed=1
    fi
    while IFS= read -r rpath; do
      if [[ "${rpath}" == /* ]]; then
        notice "${file#"${app}"/} searches ${rpath}: a path outside the bundle"
        failed=1
      fi
    done < <(otool -l "${file}" | awk '/cmd LC_RPATH/ {r = 1} r && $1 == "path" {print $2; r = 0}')
  done < <(mach_o_files "${app}")
  return "${failed}"
}

# Submits a dmg and waits; fails with Apple's log unless Accepted.
notarize() {
  local file="$1" result status id
  result="$(mktemp)"
  # Well inside the package job's timeout (150 min).
  xcrun notarytool submit "${file}" \
    --key "${APPLE_API_KEY_PATH}" --key-id "${APPLE_API_KEY_ID}" --issuer "${APPLE_API_ISSUER_ID}" \
    --wait --timeout 45m --output-format json >"${result}" || true
  status="$(plutil -extract status raw -o - "${result}" 2>/dev/null || echo unknown)"
  id="$(plutil -extract id raw -o - "${result}" 2>/dev/null || echo)"
  notice "notarization of $(basename "${file}"): ${status} (submission ${id:-none})"
  if [[ "${status}" != "Accepted" ]]; then
    cat "${result}" >&2
    if [[ -n "${id}" ]]; then
      xcrun notarytool log "${id}" \
        --key "${APPLE_API_KEY_PATH}" --key-id "${APPLE_API_KEY_ID}" --issuer "${APPLE_API_ISSUER_ID}" >&2 || true
    fi
    rm -f "${result}"
    return 1
  fi
  rm -f "${result}"
}

sign_app() {
  local app="$1" entitlements="$2" minimum="$3" file framework
  check_bundle "${app}" "${minimum}"

  # Inside out: every Mach-O file except the main executable (dylibs, Qt and
  # QML plugins, framework binaries), then each framework bundle, then the
  # app, which is the only one with the entitlements.
  while IFS= read -r -d '' file; do
    if [[ "${file}" != "${app}/Contents/MacOS/"* ]]; then
      codesign "${codesign_args[@]}" "${file}"
    fi
  done < <(mach_o_files "${app}")
  while IFS= read -r -d '' framework; do
    codesign "${codesign_args[@]}" "${framework}"
  done < <(find "${app}/Contents/Frameworks" -maxdepth 1 -name '*.framework' -print0)
  codesign "${codesign_args[@]}" --entitlements "${entitlements}" "${app}"
  codesign --verify --deep --strict --verbose=2 "${app}"
  if [[ -z "${identity}" ]]; then
    notice "APPLE_SIGNING_IDENTITY is not set: ad-hoc signature, not notarized"
  fi
}

sign_dmg() {
  local dmg="$1"
  if [[ -z "${identity}" ]]; then
    notice "APPLE_SIGNING_IDENTITY is not set: $(basename "${dmg}") is not signed"
    return
  fi
  local -a dmg_args=(--force --sign "${identity}" --timestamp)
  if [[ -n "${APPLE_KEYCHAIN:-}" ]]; then
    dmg_args+=(--keychain "${APPLE_KEYCHAIN}")
  fi
  codesign "${dmg_args[@]}" "${dmg}"
  codesign --verify --verbose=2 "${dmg}"
  if ! can_notarize; then
    notice "notary credentials (APPLE_API_KEY_*) are not set: $(basename "${dmg}") signed, not notarized"
    return
  fi
  notarize "${dmg}"
  xcrun stapler staple "${dmg}"
  xcrun stapler validate "${dmg}"
  spctl --assess --type open --context context:primary-signature --verbose=2 "${dmg}"
}

case "${1:-}" in
  app)
    [[ $# -eq 4 ]] || { echo "usage: $0 app <bundle.app> <entitlements.plist> <minimum macOS>" >&2; exit 2; }
    sign_app "$2" "$3" "$4"
    ;;
  dmg)
    [[ $# -eq 2 ]] || { echo "usage: $0 dmg <file.dmg>" >&2; exit 2; }
    sign_dmg "$2"
    ;;
  *)
    echo "usage: $0 app <bundle.app> <entitlements.plist> <minimum macOS> | dmg <file.dmg>" >&2
    exit 2
    ;;
esac
