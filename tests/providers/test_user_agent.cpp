// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <QString>
#include <catch2/catch_test_macros.hpp>
#include <string>

#include "mov/core/version.hpp"
#include "mov/providers/user_agent.hpp"

TEST_CASE("The User-Agent names the application, its version and its home",
          "[providers][user_agent]") {
  const std::string expected = "MetOceanViewer/" +
                               std::string{mov::core::version()} +
                               " (+https://github.com/zcobell/MetOceanViewer)";
  CHECK(mov::providers::default_user_agent().toStdString() == expected);
}
