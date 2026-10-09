# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# The tide engine (docs/harmonics-engine.md section 8.1). nlohmann/json reads
# the schema and the generated dictionary in the drift tests only.
find_package(nlohmann_json CONFIG REQUIRED)

mov_add_test(
    mov_core_tide_tests
    SOURCES test_tide_astronomy.cpp test_tide_catalogue.cpp test_tide_catalogue_json.cpp
    CONSTEXPR_SOURCES test_tide_constexpr.cpp
    LIBRARIES mov::core nlohmann_json::nlohmann_json
)

# Every TU that includes mov/core/tide/catalogue.hpp evaluates the catalogue's
# initializer (about 90,000 Clang constexpr steps), and the constexpr tests
# add more, against cl's documented default of 100,000 steps. A higher cap for
# these TUs only, on cl alone (clang-cl and Clang use 1,048,576 by default).
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set_source_files_properties(
        test_tide_astronomy.cpp
        test_tide_catalogue.cpp
        test_tide_catalogue_json.cpp
        test_tide_constexpr.cpp
        PROPERTIES COMPILE_OPTIONS "/constexpr:steps2000000"
    )
endif()

mov_add_fuzz_test(
    fuzz_constituent_name
    SOURCES fuzz_constituent_name.cpp
    LIBRARIES mov::core
    CORPUS core/constituent_name
)
