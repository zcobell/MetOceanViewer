// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks: compile-time in mov_core_tests_constexpr, run-time
// in mov_core_tests_relaxed_constexpr (see mov_add_test in
// tests/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>
#include <optional>

#include "mov/core/version.hpp"

using mov::core::parse_version;
using mov::core::VersionNumber;

TEST_CASE("parse_version accepts major.minor.patch", "[core][version]") {
  STATIC_REQUIRE(parse_version("5.0.0") == VersionNumber{.major_version = 5,
                                                         .minor_version = 0,
                                                         .patch_version = 0});
  STATIC_REQUIRE(parse_version("10.20.300") ==
                 VersionNumber{.major_version = 10,
                               .minor_version = 20,
                               .patch_version = 300});
  STATIC_REQUIRE(parse_version("1.2.3") < parse_version("1.10.0"));
}

TEST_CASE("parse_version rejects anything else", "[core][version]") {
  STATIC_REQUIRE(parse_version("") == std::nullopt);
  STATIC_REQUIRE(parse_version("5") == std::nullopt);
  STATIC_REQUIRE(parse_version("5.0") == std::nullopt);
  STATIC_REQUIRE(parse_version("5.0.0.0") == std::nullopt);
  STATIC_REQUIRE(parse_version("5..0") == std::nullopt);
  STATIC_REQUIRE(parse_version("-5.0.0") == std::nullopt);
  STATIC_REQUIRE(parse_version("5.0.0 ") == std::nullopt);
  STATIC_REQUIRE(parse_version("1234567890.0.0") == std::nullopt);
}
