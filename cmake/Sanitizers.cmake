# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# Requests a toolchain cannot honor fail the configure; nothing is dropped
# silently.
function(mov_enable_sanitizers target enable_address enable_undefined enable_thread)
    set(sanitizers "")
    if(enable_address)
        list(APPEND sanitizers address)
    endif()
    if(enable_undefined)
        list(APPEND sanitizers undefined)
    endif()
    if(enable_thread)
        if(enable_address)
            message(FATAL_ERROR "ThreadSanitizer cannot be combined with AddressSanitizer")
        endif()
        list(APPEND sanitizers thread)
    endif()
    if(NOT sanitizers)
        return()
    endif()
    list(JOIN sanitizers "," sanitizer_list)

    if(MSVC)
        if(NOT sanitizers STREQUAL "address")
            message(FATAL_ERROR "MSVC supports only AddressSanitizer (requested: ${sanitizer_list})")
        endif()
        target_compile_options(${target} INTERFACE /fsanitize=address)
        # The MSVC STL annotations require every linked library to be built
        # with ASan; vcpkg dependencies are not.
        target_compile_definitions(${target} INTERFACE _DISABLE_VECTOR_ANNOTATION _DISABLE_STRING_ANNOTATION)
        target_link_options(${target} INTERFACE /INCREMENTAL:NO)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND NOT WIN32)
        # Frame pointers give usable stacks; no-recover makes any UBSan report
        # fail the test instead of printing and carrying on.
        target_compile_options(
            ${target}
            INTERFACE
                -fsanitize=${sanitizer_list}
                -fno-omit-frame-pointer
                $<$<BOOL:${enable_undefined}>:-fno-sanitize-recover=undefined>
        )
        target_link_options(${target} INTERFACE -fsanitize=${sanitizer_list})
    else()
        message(
            FATAL_ERROR
            "Sanitizers (${sanitizer_list}) are not supported with ${CMAKE_CXX_COMPILER_ID} on ${CMAKE_SYSTEM_NAME}"
        )
    endif()
    message(STATUS "Sanitizers enabled: ${sanitizer_list}")
endfunction()
