# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# mov_io: Qt-free file readers/writers on top of mov_core and netCDF-C.

find_package(netCDF CONFIG REQUIRED)

mov_add_module(io SOURCES netcdf_library.cpp PUBLIC_LINK mov::core PRIVATE_LINK netCDF::netcdf)
