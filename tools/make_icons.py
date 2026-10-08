#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
"""
Render the packaging images from their SVG sources.

    tools/dev/run.sh python3 tools/make_icons.py          # write them
    tools/dev/run.sh python3 tools/make_icons.py --check  # verify they are current

Sources and outputs (the outputs are committed; rerun after editing a source):

    src/ui/qml/images/app-icon.svg   -> packaging/icons/metoceanviewer.icns  (macOS)
                                        packaging/icons/metoceanviewer.ico   (Windows)
                                        packaging/icons/hicolor/<n>x<n>.png  (Linux)
    packaging/macos/dmg-background.svg -> packaging/macos/dmg-background.png

rsvg-convert (librsvg) rasterizes; this script assembles the ICNS and ICO
files itself, so the output depends only on librsvg's version. The dev image
and CI's pre-commit job both take it from Ubuntu 24.04 (2.58), where the
icons hook of .pre-commit-config.yaml runs this with --check; another version
may render differently and fail the check.
"""

import argparse
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
APP_ICON = REPO / "src/ui/qml/images/app-icon.svg"
ICONS = REPO / "packaging/icons"
DMG_BACKGROUND = REPO / "packaging/macos/dmg-background.svg"

# The freedesktop hicolor sizes the AppImage installs.
HICOLOR_SIZES = (16, 32, 48, 64, 128, 256, 512)
# Windows picks the nearest of these; 256 is the largest .ico supports.
ICO_SIZES = (16, 24, 32, 48, 64, 128, 256)
# The PNG-carrying ICNS types iconutil writes, as (type, pixel size).
ICNS_TYPES = (
    (b"icp4", 16),
    (b"ic11", 32),  # 16@2x
    (b"icp5", 32),
    (b"ic12", 64),  # 32@2x
    (b"ic07", 128),
    (b"ic13", 256),  # 128@2x
    (b"ic08", 256),
    (b"ic14", 512),  # 256@2x
    (b"ic09", 512),
    (b"ic10", 1024),  # 512@2x
)


def render(svg: Path, width: int, height: int, scratch: Path) -> bytes:
    """Rasterize svg to a width x height PNG and return its bytes."""
    out = scratch / f"{svg.stem}-{width}x{height}.png"
    subprocess.run(
        ["rsvg-convert", "--width", str(width), "--height", str(height),
         "--output", str(out), str(svg)],
        check=True,
    )  # fmt: skip
    return out.read_bytes()


def icns(pngs: dict[int, bytes]) -> bytes:
    """Build an Apple icon image: 'icns', total length, (type, length, PNG) entries."""
    body = b"".join(
        kind + struct.pack(">I", 8 + len(pngs[size])) + pngs[size]
        for kind, size in ICNS_TYPES
    )
    return b"icns" + struct.pack(">I", 8 + len(body)) + body


def ico(pngs: dict[int, bytes]) -> bytes:
    """Build a Windows icon of PNG-compressed images (Windows Vista and later)."""
    header = struct.pack("<HHH", 0, 1, len(ICO_SIZES))
    offset = len(header) + 16 * len(ICO_SIZES)
    entries = b""
    for size in ICO_SIZES:
        data = pngs[size]
        dim = 0 if size == 256 else size  # 0 means 256
        entries += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(data), offset)
        offset += len(data)
    return header + entries + b"".join(pngs[size] for size in ICO_SIZES)


def outputs(scratch: Path) -> dict[Path, bytes]:
    sizes = sorted(
        set(HICOLOR_SIZES) | set(ICO_SIZES) | {size for _, size in ICNS_TYPES}
    )
    pngs = {size: render(APP_ICON, size, size, scratch) for size in sizes}
    files = {
        ICONS / "metoceanviewer.icns": icns(pngs),
        ICONS / "metoceanviewer.ico": ico(pngs),
        DMG_BACKGROUND.with_suffix(".png"): render(DMG_BACKGROUND, 660, 400, scratch),
    }
    for size in HICOLOR_SIZES:
        files[ICONS / "hicolor" / f"{size}x{size}.png"] = pngs[size]
    return files


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument(
        "--check", action="store_true", help="fail if a committed output is stale"
    )
    args = parser.parse_args()
    with tempfile.TemporaryDirectory() as tmp:
        files = outputs(Path(tmp))
    stale = []
    for path, data in files.items():
        if path.exists() and path.read_bytes() == data:
            continue
        if args.check:
            stale.append(path.relative_to(REPO))
            continue
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        print(f"wrote {path.relative_to(REPO)} ({len(data)} bytes)")
    if stale:
        print("stale (run tools/make_icons.py):", *stale, sep="\n  ", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
