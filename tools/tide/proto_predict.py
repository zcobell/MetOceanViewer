# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
"""
Prototype: SP98 (NOS-style) harmonic prediction vs NOAA CO-OPS predictions.

The design prototype of docs/harmonics-engine.md section 1, kept as the
reference for the C++ engine's test vectors (tools/tide/README.md). Not
run in CI.

Usage: python -I proto_predict.py <proto_dir>
Compares 'yearly' (V at Jan 1 00:00 UTC, f/u at mid-year: XTide/congen practice)
with 'instant' (V, f, u evaluated at each time).
"""

import datetime as dt
import json
import math
import os
import sys

D2R = math.pi / 180


def sind(x):
    return math.sin(x * D2R)


def cosd(x):
    return math.cos(x * D2R)


def tand(x):
    return math.tan(x * D2R)


def atan2d(y, x):
    return math.atan2(y, x) / D2R


def asind(x):
    return math.asin(x) / D2R


def acosd(x):
    return math.acos(x) / D2R


EPOCH = dt.datetime(1899, 12, 31, 12, tzinfo=dt.UTC)
SEC_PER_CENT = 36525 * 86400.0


def cent(t):
    return (t - EPOCH).total_seconds() / SEC_PER_CENT


# SP98 Table 1 polynomials (T, s, h, p, p1, c) and N
C0 = [
    0.0,
    270 + 26 / 60 + 14.72 / 3600,
    279 + 41 / 60 + 48.04 / 3600,
    334 + 19 / 60 + 40.87 / 3600,
    281 + 13 / 60 + 15 / 3600,
    1.0,
]
C1 = [
    36525 * 360.0,
    1336 * 360 + 1108411.2 / 3600,
    129602768.13 / 3600,
    11 * 360 + 392515.94 / 3600,
    6189.03 / 3600,
    0.0,
]
C2 = [0.0, 9.09 / 3600, 1.089 / 3600, -37.24 / 3600, 1.63 / 3600, 0.0]
C3 = [0.0, 0.0068 / 3600, 0.0, -0.045 / 3600, 0.012 / 3600, 0.0]
N0, N1, N2c, N3 = (
    259 + 10 / 60 + 57.12 / 3600,
    -(5 * 360 + 482912.63 / 3600),
    7.58 / 3600,
    0.008 / 3600,
)


def vterms(t):
    T = cent(t)
    return [C0[i] + C1[i] * T + C2[i] * T * T + C3[i] * T**3 for i in range(6)]


def vrates():  # deg/hour at 1900-01-01
    T = cent(dt.datetime(1900, 1, 1, tzinfo=dt.UTC))
    return [
        (C1[i] + 2 * C2[i] * T + 3 * C3[i] * T * T) / (36525 * 24) for i in range(6)
    ]


def node(t):
    T = cent(t)
    return N0 + N1 * T + N2c * T * T + N3 * T**3


OMEGA = 23 + 27 / 60 + 8.26 / 3600
INC = 5 + 8 / 60 + 43.3546 / 3600


def uterms(t):
    N = node(t)
    p = vterms(t)[3]
    cosI = cosd(OMEGA) * cosd(INC) - sind(OMEGA) * sind(INC) * cosd(N)
    sinI = math.sqrt(1 - cosI * cosI)
    I = acosd(cosI)
    sinnu = sind(INC) * sind(N) / sinI
    nu = asind(sinnu)
    cosnu = math.sqrt(1 - sinnu**2)
    sinOm = sind(OMEGA) * sind(N) / sinI
    cosOm = cosd(N) * cosnu + sind(N) * sinnu * cosd(OMEGA)
    xi = N - atan2d(sinOm, cosOm)
    m = sind(2 * I)
    nup = atan2d(m * sinnu, m * cosnu + 0.3347)
    s2 = sinI * sinI
    nupp2 = atan2d(s2 * sind(2 * nu), s2 * cosd(2 * nu) + 0.0727)
    P = p - xi
    Q = atan2d(0.483 * sind(P), cosd(P))
    Qu = P - Q
    Qa = 1 / math.sqrt(2.31 + 1.435 * cosd(2 * P))
    cot = 1 / tand(I / 2)
    R = atan2d(sind(2 * P), cot * cot / 6 - cosd(2 * P))
    tn = tand(I / 2) ** 2
    Ra = 1 / math.sqrt(1 - 12 * tn * cosd(2 * P) + 36 * tn * tn)
    return dict(
        xi=xi, nu=nu, nup=nup, nupp2=nupp2, Q=Q, R=R, Qu=Qu, I=I, P=P, Qa=Qa, Ra=Ra
    )


def ffun(k, U):
    I = U["I"]
    if k == 1:
        return 1.0
    if k == 73:
        return (2 / 3 - sind(I) ** 2) / 0.5021
    if k == 74:
        return sind(I) ** 2 / 0.1578
    if k == 75:
        return sind(I) * cosd(I / 2) ** 2 / 0.38
    if k == 76:
        return sind(2 * I) / 0.7214
    if k == 77:
        return sind(I) * sind(I / 2) ** 2 / 0.0164
    if k == 78:
        return cosd(I / 2) ** 4 / 0.9154
    if k == 79:
        return sind(I) ** 2 / 0.1565
    if k == 144:
        st = sind(I / 2) ** 2
        return (1 - 10 * st + 15 * st * st) * cosd(I / 2) ** 2 / 0.5873
    if k == 149:
        return cosd(I / 2) ** 6 / 0.8758
    if k == 206:
        return ffun(75, U) / U["Qa"]
    if k == 215:
        return ffun(78, U) / U["Ra"]
    if k == 227:
        return math.sqrt(
            0.8965 * sind(2 * I) ** 2 + 0.6001 * sind(2 * I) * cosd(U["nu"]) + 0.1006
        )
    if k == 235:
        return math.sqrt(
            19.0444 * sind(I) ** 4 + 2.7702 * sind(I) ** 2 * cosd(2 * U["nu"]) + 0.0981
        )
    raise ValueError(k)


# Darwin basics: V (T s h p p1 c), u (xi nu nup nupp2 Q R Qu), f formula
B = {
    "O1": ([1, -2, 1, 0, 0, 90], [2, -1, 0, 0, 0, 0, 0], 75),
    "K1": ([1, 0, 1, 0, 0, -90], [0, 0, -1, 0, 0, 0, 0], 227),
    "P1": ([1, 0, -1, 0, 0, 90], [0] * 7, 1),
    "M2": ([2, -2, 2, 0, 0, 0], [2, -2, 0, 0, 0, 0, 0], 78),
    "S2": ([2, 0, 0, 0, 0, 0], [0] * 7, 1),
    "N2": ([2, -3, 2, 1, 0, 0], [2, -2, 0, 0, 0, 0, 0], 78),
    "L2": ([2, -1, 2, -1, 0, 180], [2, -2, 0, 0, 0, -1, 0], 215),
    "K2": ([2, 0, 2, 0, 0, 0], [0, 0, 0, -1, 0, 0, 0], 235),
    "Q1": ([1, -3, 1, 1, 0, 90], [2, -1, 0, 0, 0, 0, 0], 75),
    "J1": ([1, 1, 1, -1, 0, -90], [0, -1, 0, 0, 0, 0, 0], 76),
    "M1": ([1, -1, 1, 0, 0, -90], [1, -1, 0, 0, 1, 0, 0], 206),
    "M3": ([3, -3, 3, 0, 0, 0], [3, -3, 0, 0, 0, 0, 0], 149),
    "2N2": ([2, -4, 2, 2, 0, 0], [2, -2, 0, 0, 0, 0, 0], 78),
    "OO1": ([1, 2, 1, 0, 0, -90], [-2, -1, 0, 0, 0, 0, 0], 77),
    "2Q1": ([1, -4, 1, 2, 0, 90], [2, -1, 0, 0, 0, 0, 0], 75),
    "R2": ([2, 0, 1, 0, -1, 180], [0] * 7, 1),
    "S1": ([1, 0, 0, 0, 0, 0], [0] * 7, 1),
    "T2": ([2, 0, -1, 0, 1, 0], [0] * 7, 1),
    "LDA2": ([2, -1, 0, 1, 0, 180], [2, -2, 0, 0, 0, 0, 0], 78),
    "MU2": ([2, -4, 4, 0, 0, 0], [2, -2, 0, 0, 0, 0, 0], 78),
    "NU2": ([2, -3, 4, -1, 0, 0], [2, -2, 0, 0, 0, 0, 0], 78),
    "RHO1": ([1, -3, 3, -1, 0, 90], [2, -1, 0, 0, 0, 0, 0], 75),
    "MF": ([0, 2, 0, 0, 0, 0], [-2, 0, 0, 0, 0, 0, 0], 74),
    "MM": ([0, 1, 0, -1, 0, 0], [0] * 7, 73),
    "SA": ([0, 0, 1, 0, 0, 0], [0] * 7, 1),
    "SSA": ([0, 0, 2, 0, 0, 0], [0] * 7, 1),
}
COMP = {
    "M4": {"M2": 2},
    "M6": {"M2": 3},
    "M8": {"M2": 4},
    "S4": {"S2": 2},
    "S6": {"S2": 3},
    "MK3": {"K1": 1, "M2": 1},
    "2MK3": {"K1": -1, "M2": 2},
    "MN4": {"M2": 1, "N2": 1},
    "MS4": {"M2": 1, "S2": 1},
    "2SM2": {"M2": -1, "S2": 2},
    "MSF": {"M2": -1, "S2": 1},
}
ALIAS = {"LAM2": "LDA2", "RHO": "RHO1"}
RATES = vrates()


def basic(name, t_v, t_u):
    V, uc, fk = B[name]
    vt = vterms(t_v)
    U = uterms(t_u)
    ut = [U["xi"], U["nu"], U["nup"], U["nupp2"], U["Q"], U["R"], U["Qu"]]
    speed = sum(r * c for r, c in zip(RATES, V)) + RATES[3] * uc[4]
    arg = sum(a * c for a, c in zip(vt, V)) + sum(a * c for a, c in zip(ut, uc))
    return speed, arg, ffun(fk, U)


def const(name, t_v, t_u):
    name = ALIAS.get(name, name)
    if name in B:
        return basic(name, t_v, t_u)
    s = a = 0.0
    f = 1.0
    for b, c in COMP[name].items():
        sb, ab, fb = basic(b, t_v, t_u)
        s += c * sb
        a += c * ab
        f *= fb ** abs(c)
    return s, a, f


def predict(cons, t, mode):
    y = t.year
    t0 = dt.datetime(y, 1, 1, tzinfo=dt.UTC)
    t1 = dt.datetime(y + 1, 1, 1, tzinfo=dt.UTC)
    mid = t0 + (t1 - t0) / 2
    h = 0.0
    for name, H, G in cons:
        if mode == "yearly":
            sp, arg, f = const(name, t0, mid)
            hours = (t - t0).total_seconds() / 3600
            h += f * H * cosd(sp * hours + arg - G)
        else:
            sp, arg, f = const(name, t, t)
            h += f * H * cosd(arg - G)
    return h


def main(d):
    for st in ["8443970", "9414290", "8761724", "8720030", "9455920"]:
        hc = json.load(open(os.path.join(d, f"harcon_{st}.json")))[
            "HarmonicConstituents"
        ]
        cons, skipped = [], []
        for c in hc:
            n = ALIAS.get(c["name"], c["name"])
            if n in B or n in COMP:
                cons.append((c["name"], c["amplitude"], c["phase_GMT"]))
            elif c["amplitude"] != 0:
                skipped.append(c["name"])
        for tag in ["20251230", "20260629", "20261001"]:
            p = json.load(open(os.path.join(d, f"pred_{st}_{tag}.json")))["predictions"]
            pts = [
                (
                    dt.datetime.strptime(x["t"], "%Y-%m-%d %H:%M").replace(
                        tzinfo=dt.UTC
                    ),
                    float(x["v"]),
                )
                for x in p
            ][::5]
            for mode in ["yearly", "instant"]:
                res = [v - predict(cons, t, mode) for t, v in pts]
                mean = sum(res) / len(res)
                mx = max(abs(r - mean) for r in res)
                rms = math.sqrt(sum((r - mean) ** 2 for r in res) / len(res))
                print(
                    f"{st} {tag} {mode:7s} n={len(res)} bias={mean * 1000:7.1f}mm rms={rms * 1000:6.2f}mm max={mx * 1000:6.2f}mm skipped={skipped}"
                )


if __name__ == "__main__":
    main(sys.argv[1])
