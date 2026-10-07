# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -D MOV_COVERAGE_DIR=<build dir> -P ZeroCoverageCounters.cmake
# Deletes the .gcda counters left by earlier runs so a report reflects one run.

file(GLOB_RECURSE counters "${MOV_COVERAGE_DIR}/*.gcda")
if(counters)
    file(REMOVE ${counters})
endif()
