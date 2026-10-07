// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <format>
#include <fstream>
#include <string>

#include "mov/core/version.hpp"
#include "mov/test/fixture.hpp"

TEST_CASE("version() and version_number() agree", "[core][version]") {
  const mov::core::VersionNumber number = mov::core::version_number();
  CHECK(mov::core::version() == std::format("{}.{}.{}", number.major_version,
                                            number.minor_version,
                                            number.patch_version));
  CHECK(mov::core::parse_version(mov::core::version()) == number);
}

TEST_CASE("parse_version reads the committed seed corpus",
          "[core][version][fixture]") {
  std::ifstream file{mov::test::fixture("core/parse_version/release.txt")};
  REQUIRE(file.is_open());
  std::string text;
  std::getline(file, text);
  CHECK(mov::core::parse_version(text) ==
        mov::core::VersionNumber{
            .major_version = 4, .minor_version = 5, .patch_version = 1});
}
