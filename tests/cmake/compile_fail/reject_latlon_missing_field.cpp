// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// LatLon has no default member values: leaving a coordinate out of a
// designated initializer is a compile error, not a silent 0.
// requires-diagnostic: missing-designated-field

#include "mov/core/geo.hpp"

int main() {
  const auto p = mov::core::Location::make({.lat = 1.0});
  return p.has_value() ? 0 : 1;
}
