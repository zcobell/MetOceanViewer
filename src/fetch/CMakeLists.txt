# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# mov_fetch: the Qt-free orchestration of provider fetches
# (docs/providers-design.md §5): stages and units, merging, retry and origin
# policies, the sans-IO machine and the provider error type. Pure functions
# over values, no clock, threads or bytes on the wire. Header-only until P6
# adds its first source file.

mov_add_module(fetch PUBLIC_LINK mov::io)
