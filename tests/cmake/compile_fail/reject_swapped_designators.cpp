// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Designated initializers must follow declaration order, so a swapped
// token / standard_name pair does not compile.

#include "mov/core/quantity.hpp"

int main() {
  const auto q = mov::core::GenericQuantity::parse(
      {.standard_name = "sea_water_ph_reported_on_total_scale", .token = "ph"});
  return q.has_value() ? 0 : 1;
}
