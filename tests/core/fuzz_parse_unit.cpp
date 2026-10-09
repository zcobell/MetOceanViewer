// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: parse_unit must never crash, and every unit it accepts
// must survive a symbol() round trip and convert to itself.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <variant>

#include "mov/core/ascii.hpp"
#include "mov/core/units.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

[[noreturn]] void fail() { std::abort(); }

// Non-empty, no whitespace at either end, no two whitespace characters in a
// row, and no whitespace other than the single space.
bool is_normalized(std::string_view text) {
  using mov::core::ascii::is_space;
  if (text.empty() or is_space(text.front()) or is_space(text.back())) {
    return false;
  }
  bool previous_space = false;
  for (const char c : text) {
    if (is_space(c) and (c != ' ' or previous_space)) {
      return false;
    }
    previous_space = c == ' ';
  }
  return true;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string text(data, data + size);
  const auto unit = mov::core::parse_unit(text);
  if (not unit) {
    return 0;
  }
  // Printing a unit and parsing the print gives the unit back.
  if (mov::core::parse_unit(mov::core::symbol(*unit)) != unit) {
    fail();
  }
  const auto same = mov::core::conversion(*unit, *unit);
  if (not same or not(*same == mov::core::Affine{})) {
    fail();
  }
  // An OtherUnit's text is normalized: no leading or trailing whitespace and
  // no run of whitespace.
  if (const auto* other = std::get_if<mov::core::OtherUnit>(&*unit)) {
    if (not is_normalized(other->symbol())) {
      fail();
    }
  }
  // A recognized unit's netCDF text parses back to it; an OtherUnit's text is
  // its own symbol.
  if (not std::holds_alternative<mov::core::OtherUnit>(*unit) and
      mov::core::parse_unit(mov::core::udunits(*unit)) != unit) {
    fail();
  }
  return 0;
}
