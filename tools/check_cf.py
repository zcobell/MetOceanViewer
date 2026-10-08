#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
"""
Format compliance of the v5 station netCDF files (docs/station-netcdf.md 14.2).

For each canonical file in DIR (station_timeseries_*.nc, written by the
station_nc_fixtures test executable):

  1. IOOS compliance-checker, test cf:1.11: fails on any failed required check
     (an "error") and on any failed recommended check (a "warning") that
     tools/cf_waivers.txt does not waive.
  2. cfchecks (the CF checker) on a copy whose Conventions is rewritten to
     CF-1.8, the newest version cfchecker 4.1.0 supports: fails on any ERROR or
     WARN.
  3. xarray smoke test: open_dataset with CF decoding; for the two SN 9 files
     the decoded values, times, strings and fills are compared with what the
     C++ tables hold.
  4. `ncdump -h` equals the committed CDL in CDL_DIR (application version
     masked); so does that of ncdump_probe.nc, which pins the header dumper the
     C++ golden tests use to the real ncdump.

Exit status 0 when every file passes every check, 1 otherwise. Needs the
pinned packages of tools/compliance/requirements.txt, udunits2 and ncdump:
run it through tools/check_station_netcdf.sh or the format-compliance CI job.
"""

from __future__ import annotations

import argparse
import contextlib
import difflib
import io
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Iterable, Iterator
from dataclasses import dataclass, field
from pathlib import Path

import netCDF4
import numpy as np
import xarray as xr
from compliance_checker.runner import CheckSuite

# IOOS compliance-checker weights: 3 = required (error), 2 = recommended
# (warning), 1 = suggestion (not checked).
HIGH, MEDIUM = 3, 2
APP_VERSION = re.compile(r"MetOceanViewer [0-9]+\.[0-9]+\.[0-9]+")


@dataclass
class Report:
    """What one file's checks found."""

    name: str
    failures: list[str] = field(default_factory=list)
    notes: list[str] = field(default_factory=list)

    def fail(self, message: str) -> None:
        self.failures.append(message)

    def note(self, message: str) -> None:
        self.notes.append(message)


def load_waivers(path: Path) -> list[re.Pattern[str]]:
    """
    Read the waivers: one regular expression per non-comment line.

    Each is searched in the IOOS finding text "<check name>: <message>".
    """
    patterns = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if line and not line.startswith("#"):
            patterns.append(re.compile(line))
    return patterns


def ioos_findings(path: Path) -> list[tuple[int, str]]:
    """(weight, "<check>: <message>") for each failed cf:1.11 check."""
    suite = CheckSuite()
    suite.load_all_available_checkers()
    dataset = suite.load_dataset(str(path))
    with contextlib.redirect_stdout(io.StringIO()):
        groups = suite.run_all(dataset, ["cf:1.11"], skip_checks=[])
    results, errors = groups["cf:1.11"]
    findings = [(HIGH, f"checker exception: {name}: {e}") for name, e in errors.items()]
    for result in flatten(results):
        scored, possible = (
            result.value if isinstance(result.value, tuple) else (result.value, True)
        )
        if scored == possible:
            continue
        for message in result.msgs or ["(no message)"]:
            findings.append((result.weight, f"{result.name}: {message}"))
    return findings


def flatten(results: Iterable) -> Iterator:
    """Every result and, depth first, its children."""
    for result in results:
        yield result
        yield from flatten(getattr(result, "children", None) or [])


def check_ioos(path: Path, waivers: list[re.Pattern[str]], report: Report) -> None:
    for weight, text in ioos_findings(path):
        if weight >= HIGH:
            report.fail(f"IOOS error: {text}")
        elif weight == MEDIUM:
            if any(w.search(text) for w in waivers):
                report.note(f"IOOS warning (waived): {text}")
            else:
                report.fail(f"IOOS warning: {text}")


def check_cfchecks(path: Path, table: Path, report: Report) -> None:
    with tempfile.TemporaryDirectory() as scratch:
        copy = Path(scratch) / path.name
        shutil.copyfile(path, copy)
        with netCDF4.Dataset(copy, "a") as ds:
            ds.Conventions = "CF-1.8"
        run = subprocess.run(
            ["cfchecks", "-v", "1.8", "-s", str(table), str(copy)],
            capture_output=True,
            text=True,
            check=False,
        )
    output = run.stdout + run.stderr
    bad = [
        line.strip()
        for line in output.splitlines()
        if re.match(r"\s*(ERROR|WARN):", line)
    ]
    for line in bad:
        report.fail(f"cfchecks: {line}")
    errors = re.search(r"ERRORS detected:\s*(\d+)", output)
    warnings = re.search(r"WARNINGS given:\s*(\d+)", output)
    if not errors or not warnings:
        report.fail(f"cfchecks: no summary (exit {run.returncode}): {output[-400:]}")
        return
    if int(errors.group(1)) or int(warnings.group(1)):
        report.fail(f"cfchecks: {errors.group(0)}, {warnings.group(0)}")
    infos = [
        line.strip() for line in output.splitlines() if re.match(r"\s*INFO:", line)
    ]
    report.note(f"cfchecks: 0 errors, 0 warnings, {len(infos)} INFO")
    for line in infos:
        report.note(f"cfchecks   {line}")


# What the C++ tables of station_nc_canonical.cpp hold (SN 9).
T0 = 1700000000000
EXPECTED = {
    "station_timeseries_orthogonal": {
        "ids": ["8761724", "8760922"],
        "names": ["Grand Isle, LA", "Pilots Station East, SW Pass"],
        "times": [[T0 + k * 360000 for k in range(4)]] * 2,
        "water_level": [[0.5, 0.6, 0.7, 0.8], [1.0, None, 1.2, 1.3]],
        "water_temperature": [[20.0, 21.0, 22.0, 23.5], [None] * 4],
    },
    "station_timeseries_incomplete": {
        "ids": ["8761724", "8760922"],
        "names": ["Grand Isle, LA", "Pilots Station East, SW Pass"],
        "times": [
            [T0, T0 + 360000, T0 + 720000],
            [T0 + k * 900000 for k in range(5)],
        ],
        "water_level": [[0.5, 0.6, 0.7], [1.0, None, 1.2, 1.3, 1.4]],
        "water_temperature": [[20.0, 21.0, 22.0], [None] * 5],
    },
}


def same(values: list[float], expected: list[float | None]) -> bool:
    """Whether each value equals the expected one; None stands for NaN."""
    if len(values) != len(expected):
        return False
    return all(
        (e is None and math.isnan(v))
        or (e is not None and not math.isnan(v) and v == e)
        for v, e in zip(values, expected, strict=True)
    )


def check_xarray(path: Path, report: Report) -> None:
    with xr.open_dataset(path, decode_cf=True) as ds:
        ds.load()
        expected = EXPECTED.get(path.stem)
        if expected is None:
            report.note(f"xarray: decoded {len(ds.data_vars)} variables")
            return
        if [str(s) for s in ds["station_id"].values] != expected["ids"]:
            report.fail(f"xarray: station_id {ds['station_id'].values!r}")
        if [str(s) for s in ds["station_name"].values] != expected["names"]:
            report.fail(f"xarray: station_name {ds['station_name'].values!r}")
        epoch = np.datetime64("1970-01-01T00:00:00", "ms")
        for s, times in enumerate(expected["times"]):
            decoded = (
                ds["time"].values if ds["time"].ndim == 1 else ds["time"].values[s]
            )
            ms = [
                int((t - epoch) / np.timedelta64(1, "ms"))
                for t in decoded[: len(times)]
            ]
            if ms != times:
                report.fail(f"xarray: station {s} times {ms} != {times}")
            if ds["time"].ndim == 2 and not np.all(np.isnat(decoded[len(times) :])):
                report.fail(f"xarray: station {s} time padding is not NaT")
        for var in ("water_level", "water_temperature"):
            for s, values in enumerate(expected[var]):
                row = [float(x) for x in ds[var].values[s]]
                if not same(row[: len(values)], values):
                    report.fail(f"xarray: {var}[{s}] = {row}, expected {values}")
                if any(not np.isnan(x) for x in row[len(values) :]):
                    report.fail(f"xarray: {var}[{s}] padding is not NaN")
        if ds["time"].ndim == 1 and "time" not in ds.indexes:
            report.fail("xarray: the orthogonal time is not an index")
        report.note("xarray: values, times, strings and fills as written")


def check_cdl(path: Path, cdl_dir: Path, report: Report) -> None:
    golden_path = cdl_dir / f"{path.stem}.cdl"
    if not golden_path.exists():
        report.fail(f"ncdump: no golden {golden_path}")
        return
    run = subprocess.run(
        ["ncdump", "-h", str(path)], capture_output=True, text=True, check=False
    )
    if run.returncode != 0:
        report.fail(f"ncdump: exit {run.returncode}: {run.stderr.strip()}")
        return
    actual = APP_VERSION.sub("MetOceanViewer X.Y.Z", run.stdout)
    golden = APP_VERSION.sub(
        "MetOceanViewer X.Y.Z", golden_path.read_text(encoding="utf-8")
    )
    if actual != golden:
        diff = "".join(
            difflib.unified_diff(
                golden.splitlines(keepends=True),
                actual.splitlines(keepends=True),
                str(golden_path),
                "ncdump -h",
            )
        )
        report.fail(f"ncdump -h differs from the golden CDL:\n{diff}")
    else:
        report.note("ncdump -h equals the golden CDL")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument(
        "dir", type=Path, help="directory of the files station_nc_fixtures wrote"
    )
    parser.add_argument(
        "--cdl", type=Path, required=True, help="directory of the golden CDL"
    )
    parser.add_argument(
        "--standard-names",
        type=Path,
        default=Path(
            os.environ.get("CF_STANDARD_NAME_TABLE", "cf-standard-name-table.xml")
        ),
        help="the pinned CF standard name table (default: $CF_STANDARD_NAME_TABLE)",
    )
    parser.add_argument(
        "--waivers",
        type=Path,
        default=Path(__file__).with_name("cf_waivers.txt"),
        help="IOOS warnings that are allowed (default: tools/cf_waivers.txt)",
    )
    args = parser.parse_args()

    waivers = load_waivers(args.waivers)
    files = sorted(args.dir.glob("station_timeseries_*.nc"))
    if not files:
        print(f"check_cf: no station_timeseries_*.nc in {args.dir}", file=sys.stderr)
        return 1
    reports = []
    for path in files:
        report = Report(path.name)
        check_ioos(path, waivers, report)
        check_cfchecks(path, args.standard_names, report)
        check_xarray(path, report)
        check_cdl(path, args.cdl, report)
        reports.append(report)
    probe = args.dir / "ncdump_probe.nc"
    report = Report(probe.name)
    check_cdl(probe, args.cdl, report)
    reports.append(report)

    for report in reports:
        status = "FAIL" if report.failures else "ok"
        print(f"[{status}] {report.name}")
        for line in report.notes:
            print(f"    {line}")
        for line in report.failures:
            print(f"    FAILED: {line}")
    failed = sum(1 for r in reports if r.failures)
    print(f"check_cf: {len(reports) - failed} of {len(reports)} files pass")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
