# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
"""
Expected HWM statistics for the F8 fixtures, computed independently of C++.

Standard library only. Every sum is exact: the CSV text is read as
fractions.Fraction, so no rounding happens until a result is printed. The C++
core uses floating point (Welford/Chan moments), so agreement within 1e-12 is
evidence for both.

    python3 golden.py                  print golden.txt
    python3 golden.py --check FILE     exit 1 if FILE differs from the output

The output is committed as golden.txt next to the fixtures; the CTest test
`hwm_golden_is_current` runs --check, so the file cannot drift from this script.

Rules mirrored from the design (docs/core-design.md section 3):
  - wet iff the raw modeled value is > -999 (the file's own unit, N2);
  - x = observed, y = modeled, e = y - x, all converted to metres;
  - free fit: ordinary least squares, R^2 = Pearson r^2;
  - through-origin fit: slope = sum(xy)/sum(x^2),
    R^2 = 1 - sum((y - slope x)^2)/sum(y^2) (UNCENTRED, decision D25);
  - sigma of e divides by n - 1 (decision D17);
  - error classes: the default breaks of the fixture's unit; an error on a
    break goes to the upper class.

Output lines are `<fixture> <key> <value>...`; floats are repr() (shortest
round-trip), `none` is an absent value. Category codes: 0 dry, 1..8 = bin0..bin7.
"""

import argparse
import csv
import math
import statistics
import sys
from fractions import Fraction
from pathlib import Path

if sys.version_info < (3, 11):  # noqa: UP036 - explicit message for direct runs
    sys.exit(
        "golden.py needs Python 3.11 (statistics.linear_regression(proportional=))"
    )

DRY_THRESHOLD = Fraction(-999)
METRES_PER_FOOT = Fraction(3048, 10000)
# Exact decimals, not floats: a tie must be a tie.
METRE_BREAKS = [Fraction(b) for b in ("-1.5", "-1", "-0.5", "0", "0.5", "1", "1.5")]
FOOT_BREAKS = [Fraction(b) for b in ("-5", "-3.5", "-1.5", "0", "1.5", "3.5", "5")]

# fixture name -> (scale from the file's unit to metres, default class breaks
# in the file's unit)
FIXTURES: dict[str, tuple[Fraction, list[Fraction]]] = {
    "hwm_basic.csv": (Fraction(1), METRE_BREAKS),
    "hwm_ft.csv": (METRES_PER_FOOT, FOOT_BREAKS),
    "hwm_allDry.csv": (Fraction(1), METRE_BREAKS),
    "hwm_one_wet.csv": (Fraction(1), METRE_BREAKS),
    "hwm_ties.csv": (Fraction(1), METRE_BREAKS),
    "hwm_ties_ft.csv": (METRES_PER_FOOT, FOOT_BREAKS),
}

Exact = list[Fraction]


def read_rows(path: Path) -> list[tuple[Fraction, Fraction]]:
    """Return (observed, modeled) as exact fractions in the file's unit."""
    with path.open(newline="") as handle:
        return [(Fraction(r[3]), Fraction(r[4])) for r in csv.reader(handle) if r]


def category(error: Fraction | None, breaks: list[Fraction]) -> int:
    """Return 0 for dry (error is None), else 1 + the number of breaks <= error."""
    if error is None:
        return 0
    return 1 + sum(1 for b in breaks if b <= error)


def free_fit(
    xs: Exact, ys: Exact
) -> tuple[str, Fraction | None, Fraction | None, Fraction | None]:
    """Return (status, slope, intercept, r2) of the ordinary least-squares line."""
    n = len(xs)
    if n < 2:
        return "too_few_for_free_fit", None, None, None
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    if sxx == 0:
        return "degenerate_observed", None, None, None
    syy = sum((y - my) ** 2 for y in ys)
    sxy = sum((x - mx) * (y - my) for x, y in zip(xs, ys, strict=True))
    slope = sxy / sxx
    # With two points the line passes through both: no degrees of freedom, no R^2.
    r2 = None if syy == 0 or n <= 2 else sxy**2 / (sxx * syy)
    return "ok", slope, my - slope * mx, r2


def origin_fit(xs: Exact, ys: Exact) -> tuple[str, Fraction | None, Fraction | None]:
    """Return (status, slope, r2) of the least-squares line through the origin."""
    sxx = sum(x * x for x in xs)
    if sxx == 0:
        return "degenerate_observed", None, None
    slope = sum(x * y for x, y in zip(xs, ys, strict=True)) / sxx
    syy = sum(y * y for y in ys)
    # Uncentred R^2: the residual sum is taken directly, not from the sums.
    residual = sum((y - slope * x) ** 2 for x, y in zip(xs, ys, strict=True))
    # One point fits exactly: no degrees of freedom, no R^2.
    no_r2 = syy == 0 or len(xs) <= 1
    return "ok", slope, None if no_r2 else 1 - residual / syy


def cross_check_origin(xs: Exact, ys: Exact, slope: Fraction) -> None:
    """
    Exit unless the standard library's proportional fit agrees with slope.

    The library needs two points; a one-point fit is slope = y/x by inspection.
    """
    if len(xs) < 2:
        return
    library = statistics.linear_regression(
        [float(x) for x in xs], [float(y) for y in ys], proportional=True
    )
    if not math.isclose(library.slope, float(slope), rel_tol=1e-12):
        sys.exit(f"origin slope {float(slope)!r} != statistics {library.slope!r}")


def close(a: float, b: float) -> bool:
    """Return whether two doubles agree to 1e-9 (the library works in floats)."""
    return math.isclose(a, b, rel_tol=1e-9, abs_tol=1e-12)


def cross_check_free(
    xs: Exact, ys: Exact, slope: Fraction, r2: Fraction | None
) -> None:
    """Exit unless statistics.linear_regression and .correlation agree."""
    fx, fy = [float(x) for x in xs], [float(y) for y in ys]
    library = statistics.linear_regression(fx, fy)
    if not close(library.slope, float(slope)):
        sys.exit(f"free slope {float(slope)!r} != statistics {library.slope!r}")
    if r2 is not None and not close(statistics.correlation(fx, fy) ** 2, float(r2)):
        sys.exit(f"free r2 {float(r2)!r} != statistics correlation squared")


def cross_check_stddev(errors: Exact, stddev: float) -> None:
    """Exit unless statistics.stdev (the n - 1 form) agrees."""
    if not close(statistics.stdev([float(e) for e in errors]), stddev):
        sys.exit(f"stddev {stddev!r} != statistics.stdev")


def fmt(value: Fraction | float | None) -> str:
    """Format an exact value as its nearest double, or `none`."""
    return "none" if value is None else repr(float(value))


def stats_lines(
    name: str,
    scale: Fraction,
    breaks: list[Fraction],
    rows: list[tuple[Fraction, Fraction]],
) -> list[str]:
    """Return the golden lines of one fixture."""
    out: list[str] = []

    def emit(key: str, *values: str) -> None:
        out.append(f"{name} {key} {' '.join(values)}")

    wet = [(x * scale, y * scale) for x, y in rows if y > DRY_THRESHOLD]
    xs = [x for x, _ in wet]
    ys = [y for _, y in wet]
    emit("total", str(len(rows)))
    emit("wet", str(len(wet)))
    # Classified in the file's unit, where the breaks are exact decimals.
    codes = [category(None if y <= DRY_THRESHOLD else y - x, breaks) for x, y in rows]
    emit("categories", *map(str, codes))

    if not wet:
        emit("free.status", "no_wet_marks")
        emit("origin.status", "no_wet_marks")
        return out

    errors = [y - x for x, y in wet]
    mean_error = sum(errors) / len(errors)
    emit("mean_error", fmt(mean_error))
    if len(errors) < 2:
        emit("error_stddev", "none")
    else:
        variance = sum((e - mean_error) ** 2 for e in errors) / (len(errors) - 1)
        cross_check_stddev(errors, math.sqrt(variance))
        emit("error_stddev", fmt(math.sqrt(variance)))

    free_status, slope, intercept, r2 = free_fit(xs, ys)
    emit("free.status", free_status)
    if free_status == "ok" and slope is not None:
        cross_check_free(xs, ys, slope, r2)
        emit("free.slope", fmt(slope))
        emit("free.intercept", fmt(intercept))
        emit("free.r2", fmt(r2))

    origin_status, origin_slope, origin_r2 = origin_fit(xs, ys)
    emit("origin.status", origin_status)
    if origin_status == "ok" and origin_slope is not None:
        cross_check_origin(xs, ys, origin_slope)
        emit("origin.slope", fmt(origin_slope))
        emit("origin.r2", fmt(origin_r2))
    return out


def generate(directory: Path) -> str:
    """Return the full golden text for the fixtures in directory."""
    lines: list[str] = []
    for name, (scale, breaks) in FIXTURES.items():
        lines += stats_lines(name, scale, breaks, read_rows(directory / name))
    return "\n".join(lines) + "\n"


def main() -> int:
    """Print the golden text, or check a committed copy of it."""
    parser = argparse.ArgumentParser(description="Expected HWM statistics (F8).")
    parser.add_argument("--check", metavar="FILE", type=Path)
    args = parser.parse_args()
    text = generate(Path(__file__).resolve().parent)
    if args.check is None:
        sys.stdout.write(text)
        return 0
    if args.check.read_text() != text:
        sys.stderr.write(f"{args.check} is out of date: rerun golden.py\n")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
