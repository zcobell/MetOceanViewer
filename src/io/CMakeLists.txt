# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# mov_io: Qt-free file readers/writers on top of mov_core, netCDF-C and PROJ.

find_package(netCDF CONFIG REQUIRED)
# PROJ is a private dependency (core-design.md C19): no PROJ header is public.
# vcpkg builds it without network (curl) and TIFF-grid support. No path of the
# build is compiled in: the application tells mov::io where proj.db is
# (set_projection_data_dir), or $MOV_PROJ_DATA does.
find_package(PROJ CONFIG REQUIRED)

mov_add_module(
    io
    SOURCES atomic_file.cpp cf_time.cpp error.cpp netcdf_library.cpp projection.cpp text.cpp text_file.cpp warning.cpp
    PUBLIC_LINK mov::core
    PRIVATE_LINK netCDF::netcdf PROJ::proj
)
