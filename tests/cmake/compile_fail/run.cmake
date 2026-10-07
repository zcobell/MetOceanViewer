# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -DCXX_COMPILER=... -DINCLUDE_DIR=... -DSOURCE_DIR=... -P run.cmake
#
# Negative-compile checks: code that the type system is meant to reject must
# fail to compile under the project's own warning level (-Wall -Wextra
# -Wpedantic -Werror), and its well-formed twin must compile, so that a failure
# cannot be blamed on the environment. Each `reject_*.cpp` must fail and each
# `accept_*.cpp` must pass.

set(flags
    -std=c++23
    -fsyntax-only
    -Wall
    -Wextra
    -Wpedantic
    -Werror
    "-I${INCLUDE_DIR}"
)

function(compile source out_result out_output)
    execute_process(
        COMMAND "${CXX_COMPILER}" ${flags} "${source}"
        RESULT_VARIABLE result
        OUTPUT_VARIABLE output
        ERROR_VARIABLE output
    )
    set(${out_result} "${result}" PARENT_SCOPE)
    set(${out_output} "${output}" PARENT_SCOPE)
endfunction()

set(failures "")
set(checked 0)

file(GLOB rejects "${SOURCE_DIR}/reject_*.cpp")
foreach(source IN LISTS rejects)
    compile("${source}" result output)
    math(EXPR checked "${checked} + 1")
    if(result EQUAL 0)
        list(APPEND failures "compiled, but must be rejected: ${source}")
    endif()
endforeach()

file(GLOB accepts "${SOURCE_DIR}/accept_*.cpp")
foreach(source IN LISTS accepts)
    compile("${source}" result output)
    math(EXPR checked "${checked} + 1")
    if(NOT result EQUAL 0)
        list(APPEND failures "rejected, but must compile: ${source}\n${output}")
    endif()
endforeach()

if(checked EQUAL 0)
    list(APPEND failures "no reject_*.cpp or accept_*.cpp in ${SOURCE_DIR}")
endif()

if(failures)
    list(JOIN failures "\n" message)
    message(FATAL_ERROR "${message}")
endif()
message(STATUS "${checked} compile-fail checks passed")
