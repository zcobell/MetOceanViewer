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
# A Qt test executable. Its sources supply main() (one QCoreApplication or
# QGuiApplication per process, created before Catch2 runs), so it is one
# ctest test.
#   QT   label `qt`: QT_QPA_PLATFORM=offscreen, no display or GL needed; runs
#        on every CI platform.
#   GUI  label `gui`: renders, so it needs a display and OpenGL. On Linux it
#        runs under xvfb-run when available (Mesa provides GL in software),
#        and RESOURCE_LOCK xvfb keeps parallel ctest from racing xvfb-run's
#        server-number pick. Jobs without a display exclude the label.
# On Windows, Qt's bin directory goes on the test's PATH.
#
# mov_add_test(<name> LIVE [QT] SOURCES ... LIBRARIES ...)
#
# A test of the real provider APIs (docs/providers-design.md §10.5): one
# ctest test labelled `live` (with QT, a Qt executable as above, offscreen).
# Every test preset excludes the label, and a preset's filter also applies
# to -L, so the live tests run by build directory:
#   MOV_LIVE_API=1 ctest --test-dir build/<preset> -L live
# Each test case starts with mov::test::require_live_api()
# (mov/test/live.hpp), so without the opt-in it skips and ctest reports the
# skip (Catch2's exit code 4); tests/live adds the label's guard test, which
# fails without the opt-in.
function(mov_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "QT;GUI;LIVE" "" "SOURCES;CONSTEXPR_SOURCES;LIBRARIES")
    if(arg_LIVE)
        if(arg_GUI OR arg_CONSTEXPR_SOURCES)
            message(FATAL_ERROR "mov_add_test(${name}): LIVE combines with QT only")
        endif()
        if(arg_QT)
            _mov_add_qt_test_executable(${name} live "${arg_SOURCES}" "${arg_LIBRARIES}")
        else()
            _mov_test_executable(${name} CATCH_MAIN SOURCES ${arg_SOURCES} LIBRARIES ${arg_LIBRARIES})
            set_tests_properties(${name} PROPERTIES LABELS live)
        endif()
        set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 4 TIMEOUT 300)
        return()
    endif()
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
        _mov_test_executable(${name} CATCH_MAIN DISCOVER SOURCES ${arg_SOURCES} LIBRARIES ${arg_LIBRARIES})
    endif()
    if(arg_CONSTEXPR_SOURCES)
        foreach(variant IN ITEMS constexpr relaxed_constexpr)
            _mov_test_executable(
                ${name}_${variant}
                CATCH_MAIN
                DISCOVER
                SOURCES ${arg_CONSTEXPR_SOURCES}
                LIBRARIES ${arg_LIBRARIES}
            )
        endforeach()
        target_compile_definitions(${name}_relaxed_constexpr PRIVATE CATCH_CONFIG_RUNTIME_STATIC_REQUIRE)
    endif()
endfunction()

# _mov_test_executable(<name> [CATCH_MAIN] [DISCOVER] SOURCES ... LIBRARIES ...)
#
# What every test executable shares: the link line, the test-only warning
# exceptions, static analysis, the coverage target's dependency list and
# NoAssertions (a test case that checks nothing fails). CATCH_MAIN links
# Catch2's main (the Qt tests bring their own). DISCOVER registers each
# TEST_CASE as its own ctest test; otherwise the executable is one ctest test.
function(_mov_test_executable name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "CATCH_MAIN;DISCOVER" "" "SOURCES;LIBRARIES")
    add_executable(${name} ${arg_SOURCES})
    if(arg_CATCH_MAIN)
        set(catch Catch2::Catch2WithMain)
    else()
        set(catch Catch2::Catch2)
    endif()
    target_link_libraries(
        ${name}
        PRIVATE ${arg_LIBRARIES} mov::test_support ${catch} mov::options mov::warnings
    )
    # Catch2's SKIP and FAIL throw, so MSVC reports the rest of a test that
    # skips on a platform (`if constexpr (not available) { SKIP(...); }`) as
    # unreachable code (C4702). Tests only; src keeps the warning.
    if(MSVC)
        target_compile_options(${name} PRIVATE /wd4702)
    endif()
    mov_enable_static_analysis(${name})
    set_property(GLOBAL APPEND PROPERTY MOV_TEST_TARGETS ${name})
    if(arg_DISCOVER)
        # PRE_TEST: list tests when ctest runs, not at build time, so
        # discovery sees the same runtime environment (DLL paths, sanitizer
        # options).
        catch_discover_tests(
            ${name}
            DISCOVERY_MODE PRE_TEST
            TEST_PREFIX "${name}:"
            EXTRA_ARGS --warn NoAssertions
            PROPERTIES TIMEOUT 60
        )
    else()
        add_test(NAME ${name} COMMAND ${name} --warn NoAssertions)
    endif()
endfunction()

function(_mov_add_qt_test_executable name label sources libraries)
    _mov_test_executable(${name} SOURCES ${sources} LIBRARIES ${libraries})
    set_tests_properties(${name} PROPERTIES LABELS ${label} TIMEOUT 120)
    if(label STREQUAL "qt" OR label STREQUAL "live")
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
add_subdirectory(fetch)
add_subdirectory(live)
if(MOV_ENABLE_QT)
    add_subdirectory(providers)
    add_subdirectory(ui)
endif()

# core, io and fetch stay Qt-free at the source level too; cmake/Layering.cmake
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

# No nested event loop and no blocking call (plan §2.3, providers-design.md
# §2.2), no TLS certificate check turned off (§6.2), nlohmann/json and zlib
# private to io (harmonics-engine.md §6.5); each gate rejects what it exists
# to reject.
add_test(
    NAME no_blocking_calls
    COMMAND
        ${CMAKE_COMMAND} "-DMOV_REPO=${PROJECT_SOURCE_DIR}" -P "${PROJECT_SOURCE_DIR}/cmake/CheckBlockingCalls.cmake"
)
add_test(
    NAME no_blocking_calls_rejects_violations
    COMMAND
        ${CMAKE_COMMAND} "-DMOV_REPO=${PROJECT_SOURCE_DIR}" -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/blocking_calls/run.cmake"
)
add_test(
    NAME no_ignore_ssl_errors
    COMMAND
        ${CMAKE_COMMAND} "-DMOV_REPO=${PROJECT_SOURCE_DIR}" -P "${PROJECT_SOURCE_DIR}/cmake/CheckIgnoreSslErrors.cmake"
)
add_test(
    NAME no_ignore_ssl_errors_rejects_violations
    COMMAND
        ${CMAKE_COMMAND} "-DMOV_REPO=${PROJECT_SOURCE_DIR}" -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ignore_ssl_errors/run.cmake"
)
add_test(
    NAME third_party_include_gate
    COMMAND
        ${CMAKE_COMMAND} "-DMOV_REPO=${PROJECT_SOURCE_DIR}" -P
        "${PROJECT_SOURCE_DIR}/cmake/CheckThirdPartyIncludes.cmake"
)
add_test(
    NAME third_party_include_gate_rejects_violations
    COMMAND
        ${CMAKE_COMMAND} "-DMOV_REPO=${PROJECT_SOURCE_DIR}" -P
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/third_party_include/run.cmake"
)

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
