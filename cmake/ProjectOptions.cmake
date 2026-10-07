# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# Cache options for this project. The presets (CMakePresets.json) are the
# intended way to set them; the defaults suit a plain consumer build.
macro(mov_declare_options)
    option(MOV_ENABLE_QT "Find Qt 6 and build the Qt-dependent layers (providers, app, ui)" OFF)
    option(MOV_WARNINGS_AS_ERRORS "Treat compiler warnings in first-party code as errors" OFF)
    option(
        MOV_ENABLE_HARDENING
        "Standard-library assertions (_GLIBCXX_ASSERTIONS, libc++ hardening, MSVC STL hardening)"
        OFF
    )
    option(MOV_ENABLE_SANITIZER_ADDRESS "Enable AddressSanitizer" OFF)
    option(MOV_ENABLE_SANITIZER_UNDEFINED "Enable UndefinedBehaviorSanitizer" OFF)
    # For the Phase 3 providers (QFuture/QtConcurrent); core and io are
    # single-threaded.
    option(MOV_ENABLE_SANITIZER_THREAD "Enable ThreadSanitizer" OFF)
    option(MOV_ENABLE_COVERAGE "Instrument first-party code for gcov-based coverage" OFF)
    set(MOV_COVERAGE_FAIL_UNDER "0" CACHE STRING "Minimum overall line coverage (percent) of src/; 0 disables")
    set(MOV_COVERAGE_LAYER_FAIL_UNDER
        "0"
        CACHE STRING
        "Minimum line coverage (percent) of each Qt-free layer (src/core, src/io); 0 disables"
    )
    option(MOV_BUILD_FUZZ_TESTS "Build the libFuzzer targets (Clang only) and register bounded fuzz runs" OFF)
    set(MOV_FUZZ_SECONDS "10" CACHE STRING "Wall-clock seconds each registered fuzz test runs")
    option(MOV_ENABLE_CLANG_TIDY "Run clang-tidy while compiling first-party targets" OFF)
    option(
        MOV_REQUIRE_LOCALES
        "Tests that need a comma-decimal locale (de_DE.UTF-8) fail instead of skipping when it is missing"
        OFF
    )
    option(MOV_ENABLE_CACHE"Use ccache/sccache as the compiler launcher when found" ON)
endmacro()

# Interface targets every first-party target links PRIVATE:
#   mov_warnings  warning flags (never reach third-party code)
#   mov_options   conformance, hardening, sanitizers, coverage, fuzzing
macro(mov_create_option_targets)
    if(PROJECT_IS_TOP_LEVEL)
        include(cmake/StandardProjectSettings.cmake)
    endif()

    add_library(mov_warnings INTERFACE)
    add_library(mov_options INTERFACE)
    add_library(mov::options ALIAS mov_options)
    add_library(mov::warnings ALIAS mov_warnings)

    if(MSVC)
        # Standards conformance: two-phase lookup, the conforming preprocessor.
        target_compile_options(mov_options INTERFACE /permissive- /Zc:preprocessor)
    endif()

    # No fused multiply-add: a * x + b must round twice everywhere, so that
    # results computed at compile time, on x86-64 and on arm64 (where GCC and
    # Clang contract by default) agree bit for bit. MSVC's default /fp:precise
    # does not contract across statements, so it needs no flag.
    if(NOT MSVC)
        target_compile_options(mov_options INTERFACE $<$<COMPILE_LANGUAGE:CXX>:-ffp-contract=off>)
    endif()

    include(cmake/CompilerWarnings.cmake)
    mov_set_project_warnings(mov_warnings ${MOV_WARNINGS_AS_ERRORS})

    include(cmake/Hardening.cmake)
    if(MOV_ENABLE_HARDENING)
        mov_enable_hardening(mov_options)
    endif()

    include(cmake/Sanitizers.cmake)
    mov_enable_sanitizers(
        mov_options
        ${MOV_ENABLE_SANITIZER_ADDRESS}
        ${MOV_ENABLE_SANITIZER_UNDEFINED}
        ${MOV_ENABLE_SANITIZER_THREAD}
    )

    include(cmake/Coverage.cmake)
    if(MOV_ENABLE_COVERAGE)
        mov_enable_coverage(mov_options)
    endif()

    include(cmake/Fuzzing.cmake)
    if(MOV_BUILD_FUZZ_TESTS)
        mov_enable_fuzzing_instrumentation(mov_options)
    endif()

    include(cmake/StaticAnalyzers.cmake)

    if(MOV_ENABLE_CACHE)
        include(cmake/Cache.cmake)
        mov_enable_cache()
    endif()
endmacro()
