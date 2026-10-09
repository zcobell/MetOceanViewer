#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
"""
Make the corresponding-source archive every GitHub Release carries (GPL-3.0 §6).

    python3 tools/source_archive.py --output-dir <dir> [--work-dir <dir>]
        [--reference <file>]...

writes <dir>/MetOceanViewer-<version>-sources.tar.xz, holding:

    metoceanviewer/        this repository at HEAD (git archive), without the
                           legacy v4 trees the packages do not contain
    vcpkg/downloads/       the source archives of the vcpkg ports the packages
                           link (vcpkg install --only-downloads, manifest mode,
                           baseline of vcpkg-configuration.json)
    vcpkg/ports/<port>/    their port recipes (portfile, patches) at the baseline
    maplibre-native-qt/    MapLibre Native Qt and the submodules it is built
                           from, at the commit the overlay port pins
    qt/                    the source archives of the Qt modules the packages
                           ship (packaging/sources/qt-sources.sha256, verified)
    SOURCES.md             what is where, and the sources referenced by URL and
                           checksum instead of copied (the GCC release whose
                           libstdc++ the AppImage carries, Qt's ICU, the Ubuntu
                           packages the AppImage bundles: --reference files)

Needs git, xz, and VCPKG_ROOT pointing at vcpkg checked out at the baseline.
"""

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
# The v4 trees, which no v5 package contains (CLAUDE.md: legacy stays until
# parity).
LEGACY = (
    "MetOceanViewer",
    "MetOceanData",
    "MetOceanHWMStats",
    "MetOceanInstaller",
    "ProcessCrmsDatabase",
    "libraries",
    "thirdparty",
)
# Not used to build the packages, and large: MapLibre's test and render-test
# data, and documents in vendored projects. They stay available upstream at
# the pinned commits (SOURCES.md says so).
MAPLIBRE_LEFT_OUT = (
    "vendor/maplibre-native/metrics",
    "vendor/maplibre-native/test/fixtures",
    "vendor/maplibre-native/vendor/maplibre-tile-spec/test",
    "vendor/maplibre-native/vendor/maplibre-tile-spec/cpp/vendor/fsst/paper",
    "vendor/maplibre-native/vendor/maplibre-tile-spec/cpp/vendor/fsst/fsst-presentation.mp4",
    "vendor/maplibre-native/vendor/maplibre-tile-spec/cpp/vendor/fsst/fsst-presentation.pptx",
    "vendor/maplibre-native/vendor/PMTiles/spec",
    "vendor/maplibre-native/vendor/unordered_dense/data",
)
# Tool binaries vcpkg downloads for itself, not sources of the packages.
NOT_SOURCES = re.compile(r"^(ninja-|patchelf-|cmake-|7z|pkgconf-.*-linux)")


def run(*command: str, **kwargs: object) -> subprocess.CompletedProcess:
    return subprocess.run(command, check=True, **kwargs)


def project_version() -> str:
    text = (REPO / "CMakeLists.txt").read_text()
    match = re.search(r"^\s*VERSION\s+([0-9.]+)\s*$", text, re.MULTILINE)
    if not match:
        sys.exit("no project(VERSION) in CMakeLists.txt")
    return match.group(1)


def versions_env() -> dict[str, str]:
    pins = {}
    for line in (REPO / "tools/versions.env").read_text().splitlines():
        if re.match(r"^[A-Z0-9_]+=", line):
            key, value = line.split("=", 1)
            pins[key] = value
    return pins


def copy_repository(stage: Path) -> None:
    target = stage / "metoceanviewer"
    target.mkdir(parents=True)
    excludes = [f":(exclude){name}" for name in LEGACY]
    archive = run("git", "-C", str(REPO), "archive", "--format=tar", "HEAD", "--", ".",
                  *excludes, stdout=subprocess.PIPE).stdout  # fmt: skip
    run("tar", "-x", "-C", str(target), input=archive)


def copy_vcpkg_sources(stage: Path, work: Path) -> list[str]:
    vcpkg_root = Path(os.environ.get("VCPKG_ROOT", ""))
    if not (vcpkg_root / "vcpkg").exists():
        sys.exit("VCPKG_ROOT must name a bootstrapped vcpkg at the baseline")
    downloads = work / "downloads"
    downloads.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, VCPKG_DOWNLOADS=str(downloads), VCPKG_BINARY_SOURCES="clear")
    # Without the "gui" feature: MapLibre's port fetches with git, below.
    result = run(
        str(vcpkg_root / "vcpkg"), "install", "--only-downloads",
        f"--x-manifest-root={REPO}", f"--x-install-root={work / 'installed'}",
        f"--x-buildtrees-root={work / 'buildtrees'}", f"--x-packages-root={work / 'packages'}",
        env=env, stdout=subprocess.PIPE, text=True,
    )  # fmt: skip
    # "Installing 5/11 hdf5[core,hl,szip,zlib]:x64-linux@2.1.1..."
    installing = r"^Installing \d+/\d+ ([a-z0-9-]+)(?:\[[^\]]*\])?:"
    ports = sorted(set(re.findall(installing, result.stdout, re.M)))
    if not ports:
        sys.exit("vcpkg reported no ports:\n" + result.stdout)
    target = stage / "vcpkg"
    (target / "downloads").mkdir(parents=True)
    for archive in sorted(downloads.iterdir()):
        if archive.is_file() and not NOT_SOURCES.match(archive.name):
            shutil.copy2(archive, target / "downloads" / archive.name)
    for port in ports:
        recipe = vcpkg_root / "ports" / port
        if recipe.is_dir():
            shutil.copytree(recipe, target / "ports" / port)
    baseline = run("git", "-C", str(vcpkg_root), "rev-parse", "HEAD",
                   stdout=subprocess.PIPE, text=True).stdout.strip()  # fmt: skip
    (target / "BASELINE").write_text(
        f"vcpkg {baseline} (https://github.com/microsoft/vcpkg)\nports: {' '.join(ports)}\n"
    )
    return ports


def portfile_list(text: str, name: str) -> list[str]:
    match = re.search(rf"^set\({name}\s+(.*?)\)", text, re.M | re.S)
    if not match:
        sys.exit(f"no set({name} ...) in the maplibre-native-qt portfile")
    return re.sub(r"#[^\n]*", "", match.group(1)).split()


def copy_maplibre(stage: Path, work: Path) -> str:
    portfile = (
        REPO / "cmake/vcpkg-ports/maplibre-native-qt/portfile.cmake"
    ).read_text()
    ref = portfile_list(portfile, "mln_qt_ref")[0]
    core = portfile_list(portfile, "mln_core_submodules")
    tile_spec = portfile_list(portfile, "mln_tile_spec_submodules")
    source = work / "maplibre-native-qt"
    shutil.rmtree(source, ignore_errors=True)
    source.mkdir(parents=True)
    git = ("git", "-c", "advice.detachedHead=false")
    shallow = (
        "submodule",
        "update",
        "--init",
        "--depth",
        "1",
        "--single-branch",
        "--jobs",
        "8",
        "--",
    )
    run(*git, "-C", str(source), "init", "--quiet")
    run(*git, "-C", str(source), "fetch", "--quiet", "--depth", "1",
        "https://github.com/maplibre/maplibre-native-qt.git", ref)  # fmt: skip
    run(*git, "-C", str(source), "checkout", "--quiet", "FETCH_HEAD")
    run(*git, "-C", str(source), *shallow, "vendor/maplibre-native")
    run(*git, "-C", str(source / "vendor/maplibre-native"), *shallow, *core)
    run(*git, "-C", str(source / "vendor/maplibre-native/vendor/maplibre-tile-spec"),
        *shallow, *tile_spec)  # fmt: skip
    for left_out in MAPLIBRE_LEFT_OUT:
        path = source / left_out
        if path.is_dir():
            shutil.rmtree(path)
        elif path.exists():
            path.unlink()
    for git_entry in list(source.rglob(".git")):
        if git_entry.is_dir():
            shutil.rmtree(git_entry)
        else:
            git_entry.unlink()
    shutil.move(str(source), stage / "maplibre-native-qt")
    return ref


def copy_qt(stage: Path) -> list[str]:
    target = stage / "qt"
    target.mkdir(parents=True)
    names = []
    for line in (REPO / "packaging/sources/qt-sources.sha256").read_text().splitlines():
        if not line.strip():
            continue
        sha256, url = line.split()
        # The online repository prefixes a build stamp: keep the plain name.
        name = re.sub(r"^[0-9.]+-\d+-\d+", "", url.rsplit("/", 1)[-1])
        request = urllib.request.Request(url, headers={"User-Agent": "metoceanviewer"})
        with urllib.request.urlopen(request) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != sha256:
            sys.exit(f"SHA-256 mismatch for {url}")
        (target / name).write_bytes(data)
        names.append(name)
    return names


def sources_md(version: str, ports: list[str], maplibre_ref: str, qt: list[str],
               references: list[Path]) -> str:  # fmt: skip
    pins = versions_env()
    gcc = pins.get("APPIMAGE_GCC_VERSION")
    gcc_url = f"https://ftp.gnu.org/gnu/gcc/gcc-{gcc}/gcc-{gcc}.tar.xz"
    runtime_url = "https://github.com/AppImage/type2-runtime/releases/tag/" + pins.get(
        "LINUXDEPLOY_APPIMAGE_RUNTIME_VERSION", "?"
    )
    lines = [
        f"# MetOceanViewer {version}: corresponding source",
        "",
        "The source of everything the MetOceanViewer packages of this release contain that",
        "its license asks to be offered (GPL-3.0 section 6; docs/packaging.md, plan §6.29).",
        "",
        "## Included",
        "",
        "- `metoceanviewer/`: this repository at the release tag, without the legacy v4",
        f"  trees ({', '.join(LEGACY)}), which no v5 package contains.",
        f"- `vcpkg/`: the source archives (`downloads/`) and recipes (`ports/`) of the vcpkg ports: {', '.join(ports)}.",
        "  `BASELINE` names the vcpkg commit; the overlay ports are in `metoceanviewer/cmake/vcpkg-ports/`.",
        f"- `maplibre-native-qt/`: MapLibre Native Qt at {maplibre_ref} with the submodules",
        "  its port builds (`metoceanviewer/cmake/vcpkg-ports/maplibre-native-qt/portfile.cmake`),",
        "  without test and render-test data and vendored documents, which the build does not",
        f"  use and which stay upstream at the same commits: {', '.join(MAPLIBRE_LEFT_OUT)}.",
        f"- `qt/`: Qt {pins.get('QT_VERSION', '?')} module sources: {', '.join(qt)}.",
        "",
        "## Referenced (URL and SHA-256)",
        "",
        "- GCC (the compiler of the Linux build; the AppImage carries its libstdc++,",
        "  conda-forge's `libstdcxx` build of the same release, under the GCC Runtime Library",
        f"  Exception): {gcc_url} sha256 {pins.get('APPIMAGE_GCC_SHA256')};",
        f"  the binary: {pins.get('APPIMAGE_LIBSTDCXX_URL')} sha256 {pins.get('APPIMAGE_LIBSTDCXX_SHA256')}.",
        "- ICU 73, which Qt's Linux binaries (and so the AppImage) carry, under the Unicode",
        "  license: https://github.com/unicode-org/icu/releases/tag/release-73-2.",
        f"- The AppImage runtime (MIT), prepended to the AppImage: {runtime_url}.",
        "- The Microsoft Visual C++ runtime DLLs of the Windows packages are redistributable",
        "  binaries without source.",
    ]
    for reference in references:
        lines += ["", reference.read_text().rstrip()]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument(
        "--work-dir", type=Path, help="scratch space (default: a temporary dir)"
    )
    parser.add_argument("--reference", type=Path, action="append", default=[],
                        help="a Markdown section to append to SOURCES.md")  # fmt: skip
    args = parser.parse_args()
    version = project_version()
    with tempfile.TemporaryDirectory(dir=args.work_dir) as tmp:
        work = Path(tmp)
        name = f"MetOceanViewer-{version}-sources"
        stage = work / name
        stage.mkdir()
        copy_repository(stage)
        ports = copy_vcpkg_sources(stage, work)
        ref = copy_maplibre(stage, work)
        qt = copy_qt(stage)
        (stage / "SOURCES.md").write_text(
            sources_md(version, ports, ref, qt, args.reference)
        )
        args.output_dir.mkdir(parents=True, exist_ok=True)
        output = args.output_dir.resolve() / f"{name}.tar.xz"
        run("tar", "-C", str(work), "--sort=name", "--owner=0", "--group=0", "--numeric-owner",
            "-I", "xz -T0 -6", "-cf", str(output), name)  # fmt: skip
    print(f"wrote {output} ({output.stat().st_size // (1 << 20)} MiB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
