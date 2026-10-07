// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <filesystem>
#include <string_view>

#include "mov/test/fixtures_dir.hpp"

namespace mov::test {

/// Path of a committed fixture, relative to tests/fixtures/, e.g.
/// fixture("io/imeds/obs.imeds"). Independent of the working directory.
[[nodiscard]] inline std::filesystem::path fixture(std::string_view relative) {
  return std::filesystem::path{fixtures_dir} / relative;
}

}  // namespace mov::test
