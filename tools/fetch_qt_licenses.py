#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
"""
Fetch the license texts of the Qt modules the packages ship.

    python3 tools/fetch_qt_licenses.py            # QT_VERSION from tools/versions.env

Qt's binaries (aqtinstall) carry no license texts, so the packages install the
LICENSES/ directories of the Qt modules they ship, taken from Qt's sources at
QT_VERSION, from packaging/licenses/qt/ (cmake/Packaging.cmake). Rerun after
a Qt bump; the files are committed. The union is kept: a text is written once,
from the first module that has it.
"""

import json
import sys
import urllib.request
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
OUT = REPO / "packaging/licenses/qt"
# Qt's GitHub mirrors of the modules in the packages (see QT_MODULES).
MODULES = ("qtbase", "qtdeclarative", "qtlocation", "qtpositioning", "qtsvg")
HOST = "github.com"


def qt_version() -> str:
    for line in (REPO / "tools/versions.env").read_text().splitlines():
        if line.startswith("QT_VERSION="):
            return line.split("=", 1)[1]
    sys.exit("QT_VERSION missing from tools/versions.env")


def get(url: str) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "metoceanviewer"})
    with urllib.request.urlopen(request) as response:
        return response.read()


def main() -> int:
    version = qt_version()
    OUT.mkdir(parents=True, exist_ok=True)
    for stale in OUT.glob("*.txt"):
        stale.unlink()
    written: dict[str, str] = {}
    for module in MODULES:
        listing = json.loads(
            get(f"https://api.{HOST}/repos/qt/{module}/contents/LICENSES?ref={version}")
        )
        for entry in listing:
            name = entry["name"]
            if name in written:
                continue
            raw = f"https://raw.githubusercontent.com/qt/{module}/{version}/LICENSES/{name}"
            (OUT / name).write_bytes(get(raw))
            written[name] = module
    sources = "\n".join(
        f"{name}  ({module})" for name, module in sorted(written.items())
    )
    (OUT / "SOURCES").write_text(
        f"License texts of Qt {version} ({', '.join(MODULES)}), from each module's\n"
        f"LICENSES/ directory; written by tools/fetch_qt_licenses.py.\n\n{sources}\n"
    )
    print(
        f"wrote {len(written)} license texts of Qt {version} to {OUT.relative_to(REPO)}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
