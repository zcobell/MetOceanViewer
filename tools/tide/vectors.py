# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
"""
Print the prototype's 2026 test vectors at full precision.

These are the values pinned in tests/core/tide (test_tide_constexpr.cpp,
elements at 2026-01-01; test_tide_astronomy.cpp, nodal terms at mid-2026
and (V0+u, f) of 12 constituents). Not run in CI; tools/tide/README.md.

Usage: python3 -I tools/tide/vectors.py
"""

import datetime as dt
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import proto_predict as pp

UTC = dt.UTC


def main() -> None:
    t0 = dt.datetime(2026, 1, 1, tzinfo=UTC)
    t1 = dt.datetime(2027, 1, 1, tzinfo=UTC)
    mid = t0 + (t1 - t0) / 2
    vt = pp.vterms(t0)
    print("elements at 2026-01-01:", " ".join(f"{x % 360:.10f}" for x in vt[:5]))
    print(f"N at 2026-01-01: {pp.node(t0) % 360:.10f}")
    u = pp.uterms(mid)
    print(
        f"mid N={pp.node(mid) % 360:.10f} I={u['I']:.10f} nu={u['nu']:.10f} "
        f"xi={((u['xi'] + 180) % 360) - 180:.10f} nup={u['nup']:.10f} "
        f"2nupp={u['nupp2']:.10f} P={u['P'] % 360:.10f} Q={u['Q']:.10f} "
        f"R={u['R']:.10f} Qu={((u['Qu'] + 180) % 360) - 180:.10f} "
        f"Qa={u['Qa']:.12f} Ra={u['Ra']:.12f}"
    )
    print("rates", " ".join(f"{x:.12f}" for x in pp.RATES))
    for name in [
        "M2",
        "S2",
        "N2",
        "K1",
        "O1",
        "M4",
        "MS4",
        "M1",
        "L2",
        "K2",
        "MSF",
        "2MK3",
    ]:
        speed, arg, f = pp.const(name, t0, mid)
        print(f"{name:4s} speed={speed:.10f} V0+u={arg % 360:.10f} f={f:.12f}")


if __name__ == "__main__":
    main()
