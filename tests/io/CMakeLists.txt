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

# The projection tests tell mov::io where this build's proj.db is
# (MOV_PROJ_DB, found by src/io/CMakeLists.txt in the PROJ install).
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

# ADCIRC ASCII output and the HWM file (WP8).
mov_add_test(
    mov_io_model_text_tests
    SOURCES
        test_model_number.cpp
        test_table_error.cpp
        test_adcirc_station_file.cpp
        test_adcirc_ascii.cpp
        test_hwm_file.cpp
        test_hwm_golden.cpp
        test_throughput.cpp
    LIBRARIES mov::io
)
if(MOV_PROJ_DB)
    set_source_files_properties(
        test_adcirc_station_file.cpp
        PROPERTIES COMPILE_DEFINITIONS "MOV_TEST_PROJ_DATA_DIR=\"${MOV_TEST_PROJ_DATA_DIR}\""
    )
endif()

# Replaces the global operator new to count allocations: its own executable.
mov_add_test(mov_io_alloc_tests SOURCES test_adcirc_allocation.cpp LIBRARIES mov::io)

mov_add_fuzz_test(
    fuzz_parse_adcirc_ascii
    SOURCES fuzz_parse_adcirc_ascii.cpp
    LIBRARIES mov::io
    CORPUS io/parse_adcirc_ascii
)
mov_add_fuzz_test(
    fuzz_parse_adcirc_station_file
    SOURCES fuzz_parse_adcirc_station_file.cpp
    LIBRARIES mov::io
    CORPUS io/parse_adcirc_station_file
)
mov_add_fuzz_test(fuzz_parse_hwm_csv SOURCES fuzz_parse_hwm_csv.cpp LIBRARIES mov::io CORPUS io/parse_hwm_csv)

# IMEDS and CSV (WP7).
mov_add_test(
    mov_io_text_formats_tests
    SOURCES test_imeds.cpp test_imeds_format.cpp test_csv_export.cpp test_text_helpers.cpp test_civil_time.cpp
    LIBRARIES mov::io
)
mov_add_fuzz_test(fuzz_parse_imeds SOURCES fuzz_parse_imeds.cpp LIBRARIES mov::io CORPUS io/imeds)

# ---- the netCDF wrapper (core-design.md section 4) --------------------------
#
# mov_nc_fixtures writes the test files with netCDF-C itself, independent of
# mov::io. mov_io_netcdf_tests may include the wrapper's private headers
# (src/io/netcdf/). On Linux it is linked with --wrap so that every
# nc_open/nc_create/nc_close/nc_abort is counted (and nc_sync, nc_close and
# remove can be observed or made to fail), and a listener fails any test
# case that ends with a netCDF id open (B5). Other linkers have no
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
        PRIVATE "LINKER:--wrap=nc_open,--wrap=nc_create,--wrap=nc_close,--wrap=nc_abort,--wrap=nc_sync,--wrap=remove"
    )
endif()

# ---- ADCIRC netCDF station output and D-Flow FM history files (WP9) -------------
#
# The fixtures are written at test time with netCDF-C itself
# (support/model_fixtures.cpp, in mov_nc_fixtures). The [legacy] tests read the
# 4 MB ADCIRC files of the legacy tree and skip when it is absent.
target_sources(mov_nc_fixtures PRIVATE support/model_fixtures.cpp)

mov_add_test(
    mov_io_model_netcdf_tests
    SOURCES measure_adcirc_netcdf.cpp test_adcirc_netcdf.cpp test_dflow.cpp test_station_groups.cpp
    LIBRARIES mov::io mov_nc_fixtures
)
target_compile_definitions(mov_io_model_netcdf_tests PRIVATE MOV_TEST_REPO_DIR="${PROJECT_SOURCE_DIR}")
if(MOV_PROJ_DB)
    target_compile_definitions(mov_io_model_netcdf_tests PRIVATE MOV_TEST_PROJ_DATA_DIR="${MOV_TEST_PROJ_DATA_DIR}")
endif()

# The model readers close their file on every path (needs the --wrap shims).
target_sources(mov_io_netcdf_tests PRIVATE test_model_nc_handles.cpp)

# ---- the v5 station netCDF format (WP10a) ------------------------------------
#
# mov_station_nc_canonical holds the canonical files of docs/station-netcdf.md
# section 9, written with mov::io; the golden CDL tests and the
# format-compliance job (station_nc_fixtures + tools/check_cf.py) share it.
# The header dumper and the raw editor are in mov_nc_fixtures (netCDF-C only).
target_sources(mov_nc_fixtures PRIVATE support/nc_header_dump.cpp)

add_library(mov_station_nc_canonical STATIC support/station_nc_canonical.cpp)
target_include_directories(mov_station_nc_canonical PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/support")
target_link_libraries(mov_station_nc_canonical PUBLIC mov::io PRIVATE mov::options mov::warnings)
target_compile_features(mov_station_nc_canonical PUBLIC cxx_std_23)
mov_enable_static_analysis(mov_station_nc_canonical)

mov_add_test(
    mov_io_station_netcdf_tests
    SOURCES
        test_station_netcdf_golden.cpp
        test_station_netcdf_read.cpp
        test_station_netcdf_validation.cpp
        test_station_netcdf_write.cpp
    LIBRARIES mov::io mov_nc_fixtures mov_station_nc_canonical
)
if(MOV_PROJ_DB)
    target_compile_definitions(mov_io_station_netcdf_tests PRIVATE MOV_TEST_PROJ_DATA_DIR="${MOV_TEST_PROJ_DATA_DIR}")
endif()

# Writes the canonical files for tools/check_cf.py (not a test).
add_executable(station_nc_fixtures station_nc_fixtures.cpp)
target_link_libraries(station_nc_fixtures PRIVATE mov_station_nc_canonical mov_nc_fixtures mov::options mov::warnings)
mov_enable_static_analysis(station_nc_fixtures)

# ---- foreign CF, legacy v4 and hostile station netCDF files (WP10b) -----------
#
# The fixtures are written at test time with netCDF-C itself (support/nc_build.hpp,
# support/legacy_fixtures.cpp), independent of mov::io.
target_sources(mov_nc_fixtures PRIVATE support/foreign_fixtures.cpp support/legacy_fixtures.cpp)
target_sources(
    mov_io_station_netcdf_tests
    PRIVATE
        test_file_type.cpp
        test_station_netcdf_foreign.cpp
        test_station_netcdf_foreign_names.cpp
        test_station_netcdf_hostile.cpp
        test_station_netcdf_legacy.cpp
)
target_sources(mov_io_netcdf_tests PRIVATE test_station_nc_handles.cpp)
target_link_libraries(mov_io_netcdf_tests PRIVATE mov_station_nc_canonical)

# The structure fuzzer (core-design.md 7.5): bytes -> a netCDF schema written
# under the build directory -> every netCDF reader. The ASan allocation cap is
# set by the target itself.
mov_add_fuzz_test(
    fuzz_station_netcdf_structure
    SOURCES fuzz_station_netcdf_structure.cpp
    LIBRARIES mov::io netCDF::netcdf
    CORPUS io/station_netcdf_structure
)
if(TARGET fuzz_station_netcdf_structure)
    target_compile_definitions(
        fuzz_station_netcdf_structure
        PRIVATE MOV_FUZZ_SCRATCH_DIR="${CMAKE_CURRENT_BINARY_DIR}/fuzz_station_netcdf_structure.scratch"
    )
endif()
