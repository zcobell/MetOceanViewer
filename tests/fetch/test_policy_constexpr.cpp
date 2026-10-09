// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks: compile-time in mov_fetch_tests_constexpr, run-time
// in mov_fetch_tests_relaxed_constexpr (see mov_add_test in
// tests/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>

#include "mov/fetch/policy.hpp"

using mov::fetch::classify;
using mov::fetch::TransportErrc;
using mov::fetch::Verdict;

TEST_CASE("A transport failure the next attempt may not repeat is retried",
          "[fetch][policy]") {
  STATIC_REQUIRE(classify(TransportErrc::inactivity_timeout) == Verdict::retry);
  STATIC_REQUIRE(classify(TransportErrc::too_slow) == Verdict::retry);
  STATIC_REQUIRE(classify(TransportErrc::connection_refused) == Verdict::retry);
  STATIC_REQUIRE(classify(TransportErrc::connection_closed) == Verdict::retry);
}

TEST_CASE("A transport failure the next attempt would repeat fails at once",
          "[fetch][policy]") {
  STATIC_REQUIRE(classify(TransportErrc::host_not_found) == Verdict::fail);
  STATIC_REQUIRE(classify(TransportErrc::tls) == Verdict::fail);
  STATIC_REQUIRE(classify(TransportErrc::redirect_refused) == Verdict::fail);
  STATIC_REQUIRE(classify(TransportErrc::body_too_large) == Verdict::fail);
  STATIC_REQUIRE(classify(TransportErrc::other) == Verdict::fail);
}
