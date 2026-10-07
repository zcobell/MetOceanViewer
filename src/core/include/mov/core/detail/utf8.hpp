// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace mov::core::detail {

/// One row of the well-formed UTF-8 table (Unicode 15, table 3-7): lead
/// bytes in [lead_lo, lead_hi] start a sequence of `length` bytes whose
/// second byte is in [second_lo, second_hi]; later bytes are 80..BF.
struct Utf8Form {
  std::uint8_t lead_lo;
  std::uint8_t lead_hi;
  std::size_t length;
  std::uint8_t second_lo;
  std::uint8_t second_hi;
};

inline constexpr std::array<Utf8Form, 9> utf8_forms{{
    {.lead_lo = 0x00,
     .lead_hi = 0x7F,
     .length = 1,
     .second_lo = 0,
     .second_hi = 0},
    {.lead_lo = 0xC2,
     .lead_hi = 0xDF,
     .length = 2,
     .second_lo = 0x80,
     .second_hi = 0xBF},
    {.lead_lo = 0xE0,
     .lead_hi = 0xE0,
     .length = 3,
     .second_lo = 0xA0,
     .second_hi = 0xBF},
    {.lead_lo = 0xE1,
     .lead_hi = 0xEC,
     .length = 3,
     .second_lo = 0x80,
     .second_hi = 0xBF},
    {.lead_lo = 0xED,
     .lead_hi = 0xED,
     .length = 3,
     .second_lo = 0x80,
     .second_hi = 0x9F},
    {.lead_lo = 0xEE,
     .lead_hi = 0xEF,
     .length = 3,
     .second_lo = 0x80,
     .second_hi = 0xBF},
    {.lead_lo = 0xF0,
     .lead_hi = 0xF0,
     .length = 4,
     .second_lo = 0x90,
     .second_hi = 0xBF},
    {.lead_lo = 0xF1,
     .lead_hi = 0xF3,
     .length = 4,
     .second_lo = 0x80,
     .second_hi = 0xBF},
    {.lead_lo = 0xF4,
     .lead_hi = 0xF4,
     .length = 4,
     .second_lo = 0x80,
     .second_hi = 0x8F},
}};

[[nodiscard]] constexpr bool in_range(std::uint8_t b, std::uint8_t lo,
                                      std::uint8_t hi) noexcept {
  return b >= lo and b <= hi;
}

/// The length of the well-formed sequence at the start of `s`, or 0.
[[nodiscard]] constexpr std::size_t utf8_sequence_length(
    std::string_view s) noexcept {
  const auto byte = [s](std::size_t i) {
    return static_cast<std::uint8_t>(s[i]);
  };
  const auto form =
      std::ranges::find_if(utf8_forms, [lead = byte(0)](const Utf8Form& f) {
        return in_range(lead, f.lead_lo, f.lead_hi);
      });
  if (form == utf8_forms.end() or s.size() < form->length) {
    return 0;
  }
  if (form->length > 1 and
      not in_range(byte(1), form->second_lo, form->second_hi)) {
    return 0;
  }
  for (std::size_t i = 2; i < form->length; ++i) {
    if (not in_range(byte(i), 0x80, 0xBF)) {
      return 0;
    }
  }
  return form->length;
}

/// Well-formed UTF-8: no stray continuation bytes, overlong forms,
/// surrogates (U+D800..DFFF) or code points above U+10FFFF.
[[nodiscard]] constexpr bool is_valid_utf8(std::string_view s) noexcept {
  while (not s.empty()) {
    const std::size_t n = utf8_sequence_length(s);
    if (n == 0) {
      return false;
    }
    s.remove_prefix(n);
  }
  return true;
}

}  // namespace mov::core::detail
