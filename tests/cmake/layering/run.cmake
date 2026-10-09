# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -DMOV_REPO=... -DWORK_DIR=... -DGENERATOR=... -DCXX_COMPILER=... -P run.cmake
#
# Configures the fixture project once per CASE: "clean" must succeed, every
# other case must fail with a layering violation.

set(violating_cases
    genex_link
    versionless_target
    link_flag
    bare_library_name
    library_path
    include_dir
    direct_link
    transitive
    upward_link
    fetch_links_qt
    io_links_fetch
    fetch_links_providers
)
set(failures "")
foreach(case IN ITEMS clean ${violating_cases})
    set(build_dir "${WORK_DIR}/${case}")
    file(REMOVE_RECURSE "${build_dir}")
    execute_process(
        COMMAND
            "${CMAKE_COMMAND}" -S "${CMAKE_CURRENT_LIST_DIR}" -B "${build_dir}" -G "${GENERATOR}"
            "-DCMAKE_CXX_COMPILER=${CXX_COMPILER}" "-DMOV_REPO=${MOV_REPO}" "-DCASE=${case}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
    )
    if(case STREQUAL "clean")
        if(NOT result EQUAL 0)
            list(APPEND failures "clean: configure failed:\n${output}")
        endif()
    elseif(result EQUAL 0 OR NOT output MATCHES "Layering violation")
        list(APPEND failures "${case}: not rejected:\n${output}")
    else()
        string(REGEX MATCH "  mov_[a-z]+ -> [^\n]*" chain "${output}")
        message(STATUS "${case}: rejected (${chain})")
    endif()
endforeach()

if(failures)
    list(JOIN failures "\n" report)
    message(FATAL_ERROR "${report}")
endif()
