# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

find_package(Catch2 3 CONFIG REQUIRED)
include(Catch)

add_subdirectory(support)

# mov_add_test(<name> [SOURCES ...] [CONSTEXPR_SOURCES ...] LIBRARIES ...)
#
# One Catch2 executable per module (tests/core -> mov_core_tests, ...); each
# TEST_CASE becomes its own ctest test. Every executable links
# mov::test_support, whose mov::test::fixture() resolves files under
# tests/fixtures/.
#
# CONSTEXPR_SOURCES hold STATIC_REQUIRE tests. They build twice:
#   <name>_constexpr          STATIC_REQUIRE is a static_assert: a false
#                             check fails the build
#   <name>_relaxed_constexpr  CATCH_CONFIG_RUNTIME_STATIC_REQUIRE: the same
#                             checks at run time, for a debugger and coverage
function(mov_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;CONSTEXPR_SOURCES;LIBRARIES")
    if(arg_SOURCES)
        _mov_add_test_executable(${name} "${arg_SOURCES}" "${arg_LIBRARIES}")
    endif()
    if(arg_CONSTEXPR_SOURCES)
        _mov_add_test_executable(${name}_constexpr "${arg_CONSTEXPR_SOURCES}" "${arg_LIBRARIES}")
        _mov_add_test_executable(${name}_relaxed_constexpr "${arg_CONSTEXPR_SOURCES}" "${arg_LIBRARIES}")
        target_compile_definitions(${name}_relaxed_constexpr PRIVATE CATCH_CONFIG_RUNTIME_STATIC_REQUIRE)
    endif()
endfunction()

function(_mov_add_test_executable name sources libraries)
    add_executable(${name} ${sources})
    target_link_libraries(
        ${name}
        PRIVATE ${libraries} mov::test_support Catch2::Catch2WithMain mov::options mov::warnings
    )
    mov_enable_static_analysis(${name})
    set_property(GLOBAL APPEND PROPERTY MOV_TEST_TARGETS ${name})
    # PRE_TEST: list tests when ctest runs, not at build time, so discovery
    # sees the same runtime environment (DLL paths, sanitizer options).
    # NoAssertions: a test case that checks nothing fails.
    catch_discover_tests(
        ${name}
        DISCOVERY_MODE PRE_TEST
        TEST_PREFIX "${name}:"
        EXTRA_ARGS --warn NoAssertions
        PROPERTIES TIMEOUT 60
    )
endfunction()

add_subdirectory(core)
add_subdirectory(io)

# core and io stay Qt-free at the source level too; cmake/Layering.cmake
# guards the link level.
mov_qt_free_source_dirs(qt_free_dirs)
add_test(
    NAME qt_free_sources
    COMMAND
        ${CMAKE_COMMAND} "-DMOV_SOURCE_DIRS=${qt_free_dirs}" -P "${PROJECT_SOURCE_DIR}/cmake/CheckQtFreeSources.cmake"
)

# The guards themselves: each must reject what it exists to reject.
add_test(
    NAME layering_guard_rejects_violations
    COMMAND
        ${CMAKE_COMMAND} "-DMOV_REPO=${PROJECT_SOURCE_DIR}" "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/layering_guard"
        "-DGENERATOR=${CMAKE_GENERATOR}" "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}" -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/layering/run.cmake"
)
add_test(
    NAME qt_include_scanner_rejects_qt_headers
    COMMAND
        ${CMAKE_COMMAND} "-DMOV_REPO=${PROJECT_SOURCE_DIR}" -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/qt_include/run.cmake"
)
set_tests_properties(layering_guard_rejects_violations qt_include_scanner_rejects_qt_headers PROPERTIES TIMEOUT 300)

# Negative-compile checks of the core's strong types (GCC/Clang flag syntax;
# MSVC's warning flags differ and are not covered yet).
if(NOT MSVC)
    add_test(
        NAME core_compile_fail_checks
        COMMAND
            ${CMAKE_COMMAND} "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}"
            "-DINCLUDE_DIR=${PROJECT_SOURCE_DIR}/src/core/include"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_fail" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_fail/run.cmake"
    )
    set_tests_properties(core_compile_fail_checks PROPERTIES TIMEOUT 300)
endif()
