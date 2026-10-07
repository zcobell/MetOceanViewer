#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Code signing and notarization of the macOS package. CPack calls it (see
# packaging/macos/cpack-sign-app.cmake and cpack-sign-dmg.cmake):
#
#   sign.sh app <MetOceanViewer.app> <entitlements.plist>
#       Signs every Mach-O file in the bundle inside out, then the bundle
#       with the hardened runtime and the entitlements; notarizes and
#       staples it when the notary credentials are set.
#   sign.sh dmg <file.dmg>
#       Signs, notarizes and staples the disk image.
#
# Environment (docs/packaging.md; CI fills it from the APPLE_* secrets):
#   APPLE_SIGNING_IDENTITY  "Developer ID Application: <name> (<team id>)".
#                           Unset or empty: the app gets an ad-hoc signature
#                           (Apple Silicon runs nothing unsigned, and a
#                           consistent bundle signature lets Gatekeeper offer
#                           "Open Anyway" instead of "damaged"); the DMG stays
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

# codesign's identity options. Bash 3.2 compatible (macOS's /bin/bash): no
# mapfile, and the array is never empty.
if [[ -z "${identity}" ]]; then
  codesign_args=(--force --sign - --timestamp=none)
else
  codesign_args=(--force --sign "${identity}" --timestamp)
  if [[ -n "${APPLE_KEYCHAIN:-}" ]]; then
    codesign_args+=(--keychain "${APPLE_KEYCHAIN}")
  fi
fi

# Submits a zip or dmg and waits; fails with Apple's log unless Accepted.
notarize() {
  local file="$1" result status id
  result="$(mktemp)"
  xcrun notarytool submit "${file}" \
    --key "${APPLE_API_KEY_PATH}" --key-id "${APPLE_API_KEY_ID}" --issuer "${APPLE_API_ISSUER_ID}" \
    --wait --timeout 2h --output-format json >"${result}" || true
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
  local app="$1" entitlements="$2" file framework

  # Inside out: every Mach-O file except the main executable (dylibs, Qt and
  # QML plugins, framework binaries), then each framework bundle, then the
  # app, which is the only one with the entitlements.
  while IFS= read -r -d '' file; do
    if file -b "${file}" | grep -q 'Mach-O'; then
      codesign "${codesign_args[@]}" --options runtime "${file}"
    fi
  done < <(find "${app}/Contents" -type f ! -path "${app}/Contents/MacOS/*" -print0)
  while IFS= read -r -d '' framework; do
    codesign "${codesign_args[@]}" --options runtime "${framework}"
  done < <(find "${app}/Contents/Frameworks" -maxdepth 1 -name '*.framework' -print0)
  codesign "${codesign_args[@]}" --options runtime --entitlements "${entitlements}" "${app}"
  codesign --verify --deep --strict --verbose=2 "${app}"

  if [[ -z "${identity}" ]]; then
    notice "APPLE_SIGNING_IDENTITY is not set: ad-hoc signature, not notarized"
    return
  fi
  if ! can_notarize; then
    notice "notary credentials (APPLE_API_KEY_*) are not set: signed, not notarized"
    return
  fi
  local zip
  zip="$(mktemp -d)/$(basename "${app}" .app).zip"
  ditto -c -k --keepParent "${app}" "${zip}"
  notarize "${zip}"
  rm -f "${zip}"
  xcrun stapler staple "${app}"
  xcrun stapler validate "${app}"
}

sign_dmg() {
  local dmg="$1"
  if [[ -z "${identity}" ]]; then
    notice "APPLE_SIGNING_IDENTITY is not set: $(basename "${dmg}") is not signed"
    return
  fi
  codesign "${codesign_args[@]}" "${dmg}"
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
    [[ $# -eq 3 ]] || { echo "usage: $0 app <bundle.app> <entitlements.plist>" >&2; exit 2; }
    sign_app "$2" "$3"
    ;;
  dmg)
    [[ $# -eq 2 ]] || { echo "usage: $0 dmg <file.dmg>" >&2; exit 2; }
    sign_dmg "$2"
    ;;
  *)
    echo "usage: $0 app <bundle.app> <entitlements.plist> | dmg <file.dmg>" >&2
    exit 2
    ;;
esac
