# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -DMOV_REPO=... -P run.cmake
#
# cmake/CheckThirdPartyIncludes.cmake must report nlohmann/json outside
# io/json/ (a public io header, another layer) and zlib outside io/gzip.cpp
# (another io source, another layer, through #include_next), must not
# report the allowed sources or a commented-out include, must accept the
# clean tree and must fail on a missing directory.

set(scanner "${MOV_REPO}/cmake/CheckThirdPartyIncludes.cmake")

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
    list(APPEND failures "src/ with misplaced includes was accepted")
endif()
foreach(
    reported
    "src/io/include/mov/io/series_json.hpp: [nlohmann_json]"
    "src/io/json/zlib_elsewhere.cpp: [zlib]"
    "src/providers/catalog.cpp: [nlohmann_json] #include <nlohmann/json.hpp>"
    "src/providers/catalog.cpp: [zlib]"
    "src/providers/next.cpp: [zlib] #include_next <zlib.h>"
)
    string(FIND "${output}" "${reported}" position)
    if(position EQUAL -1)
        list(APPEND failures "not reported: ${reported}")
    endif()
endforeach()
foreach(silent "harmonics_json.cpp: [" "io/gzip.cpp: [" "in a comment")
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
message(STATUS "Third-party include gate: all cases behave")
