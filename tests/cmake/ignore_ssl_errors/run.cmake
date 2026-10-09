# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -DMOV_REPO=... -P run.cmake
#
# cmake/CheckIgnoreSslErrors.cmake must report both ways of turning TLS
# certificate checks off, in src/ and in tests/ (the test driver included),
# must not report comments or the tests/cmake fixtures, must accept the
# clean tree and must fail on a missing directory.

set(scanner "${MOV_REPO}/cmake/CheckIgnoreSslErrors.cmake")

function(run_scanner root out_result out_output)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DMOV_REPO=${root}" -P "${scanner}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
    )
    set(${out_result} "${result}" PARENT_SCOPE)
    set(${out_output} "${output}" PARENT_SCOPE)
endfunction()

set(failures "")

run_scanner("${CMAKE_CURRENT_LIST_DIR}" result output)
if(result EQUAL 0)
    list(APPEND failures "a tree that turns certificate checks off was accepted")
endif()
foreach(
    reported
    "src/providers/transport.cpp: [ignore_ssl_errors]"
    "src/providers/transport.cpp: [verify_none]"
    "tests/support/qt_drive.hpp: [ignore_ssl_errors]"
)
    string(FIND "${output}" "${reported}" position)
    if(position EQUAL -1)
        list(APPEND failures "not reported: ${reported}")
    endif()
endforeach()
foreach(silent "comments.cpp: [" "tests/cmake/")
    string(FIND "${output}" "${silent}" position)
    if(NOT position EQUAL -1)
        list(APPEND failures "reported, but allowed: ${silent}")
    endif()
endforeach()
if(failures)
    list(APPEND failures "scanner output:\n${output}")
endif()

run_scanner("${CMAKE_CURRENT_LIST_DIR}/clean" result output)
if(NOT result EQUAL 0)
    list(APPEND failures "the clean tree was rejected:\n${output}")
endif()

run_scanner("${CMAKE_CURRENT_LIST_DIR}/does-not-exist" result output)
if(result EQUAL 0 OR NOT output MATCHES "does not exist")
    list(APPEND failures "a missing directory was accepted")
endif()

if(failures)
    list(JOIN failures "\n" report)
    message(FATAL_ERROR "${report}")
endif()
message(STATUS "TLS gate: all cases behave")
