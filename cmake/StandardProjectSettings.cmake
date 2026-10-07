# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'RelWithDebInfo' as none was specified.")
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build." FORCE)
    set_property(
        CACHE CMAKE_BUILD_TYPE
        PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo"
    )
endif()

# compile_commands.json for clang-tidy (tools/clang_tidy_gate.py) and editors.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Ninja captures compiler output, which strips color unless asked for.
if(NOT DEFINED CMAKE_COLOR_DIAGNOSTICS)
    set(CMAKE_COLOR_DIAGNOSTICS ON)
endif()

if(MSVC)
    # Report the real __cplusplus, read sources as UTF-8, point at columns.
    add_compile_options(/Zc:__cplusplus /utf-8 /diagnostics:column)
endif()
