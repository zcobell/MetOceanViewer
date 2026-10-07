// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: parse_vertical_datum must never crash, and the token of
// every datum it accepts parses back to the same datum.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "mov/core/datum.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string text(data, data + size);
  const auto datum = mov::core::parse_vertical_datum(text);
  if (datum and
      mov::core::parse_vertical_datum(mov::core::to_string(*datum)) != datum) {
    std::abort();
  }
  return 0;
}
