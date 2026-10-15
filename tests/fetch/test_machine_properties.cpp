// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The machine's laws (docs/providers-design.md §5.5) over generated event
// scripts: statuses, transport errors, delays, late and unknown ids,
// cancellation and deadlines, with a fixed seed so a failure reproduces.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

#include "machine_driver.hpp"

namespace {

/// A generator with a fixed seed: a failure must reproduce.
std::mt19937_64 fixed_rng(std::uint32_t stream) {
  std::seed_seq seed{2026U, 10U, 15U, stream};
  return std::mt19937_64{seed};
}

std::vector<std::uint8_t> random_script(std::mt19937_64& rng) {
  std::uniform_int_distribution<std::size_t> length{3, 160};
  std::uniform_int_distribution<int> byte{0, 255};
  std::vector<std::uint8_t> bytes(length(rng));
  for (auto& b : bytes) {
    b = static_cast<std::uint8_t>(byte(rng));
  }
  return bytes;
}

}  // namespace

TEST_CASE("the machine's laws hold over 2000 generated scripts",
          "[fetch][machine][property]") {
  std::mt19937_64 rng = fixed_rng(1);
  constexpr int scripts = 2000;
  int succeeded = 0;
  int failed = 0;
  for (int i = 0; i < scripts; ++i) {
    const std::vector<std::uint8_t> script = random_script(rng);
    const auto driver = mov::fetch::test::run_script(script);
    INFO("script " << i << ": " << driver.violation());
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

TEST_CASE("an empty script drains to a successful fetch",
          "[fetch][machine][property]") {
  const auto driver =
      mov::fetch::test::run_script(std::span<const std::uint8_t>{});
  CHECK(driver.violation().empty());
  REQUIRE(driver.finished());
  CHECK(driver.succeeded());
}
