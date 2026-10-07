# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

mov_add_test(
    mov_core_tests
    SOURCES
        test_version.cpp
        test_time.cpp
        test_units.cpp
        test_datum.cpp
        test_quantity.cpp
        test_toolchain_probe.cpp
        test_meta.cpp
        test_timeseries.cpp
        test_station.cpp
        test_station_table.cpp
        test_vector_series.cpp
    CONSTEXPR_SOURCES
        test_version_constexpr.cpp
        test_numeric_constexpr.cpp
        test_time_constexpr.cpp
        test_sample_constexpr.cpp
        test_geo_constexpr.cpp
        test_units_constexpr.cpp
        test_datum_constexpr.cpp
        test_quantity_constexpr.cpp
        test_toolchain_probe_constexpr.cpp
        test_timeseries_constexpr.cpp
        test_station_constexpr.cpp
        test_station_table_constexpr.cpp
    LIBRARIES mov::core
)

mov_add_fuzz_test(fuzz_parse_version SOURCES fuzz_parse_version.cpp LIBRARIES mov::core CORPUS core/parse_version)
mov_add_fuzz_test(fuzz_parse_unit SOURCES fuzz_parse_unit.cpp LIBRARIES mov::core CORPUS core/parse_unit)
mov_add_fuzz_test(
    fuzz_parse_utc_datetime
    SOURCES fuzz_parse_utc_datetime.cpp
    LIBRARIES mov::core
    CORPUS core/parse_utc_datetime
)
mov_add_fuzz_test(
    fuzz_parse_vertical_datum
    SOURCES fuzz_parse_vertical_datum.cpp
    LIBRARIES mov::core
    CORPUS core/parse_vertical_datum
)
mov_add_fuzz_test(
    fuzz_parse_quantity_token
    SOURCES fuzz_parse_quantity_token.cpp
    LIBRARIES mov::core
    CORPUS core/parse_quantity_token
)
mov_add_fuzz_test(fuzz_checked_time SOURCES fuzz_checked_time.cpp LIBRARIES mov::core CORPUS core/checked_time)
mov_add_fuzz_test(fuzz_normalize SOURCES fuzz_normalize.cpp LIBRARIES mov::core CORPUS core/normalize)
