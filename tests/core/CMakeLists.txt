# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

mov_add_test(
    mov_core_tests
    SOURCES test_version.cpp test_time.cpp test_units.cpp test_datum.cpp test_quantity.cpp test_toolchain_probe.cpp
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

# HWM (WP4). A separate executable keeps this package's edit to the file a pure
# append; it can be folded into mov_core_tests above.
mov_add_test(
    mov_core_hwm_tests
    SOURCES test_hwm.cpp test_hwm_stats.cpp
    CONSTEXPR_SOURCES test_hwm_constexpr.cpp test_hwm_stats_constexpr.cpp
    LIBRARIES mov::core
)

# golden.txt is what golden.py prints; the F8 tests read it. This keeps the two
# from drifting (the script needs only a Python 3 interpreter).
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(
        NAME hwm_golden_is_current
        COMMAND
            ${Python3_EXECUTABLE} -I "${PROJECT_SOURCE_DIR}/tests/fixtures/core/hwm/golden.py" --check
            "${PROJECT_SOURCE_DIR}/tests/fixtures/core/hwm/golden.txt"
    )
else()
    message(STATUS "Python 3 not found: hwm_golden_is_current is not registered")
endif()
