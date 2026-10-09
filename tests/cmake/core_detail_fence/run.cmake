# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -DMOV_REPO=... -P run.cmake
#
# cmake/CheckCoreDetailFence.cmake must report every use of mov::core::detail
# outside the core in the tree here (an include in src/io, a name in tests/io),
# must not report the core's own sources and tests, and must fail on a missing
# directory.

set(scanner "${MOV_REPO}/cmake/CheckCoreDetailFence.cmake")

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
    list(APPEND failures "a tree that uses mov::core::detail outside the core was accepted")
endif()
foreach(reported "src/io/include_detail.cpp" "tests/io/name_detail.cpp")
    string(FIND "${output}" "${reported}" position)
    if(position EQUAL -1)
        list(APPEND failures "not reported: ${reported}")
    endif()
endforeach()
if(output MATCHES "allowed_")
    list(APPEND failures "the core's own use was reported:\n${output}")
endif()

run_scanner("${CMAKE_CURRENT_LIST_DIR}/does-not-exist" result output)
if(result EQUAL 0 OR NOT output MATCHES "does not exist")
    list(APPEND failures "a missing directory was accepted")
endif()

if(failures)
    list(JOIN failures "\n" report)
    message(FATAL_ERROR "${report}")
endif()
