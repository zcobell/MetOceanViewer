// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The well-formed twin of reject_tide_argument_overflow.cpp: one step less
// stays inside int32.

#include <cstdint>

#include "mov/core/tide/constituent.hpp"

namespace tide = mov::core::tide;

constexpr tide::Argument grow() {
  tide::Argument a =
      tide::Argument::darwin(tide::Darwin{.v = {1, 0, 0, 0, 0},
                                          .c_deg = 32767,
                                          .u = {0, 0, 0, 0, 0, 0, 0},
                                          .f = tide::NodeFactor::unity});
  for (int i = 0; i < 2; ++i) {
    a = scale(127, a);  // 32767 * 127^2 fits int32
  }
  return a;
}

int main() {
  constexpr tide::Argument a = grow();
  return a.c_deg() > 0 ? 0 : 1;
}
