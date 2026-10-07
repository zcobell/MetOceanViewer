# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# Compiler launcher: sccache first on Windows (ccache's MSVC support is
# younger), ccache first elsewhere.
function(mov_enable_cache)
    if(DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
        return() # set by the user, a preset or CI
    endif()
    if(WIN32)
        set(candidates sccache ccache)
    else()
        set(candidates ccache sccache)
    endif()
    find_program(MOV_CACHE_BINARY NAMES ${candidates})
    if(MOV_CACHE_BINARY)
        message(STATUS "Compiler cache: ${MOV_CACHE_BINARY}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${MOV_CACHE_BINARY}" CACHE FILEPATH "CXX compiler cache")
    else()
        message(STATUS "Compiler cache: none found (${candidates})")
    endif()
endfunction()
