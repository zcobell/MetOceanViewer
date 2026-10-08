# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# mov_io: Qt-free file readers/writers on top of mov_core, netCDF-C and PROJ.

find_package(netCDF CONFIG REQUIRED)
# PROJ is a private dependency (core-design.md C19): no PROJ header is public.
# vcpkg builds it without network (curl) and TIFF-grid support. No path of the
# build is compiled in: the application tells mov::io where proj.db is
# (set_projection_data_dir), or $MOV_PROJ_DATA does.
find_package(PROJ CONFIG REQUIRED)
# proj.db in that PROJ install (vcpkg: share/proj), which the application
# ships (cmake/Packaging.cmake) and the projection tests point mov::io at.
find_file(MOV_PROJ_DB proj.db PATHS "${PROJ_DIR}" "${PROJ_DIR}/.." NO_DEFAULT_PATH)

# The netCDF wrapper lives in netcdf/: the only sources that include
# <netcdf.h> (the netcdf_include_gate test checks it).
mov_add_module(
    io
    SOURCES
        atomic_file.cpp
        cf_time.cpp
        error.cpp
        netcdf/attribute.cpp
        netcdf/blocks.cpp
        netcdf/file.cpp
        netcdf/library.cpp
        netcdf/masking.cpp
        netcdf/path.cpp
        netcdf/read.cpp
        netcdf/write.cpp
        projection.cpp
        text.cpp
        text_file.cpp
        warning.cpp
    PUBLIC_LINK mov::core
    PRIVATE_LINK netCDF::netcdf PROJ::proj
)

# ADCIRC ASCII output and the HWM file (WP8).
target_sources(
    mov_io
    PRIVATE model_number.cpp table_error.cpp adcirc_station_file.cpp adcirc_ascii.cpp hwm_file.cpp
)
# IMEDS and CSV (WP7).
target_sources(mov_io PRIVATE csv_export.cpp imeds.cpp imeds_write.cpp station_names.cpp)

# ADCIRC netCDF station output and D-Flow FM history files (WP9).
target_sources(mov_io PRIVATE adcirc_netcdf.cpp adcirc_schema.cpp dflow.cpp model_netcdf.cpp)

# The v5 station netCDF writer and reader (WP10a).
target_sources(
    mov_io
    PRIVATE
        station_netcdf.cpp
        station_netcdf_open.cpp
        station_netcdf_read.cpp
        station_netcdf_samples.cpp
        station_netcdf_write.cpp
)
