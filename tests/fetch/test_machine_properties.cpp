// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The machines' laws (docs/providers-design.md §5.5) over generated event
// scripts: statuses, transport errors, delays and early timers, late and
// unknown ids, cancellation, deadlines, time going back, jitter at both
// ends, id cursors; for the toy spec and for USGS and CO-OPS through Then.
// Fixed seeds, so a failure reproduces.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "protocol_driver.hpp"
#include "worlds.hpp"

namespace {

using mov::fetch::test::CoopsWorld;
using mov::fetch::test::run_script;
using mov::fetch::test::ScriptBytes;
using mov::fetch::test::ToyWorld;
using mov::fetch::test::UsgsWorld;

/// A generator with a fixed seed: a failure must reproduce.
std::mt19937_64 fixed_rng(std::uint32_t stream) {
  std::seed_seq seed{2026U, 10U, 15U, stream};
  return std::mt19937_64{seed};
}

std::vector<std::uint8_t> random_script(std::mt19937_64& rng) {
  std::uniform_int_distribution<std::size_t> length{4, 160};
  std::uniform_int_distribution<int> byte{0, 255};
  std::vector<std::uint8_t> bytes(length(rng));
  for (auto& b : bytes) {
    b = static_cast<std::uint8_t>(byte(rng));
  }
  return bytes;
}

/// The script as hex, to replay a failure with the fuzz target.
std::string hex(const std::vector<std::uint8_t>& bytes) {
  constexpr std::string_view digits = "0123456789abcdef";
  constexpr unsigned nibble = 4;
  constexpr unsigned low = 0x0F;
  std::string out;
  for (const std::uint8_t b : bytes) {
    out += digits[b >> nibble];
    out += digits[b & low];
  }
  return out;
}

template <class World>
void check_scripts(std::uint32_t stream, int scripts) {
  std::mt19937_64 rng = fixed_rng(stream);
  int succeeded = 0;
  int failed = 0;
  for (int i = 0; i < scripts; ++i) {
    const std::vector<std::uint8_t> script = random_script(rng);
    const auto driver = run_script<World>(ScriptBytes{script});
    INFO("script " << i << ": " << driver.violation()
                   << "\nbytes: " << hex(script));
    REQUIRE(driver.violation().empty());
    REQUIRE(driver.finished());
    if (driver.succeeded()) {
      ++succeeded;
    } else {
      ++failed;
    }
  }
  // The generator reaches both ends.
  CHECK(succeeded > 0);
  CHECK(failed > 0);
}

}  // namespace

TEST_CASE("the machine's laws hold over 2000 generated toy scripts",
          "[fetch][machine][property]") {
  check_scripts<ToyWorld>(1, 2000);
}

TEST_CASE("the laws hold for USGS resolution then data through Then",
          "[fetch][then][property]") {
  check_scripts<UsgsWorld>(2, 1000);
}

TEST_CASE("the laws hold for CO-OPS lists then datums through Then",
          "[fetch][then][property]") {
  check_scripts<CoopsWorld>(3, 1000);
}

TEST_CASE("an empty script drains to a successful fetch",
          "[fetch][machine][property]") {
  const std::vector<std::uint8_t> setup{0x05, 0x00, 0x00, 0x00};
  const auto driver = run_script<ToyWorld>(ScriptBytes{setup});
  CHECK(driver.violation().empty());
  REQUIRE(driver.finished());
  CHECK(driver.succeeded());
}
