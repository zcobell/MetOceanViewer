# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

mov_add_test(
    mov_core_tests
    SOURCES test_version.cpp test_time.cpp test_units.cpp test_datum.cpp test_quantity.cpp test_toolchain_probe.cpp
    CONSTEXPR_SOURCES
        test_version_constexpr.cpp
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
