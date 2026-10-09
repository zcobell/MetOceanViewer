# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# The tide engine (docs/harmonics-engine.md section 8.1). nlohmann/json reads
# the schema and the generated dictionary in the drift tests only.
find_package(nlohmann_json CONFIG REQUIRED)

mov_add_test(
    mov_core_tide_tests
    SOURCES test_tide_astronomy.cpp test_tide_catalogue_json.cpp
    CONSTEXPR_SOURCES test_tide_constexpr.cpp
    LIBRARIES mov::core nlohmann_json::nlohmann_json
)

mov_add_fuzz_test(
    fuzz_constituent_name
    SOURCES fuzz_constituent_name.cpp
    LIBRARIES mov::core
    CORPUS core/constituent_name
)
