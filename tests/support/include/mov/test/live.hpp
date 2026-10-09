// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// Opt-in for the tests that talk to the real provider APIs
// (docs/providers-design.md §10.5). They are registered with
// mov_add_test(<name> LIVE ...), labelled `live`, excluded by every test
// preset, and run by the nightly live job with `ctest -L live` and
// MOV_LIVE_API=1. Run any other way they skip.

#include <catch2/catch_test_macros.hpp>
#include <cstdlib>
#include <string_view>

namespace mov::test {

/// Whether MOV_LIVE_API is exactly "1".
[[nodiscard]] inline bool live_api_enabled() {
#ifdef _WIN32
  // std::getenv is C4996 with MSVC, which the warning level makes an error.
  char* buffer = nullptr;
  std::size_t size = 0;
  if (_dupenv_s(&buffer, &size, "MOV_LIVE_API") != 0 or buffer == nullptr) {
    return false;
  }
  const bool enabled = std::string_view{buffer} == "1";
  std::free(buffer);  // NOLINT(cppcoreguidelines-no-malloc,hicpp-no-malloc)
  return enabled;
#else
  const char* value =
      std::getenv("MOV_LIVE_API");  // NOLINT(concurrency-mt-unsafe)
  return value != nullptr and std::string_view{value} == "1";
#endif
}

/// Skips the calling test case unless the live APIs were opted into. Call it
/// first in every live test case.
inline void require_live_api() {
  if (not live_api_enabled()) {
    SKIP("a live-API test: set MOV_LIVE_API=1 to run it");
  }
}

}  // namespace mov::test
