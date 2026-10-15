// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: the bytes are a script for the admission gate
// (gate_driver.hpp: a setup byte, then (op, arg) pairs, then a drain). The
// driver checks after every call: never more than max_in_flight per origin,
// starts min_spacing apart, nothing admitted during a cool-down, withdrawn
// tickets never admitted, each ticket admitted once and in its job's order,
// jobs taking turns, background progressing behind interactive work,
// nothing left waiting that the rules allow, and the queue draining.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "gate_driver.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  constexpr std::size_t max_script = 2048;
  if (size > max_script) {
    return -1;
  }
  const auto d = mov::fetch::test::run_gate_script(
      std::span<const std::uint8_t>{data, size});
  if (not d.ok()) {
    std::abort();
  }
  return 0;
}
