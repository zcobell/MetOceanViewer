// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The well-formed twin of reject_unit_family_mix.cpp.

#include "mov/core/units.hpp"

int main() {
  const auto a = mov::core::conversion(mov::core::LengthUnit::foot,
                                       mov::core::LengthUnit::meter);
  return a.scale > 0.0 ? 0 : 1;
}
