# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# gcov-based coverage for GCC and Clang, reported with gcovr. Only src/ is
# measured: tests, generated files and vcpkg dependencies are not.

function(mov_enable_coverage target)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR "Coverage requires GCC or Clang, not ${CMAKE_CXX_COMPILER_ID}")
    endif()
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        message(WARNING "Coverage is most accurate in a Debug build (current: ${CMAKE_BUILD_TYPE})")
    endif()
    # Atomic counter updates: the default (-fprofile-update=single, without
    # -pthread) loses increments when threads run instrumented code at once
    # (tests/io/test_projection.cpp starts four). gcov then derives negative
    # counts from the inconsistent arcs and gcovr aborts with "count must not
    # be a negative value", on some runs only.
    target_compile_options(${target} INTERFACE --coverage -fprofile-update=atomic)
    target_link_options(${target} INTERFACE --coverage)
endfunction()

# gcov must match the compiler that wrote the .gcno files: gcov-<major> for
# GCC, `llvm-cov gcov` for Clang.
function(mov_find_gcov_command out_var)
    string(REGEX MATCH "^[0-9]+" major "${CMAKE_CXX_COMPILER_VERSION}")
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        find_program(MOV_GCOV NAMES gcov-${major} gcov REQUIRED)
        set(${out_var} "${MOV_GCOV}" PARENT_SCOPE)
    elseif(APPLE AND CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
        set(${out_var} "xcrun llvm-cov gcov" PARENT_SCOPE)
    else()
        find_program(MOV_LLVM_COV NAMES llvm-cov-${major} llvm-cov REQUIRED)
        set(${out_var} "${MOV_LLVM_COV} gcov" PARENT_SCOPE)
    endif()
endfunction()

# Target `coverage`: zero counters, run the test suite, then write
#   coverage-report/index.html    per-file HTML report
#   coverage-report/coverage.xml  Cobertura XML (Codecov)
#   coverage-report/coverage.json gcovr JSON (input to the per-layer gates)
#   coverage-report/summary.txt   text summary (also printed)
# and fail if line coverage of src/ is below MOV_COVERAGE_FAIL_UNDER, or that of
# any Qt-free layer (src/core, src/io) below MOV_COVERAGE_LAYER_FAIL_UNDER.
# Fuzz tests are not part of the run.
function(mov_add_coverage_target)
    find_program(MOV_GCOVR gcovr REQUIRED)
    mov_find_gcov_command(gcov_command)
    set(report_dir "${PROJECT_BINARY_DIR}/coverage-report")
    set(gcovr_common --root "${PROJECT_SOURCE_DIR}" --exclude-unreachable-branches --exclude-throw-branches)

    set(layer_gates "")
    mov_qt_free_source_dirs(layer_dirs)
    foreach(dir IN LISTS layer_dirs)
        file(RELATIVE_PATH layer "${PROJECT_SOURCE_DIR}" "${dir}")
        list(
            APPEND layer_gates
            COMMAND
            ${CMAKE_COMMAND}
            -E
            echo
            "Layer gate ${layer}: line coverage >= ${MOV_COVERAGE_LAYER_FAIL_UNDER}%"
            COMMAND
            ${MOV_GCOVR}
            ${gcovr_common}
            --json-add-tracefile
            "${report_dir}/coverage.json"
            --filter
            "${dir}/"
            --txt-summary
            --fail-under-line
            ${MOV_COVERAGE_LAYER_FAIL_UNDER}
        )
    endforeach()

    add_custom_target(
        coverage
        COMMAND ${CMAKE_COMMAND} -E rm -rf "${report_dir}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${report_dir}"
        COMMAND
            ${CMAKE_COMMAND} -D "MOV_COVERAGE_DIR=${PROJECT_BINARY_DIR}" -P
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/ZeroCoverageCounters.cmake"
        COMMAND ${CMAKE_CTEST_COMMAND} --test-dir "${PROJECT_BINARY_DIR}" --output-on-failure --label-exclude fuzz
        COMMAND
            ${MOV_GCOVR} ${gcovr_common} --object-directory "${PROJECT_BINARY_DIR}" --filter
            "${PROJECT_SOURCE_DIR}/src/" --gcov-executable "${gcov_command}" --print-summary --html-details
            "${report_dir}/index.html" --cobertura "${report_dir}/coverage.xml" --json "${report_dir}/coverage.json"
            --txt "${report_dir}/summary.txt" --fail-under-line ${MOV_COVERAGE_FAIL_UNDER}
        COMMAND ${CMAKE_COMMAND} -E cat "${report_dir}/summary.txt" ${layer_gates}
        WORKING_DIRECTORY "${PROJECT_BINARY_DIR}"
        COMMENT "Running tests and writing the coverage report to ${report_dir}"
        VERBATIM
    )
    # Building the report rebuilds whatever the tests need first.
    get_property(test_targets GLOBAL PROPERTY MOV_TEST_TARGETS)
    if(test_targets)
        add_dependencies(coverage ${test_targets})
    endif()
endfunction()
