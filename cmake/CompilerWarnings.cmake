# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Warning set from
# https://github.com/lefticus/cppbestpractices/blob/master/02-Use_the_Tools_Available.md
# Applied through the mov_warnings interface target, so only first-party
# targets see these flags; dependencies come prebuilt from vcpkg, and CMake
# marks their imported include directories as system/external, which keeps
# their headers' warnings out.

function(mov_set_project_warnings target warnings_as_errors)
    set(MSVC_WARNINGS
        /W4 # baseline reasonable warnings
        /w14242 # 'identifier': conversion from 'type1' to 'type2', possible loss of data
        /w14254 # 'operator': conversion from 'type1:field_bits' to 'type2:field_bits', possible loss of data
        /w14263 # 'function': member function does not override any base class virtual member function
        /w14265 # 'classname': class has virtual functions, but destructor is not virtual
        /w14287 # 'operator': unsigned/negative constant mismatch
        /we4289 # loop control variable declared in the for-loop is used outside the for-loop scope
        /w14296 # 'operator': expression is always 'boolean_value'
        /w14311 # 'variable': pointer truncation from 'type1' to 'type2'
        /w14545 # expression before comma evaluates to a function which is missing an argument list
        /w14546 # function call before comma missing argument list
        /w14547 # 'operator': operator before comma has no effect; expected operator with side-effect
        /w14549 # 'operator': operator before comma has no effect; did you intend 'operator'?
        /w14555 # expression has no effect; expected expression with side-effect
        /w14619 # pragma warning: there is no warning number 'number'
        /w14640 # thread un-safe static member initialization
        /w14826 # conversion from 'type1' to 'type2' is sign-extended
        /w14905 # wide string literal cast to 'LPSTR'
        /w14906 # string literal cast to 'LPWSTR'
        /w14928 # illegal copy-initialization; more than one user-defined conversion
        /w14062 # enumerator in switch of enum is not handled
    )

    if(MSVC AND CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        # clang-cl (the local Windows cross-check, cmake/toolchains/clang-cl-xwin.cmake):
        # /W4 selects Clang's -Wall -Wextra, which lacks Clang's counterparts of
        # cl /W4 warnings that have broken Windows builds before.
        list(
            APPEND MSVC_WARNINGS
            -Wunreachable-code # C4702 unreachable code (not after a fully covered enum switch)
            -Wshadow # C4456, C4457, C4458, C4459 declaration hides another
            -Wconversion # C4244, C4267, C4305 narrowing conversions
            -Wno-sign-conversion # keep -Wconversion to what cl /W4 reports
        )
    endif()

    set(CLANG_WARNINGS
        -Wall
        -Wextra
        -Wpedantic # non-standard C++
        -Wshadow # declaration shadows one from a parent context
        -Wnon-virtual-dtor # class with virtual functions has a non-virtual destructor
        -Wold-style-cast # C-style casts
        -Wcast-align # potential performance problem casts
        -Wunused # anything unused
        -Woverloaded-virtual # overload (not override) of a virtual function
        -Wconversion # type conversions that may lose data
        -Wsign-conversion # sign conversions
        -Wnull-dereference # null dereference detected
        -Wdouble-promotion # float implicitly promoted to double
        -Wformat=2 # security issues around printf-style formatting
        -Wimplicit-fallthrough # switch fallthrough without [[fallthrough]]
    )

    set(CLANG_ONLY_WARNINGS
        -Wcovered-switch-default # default: label in a switch that covers every enumerator
        -Wmissing-prototypes # non-static function without a prior declaration
    )

    set(GCC_WARNINGS
        ${CLANG_WARNINGS}
        -Wmissing-declarations # non-static function without a prior declaration
        -Wmisleading-indentation # indentation implies blocks where none exist
        -Wduplicated-cond # if / else chain has duplicated conditions
        -Wduplicated-branches # if / else branches have duplicated code
        -Wlogical-op # logical operations where bitwise were probably wanted
        -Wuseless-cast # cast to the same type
        -Wsuggest-override # overriding member function not marked override
    )

    if(warnings_as_errors)
        list(APPEND CLANG_WARNINGS -Werror)
        list(APPEND GCC_WARNINGS -Werror)
        list(APPEND MSVC_WARNINGS /WX)
    endif()

    if(MSVC)
        set(project_warnings ${MSVC_WARNINGS})
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(project_warnings ${CLANG_WARNINGS} ${CLANG_ONLY_WARNINGS})
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(project_warnings ${GCC_WARNINGS})
    else()
        message(AUTHOR_WARNING "No compiler warnings set for CXX compiler: '${CMAKE_CXX_COMPILER_ID}'")
    endif()

    target_compile_options(${target} INTERFACE $<$<COMPILE_LANGUAGE:CXX>:${project_warnings}>)
endfunction()
