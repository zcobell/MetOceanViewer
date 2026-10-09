# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell
#
# cmake -D "MOV_REPO=<root>" -P CheckThirdPartyIncludes.cmake
#
# Fails if a C++ file under <root>/src includes (#include or #include_next)
# nlohmann/json anywhere but src/io/json/, or zlib (<zlib.h>, <zconf.h>)
# anywhere but src/io/gzip.cpp: both are private dependencies of mov_io, and
# no public header names them (docs/harmonics-engine.md §6.5,
# docs/providers-design.md §2.2). tests/ is not scanned: a test may include
# them to build fixtures. Unlike the netCDF gate it does not require a use,
# since no reader includes them yet (harmonics-engine.md §6.3,
# providers-design.md §4.2). Registered as the ctest test
# third_party_include_gate.

cmake_minimum_required(VERSION 4.4)

include("${CMAKE_CURRENT_LIST_DIR}/SourceScan.cmake")

if(NOT MOV_REPO)
    message(FATAL_ERROR "MOV_REPO is not set")
endif()

set(directive "(^|\n)[ \t]*#[ \t]*include(_next)?[ \t]*[<\"]")
set(nlohmann_json_regex "${directive}nlohmann/")
set(nlohmann_json_allowed "^src/io/json/")
set(zlib_regex "${directive}(zlib|zconf)\\.h[>\"]")
set(zlib_allowed "^src/io/gzip\\.cpp$")

mov_scan_banned(violations ROOT "${MOV_REPO}" DIRS src RULES nlohmann_json zlib)

if(violations)
    message(FATAL_ERROR "nlohmann/json or zlib included outside src/io/json/ and src/io/gzip.cpp:\n  ${violations}")
endif()
message(STATUS "nlohmann/json and zlib are included only where mov_io allows them under ${MOV_REPO}/src")
