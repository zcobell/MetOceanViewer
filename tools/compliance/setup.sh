#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Installs the format-compliance checkers under <prefix> on Ubuntu 24.04:
#   <prefix>/venv                          the pinned Python packages
#   <prefix>/cf-standard-name-table.xml    the CF standard name table, pinned
# plus the system packages they need (udunits for both checkers, ncdump).
# The compliance image (tools/compliance/Dockerfile) and the format-compliance
# CI job both run it, so the two cannot differ.
#
#   tools/compliance/setup.sh <prefix>

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <prefix>" >&2
  exit 2
fi
prefix="$1"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The CF standard name table, by version and digest (4.5 MB: too large for
# the repository's 1 MB file limit, so it is fetched and verified instead).
table_version=95
table_sha256=d202558c2f2d0bf99e9fd4fbf8b8450902c56e09e7974b70b8726e740bbd51ba

sudo_cmd=()
if [[ "$(id -u)" -ne 0 ]]; then
  sudo_cmd=(sudo)
fi
"${sudo_cmd[@]}" apt-get update
"${sudo_cmd[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  ca-certificates curl libudunits2-0 netcdf-bin python3 python3-venv udunits-bin

mkdir -p "${prefix}"
python3 -m venv "${prefix}/venv"
"${prefix}/venv/bin/pip" install --no-cache-dir -r "${here}/requirements.txt"

curl -fsSL -o "${prefix}/cf-standard-name-table.xml" \
  "https://cfconventions.org/Data/cf-standard-names/${table_version}/src/cf-standard-name-table.xml"
echo "${table_sha256}  ${prefix}/cf-standard-name-table.xml" | sha256sum --check --strict
