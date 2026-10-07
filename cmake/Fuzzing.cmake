# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# libFuzzer (Clang only). With MOV_BUILD_FUZZ_TESTS every first-party target is
# built with coverage instrumentation for the fuzzer (-fsanitize=fuzzer-no-link);
# fuzz executables add the libFuzzer driver (-fsanitize=fuzzer). Combine with
# the ASan/UBSan options, as the `fuzz` preset does.
#
# mov_add_fuzz_test(<name> SOURCES ... LIBRARIES ... CORPUS <dir under tests/fixtures>)
#   builds the fuzzer and registers a ctest (label "fuzz") that runs it for
#   MOV_FUZZ_SECONDS seconds, seeded from the committed corpus. New inputs go to
#   a corpus directory in the build tree; crash reproducers are written next to
#   it with the prefix <name>-.

function(mov_enable_fuzzing_instrumentation target)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "MOV_BUILD_FUZZ_TESTS requires Clang (libFuzzer), not ${CMAKE_CXX_COMPILER_ID}")
    endif()
    target_compile_options(${target} INTERFACE -fsanitize=fuzzer-no-link)
endfunction()

function(mov_add_fuzz_test name)
    if(NOT MOV_BUILD_FUZZ_TESTS)
        return()
    endif()
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "CORPUS" "SOURCES;LIBRARIES")
    set(seed_dir "${PROJECT_SOURCE_DIR}/tests/fixtures/${arg_CORPUS}")
    if(NOT IS_DIRECTORY "${seed_dir}")
        message(FATAL_ERROR "mov_add_fuzz_test(${name}): seed corpus ${seed_dir} does not exist")
    endif()

    add_executable(${name} ${arg_SOURCES})
    target_link_libraries(${name} PRIVATE ${arg_LIBRARIES} mov::options mov::warnings)
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
    mov_enable_static_analysis(${name})

    set(work_dir "${CMAKE_CURRENT_BINARY_DIR}/${name}.work")
    file(MAKE_DIRECTORY "${work_dir}/corpus")
    add_test(
        NAME ${name}
        COMMAND
            ${name} -max_total_time=${MOV_FUZZ_SECONDS} -timeout=10 -rss_limit_mb=2048 -print_final_stats=1
            "-artifact_prefix=${work_dir}/${name}-" "${work_dir}/corpus" "${seed_dir}"
    )
    math(EXPR timeout "${MOV_FUZZ_SECONDS} + 60")
    set_tests_properties(${name} PROPERTIES LABELS fuzz TIMEOUT ${timeout})
endfunction()
