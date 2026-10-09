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
#
# mov_add_test(<name> QT|GUI SOURCES ... LIBRARIES ...)
#
# A Qt test executable. Its sources supply main() (one QGuiApplication per
# process, created before Catch2 runs), so it is one ctest test.
#   QT   label `qt`: QT_QPA_PLATFORM=offscreen, no display or GL needed; runs
#        on every CI platform.
#   GUI  label `gui`: renders, so it needs a display and OpenGL. On Linux it
#        runs under xvfb-run when available (Mesa provides GL in software),
#        and RESOURCE_LOCK xvfb keeps parallel ctest from racing xvfb-run's
#        server-number pick. Jobs without a display exclude it with -LE gui.
# On Windows, Qt's bin directory goes on the test's PATH.
function(mov_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "QT;GUI" "" "SOURCES;CONSTEXPR_SOURCES;LIBRARIES")
    if(arg_QT OR arg_GUI)
        if(arg_QT AND arg_GUI)
            message(FATAL_ERROR "mov_add_test(${name}): QT and GUI are exclusive")
        endif()
        if(arg_CONSTEXPR_SOURCES)
            message(FATAL_ERROR "mov_add_test(${name}): CONSTEXPR_SOURCES do not combine with QT or GUI")
        endif()
        if(arg_GUI)
            set(label gui)
        else()
            set(label qt)
        endif()
        _mov_add_qt_test_executable(${name} ${label} "${arg_SOURCES}" "${arg_LIBRARIES}")
        return()
    endif()
    if(arg_SOURCES)
        _mov_add_test_executable(${name} "${arg_SOURCES}" "${arg_LIBRARIES}")
    endif()
    if(arg_CONSTEXPR_SOURCES)
        _mov_add_test_executable(${name}_constexpr "${arg_CONSTEXPR_SOURCES}" "${arg_LIBRARIES}")
        _mov_add_test_executable(${name}_relaxed_constexpr "${arg_CONSTEXPR_SOURCES}" "${arg_LIBRARIES}")
        target_compile_definitions(${name}_relaxed_constexpr PRIVATE CATCH_CONFIG_RUNTIME_STATIC_REQUIRE)
    endif()
endfunction()

# Catch2's SKIP and FAIL throw, so MSVC reports the rest of a test that skips
# on a platform (`if constexpr (not available) { SKIP(...); }`) as unreachable
# code (C4702). Tests only; src keeps the warning.
function(_mov_test_warning_exceptions name)
    if(MSVC)
        target_compile_options(${name} PRIVATE /wd4702)
    endif()
endfunction()

function(_mov_add_test_executable name sources libraries)
    add_executable(${name} ${sources})
    target_link_libraries(
        ${name}
        PRIVATE ${libraries} mov::test_support Catch2::Catch2WithMain mov::options mov::warnings
    )
    _mov_test_warning_exceptions(${name})
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

function(_mov_add_qt_test_executable name label sources libraries)
    add_executable(${name} ${sources})
    target_link_libraries(
        ${name}
        PRIVATE ${libraries} mov::test_support Catch2::Catch2 mov::options mov::warnings
    )
    _mov_test_warning_exceptions(${name})
    mov_enable_static_analysis(${name})
    set_property(GLOBAL APPEND PROPERTY MOV_TEST_TARGETS ${name})
    add_test(NAME ${name} COMMAND ${name} --warn NoAssertions)
    set_tests_properties(${name} PROPERTIES LABELS ${label} TIMEOUT 120)
    if(label STREQUAL "qt")
        set_property(TEST ${name} APPEND PROPERTY ENVIRONMENT QT_QPA_PLATFORM=offscreen)
    else()
        set_tests_properties(${name} PROPERTIES RESOURCE_LOCK xvfb)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_program(MOV_XVFB_RUN xvfb-run)
            if(MOV_XVFB_RUN)
                # xvfb-run's default screen is 8-bit; GL wants 24.
                set_property(
                    TARGET ${name}
                    PROPERTY TEST_LAUNCHER "${MOV_XVFB_RUN}" --auto-servernum "--server-args=-screen 0 1600x1000x24"
                )
            endif()
        endif()
    endif()
    if(WIN32)
        set_property(
            TEST ${name}
            APPEND
            PROPERTY ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
        )
    endif()
endfunction()

add_subdirectory(core)
add_subdirectory(io)
if(MOV_ENABLE_QT)
    add_subdirectory(ui)
endif()

# core and io stay Qt-free at the source level too; cmake/Layering.cmake
# guards the link level.
mov_qt_free_source_dirs(qt_free_dirs)
add_test(
    NAME qt_free_sources
    COMMAND
        ${CMAKE_COMMAND} "-DMOV_SOURCE_DIRS=${qt_free_dirs}" -P "${PROJECT_SOURCE_DIR}/cmake/CheckQtFreeSources.cmake"
)

# Names in mov::core::detail are core-private (detail/core_key.hpp): no other
# layer, and no test of another layer, uses them.
add_test(
    NAME core_detail_fence
    COMMAND
        ${CMAKE_COMMAND} "-DMOV_REPO=${PROJECT_SOURCE_DIR}" -P "${PROJECT_SOURCE_DIR}/cmake/CheckCoreDetailFence.cmake"
)
add_test(
    NAME core_detail_fence_rejects_violations
    COMMAND
        ${CMAKE_COMMAND} "-DMOV_REPO=${PROJECT_SOURCE_DIR}" -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/core_detail_fence/run.cmake"
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
    # The harness runs the compiler by hand, so it needs the flags that pick
    # the standard library and SDK (-stdlib=libc++, -isysroot on macOS) too.
    # Cases tagged `requires-diagnostic` are skipped only on Apple Clang when
    # it lacks the diagnostic (see cmake/compile_fail/run.cmake).
    set(compile_fail_flags "${CMAKE_CXX_FLAGS}")
    if(CMAKE_OSX_SYSROOT)
        string(APPEND compile_fail_flags " -isysroot ${CMAKE_OSX_SYSROOT}")
    endif()
    add_test(
        NAME core_compile_fail_checks
        COMMAND
            ${CMAKE_COMMAND} "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}" "-DCXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
            "-DCXX_FLAGS=${compile_fail_flags}" "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_fail_probe"
            "-DINCLUDE_DIR=${PROJECT_SOURCE_DIR}/src/core/include"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_fail" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_fail/run.cmake"
    )
    set_tests_properties(core_compile_fail_checks PROPERTIES TIMEOUT 300)
    # The same for io's netCDF names (compile-time checked literals, no view
    # of a temporary). Those headers need only the standard library.
    add_test(
        NAME io_compile_fail_checks
        COMMAND
            ${CMAKE_COMMAND} "-DCXX_COMPILER=${CMAKE_CXX_COMPILER}" "-DCXX_COMPILER_ID=${CMAKE_CXX_COMPILER_ID}"
            "-DCXX_FLAGS=${compile_fail_flags}" "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/compile_fail_probe"
            "-DINCLUDE_DIR=${PROJECT_SOURCE_DIR}/src/io/include"
            "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_fail_io" -P
            "${CMAKE_CURRENT_SOURCE_DIR}/cmake/compile_fail/run.cmake"
    )
    set_tests_properties(io_compile_fail_checks PROPERTIES TIMEOUT 300)
endif()

# netCDF-C is included only by the wrapper's sources (core-design.md 4.1),
# and the gate rejects what it exists to reject.
add_test(
    NAME netcdf_include_gate
    COMMAND
        ${CMAKE_COMMAND} "-DMOV_SOURCE_ROOT=${PROJECT_SOURCE_DIR}/src" -P
        "${PROJECT_SOURCE_DIR}/cmake/CheckNetcdfInclude.cmake"
)
add_test(
    NAME netcdf_include_gate_rejects_violations
    COMMAND
        ${CMAKE_COMMAND} "-DMOV_REPO=${PROJECT_SOURCE_DIR}" -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/netcdf_include/run.cmake"
)
