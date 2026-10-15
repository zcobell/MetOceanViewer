# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# mov_core: Qt-free domain model (C++23, standard library only).

mov_add_module(
    core
    SOURCES version.cpp units.cpp quantity.cpp meta.cpp timeseries.cpp station_table.cpp vector_series.cpp
)

# Generated from project(VERSION); private, the public API is mov/core/version.hpp.
configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/version.hpp.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/mov/core/build_info.hpp"
    @ONLY
)
target_include_directories(mov_core PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")

# HWM statistics.
target_sources(mov_core PRIVATE hwm_stats.cpp)

# Series operations and the datum shift.
target_sources(mov_core PRIVATE series_ops.cpp datum_shift.cpp)

# Provider request vocabulary (docs/providers-design.md section 3) and the
# daily series.
target_sources(mov_core PRIVATE daily_series.cpp provider_usgs.cpp)

# Tide engine (docs/harmonics-engine.md section 2): constituents, the
# built-in catalogue, SP 98 astronomy and the yearly nodal scheme.
target_sources(mov_core PRIVATE tide/astronomy.cpp tide/constituent.cpp tide/year_arguments.cpp)
# The harmonics file model (WP H2): the constituent set, licences, datums and
# the file with its views.
target_sources(mov_core PRIVATE tide/harmonics.cpp)
# constituent.cpp includes the catalogue and static_asserts its invariants:
# about 90,000 Clang constexpr steps for the catalogue's initializer and up to
# 200,000 for one check, against cl's documented default of 100,000. A higher
# cap for this TU only, on cl alone (Clang's default is 1,048,576).
# harmonics.cpp reaches the catalogue through builtin.hpp, not catalogue.hpp.
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set_source_files_properties(tide/constituent.cpp PROPERTIES COMPILE_OPTIONS "/constexpr:steps2000000")
endif()
