// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace mov::test {

/// The value of the environment variable `name`, or nullopt when it is not
/// set. A copy: a later setenv cannot change it. (MSVC's C4996 on
/// std::getenv is off project-wide through _CRT_SECURE_NO_WARNINGS,
/// cmake/ProjectOptions.cmake.)
[[nodiscard]] inline std::optional<std::string> env(std::string_view name) {
  const std::string terminated{name};
  const char* value = std::getenv(terminated.c_str());
  return value != nullptr ? std::optional<std::string>{value} : std::nullopt;
}

}  // namespace mov::test
