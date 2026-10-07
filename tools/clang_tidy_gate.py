#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
"""
Run clang-tidy over every first-party translation unit and fail on any finding.

The checks come from the repo-root .clang-tidy; the translation units from a
compile database (the `tidy` configure preset writes build/tidy). Only files
under src/, tests/ and tools/ are checked: legacy v4 code, thirdparty/ and
vcpkg-installed headers are not. --export-fixes turns the sweep into a fixer.

CAUTION when applying exported fixes: clang-tidy spells each fix's FilePath
through the include chain (src/a/../b/x.hpp), so one file can appear under
several aliases and clang-apply-replacements would apply the same edit once
per alias. Deduplicate by realpath before applying.

    tools/dev/run.sh cmake --preset tidy
    tools/dev/run.sh python3 tools/clang_tidy_gate.py -p build/tidy
    tools/dev/run.sh python3 tools/clang_tidy_gate.py -p build/tidy --export-fixes fixes/
"""

import argparse
import json
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path

# Top-level repo directories whose translation units the gate covers.
FIRST_PARTY_DIRS = ("src", "tests", "tools")

REPO_ROOT = Path(__file__).resolve().parent.parent


def first_party_sources(build_dir: Path) -> list[Path]:
    """First-party translation units from the compile database, deduplicated."""
    entries = json.loads((build_dir / "compile_commands.json").read_text())
    selected: set[Path] = set()
    for entry in entries:
        path = (Path(entry["directory"]) / entry["file"]).resolve()
        try:
            top = path.relative_to(REPO_ROOT).parts[0]
        except ValueError:
            continue  # outside the repository
        if top in FIRST_PARTY_DIRS:
            selected.add(path)
    return sorted(selected)


def run_one(
    clang_tidy: str, build_dir: Path, source: Path, fixes_yaml: Path | None
) -> tuple[Path, int, str]:
    # gcc-only warning flags in a gcc compile database must not turn into
    # clang-tidy errors under WarningsAsErrors.
    command = [
        clang_tidy,
        "-p",
        str(build_dir),
        "--quiet",
        "--extra-arg=-Wno-unknown-warning-option",
    ]
    if fixes_yaml is not None:
        command.append(f"--export-fixes={fixes_yaml}")
    command.append(str(source))
    result = subprocess.run(command, capture_output=True, text=True, check=False)
    return source, result.returncode, result.stdout + result.stderr


def main() -> None:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "-p",
        dest="build_dir",
        type=Path,
        required=True,
        help="build dir with compile_commands.json",
    )
    parser.add_argument(
        "--clang-tidy", default="clang-tidy", help="clang-tidy executable"
    )
    parser.add_argument(
        "--export-fixes",
        type=Path,
        default=None,
        metavar="DIR",
        help="also write per-TU fix YAML into DIR",
    )
    parser.add_argument(
        "-j",
        dest="jobs",
        type=int,
        default=os.cpu_count() or 1,
        help="parallel jobs (clamped to 1..number of translation units)",
    )
    args = parser.parse_args()

    sources = first_party_sources(args.build_dir)
    if not sources:
        sys.stderr.write("no first-party translation units in the compile database\n")
        sys.exit(1)
    if args.export_fixes is not None:
        args.export_fixes.mkdir(parents=True, exist_ok=True)

    def submit(index: int, source: Path) -> tuple[Path, int, str]:
        fixes = (
            args.export_fixes / f"fixes-{index:04d}.yaml"
            if args.export_fixes is not None
            else None
        )
        return run_one(args.clang_tidy, args.build_dir, source, fixes)

    total = len(sources)
    jobs = max(1, min(args.jobs, total))
    results: list[tuple[Path, int, str]] = []
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(submit, i, s) for i, s in enumerate(sources)]
        # Progress per TU, so a sweep killed from outside still leaves a log.
        for done, future in enumerate(as_completed(futures), start=1):
            result = future.result()
            results.append(result)
            print(f"[{done:>4}/{total}] {result[0].relative_to(REPO_ROOT)}", flush=True)

    # Report in path order, not completion order, so runs diff cleanly.
    failures = sorted((r for r in results if r[1] != 0), key=lambda r: r[0])
    for source, code, output in failures:
        rel = source.relative_to(REPO_ROOT)
        if code < 0:
            sys.stderr.write(f"== {rel} == killed by signal {-code}\n")
        else:
            sys.stderr.write(f"== {rel} ==\n{output}\n")
    print(f"{total} translation units checked, {len(failures)} with findings")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
