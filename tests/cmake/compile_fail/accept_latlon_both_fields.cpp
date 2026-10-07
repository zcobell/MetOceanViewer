// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The well-formed twin of reject_latlon_missing_field.cpp.

#include "mov/core/geo.hpp"

int main() {
  const auto p = mov::core::Location::make({.lat = 1.0, .lon = 2.0});
  return p.has_value() ? 0 : 1;
}
