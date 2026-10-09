# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -P check_opt_in.cmake
#
# The guard of the `live` label: fails unless MOV_LIVE_API=1, so a live run
# that forgot the opt-in (and whose tests therefore all skipped) is red.

if(NOT "$ENV{MOV_LIVE_API}" STREQUAL "1")
    message(
        FATAL_ERROR
        "The live tests ran without MOV_LIVE_API=1, so they skipped. Run them as "
        "MOV_LIVE_API=1 ctest --test-dir build/<preset> -L live"
    )
endif()
message(STATUS "MOV_LIVE_API=1: the live tests run")
