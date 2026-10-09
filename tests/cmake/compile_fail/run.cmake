# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -DCXX_COMPILER=... -DCXX_COMPILER_ID=... [-DCXX_FLAGS=...] -DINCLUDE_DIR=...
#       -DSOURCE_DIR=... -DWORK_DIR=... -P run.cmake
#
# Negative-compile checks: code that the type system is meant to reject must
# fail to compile under the project's own warning level (-Wall -Wextra
# -Wpedantic -Werror), and its well-formed twin must compile, so that a failure
# cannot be blamed on the environment. Each `reject_*.cpp` must fail and each
# `accept_*.cpp` must pass.
#
# A reject case that depends on a compiler diagnostic not every supported
# compiler has carries a line `// requires-diagnostic: <name>` (names below).
# The harness first checks that the compiler diagnoses a minimal probe of that
# kind. On Apple Clang a probe that compiles skips the case (reported as
# skipped, e.g. Apple Clang 16 has no -Wmissing-designated-field-initializers,
# which is Clang 19+); on any other compiler it fails the test, so
# the gate cannot silently weaken GCC or Clang.

# CXX_FLAGS: the build's own flags that select the standard library and SDK.
separate_arguments(extra_flags UNIX_COMMAND "${CXX_FLAGS}")

set(flags
    ${extra_flags}
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

# Probes: the smallest code the diagnostic named in `requires-diagnostic` must
# reject, shaped like the real cases (aggregates passed to a function).
set(probe_missing_designated_field
    "struct P { double lat; double lon; };\nvoid f(P);\nint main() { f({.lat = 1.0}); }\n"
)
set(probe_reordered_designators
    "#include <string_view>\nstruct S { std::string_view token; std::string_view name; };\nvoid f(S);\nint main() { f({.name = \"n\", .token = \"t\"}); }\n"
)

# True when the compiler rejects the named probe under the harness's flags.
function(compiler_diagnoses name out_var)
    string(MAKE_C_IDENTIFIER "${name}" id)
    if(NOT DEFINED probe_${id})
        message(FATAL_ERROR "unknown requires-diagnostic '${name}'")
    endif()
    if(NOT WORK_DIR)
        message(FATAL_ERROR "-DWORK_DIR is required for requires-diagnostic")
    endif()
    file(MAKE_DIRECTORY "${WORK_DIR}")
    file(WRITE "${WORK_DIR}/probe_${id}.cpp" "${probe_${id}}")
    compile("${WORK_DIR}/probe_${id}.cpp" result output)
    if(result EQUAL 0)
        set(${out_var} FALSE PARENT_SCOPE)
    else()
        set(${out_var} TRUE PARENT_SCOPE)
    endif()
endfunction()

set(failures "")
set(skipped "")
set(checked 0)

file(GLOB rejects "${SOURCE_DIR}/reject_*.cpp")
foreach(source IN LISTS rejects)
    file(STRINGS "${source}" tag REGEX "^// requires-diagnostic: ")
    if(tag)
        string(REGEX REPLACE "^// requires-diagnostic: *" "" needed "${tag}")
        compiler_diagnoses("${needed}" available)
        if(NOT available)
            if(CXX_COMPILER_ID STREQUAL "AppleClang")
                list(APPEND skipped "${source} (this compiler has no '${needed}' diagnostic)")
                continue()
            endif()
            list(
                APPEND failures
                "${CXX_COMPILER_ID} should diagnose '${needed}' but compiled the probe; ${source} cannot be checked"
            )
            continue()
        endif()
    endif()
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
foreach(entry IN LISTS skipped)
    message(STATUS "SKIPPED: ${entry}")
endforeach()
message(STATUS "${checked} compile-fail checks passed")
