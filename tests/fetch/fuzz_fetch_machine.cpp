// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: the bytes are an event script for the fetch machine
// (machine_driver.hpp: a setup prefix, then (op, arg) pairs, then a drain
// that answers everything well). The driver checks the laws of
// docs/providers-design.md §5.5 after every step: exchanges bounded by the
// plan and by max_attempts, every Send answered or aborted by Finish, no
// Send during a cool-down or after Cancel or the deadline, exactly one
// Finish and nothing after it, unknown and stale ids ignored, the merge in
// plan order, and the run finishes.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>

#include "machine_driver.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  constexpr std::size_t max_script = 4096;
  if (size > max_script) {
    return -1;
  }
  const auto driver =
      mov::fetch::test::run_script(std::span<const std::uint8_t>{data, size});
  if (not driver.violation().empty() or not driver.finished()) {
    std::abort();
  }
  return 0;
}
