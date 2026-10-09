# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -D "MOV_SOURCE_ROOT=<src>" -P CheckThirdPartyIncludes.cmake
#
# Fails if a C++ file under <src> includes nlohmann/json anywhere but
# <src>/io/json/, or zlib (<zlib.h>, <zconf.h>) anywhere but
# <src>/io/gzip.cpp: both are private dependencies of mov_io, and no public
# header names them (docs/harmonics-engine.md §6.5, docs/providers-design.md
# §2.2). Tests may include them (to build fixtures). Unlike the netCDF gate
# it does not require a use: the readers that include them arrive with the
# harmonics reader (H5) and the NDBC parsers (P4). Registered as the ctest
# test third_party_include_gate.

cmake_minimum_required(VERSION 4.4)

include("${CMAKE_CURRENT_LIST_DIR}/SourceScan.cmake")

if(NOT MOV_SOURCE_ROOT)
    message(FATAL_ERROR "MOV_SOURCE_ROOT is not set")
endif()

set(directive "^[ \t]*#[ \t]*include[ \t]*[<\"]")
set(nlohmann_json_regex "${directive}nlohmann/")
set(nlohmann_json_allowed "^io/json/")
set(zlib_regex "${directive}(zlib|zconf)\\.h[>\"]")
set(zlib_allowed "^io/gzip\\.cpp$")

mov_scan_banned(violations ROOT "${MOV_SOURCE_ROOT}" DIRS . RULES nlohmann_json zlib)

if(violations)
    message(FATAL_ERROR "nlohmann/json or zlib included outside io/json/ and io/gzip.cpp:\n  ${violations}")
endif()
message(STATUS "nlohmann/json and zlib are included only where mov_io allows them under ${MOV_SOURCE_ROOT}")
