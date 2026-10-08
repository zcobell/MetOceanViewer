#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Installs the format-compliance checkers under <prefix> on Ubuntu 24.04:
#   <prefix>/venv                          the pinned Python packages
#   <prefix>/cf-standard-name-table.xml    the CF tables cfchecks reads, each
#   <prefix>/area-type-table.xml           pinned by version (or commit) and
#   <prefix>/standardized-region-list.xml  SHA-256, so a check needs no network
# plus the system packages they need (udunits for both checkers, ncdump),
# pinned to the Ubuntu 24.04 release versions. The compliance image
# (tools/compliance/Dockerfile) and the format-compliance CI job both run it,
# so the two cannot differ.
#
#   tools/compliance/setup.sh <prefix>

set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "usage: $0 <prefix>" >&2
  exit 2
fi
prefix="$1"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# The CF tables, by version and digest (the standard name table is 4.5 MB,
# too large for the repository's 1 MB file limit, so all three are fetched and
# verified instead). The region list has no versioned URL: it is taken from
# the cfconventions.org repository at a fixed commit (version 5).
cf_data="https://cfconventions.org/Data"
region_commit=83a9da12a089cb2178d203595871fff863f43936
tables=(
  "cf-standard-name-table.xml ${cf_data}/cf-standard-names/95/src/cf-standard-name-table.xml d202558c2f2d0bf99e9fd4fbf8b8450902c56e09e7974b70b8726e740bbd51ba"
  "area-type-table.xml ${cf_data}/area-type-table/13/src/area-type-table.xml 2d01a9eab93c77e8d956378558f2bf3631b62a4d5ab6f2e5417995e145e080a2"
  "standardized-region-list.xml https://raw.githubusercontent.com/cf-convention/cf-convention.github.io/${region_commit}/Data/standardized-region-list/standardized-region-list.xml 80b444d86ed140b0fd866bd6e5d4f3c8aed73e1391c56bf66df6ebbc0a7952da"
)

sudo_cmd=()
if [[ "$(id -u)" -ne 0 ]]; then
  sudo_cmd=(sudo)
fi
"${sudo_cmd[@]}" apt-get update
"${sudo_cmd[@]}" env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  ca-certificates curl python3 python3-venv \
  libudunits2-0=2.2.28-7build1 udunits-bin=2.2.28-7build1 \
  netcdf-bin=1:4.9.2-5ubuntu4

mkdir -p "${prefix}"
python3 -m venv "${prefix}/venv"
"${prefix}/venv/bin/pip" install --no-cache-dir --require-hashes -r "${here}/requirements.txt"

for table in "${tables[@]}"; do
  read -r name url sha256 <<<"${table}"
  curl -fsSL -o "${prefix}/${name}" "${url}"
  echo "${sha256}  ${prefix}/${name}" | sha256sum --check --strict
done
