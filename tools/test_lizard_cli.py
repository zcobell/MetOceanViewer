#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
"""
Self-test for tools/lizard_cli.py.

'and'/'or' must count toward cyclomatic complexity exactly like '&&'/'||', or
the pre-commit CCN gate under-counts.

    python tools/test_lizard_cli.py
"""

import runpy
import sys
import unittest
from pathlib import Path

import lizard

# Apply the patch the CLI wrapper applies, without running its main().
runpy.run_path(str(Path(__file__).with_name("lizard_cli.py")), run_name="lizard_cli")

SYMBOLS = "bool f(bool a, bool b, bool c) { return (a && b) || c; }\n"
WORDS = "bool f(bool a, bool b, bool c) { return (a and b) or c; }\n"


def ccn(code: str) -> int:
    analysis = lizard.analyze_file.analyze_source_code("probe.cpp", code)
    (function,) = analysis.function_list
    return function.cyclomatic_complexity


class LizardCliTest(unittest.TestCase):
    def test_word_operators_count_like_symbols(self) -> None:
        self.assertEqual(ccn(SYMBOLS), 3)
        self.assertEqual(ccn(WORDS), ccn(SYMBOLS))


if __name__ == "__main__":
    sys.exit(
        unittest.main(argv=sys.argv[:1], exit=False).result.wasSuccessful() is False
    )
