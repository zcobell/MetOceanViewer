// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The admission gate (docs/providers-design.md §5.3): per-origin limits,
// spacing, cool-downs, withdrawal, turns across jobs, and background work
// that still progresses behind interactive work.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include "gate_driver.hpp"
#include "mov/fetch/gate.hpp"

namespace {

using namespace mov::fetch;
using std::chrono::milliseconds;

const mov::core::Time t0{
    std::chrono::sys_days{std::chrono::year{2026} / std::chrono::October / 1}};

Ticket ticket(std::uint32_t job, std::uint32_t id, Origin origin,
              Priority priority = Priority::interactive) {
  return {.job = JobId{.value = job},
          .exchange = ExchangeId{.value = id},
          .origin = origin,
          .priority = priority};
}

Admit admit(std::uint32_t job, std::uint32_t id) {
  return {.job = JobId{.value = job}, .exchange = ExchangeId{.value = id}};
}

}  // namespace

TEST_CASE("an origin admits up to max_in_flight, then on release",
          "[fetch][gate]") {
  OriginGate gate{KeyPresence::none};
  CHECK(gate.enqueue(ticket(0, 1, Origin::usgs), t0) ==
        std::vector<Admit>{admit(0, 1)});
  CHECK(gate.enqueue(ticket(0, 2, Origin::usgs), t0) ==
        std::vector<Admit>{admit(0, 2)});
  CHECK(gate.enqueue(ticket(0, 3, Origin::usgs), t0).empty());  // 2 in flight
  CHECK(gate.queued(Origin::usgs) == 1);
  CHECK_FALSE(gate.next_wake());  // waits for a release, not the clock
  CHECK(gate.release(JobId{.value = 0}, ExchangeId{.value = 1}, t0) ==
        std::vector<Admit>{admit(0, 3)});
  CHECK(gate.in_flight(Origin::usgs) == 2);
}

TEST_CASE("starts are spaced and the gate says when to tick", "[fetch][gate]") {
  OriginGate gate{KeyPresence::none};
  CHECK(gate.enqueue(ticket(0, 1, Origin::ndbc), t0).size() == 1);
  CHECK(gate.release(JobId{.value = 0}, ExchangeId{.value = 1}, t0).empty());
  CHECK(gate.enqueue(ticket(0, 2, Origin::ndbc), t0).empty());  // 250 ms apart
  CHECK(gate.next_wake() == t0 + milliseconds{250});
  CHECK(gate.tick(t0 + milliseconds{249}).empty());
  CHECK(gate.tick(t0 + milliseconds{250}) == std::vector<Admit>{admit(0, 2)});
}

TEST_CASE("a cool-down holds queued tickets", "[fetch][gate]") {
  OriginGate gate{KeyPresence::present};
  CHECK(gate.cool_down(Origin::usgs, milliseconds{5000}, t0).empty());
  CHECK(gate.enqueue(ticket(0, 1, Origin::usgs), t0).empty());
  CHECK(gate.next_wake() == t0 + milliseconds{5000});
  // A shorter cool-down does not shorten it.
  CHECK(gate.cool_down(Origin::usgs, milliseconds{10}, t0).empty());
  CHECK(gate.tick(t0 + milliseconds{5000}) == std::vector<Admit>{admit(0, 1)});
}

TEST_CASE("a withdrawn or aborted ticket is never admitted", "[fetch][gate]") {
  OriginGate gate{KeyPresence::none};
  CHECK(gate.enqueue(ticket(0, 1, Origin::github), t0).size() == 1);
  CHECK(gate.enqueue(ticket(0, 2, Origin::github), t0).empty());
  CHECK(gate.enqueue(ticket(1, 3, Origin::github), t0).empty());
  CHECK(gate.release(JobId{.value = 0}, ExchangeId{.value = 2}, t0).empty());
  // Job 0 is aborted: its ticket in flight frees the origin for job 1.
  CHECK(gate.abort_job(JobId{.value = 0}, t0) ==
        std::vector<Admit>{admit(1, 3)});
}

TEST_CASE("jobs take turns within a class", "[fetch][gate]") {
  OriginGate gate{KeyPresence::none};
  // github admits one at a time.
  CHECK(gate.enqueue(ticket(0, 1, Origin::github), t0).size() == 1);
  CHECK(gate.enqueue(ticket(0, 2, Origin::github), t0).empty());
  CHECK(gate.enqueue(ticket(0, 3, Origin::github), t0).empty());
  CHECK(gate.enqueue(ticket(1, 4, Origin::github), t0).empty());
  std::vector<Admit> order;
  ExchangeId current{.value = 1};
  JobId current_job{.value = 0};
  for (int i = 0; i < 3; ++i) {
    const auto next = gate.release(current_job, current, t0);
    REQUIRE(next.size() == 1);
    order.push_back(next[0]);
    current = next[0].exchange;
    current_job = next[0].job;
  }
  CHECK(order == std::vector<Admit>{admit(0, 2), admit(1, 4), admit(0, 3)});
}

TEST_CASE("interactive goes first, background still progresses",
          "[fetch][gate]") {
  OriginGate gate{KeyPresence::none, 2};
  CHECK(gate.enqueue(ticket(0, 1, Origin::github), t0).size() == 1);
  CHECK(gate.enqueue(ticket(9, 100, Origin::github, Priority::background), t0)
            .empty());
  for (std::uint32_t i = 2; i < 6; ++i) {
    CHECK(gate.enqueue(ticket(0, i, Origin::github), t0).empty());
  }
  std::vector<Admit> order;
  Admit current = admit(0, 1);
  for (int i = 0; i < 5; ++i) {
    const auto next = gate.release(current.job, current.exchange, t0);
    REQUIRE(next.size() == 1);
    order.push_back(next[0]);
    current = next[0];
  }
  CHECK(order == std::vector<Admit>{admit(0, 2), admit(0, 3), admit(9, 100),
                                    admit(0, 4), admit(0, 5)});
}

TEST_CASE("a feed back in time is taken as the latest seen", "[fetch][gate]") {
  OriginGate gate{KeyPresence::none};
  CHECK(gate.enqueue(ticket(0, 1, Origin::ndbc), t0 + milliseconds{1000})
            .size() == 1);
  CHECK(gate.release(JobId{.value = 0}, ExchangeId{.value = 1}, t0).empty());
  CHECK(gate.enqueue(ticket(0, 2, Origin::ndbc), t0).empty());
  CHECK(gate.next_wake() == t0 + milliseconds{1250});
}

TEST_CASE("the gate's laws hold over 2000 generated scripts",
          "[fetch][gate][property]") {
  std::seed_seq seed{2026U, 10U, 15U, 7U};
  std::mt19937_64 rng{seed};
  std::uniform_int_distribution<std::size_t> length{1, 200};
  std::uniform_int_distribution<int> byte{0, 255};
  std::size_t admitted = 0;
  for (int i = 0; i < 2000; ++i) {
    std::vector<std::uint8_t> script(length(rng));
    for (auto& b : script) {
      b = static_cast<std::uint8_t>(byte(rng));
    }
    const auto d = mov::fetch::test::run_gate_script(script);
    INFO("script " << i << ": " << d.violation());
    REQUIRE(d.ok());
    admitted += d.admitted();
  }
  CHECK(admitted > 0);
}
