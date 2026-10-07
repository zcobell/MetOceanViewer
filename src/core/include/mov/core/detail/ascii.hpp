// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <string_view>

// ASCII-only text helpers, constexpr and locale-independent (std::isspace and
// std::tolower depend on the global C locale).

namespace mov::core::detail {

[[nodiscard]] constexpr bool is_space(char c) noexcept {
  return c == ' ' or c == '\t' or c == '\n' or c == '\r' or c == '\v' or
         c == '\f';
}

[[nodiscard]] constexpr char to_lower(char c) noexcept {
  return (c >= 'A' and c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] constexpr char to_upper(char c) noexcept {
  return (c >= 'a' and c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

/// `text` without leading and trailing whitespace.
[[nodiscard]] constexpr std::string_view trim(std::string_view text) noexcept {
  while (not text.empty() and is_space(text.front())) {
    text.remove_prefix(1);
  }
  while (not text.empty() and is_space(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

/// Equal ignoring ASCII case; a non-ASCII byte must match exactly.
[[nodiscard]] constexpr bool equal_ignore_case(std::string_view a,
                                               std::string_view b) noexcept {
  return std::ranges::equal(
      a, b, [](char x, char y) noexcept { return to_lower(x) == to_lower(y); });
}

}  // namespace mov::core::detail
