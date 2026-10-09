// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>

#include "mov/core/stop_token.hpp"

using mov::core::StopToken;

TEST_CASE("A default StopToken never requests a stop", "[core][stop_token]") {
  const StopToken token;
  CHECK(not token.stop_requested());
}

TEST_CASE("A StopToken asks its predicate every time", "[core][stop_token]") {
  bool requested = false;
  int asked = 0;
  const StopToken token{[&] {
    ++asked;
    return requested;
  }};
  CHECK(not token.stop_requested());
  requested = true;
  CHECK(token.stop_requested());
  CHECK(asked == 2);
}
