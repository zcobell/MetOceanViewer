// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// A length unit does not convert to a speed unit: compile error.

#include "mov/core/units.hpp"

int main() {
  const auto a = mov::core::conversion(mov::core::LengthUnit::foot,
                                       mov::core::SpeedUnit::knot);
  return a.scale > 0.0 ? 0 : 1;
}
