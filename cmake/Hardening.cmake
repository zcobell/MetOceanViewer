# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# Standard-library precondition checks (bounds, null, empty front()/back(),
# ...). Cheap enough for every dev, sanitizer, coverage and CI build; shipped
# release builds leave them off for now (docs/rearchitecture-plan.md §6.20).
# None of these change the ABI, so prebuilt vcpkg dependencies still link.
# _GLIBCXX_DEBUG is deliberately not used: it changes the ABI.

function(mov_enable_hardening target)
    if(MSVC)
        # MSVC STL hardening (VS 2022 17.14+; ignored by older toolsets).
        target_compile_definitions(${target} INTERFACE _MSVC_STL_HARDENING=1)
        return()
    endif()
    # Each library ignores the other's macro, so set both: libstdc++ (GCC and
    # Clang on Linux) and libc++ (Apple Clang), extensive checks in Debug.
    target_compile_definitions(
        ${target}
        INTERFACE
            _GLIBCXX_ASSERTIONS
            $<IF:$<CONFIG:Debug>,_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_EXTENSIVE,_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST>
    )
endfunction()

# Code-generation hardening for optimized builds on Linux: what distribution
# compilers enable by default, made explicit so a self-built toolchain (the
# AppImage's GCC, docs/packaging.md) produces the same binaries. Off for
# Debug (_FORTIFY_SOURCE needs optimization) and for sanitizer, coverage and
# fuzz builds, which instrument the same calls.
function(mov_enable_codegen_hardening target)
    if(MSVC OR APPLE OR NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        return()
    endif()
    set(optimized "$<NOT:$<CONFIG:Debug>>")
    # Undefine first: Ubuntu's GCC predefines _FORTIFY_SOURCE, and redefining
    # it is a warning that -Werror makes fatal.
    target_compile_options(
        ${target}
        INTERFACE
            "$<${optimized}:-U_FORTIFY_SOURCE;-D_FORTIFY_SOURCE=3>"
            -fstack-clash-protection
            -fstack-protector-strong
    )
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
        target_compile_options(${target} INTERFACE -fcf-protection)
    endif()
    target_link_options(${target} INTERFACE LINKER:-z,relro LINKER:-z,now)
endfunction()
