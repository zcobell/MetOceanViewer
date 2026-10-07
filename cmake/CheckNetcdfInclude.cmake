# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -D "MOV_SOURCE_ROOT=<src>" -P CheckNetcdfInclude.cmake
#
# Fails if a C++ file under <src> includes a netCDF-C header (<netcdf.h>,
# <netcdf_meta.h>, <netcdf_mem.h>, ...) anywhere but <src>/io/netcdf/*.cpp:
# the wrapper is the only code that talks to netCDF-C, and its headers stay
# free of it (core-design.md section 4.1). Also fails if <src> is missing or
# no file includes netCDF-C at all, so a moved wrapper cannot pass vacuously.
# Registered as the ctest test netcdf_include_gate.

set(netcdf_include_regex "^[ \t]*#[ \t]*include[ \t]*[<\"]netcdf[A-Za-z0-9_]*\\.h[>\"]")
set(extensions
    cpp
    cc
    cxx
    hpp
    hh
    hxx
    h
    ipp
    inl
    tpp
)

if(NOT MOV_SOURCE_ROOT OR NOT IS_DIRECTORY "${MOV_SOURCE_ROOT}")
    message(FATAL_ERROR "Source directory '${MOV_SOURCE_ROOT}' does not exist")
endif()

set(globs "")
foreach(extension IN LISTS extensions)
    list(APPEND globs "${MOV_SOURCE_ROOT}/*.${extension}")
endforeach()
file(GLOB_RECURSE sources ${globs})

set(allowed_regex "^${MOV_SOURCE_ROOT}/io/netcdf/[^/]+\\.cpp$")
set(violations "")
set(allowed_includes 0)
foreach(source IN LISTS sources)
    file(STRINGS "${source}" lines REGEX "${netcdf_include_regex}")
    if(NOT lines)
        continue()
    endif()
    if(source MATCHES "${allowed_regex}")
        math(EXPR allowed_includes "${allowed_includes} + 1")
        continue()
    endif()
    foreach(line IN LISTS lines)
        list(APPEND violations "  ${source}: ${line}")
    endforeach()
endforeach()

if(violations)
    list(JOIN violations "\n" report)
    message(FATAL_ERROR "netCDF-C included outside io/netcdf/*.cpp:\n${report}")
endif()
if(allowed_includes EQUAL 0)
    message(FATAL_ERROR "No file under ${MOV_SOURCE_ROOT}/io/netcdf/ includes netCDF-C")
endif()
message(STATUS "netCDF-C is included only by ${allowed_includes} files in ${MOV_SOURCE_ROOT}/io/netcdf/")
