// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// split takes a WindowSpan, which is positive by construction: a raw
// duration, which could be zero or negative, does not compile.

#include <chrono>

#include "mov/core/windows.hpp"

int main() {
  namespace core = mov::core;
  const auto range =
      core::TimeRange::make(core::Time{std::chrono::milliseconds{0}},
                            core::Time{std::chrono::milliseconds{1}});
  const auto windows = core::split(*range, std::chrono::milliseconds{0});
  return windows.size() == 1 ? 0 : 1;
}
