// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// Opt-in for the tests that talk to the real provider APIs
// (docs/providers-design.md §10.5). They are registered with
// mov_add_test(<name> LIVE ...) and labelled `live`, which every test preset
// excludes. Run them by build directory, not by preset (a preset's label
// filter also applies to -L, so `ctest --preset <p> -L live` selects
// nothing):
//
//   MOV_LIVE_API=1 ctest --test-dir build/<preset> -L live
//
// Without MOV_LIVE_API=1 every live test case skips, and the label's guard
// test (live_api_opt_in_guard) fails, so a forgotten opt-in is red.

#include <catch2/catch_test_macros.hpp>

#include "mov/test/env.hpp"

namespace mov::test {

/// Whether MOV_LIVE_API is exactly "1".
[[nodiscard]] inline bool live_api_enabled() {
  return env("MOV_LIVE_API") == "1";
}

/// Skips the calling test case unless the live APIs were opted into. Call it
/// first in every live test case.
inline void require_live_api() {
  if (not live_api_enabled()) {
    SKIP("a live-API test: set MOV_LIVE_API=1 to run it");
  }
}

}  // namespace mov::test
