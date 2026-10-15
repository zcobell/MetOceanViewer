// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: the bytes are an event script for a fetch machine
// (protocol_driver.hpp). The first byte picks the world: the toy spec, USGS
// resolution then data, or a CO-OPS catalog's lists then datums (both
// through Then); then setup bytes, (op, arg) pairs, and a drain that
// answers everything well. The driver checks the laws of
// docs/providers-design.md §5.5 after every step: unique ids, every Send
// answered or aborted and every parse delivered or stopped by Finish,
// sends within max_attempts and the plan, no Send during a cool-down or
// after Cancel or the deadline, one CoolDown for each 429 or exhausted rate
// limit and none otherwise, early timers waiting again, exactly one Finish
// and nothing after it, a run past its deadline ending DeadlineExceeded,
// unknown and stale ids ignored, the merge in plan order and complete, and
// the run finishing.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <span>

#include "protocol_driver.hpp"
#include "worlds.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

template <class World>
void run(mov::fetch::test::ScriptBytes bytes) {
  const auto driver = mov::fetch::test::run_script<World>(bytes);
  if (not driver.violation().empty() or not driver.finished()) {
    std::fputs(driver.violation().c_str(), stderr);
    std::fputs("\n", stderr);
    std::abort();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  constexpr std::size_t max_script = 4096;
  if (size == 0 or size > max_script) {
    return -1;
  }
  const mov::fetch::test::ScriptBytes rest{
      std::span<const std::uint8_t>{data + 1, size - 1}};
  switch (data[0] % 3) {
    case 0:
      run<mov::fetch::test::ToyWorld>(rest);
      break;
    case 1:
      run<mov::fetch::test::UsgsWorld>(rest);
      break;
    default:
      run<mov::fetch::test::CoopsWorld>(rest);
      break;
  }
  return 0;
}
