# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# CI gates clang-tidy over the compile database (tools/clang_tidy_gate.py), so
# the build need not run it. MOV_ENABLE_CLANG_TIDY instead runs it as part of
# compiling each first-party target, for immediate feedback in an IDE.

function(mov_enable_static_analysis target)
    if(NOT MOV_ENABLE_CLANG_TIDY)
        return()
    endif()
    find_program(MOV_CLANG_TIDY NAMES clang-tidy REQUIRED)
    # The repo-root .clang-tidy supplies the checks; gcc-only warning flags in
    # the compile command must not become clang-tidy errors.
    set_target_properties(
        ${target}
        PROPERTIES CXX_CLANG_TIDY "${MOV_CLANG_TIDY};--extra-arg=-Wno-unknown-warning-option"
    )
endfunction()
