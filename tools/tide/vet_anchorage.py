# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
"""
One-off vetting of the catalogue's readings of NOAA names at 9455920.

Compares the prototype, with every Basic and Compound line of libcongen's
congen_input.txt, against CO-OPS 6-minute predictions at Anchorage, for
alias choices (docs/harmonics-engine.md section 1.1; the RP1, KJ2 and 3KM5
notes in src/core/include/mov/core/tide/catalogue.hpp). Then flips each
constituent by 180 degrees in turn to look for further phase conventions.
Not run in CI; tools/tide/README.md says where the inputs come from.

Usage: python3 -I tools/tide/vet_anchorage.py CONGEN_INPUT PROTO_DIR [STRIDE]
PROTO_DIR holds harcon_9455920.json and pred_9455920_{20251230,20260629,
20261001}.json from the CO-OPS APIs.
"""

import datetime as dt
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import proto_predict as pp

UTC = dt.UTC
BASES = [
    "O1",
    "K1",
    "P1",
    "M2",
    "S2",
    "N2",
    "L2",
    "K2",
    "Q1",
    "NU2",
    "S1",
    "M1-DUTCH",
    "LDA2",
]
NOAA_TO_CONGEN = {
    "SIGMA1": "SIG1",
    "THETA1": "THE1",
    "OO2": "OQ2-HORN",
    "2MS8": "2(MS)8",
    "2MN8": "2(MN)8",
    "2MLNS6": "2MNLS6",
    "M2KS2": "M2(KS)2",
    "2SNMK2": "2SN(MK)2",
    "2KMSN2": "2KM(SN)2",
}


def load_congen(path: str) -> None:
    pp.B["M1-DUTCH"] = ([1, -1, 1, 1, 0, -90], [0, -1, 0, 0, 0, 0, -1], 206)
    with open(path, encoding="latin-1") as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            fields = line.split()
            if len(fields) < 2:
                continue
            if fields[1] == "Basic" and fields[0] not in pp.B:
                n = [int(x) for x in fields[2:]]
                pp.B[fields[0]] = (n[0:6], [*n[6:12], 0], n[12])
            elif (
                fields[1] == "Compound"
                and fields[0] not in pp.COMP
                and fields[0] not in pp.B
            ):
                cs = [int(x) for x in fields[2:]]
                pp.COMP[fields[0]] = {BASES[i]: c for i, c in enumerate(cs) if c}
    pp.COMP["3KM5-OLD"] = {"K1": 3, "M2": 1}


def load_station(
    d: str, stride: int
) -> tuple[list[dict], list[tuple[dt.datetime, float]]]:
    with open(os.path.join(d, "harcon_9455920.json")) as f:
        hc = json.load(f)["HarmonicConstituents"]
    series = []
    for tag in ["20251230", "20260629", "20261001"]:
        with open(os.path.join(d, f"pred_9455920_{tag}.json")) as f:
            p = json.load(f)["predictions"]
        series += [
            (
                dt.datetime.strptime(x["t"], "%Y-%m-%d %H:%M").replace(tzinfo=UTC),
                float(x["v"]),
            )
            for x in p
        ]
    return hc, series[::stride]


def rows(
    hc: list[dict], alias: dict[str, str]
) -> tuple[list[tuple[str, float, float]], list[str]]:
    out, skipped = [], []
    for c in hc:
        if c["amplitude"] == 0:
            continue
        n = alias.get(c["name"], pp.ALIAS.get(c["name"], c["name"]))
        if n in pp.B or n in pp.COMP:
            out.append((n, c["amplitude"], c["phase_GMT"]))
        else:
            skipped.append(c["name"])
    return out, skipped


def contributions(
    cons: list[tuple[str, float, float]], series: list[tuple[dt.datetime, float]]
) -> tuple[dict[str, list[float]], list[float]]:
    """Per-constituent heights and the residual, yearly scheme."""
    per = {n: [] for n, _, _ in cons}
    residual = []
    cache = {}
    for t, v in series:
        if t.year not in cache:
            t0 = dt.datetime(t.year, 1, 1, tzinfo=UTC)
            t1 = dt.datetime(t.year + 1, 1, 1, tzinfo=UTC)
            cache[t.year] = (
                t0,
                [(n, pp.const(n, t0, t0 + (t1 - t0) / 2), h, g) for n, h, g in cons],
            )
        t0, rr = cache[t.year]
        hours = (t - t0).total_seconds() / 3600
        total = 0.0
        for n, (speed, arg, f), h, g in rr:
            c = f * h * math.cos(math.radians(speed * hours + arg - g))
            per[n].append(c)
            total += c
        residual.append(v - total)
    return per, residual


def report(
    label: str,
    hc: list[dict],
    series: list[tuple[dt.datetime, float]],
    alias: dict[str, str],
) -> None:
    cons, skipped = rows(hc, alias)
    _, res = contributions(cons, series)
    mean = sum(res) / len(res)
    peak = max(abs(r - mean) for r in res)
    raw = max(abs(r) for r in res)
    rms = math.sqrt(sum((r - mean) ** 2 for r in res) / len(res))
    print(
        f"{label:36s} n={len(res)} used={len(cons)} skipped={skipped} bias={mean * 1000:6.2f} "
        f"mean-removed max={peak * 1000:6.2f} raw max={raw * 1000:6.2f} rms={rms * 1000:5.2f} mm"
    )


def main() -> None:
    load_congen(sys.argv[1])
    hc, series = load_station(sys.argv[2], int(sys.argv[3]) if len(sys.argv) > 3 else 1)
    undefined = {"M2KS2": "x", "2SNMK2": "x", "2KMSN2": "x"}
    variants = {
        "design (RP1->PSI1, 3 undefined)": {
            **NOAA_TO_CONGEN,
            "RP1": "PSI1",
            **undefined,
        },
        "+ parenthesised aliases": {**NOAA_TO_CONGEN, "RP1": "PSI1"},
        "+ KJ2->KJ2-IHO (the catalogue)": {
            **NOAA_TO_CONGEN,
            "RP1": "PSI1",
            "KJ2": "KJ2-IHO",
        },
        "RP1 as congen RP1": {**NOAA_TO_CONGEN, "KJ2": "KJ2-IHO"},
        "3KM5 as 3 K1 + M2": {
            **NOAA_TO_CONGEN,
            "RP1": "PSI1",
            "KJ2": "KJ2-IHO",
            "3KM5": "3KM5-OLD",
        },
    }
    for label, alias in variants.items():
        report(label, hc, series, alias)

    # Flip each constituent of the catalogue's reading by 180 degrees.
    cons, _ = rows(hc, variants["+ KJ2->KJ2-IHO (the catalogue)"])
    per, res = contributions(cons, series)

    def rms(x: list[float]) -> float:
        return math.sqrt(sum(a * a for a in x) / len(x))

    base = rms(res)
    changes = sorted(
        (
            rms([r + 2 * c for r, c in zip(res, per[n], strict=True)]) - base,
            n,
            max(abs(c) for c in per[n]),
        )
        for n in per
    )
    for change, n, amplitude in changes[:8]:
        print(
            f"flip {n:10s} amp {amplitude * 1000:6.1f} mm  rms change {change * 1000:+.2f} mm"
        )


if __name__ == "__main__":
    main()
