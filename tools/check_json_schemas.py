#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
"""
Schema conformance of the project's JSON formats.

The station catalog (docs/station-catalog.md 11):
  1. docs/schemas/station-catalog-v1.schema.json is a valid JSON Schema
     (draft 2020-12).
  2. Each fixture of tests/fixtures/io/station_catalog/cases.txt gets the
     schema verdict the manifest gives it: `valid` files validate, `invalid`
     ones do not, `any` is not asserted (a newer minor).
  3. The worked example of docs/station-catalog.md is the fixture
     worked_example.json byte for byte, so the example the spec shows is the
     one the C++ tests read and write back.

The series JSON (docs/series-json.md):
  4. docs/schemas/series-v1.schema.json is a valid JSON Schema, and every
     golden file of tests/fixtures/io/series_json/ validates.
  5. The worked examples of docs/series-json.md are table.json and daily.json.

And each FILE given with --catalog or --series validates (a file a producer
made).

Exit status 0 when everything passes, 1 otherwise, and 77 (a skip for ctest)
when the jsonschema package is missing, unless --require is given. The
format-compliance CI job runs it with the pinned jsonschema of
tools/compliance/requirements.txt; so does the json_schemas test in the dev
image.
"""

from __future__ import annotations

import argparse
import importlib
import json
import re
import sys
from pathlib import Path
from types import ModuleType
from typing import Any

SKIP = 77
REPO = Path(__file__).resolve().parent.parent
SCHEMAS = REPO / "docs" / "schemas"
FIXTURES = REPO / "tests" / "fixtures" / "io"
EXAMPLE_START = "<!-- worked-example:begin -->"


def load(path: Path) -> object:
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def catalog_cases() -> list[tuple[str, str]]:
    """(file, schema verdict) of each line of the catalog's cases.txt."""
    out = []
    manifest = FIXTURES / "station_catalog" / "cases.txt"
    for line in manifest.read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        words = line.split()
        out.append((words[0], words[2]))
    return out


def spec_examples(spec: Path) -> list[str]:
    """Return the ```json blocks after the worked-example marker of a spec."""
    text = spec.read_text(encoding="utf-8")
    start = text.find(EXAMPLE_START)
    if start < 0:
        return []
    return re.findall(r"```json\n(.*?)```", text[start:], re.DOTALL)


def validator_for(jsonschema: ModuleType, name: str) -> Any:  # noqa: ANN401
    """Return a draft 2020-12 validator of the schema, after checking it."""
    schema = load(SCHEMAS / name)
    jsonschema.Draft202012Validator.check_schema(schema)
    return jsonschema.Draft202012Validator(schema)


def first_error(validator: Any, path: Path) -> str | None:  # noqa: ANN401
    """Return the message of the file's first error, or None."""
    errors = sorted(validator.iter_errors(load(path)), key=lambda e: list(e.path))
    return errors[0].message if errors else None


def check_examples(spec: Path, fixtures: list[Path], failures: list[str]) -> None:
    examples = spec_examples(spec)
    if len(examples) != len(fixtures):
        failures.append(
            f"{spec.name}: {len(examples)} worked examples, expected {len(fixtures)}"
        )
        return
    for example, fixture in zip(examples, fixtures, strict=True):
        if example != fixture.read_text(encoding="utf-8"):
            failures.append(
                f"{spec.name}: a worked example differs from {fixture.name}"
            )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[1])
    parser.add_argument("--catalog", nargs="*", type=Path, default=[], help="catalogs")
    parser.add_argument(
        "--series", nargs="*", type=Path, default=[], help="series files"
    )
    parser.add_argument(
        "--require", action="store_true", help="fail, not skip, without jsonschema"
    )
    args = parser.parse_args()
    try:
        jsonschema = importlib.import_module("jsonschema")
    except ImportError:
        print("check_json_schemas: jsonschema is not installed", file=sys.stderr)
        return 1 if args.require else SKIP

    failures: list[str] = []

    catalog = validator_for(jsonschema, "station-catalog-v1.schema.json")
    for name, verdict in catalog_cases():
        error = first_error(catalog, FIXTURES / "station_catalog" / name)
        if verdict == "valid" and error:
            failures.append(f"{name}: expected valid, got: {error}")
        elif verdict == "invalid" and not error:
            failures.append(f"{name}: expected invalid, but it validates")
        elif verdict not in ("valid", "invalid", "any"):
            failures.append(f"{name}: unknown verdict {verdict!r} in cases.txt")
    check_examples(
        REPO / "docs" / "station-catalog.md",
        [FIXTURES / "station_catalog" / "worked_example.json"],
        failures,
    )

    series = validator_for(jsonschema, "series-v1.schema.json")
    goldens = sorted((FIXTURES / "series_json").glob("*.json"))
    if not goldens:
        failures.append("no series JSON golden files")
    for path in goldens:
        if error := first_error(series, path):
            failures.append(f"{path.name}: {error}")
    check_examples(
        REPO / "docs" / "series-json.md",
        [
            FIXTURES / "series_json" / "table.json",
            FIXTURES / "series_json" / "daily.json",
        ],
        failures,
    )

    for validator, paths in ((catalog, args.catalog), (series, args.series)):
        for path in paths:
            if error := first_error(validator, path):
                failures.append(f"{path}: {error}")

    for failure in failures:
        print(f"check_json_schemas: {failure}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
