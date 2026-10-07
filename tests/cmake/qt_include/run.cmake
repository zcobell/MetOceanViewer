# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -DMOV_REPO=... -P run.cmake
#
# cmake/CheckQtFreeSources.cmake must reject every include in src/ and fail on
# a missing directory; the include-free clean/ directory must pass.

set(scanner "${MOV_REPO}/cmake/CheckQtFreeSources.cmake")

function(run_scanner dirs out_result out_output)
    execute_process(
        COMMAND "${CMAKE_COMMAND}" "-DMOV_SOURCE_DIRS=${dirs}" -P "${scanner}"
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
    list(APPEND failures "src/ with Qt includes was accepted")
endif()
foreach(
    header
    "<QString>"
    "<QtCore>"
    "<QtGlobal>"
    "<QtCore/qstring.h>"
    "\"qglobal.h\""
    "<private/qobject_p.h>"
    "<QtConcurrent/QtConcurrent>"
)
    string(FIND "${output}" "${header}" position)
    if(position EQUAL -1)
        list(APPEND failures "not reported: #include ${header}")
    endif()
endforeach()
if(output MATCHES "<string>|<vector>")
    list(APPEND failures "standard header reported as Qt:\n${output}")
endif()

run_scanner("${CMAKE_CURRENT_LIST_DIR}/clean" result output)
if(NOT result EQUAL 0)
    list(APPEND failures "clean/ was rejected:\n${output}")
endif()

run_scanner("${CMAKE_CURRENT_LIST_DIR}/does-not-exist" result output)
if(result EQUAL 0 OR NOT output MATCHES "does not exist")
    list(APPEND failures "a missing directory was accepted")
endif()

if(failures)
    list(JOIN failures "\n" report)
    message(FATAL_ERROR "${report}")
endif()
message(STATUS "Qt include scanner: all cases behave")
