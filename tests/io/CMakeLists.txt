# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

mov_add_test(
    mov_io_tests
    SOURCES
        test_netcdf_library.cpp
        test_error.cpp
        test_read.cpp
        test_read_chain.cpp
        test_text.cpp
        test_text_file.cpp
        test_atomic_file.cpp
        test_cf_time.cpp
        test_projection.cpp
    CONSTEXPR_SOURCES test_cf_time_constexpr.cpp
    LIBRARIES mov::io
)

# The projection tests tell mov::io where this build's proj.db is. It sits in
# the PROJ install that find_package found (vcpkg: share/proj).
find_file(MOV_PROJ_DB proj.db PATHS "${PROJ_DIR}" "${PROJ_DIR}/.." NO_DEFAULT_PATH)
if(MOV_PROJ_DB)
    get_filename_component(MOV_TEST_PROJ_DATA_DIR "${MOV_PROJ_DB}" DIRECTORY)
    set_source_files_properties(
        test_projection.cpp
        PROPERTIES COMPILE_DEFINITIONS "MOV_TEST_PROJ_DATA_DIR=\"${MOV_TEST_PROJ_DATA_DIR}\""
    )
else()
    message(
        WARNING
        "proj.db not found under ${PROJ_DIR}: the projection tests that need a "
        "database will fail unless $MOV_PROJ_DATA names its directory."
    )
endif()

# The locale-independence tests need de_DE.UTF-8 (the dev image and CI
# generate it). With the option on they fail, rather than skip, when it is
# missing; MOV_REQUIRE_LOCALES=1 in the environment does the same at run time.
if(MOV_REQUIRE_LOCALES)
    target_compile_definitions(mov_io_tests PRIVATE MOV_REQUIRE_LOCALES=1)
endif()

mov_add_fuzz_test(fuzz_parse_double SOURCES fuzz_parse_double.cpp LIBRARIES mov::io CORPUS io/parse_double)
mov_add_fuzz_test(fuzz_line_cursor SOURCES fuzz_line_cursor.cpp LIBRARIES mov::io CORPUS io/line_cursor)
mov_add_fuzz_test(
    fuzz_parse_cf_time_units
    SOURCES fuzz_parse_cf_time_units.cpp
    LIBRARIES mov::io
    CORPUS io/parse_cf_time_units
)

# ---- the netCDF wrapper (core-design.md section 4) --------------------------
#
# mov_nc_fixtures writes the test files with netCDF-C itself, independent of
# mov::io. mov_io_netcdf_tests may include the wrapper's private headers
# (src/io/netcdf/). On Linux it is linked with --wrap so that every
# nc_open/nc_create/nc_close/nc_abort is counted, and a listener fails any
# test case that ends with a netCDF id open (B5). Other linkers have no
# --wrap; there the counting tests skip.
find_package(netCDF CONFIG REQUIRED)

add_library(mov_nc_fixtures STATIC support/nc_fixtures.cpp support/nc_counts.cpp)
target_include_directories(mov_nc_fixtures PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(mov_nc_fixtures PUBLIC netCDF::netcdf PRIVATE mov::options mov::warnings)
target_compile_features(mov_nc_fixtures PUBLIC cxx_std_23)
mov_enable_static_analysis(mov_nc_fixtures)

mov_add_test(
    mov_io_netcdf_tests
    SOURCES
        test_nc_attributes.cpp
        test_nc_call.cpp
        test_nc_file.cpp
        test_nc_hostile.cpp
        test_nc_masking.cpp
        test_nc_name.cpp
        test_nc_paths.cpp
        test_nc_read.cpp
        test_nc_regression.cpp
        test_nc_typed.cpp
        test_nc_write.cpp
    CONSTEXPR_SOURCES test_nc_constexpr.cpp
    LIBRARIES mov::io mov_nc_fixtures
)
target_include_directories(mov_io_netcdf_tests PRIVATE "${PROJECT_SOURCE_DIR}/src/io/netcdf")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_sources(mov_io_netcdf_tests PRIVATE support/nc_wrap.cpp)
    target_compile_definitions(mov_io_netcdf_tests PRIVATE MOV_TEST_NC_WRAP=1)
    target_link_options(
        mov_io_netcdf_tests
        PRIVATE "LINKER:--wrap=nc_open,--wrap=nc_create,--wrap=nc_close,--wrap=nc_abort"
    )
endif()
