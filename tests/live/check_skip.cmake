# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -DEXECUTABLE=<live test> [-DEMULATOR=<launcher>] -P check_skip.cmake
#
# Runs the live executable's plumbing test case as its live registration
# runs it (--warn NoAssertions), without MOV_LIVE_API: it must skip, that is
# exit with Catch2's code 4 (every test case skipped, the SKIP_RETURN_CODE
# of mov_add_test(... LIVE ...)) and print the reason.

if(NOT EXECUTABLE)
    message(FATAL_ERROR "EXECUTABLE is not set")
endif()
unset(ENV{MOV_LIVE_API})
execute_process(
    COMMAND ${EMULATOR} "${EXECUTABLE}" "[live-plumbing]" --warn NoAssertions
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output
)
if(NOT result EQUAL 4)
    message(FATAL_ERROR "expected exit code 4 (skipped), got '${result}':\n${output}")
endif()
if(NOT output MATCHES "set MOV_LIVE_API=1 to run it")
    message(FATAL_ERROR "the skip does not say how to opt in:\n${output}")
endif()
message(STATUS "Without MOV_LIVE_API the live test skips")
