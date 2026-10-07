#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
"""
Check that each given file starts with the project's GPL-3.0 header.

The header is two comment lines in the file's own comment syntax:

    // SPDX-License-Identifier: GPL-3.0-or-later
    // Copyright (c) <year> Zach Cobell

'//' for C++ (including CMake-configured .hpp.in templates) and QML, '#' for
CMake, Python, shell, YAML, Dockerfiles and env files, ';' for INI files, and
'<!-- ... -->' for SVG. A shebang, a Dockerfile parser directive ('# syntax=...',
'# check=...') or an XML declaration may come first. pre-commit passes the
file names.
"""

import re
import sys
from pathlib import Path

CPP_SUFFIXES = {
    ".cpp", ".hpp", ".h", ".cc", ".cxx", ".hxx", ".ipp", ".inl", ".tpp", ".ixx", ".cppm",
}  # fmt: skip
SLASH_SUFFIXES = CPP_SUFFIXES | {".qml"}
HASH_SUFFIXES = {".cmake", ".py", ".sh", ".yml", ".yaml", ".env", ".txt"}
HASH_NAMES = {"CMakeLists.txt", "Dockerfile"}

PREAMBLE = re.compile(r"^(#!|# (syntax|check|escape)=|<\?xml )")
YEAR = r"\d{4}(-\d{4})?"


def comment_marker(path: Path) -> tuple[str, str] | None:
    """Return (opening, closing) comment markers for this file type, or None."""
    suffixes = path.suffixes
    if path.name in HASH_NAMES:
        return ("#", "")
    # foo.hpp.in is a C++ template; foo.cmake.in a CMake one.
    suffix = (
        suffixes[-2]
        if suffixes and suffixes[-1] == ".in" and len(suffixes) > 1
        else path.suffix
    )
    if suffix in SLASH_SUFFIXES:
        return ("//", "")
    if suffix in HASH_SUFFIXES:
        return ("#", "")
    if suffix == ".ini":
        return (";", "")
    if suffix == ".svg":
        return ("<!--", " -->")
    return None


def has_header(path: Path, marker: tuple[str, str]) -> bool:
    opening, closing = marker
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    while lines and PREAMBLE.match(lines[0]):
        lines = lines[1:]
    spdx = f"{opening} SPDX-License-Identifier: GPL-3.0-or-later{closing}"
    copyright_line = re.compile(
        rf"^{re.escape(opening)} Copyright \(c\) {YEAR} Zach Cobell{re.escape(closing)}$"
    )
    return (
        len(lines) >= 2
        and lines[0] == spdx
        and copyright_line.match(lines[1]) is not None
    )


def main(argv: list[str]) -> int:
    status = 0
    for name in argv:
        path = Path(name)
        marker = comment_marker(path)
        if marker is None:
            sys.stderr.write(f"{name}: no license-header rule for this file type\n")
            status = 1
        elif not has_header(path, marker):
            opening, closing = marker
            sys.stderr.write(
                f"{name}: missing license header; start the file with\n"
                f"  {opening} SPDX-License-Identifier: GPL-3.0-or-later{closing}\n"
                f"  {opening} Copyright (c) <year> Zach Cobell{closing}\n"
            )
            status = 1
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
