// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: parse_version must never crash, and whatever it accepts
// must survive a format/parse round trip.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <string>

#include "mov/core/version.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string text(data, data + size);
  const auto parsed = mov::core::parse_version(text);
  if (not parsed) {
    return 0;
  }
  const std::string formatted =
      std::format("{}.{}.{}", parsed->major_version, parsed->minor_version,
                  parsed->patch_version);
  if (mov::core::parse_version(formatted) != parsed) {
    std::abort();
  }
  return 0;
}
