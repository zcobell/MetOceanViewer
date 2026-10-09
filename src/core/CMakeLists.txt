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
