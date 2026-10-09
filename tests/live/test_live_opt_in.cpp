// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The live tier's own plumbing check (tag [live-plumbing], which the
// every-preset plumbing tests select): a live test case runs only when opted
// into. The provider checks of docs/providers-design.md §10.5 join this
// executable without the tag. Makes no request.

#include <catch2/catch_test_macros.hpp>

#include "mov/test/live.hpp"

TEST_CASE("A live test runs only with MOV_LIVE_API=1", "[live-plumbing]") {
  mov::test::require_live_api();
  CHECK(mov::test::live_api_enabled());
}
