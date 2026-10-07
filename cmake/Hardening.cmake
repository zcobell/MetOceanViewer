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
