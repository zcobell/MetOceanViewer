#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
"""
Run lizard with the C++ alternative tokens counted as decision points.

lizard's C-family reader counts only '&&' and '||' toward cyclomatic
complexity, but this codebase spells logical operators 'and'/'or' (enforced by
readability-operators-representation in .clang-tidy), which would silently
deflate every CCN the pre-commit gate measures. Teach the reader the word
spellings, then delegate to lizard's own CLI.
"""

import sys

import lizard
from lizard_languages.clike import CLikeReader

CLikeReader._logical_operators = CLikeReader._logical_operators | {"and", "or"}

if __name__ == "__main__":
    sys.exit(lizard.main(sys.argv))
