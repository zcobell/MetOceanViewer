# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -DMOV_REPO=... -P run.cmake
#
# cmake/CheckNetcdfInclude.cmake must report every netCDF-C include outside
# io/netcdf/*.cpp in the src/ tree here (a header in io/netcdf/, a source
# elsewhere), must not report the one allowed source, and must fail on a
# missing directory.

set(scanner "${MOV_REPO}/cmake/CheckNetcdfInclude.cmake")

function(run_scanner root out_result out_output)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DMOV_SOURCE_ROOT=${root}" -P "${scanner}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
    )
    set(${out_result} "${result}" PARENT_SCOPE)
    set(${out_output} "${output}" PARENT_SCOPE)
endfunction()

set(failures "")

run_scanner("${CMAKE_CURRENT_LIST_DIR}/src" result output)
if(result EQUAL 0)
    list(APPEND failures "src/ with misplaced netCDF-C includes was accepted")
endif()
foreach(reported "io/netcdf/private.hpp" "io/reader.cpp")
    string(FIND "${output}" "${reported}" position)
    if(position EQUAL -1)
        list(APPEND failures "not reported: ${reported}")
    endif()
endforeach()
if(output MATCHES "allowed\\.cpp")
    list(APPEND failures "the allowed source was reported:\n${output}")
endif()

run_scanner("${CMAKE_CURRENT_LIST_DIR}/does-not-exist" result output)
if(result EQUAL 0 OR NOT output MATCHES "does not exist")
    list(APPEND failures "a missing directory was accepted")
endif()

if(failures)
    list(JOIN failures "\n" report)
    message(FATAL_ERROR "${report}")
endif()
