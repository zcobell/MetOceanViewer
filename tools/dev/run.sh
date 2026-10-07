#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Run a command inside the MetOceanViewer dev container.
#
#   tools/dev/run.sh cmake --workflow --preset dev
#   tools/dev/run.sh                      # interactive shell
#   MOV_DEV_QT=1 tools/dev/run.sh ...     # make sure Qt is installed first
#
# The repository is mounted at its host path and the command runs as the host
# uid/gid, so build trees and compile_commands.json are valid on both sides.
#
# The image is tagged with a hash of its inputs (Dockerfile, requirements.txt,
# tools/versions.env, the vcpkg baseline) and rebuilt when any of them change.
#
# Qt is installed with aqtinstall into ~/Qt on demand: when MOV_DEV_QT=1, or
# when an argument names a Qt preset (one whose name ends in "-qt", or "tidy",
# whose gate covers the Qt layers). A stamp file records version, arch and
# modules; a mismatch reinstalls.
#
# Persistent state lives on the host:
#   ~/Qt                         Qt
#   ~/.cache/metoceanviewer-dev  ccache, vcpkg downloads + binary cache, $HOME
#
# Environment overrides: MOV_DEV_QT, MOV_QT_ROOT, MOV_DEV_CACHE,
# MOV_DOCKER_ARGS (extra `docker run` arguments).

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "${script_dir}/../.." && pwd)"
versions_file="${repo_root}/tools/versions.env"

# Value of KEY in tools/versions.env (KEY=value, no quoting).
version_of() {
  local value
  value="$(sed -n "s/^$1=//p" "${versions_file}")"
  if [[ -z "${value}" ]]; then
    echo "run.sh: $1 missing from ${versions_file}" >&2
    exit 1
  fi
  printf '%s' "${value}"
}

vcpkg_commit="$(sed -n 's/.*"baseline": *"\([0-9a-f]*\)".*/\1/p' "${repo_root}/vcpkg-configuration.json")"
qt_version="$(version_of QT_VERSION)"
qt_arch=linux_gcc_64
read -r -a qt_modules <<<"$(version_of QT_MODULES)"
qt_root="${MOV_QT_ROOT:-${HOME}/Qt}"
qt_prefix="${qt_root}/${qt_version}/gcc_64"
cache_root="${MOV_DEV_CACHE:-${HOME}/.cache/metoceanviewer-dev}"

input_hash="$(cat "${script_dir}/Dockerfile" "${script_dir}/requirements.txt" "${versions_file}" \
  <(echo "${vcpkg_commit}") | sha256sum | cut -c1-12)"
image="metoceanviewer-dev:${input_hash}"

if ! docker image inspect "${image}" >/dev/null 2>&1; then
  echo "run.sh: building ${image}" >&2
  docker build \
    --build-arg "UBUNTU_IMAGE=$(version_of UBUNTU_IMAGE)" \
    --build-arg "GCC_VERSION=$(version_of GCC_VERSION)" \
    --build-arg "LLVM_VERSION=$(version_of LLVM_VERSION)" \
    --build-arg "CMAKE_VERSION=$(version_of CMAKE_VERSION)" \
    --build-arg "CMAKE_SHA256=$(version_of CMAKE_SHA256)" \
    --build-arg "VCPKG_COMMIT=${vcpkg_commit}" \
    --tag "${image}" --tag metoceanviewer-dev:latest \
    "${script_dir}"
fi

mkdir -p "${qt_root}" "${cache_root}"/{ccache,home,vcpkg/archives,vcpkg/downloads}

# Keep the caller's working directory when it is inside the (mounted) repo.
case "${PWD}" in
  "${repo_root}" | "${repo_root}"/*) workdir="${PWD}" ;;
  *) workdir="${repo_root}" ;;
esac

docker_args=(
  --rm
  --init
  --user "$(id -u):$(id -g)"
  --volume "${repo_root}:${repo_root}"
  --volume "${qt_root}:${qt_root}"
  --volume "${cache_root}/ccache:/cache/ccache"
  --volume "${cache_root}/vcpkg:/cache/vcpkg"
  --volume "${cache_root}/home:/cache/home"
  --workdir "${workdir}"
  --env HOME=/cache/home
  --env CCACHE_DIR=/cache/ccache
  --env CCACHE_MAXSIZE=10G
  --env VCPKG_DOWNLOADS=/cache/vcpkg/downloads
  --env VCPKG_DEFAULT_BINARY_CACHE=/cache/vcpkg/archives
  # Read by the Qt presets (CMAKE_PREFIX_PATH), as in CI (install-qt-action).
  --env "QT_ROOT_DIR=${qt_prefix}"
)
if [[ -t 0 && -t 1 ]]; then
  docker_args+=(--interactive --tty)
fi
if [[ -n "${MOV_DOCKER_ARGS:-}" ]]; then
  read -r -a extra <<<"${MOV_DOCKER_ARGS}"
  docker_args+=("${extra[@]}")
fi

needs_qt="${MOV_DEV_QT:-0}"
for arg in "$@"; do
  if [[ "${arg}" == *-qt || "${arg}" == --preset=*-qt || "${arg}" == tidy || "${arg}" == --preset=tidy ]]; then
    needs_qt=1
  fi
done

install_qt() {
  local stamp="${qt_prefix}/.mov-qt-install"
  local wanted
  wanted="${qt_version} ${qt_arch} $(printf '%s\n' "${qt_modules[@]}" | sort | tr '\n' ' ')"
  # One installer at a time; a second caller waits, then sees the stamp.
  exec 9>"${qt_root}/.mov-qt-install.lock"
  flock 9
  if [[ -f "${stamp}" && "$(cat "${stamp}")" == "${wanted}" ]]; then
    return
  fi
  echo "run.sh: installing Qt ${qt_version} (${qt_arch}: ${qt_modules[*]}) into ${qt_root}" >&2
  # --workdir /tmp: aqt writes aqtinstall.log into its working directory.
  docker run "${docker_args[@]}" --workdir /tmp "${image}" \
    aqt install-qt linux desktop "${qt_version}" "${qt_arch}" \
    --outputdir "${qt_root}" --modules "${qt_modules[@]}"
  printf '%s' "${wanted}" >"${stamp}"
  exec 9>&-
}

if [[ "${needs_qt}" == 1 ]]; then
  install_qt
fi

if [[ $# -eq 0 ]]; then
  set -- bash
fi
exec docker run "${docker_args[@]}" "${image}" "$@"
