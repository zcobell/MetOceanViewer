// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The well-formed twin of reject_swapped_designators.cpp.

#include "mov/core/quantity.hpp"

int main() {
  const auto q = mov::core::GenericQuantity::parse(
      {.token = "ph", .standard_name = "sea_water_ph_reported_on_total_scale"});
  return q.has_value() ? 0 : 1;
}
