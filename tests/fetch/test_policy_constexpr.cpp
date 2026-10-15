// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks: compile-time in mov_fetch_tests_constexpr, run-time
// in mov_fetch_tests_relaxed_constexpr (see mov_add_test in
// tests/CMakeLists.txt).

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>

#include "mov/fetch/limits.hpp"
#include "mov/fetch/policy.hpp"
#include "mov/fetch/retry.hpp"
#include "mov/io/wire/service.hpp"

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

TEST_CASE(
    "A gateway failure and station_or_system are retried; other "
    "refusals fail",
    "[fetch][policy]") {
  using mov::io::wire::ServiceErrc;
  STATIC_REQUIRE(classify(ServiceErrc::gateway) == Verdict::retry);
  STATIC_REQUIRE(classify(ServiceErrc::station_or_system) == Verdict::retry);
  STATIC_REQUIRE(classify(ServiceErrc::datum_unavailable) == Verdict::fail);
  STATIC_REQUIRE(classify(ServiceErrc::range_exceeded) == Verdict::fail);
  STATIC_REQUIRE(classify(ServiceErrc::invalid_parameter) == Verdict::fail);
  STATIC_REQUIRE(classify(ServiceErrc::api_key_invalid) == Verdict::fail);
  STATIC_REQUIRE(classify(ServiceErrc::unexpected_content) == Verdict::fail);
  STATIC_REQUIRE(classify(ServiceErrc::unknown) == Verdict::fail);
}

// ---- HTTP statuses --------------------------------------------------------

using mov::fetch::Origin;

TEST_CASE("2xx is accepted at every origin", "[fetch][policy]") {
  STATIC_REQUIRE(classify(Origin::coops_data, 200) == Verdict::accept);
  STATIC_REQUIRE(classify(Origin::usgs, 204) == Verdict::accept);
  STATIC_REQUIRE(classify(Origin::ndbc, 299) == Verdict::accept);
  STATIC_REQUIRE(classify(Origin::github, 199) == Verdict::fail);
  STATIC_REQUIRE(classify(Origin::github, 300) == Verdict::fail);
}

TEST_CASE("429 cools down; 500, 502, 503, 504 are retried", "[fetch][policy]") {
  STATIC_REQUIRE(classify(Origin::usgs, 429) == Verdict::cool_down);
  STATIC_REQUIRE(classify(Origin::coops_data, 500) == Verdict::retry);
  STATIC_REQUIRE(classify(Origin::coops_metadata, 502) == Verdict::retry);
  STATIC_REQUIRE(classify(Origin::ndbc, 503) == Verdict::retry);
  STATIC_REQUIRE(classify(Origin::github, 504) == Verdict::retry);
  STATIC_REQUIRE(classify(Origin::usgs, 501) == Verdict::fail);
  STATIC_REQUIRE(classify(Origin::usgs, 505) == Verdict::fail);
}

TEST_CASE("404 is absent at NDBC only", "[fetch][policy]") {
  STATIC_REQUIRE(classify(Origin::ndbc, 404) == Verdict::absent);
  STATIC_REQUIRE(classify(Origin::coops_data, 404) == Verdict::fail);
  STATIC_REQUIRE(classify(Origin::usgs, 404) == Verdict::fail);
  STATIC_REQUIRE(classify(Origin::github, 404) == Verdict::fail);
}

TEST_CASE("other statuses fail, out-of-range ones included",
          "[fetch][policy]") {
  STATIC_REQUIRE(classify(Origin::usgs, 302) == Verdict::fail);
  STATIC_REQUIRE(classify(Origin::usgs, 304) == Verdict::fail);
  STATIC_REQUIRE(classify(Origin::usgs, 400) == Verdict::fail);
  STATIC_REQUIRE(classify(Origin::usgs, 403) == Verdict::fail);
  STATIC_REQUIRE(classify(Origin::ndbc, 0) == Verdict::fail);
  STATIC_REQUIRE(classify(Origin::ndbc, -200) == Verdict::fail);
  STATIC_REQUIRE(classify(Origin::ndbc, 1200) == Verdict::fail);
}

// ---- origin policies ------------------------------------------------------

using mov::fetch::KeyPresence;
using mov::fetch::OriginPolicy;
using mov::fetch::policy;
using std::chrono::milliseconds;

TEST_CASE("each origin's concurrency and spacing", "[fetch][policy]") {
  STATIC_REQUIRE(
      policy(Origin::coops_data, KeyPresence::none) ==
      OriginPolicy{.max_in_flight = 2, .min_spacing = milliseconds{200}});
  STATIC_REQUIRE(
      policy(Origin::coops_metadata, KeyPresence::present) ==
      OriginPolicy{.max_in_flight = 2, .min_spacing = milliseconds{200}});
  STATIC_REQUIRE(
      policy(Origin::usgs, KeyPresence::none) ==
      OriginPolicy{.max_in_flight = 2, .min_spacing = milliseconds{0}});
  STATIC_REQUIRE(
      policy(Origin::usgs, KeyPresence::present) ==
      OriginPolicy{.max_in_flight = 4, .min_spacing = milliseconds{0}});
  STATIC_REQUIRE(
      policy(Origin::ndbc, KeyPresence::none) ==
      OriginPolicy{.max_in_flight = 1, .min_spacing = milliseconds{250}});
  STATIC_REQUIRE(
      policy(Origin::github, KeyPresence::none) ==
      OriginPolicy{.max_in_flight = 1, .min_spacing = milliseconds{0}});
}

// ---- retry policy and backoff ---------------------------------------------

using mov::fetch::backoff;
using mov::fetch::Jitter;
using mov::fetch::RetryPolicy;
using mov::fetch::RetryPolicyError;
using std::chrono::seconds;

TEST_CASE("the default retry policy", "[fetch][retry]") {
  constexpr RetryPolicy p = RetryPolicy::defaults();
  STATIC_REQUIRE(p.max_attempts() == 4);
  STATIC_REQUIRE(p.base() == seconds{1});
  STATIC_REQUIRE(p.cap() == seconds{30});
  STATIC_REQUIRE(p.max_retry_after() == seconds{120});
  STATIC_REQUIRE(RetryPolicy::make({}) == p);
}

TEST_CASE("a retry policy out of bounds is refused", "[fetch][retry]") {
  STATIC_REQUIRE(RetryPolicy::make({.max_attempts = 0}).error() ==
                 RetryPolicyError::attempts_out_of_range);
  STATIC_REQUIRE(RetryPolicy::make({.max_attempts = 17}).error() ==
                 RetryPolicyError::attempts_out_of_range);
  STATIC_REQUIRE(RetryPolicy::make({.max_attempts = 16}).has_value());
  STATIC_REQUIRE(RetryPolicy::make({.base = milliseconds{0}}).error() ==
                 RetryPolicyError::base_out_of_range);
  STATIC_REQUIRE(RetryPolicy::make({.base = seconds{31}}).error() ==
                 RetryPolicyError::base_out_of_range);
  STATIC_REQUIRE(RetryPolicy::make({.cap = std::chrono::hours{2}}).error() ==
                 RetryPolicyError::cap_out_of_range);
  STATIC_REQUIRE(RetryPolicy::make({.max_retry_after = seconds{0}}).error() ==
                 RetryPolicyError::retry_after_out_of_range);
  STATIC_REQUIRE(
      RetryPolicy::make({.max_retry_after = std::chrono::hours{25}}).error() ==
      RetryPolicyError::retry_after_out_of_range);
}

TEST_CASE("jitter is a number in [0, 1)", "[fetch][retry]") {
  STATIC_REQUIRE(Jitter::make(0.0).has_value());
  STATIC_REQUIRE(Jitter::make(0.999).has_value());
  STATIC_REQUIRE_FALSE(Jitter::make(1.0).has_value());
  STATIC_REQUIRE_FALSE(Jitter::make(-0.001).has_value());
  STATIC_REQUIRE(Jitter::from_bits(0).value() == 0.0);
  STATIC_REQUIRE(Jitter::from_bits(~std::uint64_t{0}).value() < 1.0);
  STATIC_REQUIRE(Jitter::from_bits(std::uint64_t{1} << 63U).value() == 0.5);
}

TEST_CASE("backoff is full jitter under a doubling, capped ceiling",
          "[fetch][retry]") {
  constexpr RetryPolicy p = RetryPolicy::defaults();
  constexpr Jitter half = Jitter::from_bits(std::uint64_t{1} << 63U);
  constexpr Jitter top = Jitter::from_bits(~std::uint64_t{0});
  STATIC_REQUIRE(backoff(p, 1, std::nullopt, half) == milliseconds{500});
  STATIC_REQUIRE(backoff(p, 2, std::nullopt, half) == milliseconds{1000});
  STATIC_REQUIRE(backoff(p, 3, std::nullopt, half) == milliseconds{2000});
  STATIC_REQUIRE(backoff(p, 6, std::nullopt, half) == milliseconds{15000});
  STATIC_REQUIRE(backoff(p, 7, std::nullopt, half) == milliseconds{15000});
  STATIC_REQUIRE(backoff(p, 1, std::nullopt, Jitter::from_bits(0)) ==
                 milliseconds{0});
  // The exponent is clamped: no overflow however many attempts.
  STATIC_REQUIRE(backoff(p, 1000, std::nullopt, half) == milliseconds{15000});
  STATIC_REQUIRE(backoff(p, -5, std::nullopt, half) == milliseconds{500});
  // The largest jitter stays below the ceiling.
  STATIC_REQUIRE(backoff(p, 9, std::nullopt, top) < milliseconds{30000});
}

TEST_CASE("a server's Retry-After is honored as it is", "[fetch][retry]") {
  constexpr RetryPolicy p = RetryPolicy::defaults();
  STATIC_REQUIRE(backoff(p, 3, seconds{7}, Jitter::from_bits(0)) ==
                 milliseconds{7000});
}

// ---- exchange limits ------------------------------------------------------

using mov::fetch::ExchangeLimits;
using mov::fetch::ThroughputCheck;

namespace {

constexpr ExchangeLimits small_limits{.inactivity = seconds{30},
                                      .min_bytes_per_window = 100,
                                      .throughput_window = seconds{60},
                                      .max_body_bytes = 1000};

/// What the transport saw: the time since start and the bytes so far.
struct Report {
  std::int64_t at;
  std::uint64_t total;
};

constexpr Report r(std::int64_t at, std::uint64_t total) {
  return {.at = at, .total = total};
}

/// The verdict after a series of reports.
template <class... R>
constexpr std::optional<TransportErrc> run_reports(R... reports) {
  ThroughputCheck check{small_limits};
  for (const Report& report : {reports...}) {
    if (const auto failed =
            check.on_progress(milliseconds{report.at}, report.total)) {
      return failed;
    }
  }
  return std::nullopt;
}

}  // namespace

TEST_CASE("the default exchange limits", "[fetch][limits]") {
  constexpr ExchangeLimits limits{};
  STATIC_REQUIRE(limits.inactivity == seconds{30});
  STATIC_REQUIRE(limits.min_bytes_per_window == 65536);
  STATIC_REQUIRE(limits.throughput_window == seconds{60});
  STATIC_REQUIRE(limits.max_body_bytes == 67108864);
}

TEST_CASE("the throughput floor fails a drip, not a slow start",
          "[fetch][limits]") {
  // Nothing for 90 s, then a fast stream: the clock starts at the first byte.
  STATIC_REQUIRE_FALSE(
      run_reports(r(0, 0), r(90'000, 50), r(120'000, 500)).has_value());
  // 99 bytes in the first 60 s after the first byte: too slow.
  STATIC_REQUIRE(run_reports(r(1'000, 1), r(61'000, 99)) ==
                 TransportErrc::too_slow);
  // 100 bytes: enough; the next window starts there.
  STATIC_REQUIRE_FALSE(run_reports(r(1'000, 1), r(61'000, 100)).has_value());
  STATIC_REQUIRE(run_reports(r(1'000, 1), r(61'000, 100), r(121'000, 150)) ==
                 TransportErrc::too_slow);
  // A report inside the window decides nothing.
  STATIC_REQUIRE_FALSE(run_reports(r(1'000, 1), r(59'999, 2)).has_value());
  // Bytes that go back count as none.
  STATIC_REQUIRE(run_reports(r(1'000, 1), r(61'000, 200), r(121'000, 10)) ==
                 TransportErrc::too_slow);
}

TEST_CASE("the body cap fails at once", "[fetch][limits]") {
  STATIC_REQUIRE(run_reports(r(10, 1001)) == TransportErrc::body_too_large);
  STATIC_REQUIRE_FALSE(run_reports(r(10, 1000)).has_value());
}
