# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Zach Cobell

# mov::test_support: helpers shared by every test executable.

set(MOV_TEST_FIXTURES_DIR "${PROJECT_SOURCE_DIR}/tests/fixtures")
configure_file(fixtures_dir.hpp.in "${CMAKE_CURRENT_BINARY_DIR}/generated/mov/test/fixtures_dir.hpp" @ONLY)

add_library(mov_test_support INTERFACE)
add_library(mov::test_support ALIAS mov_test_support)
target_include_directories(
    mov_test_support
    INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include" "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_compile_features(mov_test_support INTERFACE cxx_std_23)
