# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# mov_fetch: the Qt-free orchestration of provider fetches
# (docs/providers-design.md §5): errors and labels, the units of each
# provider's plan, merging, retry and origin policies, and the sans-IO
# machine. Pure functions over values, no clock, threads or bytes on the
# wire.

mov_add_module(
    fetch
    SOURCES coops_plan.cpp error.cpp ndbc_plan.cpp part.cpp retry_after.cpp usgs_plan.cpp
    PUBLIC_LINK mov::io
)
