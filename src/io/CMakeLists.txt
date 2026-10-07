# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# mov_io: Qt-free file readers/writers on top of mov_core, netCDF-C and PROJ.

find_package(netCDF CONFIG REQUIRED)
# PROJ is a private dependency (core-design.md C19): no PROJ header is public.
# vcpkg builds it without network (curl) and TIFF-grid support.
find_package(PROJ CONFIG REQUIRED)

mov_add_module(
    io
    SOURCES error.cpp netcdf_library.cpp warning.cpp
    PUBLIC_LINK mov::core
    PRIVATE_LINK netCDF::netcdf PROJ::proj
)

# proj.db is looked up in the PROJ install this build found, unless PROJ_DATA
# is set (projection.cpp). A relocated or packaged build must set PROJ_DATA.
find_file(MOV_PROJ_DB proj.db PATHS "${PROJ_DIR}" "${PROJ_DIR}/.." NO_DEFAULT_PATH)
if(MOV_PROJ_DB)
    get_filename_component(MOV_PROJ_DATA_DIR "${MOV_PROJ_DB}" DIRECTORY)
    set_source_files_properties(
        projection.cpp
        PROPERTIES COMPILE_DEFINITIONS "MOV_PROJ_DATA_DIR=\"${MOV_PROJ_DATA_DIR}\""
    )
endif()
