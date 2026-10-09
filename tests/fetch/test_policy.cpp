// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "mov/fetch/policy.hpp"

using mov::fetch::classify;
using mov::fetch::TransportErrc;
using mov::fetch::Verdict;

// The same table as test_policy_constexpr.cpp, with run-time arguments.
TEST_CASE("classify(TransportErrc) never accepts, defers or cools down",
          "[fetch][policy]") {
  const TransportErrc code =
      GENERATE(TransportErrc::inactivity_timeout, TransportErrc::too_slow,
               TransportErrc::connection_refused,
               TransportErrc::connection_closed, TransportErrc::host_not_found,
               TransportErrc::tls, TransportErrc::redirect_refused,
               TransportErrc::body_too_large, TransportErrc::other);
  const Verdict verdict = classify(code);
  CHECK((verdict == Verdict::retry or verdict == Verdict::fail));
  const bool transient = code == TransportErrc::inactivity_timeout or
                         code == TransportErrc::too_slow or
                         code == TransportErrc::connection_refused or
                         code == TransportErrc::connection_closed;
  CHECK((verdict == Verdict::retry) == transient);
}
