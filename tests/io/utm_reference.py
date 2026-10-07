#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
"""
Reference values for tests/io/test_projection.cpp, computed without PROJ.

The Krueger series of the UTM projection (GRS80, the ellipsoid of EPSG:26915
NAD83 / UTM zone 15N, central meridian 93 W) and the closed form of spherical
Web Mercator (EPSG:3857, R = 6378137).

    python3 tests/io/utm_reference.py

prints the (lat, lon, easting, northing) rows pasted into the test. The sixth
order series is accurate to well below a millimetre inside a UTM zone.
"""

import math

A = 6378137.0
F = 1 / 298.257222101
K0 = 0.9996
FALSE_EASTING = 500000.0
CENTRAL_MERIDIAN = -93.0
WEB_MERCATOR_RADIUS = 6378137.0

N = F / (2 - F)
E = math.sqrt(F * (2 - F))
RECTIFYING_RADIUS = A / (1 + N) * (1 + N**2 / 4 + N**4 / 64 + N**6 / 256)
ALPHA = [
    N / 2
    - 2 * N**2 / 3
    + 5 * N**3 / 16
    + 41 * N**4 / 180
    - 127 * N**5 / 288
    + 7891 * N**6 / 37800,
    13 * N**2 / 48
    - 3 * N**3 / 5
    + 557 * N**4 / 1440
    + 281 * N**5 / 630
    - 1983433 * N**6 / 1935360,
    61 * N**3 / 240 - 103 * N**4 / 140 + 15061 * N**5 / 26880 + 167603 * N**6 / 181440,
    49561 * N**4 / 161280 - 179 * N**5 / 168 + 6601661 * N**6 / 7257600,
    34729 * N**5 / 80640 - 3418889 * N**6 / 1995840,
    212378941 * N**6 / 319334400,
]


def utm(lat: float, lon: float) -> tuple[float, float]:
    """Easting and northing in zone 15N of a WGS84/NAD83 position in degrees."""
    phi = math.radians(lat)
    lam = math.radians(lon - CENTRAL_MERIDIAN)
    t = math.sinh(math.atanh(math.sin(phi)) - E * math.atanh(E * math.sin(phi)))
    xi0 = math.atan2(t, math.cos(lam))
    eta0 = math.atanh(math.sin(lam) / math.sqrt(1 + t * t))
    xi = xi0 + sum(
        a * math.sin(2 * j * xi0) * math.cosh(2 * j * eta0)
        for j, a in enumerate(ALPHA, 1)
    )
    eta = eta0 + sum(
        a * math.cos(2 * j * xi0) * math.sinh(2 * j * eta0)
        for j, a in enumerate(ALPHA, 1)
    )
    return FALSE_EASTING + K0 * RECTIFYING_RADIUS * eta, K0 * RECTIFYING_RADIUS * xi


def web_mercator(lat: float, lon: float) -> tuple[float, float]:
    x = WEB_MERCATOR_RADIUS * math.radians(lon)
    y = WEB_MERCATOR_RADIUS * math.log(math.tan(math.pi / 4 + math.radians(lat) / 2))
    return x, y


def main() -> None:
    print("UTM 15N (lat, lon, easting, northing)")
    for lat, lon in [
        (29.98, -90.01),
        (29.0, -94.0),
        (45.0, -93.0),
        (0.5, -91.5),
        (60.0, -95.25),
    ]:
        print(f"  utm({lat}, {lon}, {utm(lat, lon)[0]!r}, {utm(lat, lon)[1]!r}),")
    print("Web Mercator (lat, lon, x, y)")
    for lat, lon in [(29.98, -90.01), (-45.5, 170.25)]:
        print(f"  {lat}, {lon}: {web_mercator(lat, lon)!r}")


if __name__ == "__main__":
    main()
