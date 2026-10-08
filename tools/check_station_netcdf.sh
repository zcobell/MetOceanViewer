#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# The format-compliance check of the station netCDF files, locally
# (docs/station-netcdf.md 14.2; the CI job of the same name runs the same
# tools/check_cf.py):
#
#   1. builds station_nc_fixtures in the dev container (preset
#      $MOV_COMPLIANCE_PRESET, default dev) and writes the canonical files into
#      build/format-compliance/;
#   2. builds the checker image (tools/compliance/Dockerfile: IOOS
#      compliance-checker, cfchecker, xarray, ncdump, all pinned), tagged with a
#      hash of its inputs, unless it exists;
#   3. runs tools/check_cf.py on the files in it.
#
#   tools/check_station_netcdf.sh
#
# Exit status: check_cf.py's (0 when every file passes).

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/.." && pwd)"
preset="${MOV_COMPLIANCE_PRESET:-dev}"
out_dir="build/format-compliance"
compliance_dir="${repo_root}/tools/compliance"

ubuntu_image="$(sed -n 's/^UBUNTU_IMAGE=//p' "${repo_root}/tools/versions.env")"
if [[ -z "${ubuntu_image}" ]]; then
  echo "check_station_netcdf.sh: UBUNTU_IMAGE missing from tools/versions.env" >&2
  exit 1
fi

cd "${repo_root}"
if [[ ! -f "build/${preset}/CMakeCache.txt" ]]; then
  tools/dev/run.sh cmake --preset "${preset}"
fi
tools/dev/run.sh cmake --build --preset "${preset}" --target station_nc_fixtures
rm -rf "${out_dir}"
tools/dev/run.sh "build/${preset}/tests/io/station_nc_fixtures" "${out_dir}"

input_hash="$(cat "${compliance_dir}/Dockerfile" "${compliance_dir}/requirements.txt" \
  "${compliance_dir}/setup.sh" <(echo "${ubuntu_image}") | sha256sum | cut -c1-12)"
image="metoceanviewer-compliance:${input_hash}"
if ! docker image inspect "${image}" >/dev/null 2>&1; then
  echo "check_station_netcdf.sh: building ${image}" >&2
  docker build --build-arg "UBUNTU_IMAGE=${ubuntu_image}" --tag "${image}" "${compliance_dir}"
fi

docker run --rm --init \
  --user "$(id -u):$(id -g)" \
  --env HOME=/tmp \
  --volume "${repo_root}:${repo_root}:ro" \
  --workdir "${repo_root}" \
  "${image}" \
  python3 tools/check_cf.py "${out_dir}" --cdl tests/fixtures/io/station_netcdf
