// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <catch2/catch_tostring.hpp>
#include <string>

#include "mov/core/time.hpp"

// TimeRange has begin() and end(), so Catch2 mistakes it for a range of
// iterators and fails to compile its stringification (as in
// tests/core/test_helpers.hpp).
template <>
struct Catch::StringMaker<mov::core::TimeRange> {
  static std::string convert(const mov::core::TimeRange& range) {
    return "[" + std::to_string(range.begin().time_since_epoch().count()) +
           ", " + std::to_string(range.end().time_since_epoch().count()) +
           ") ms";
  }
};
