// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/detail/model_number.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <string_view>
#include <variant>

#include "mov/core/detail/ascii.hpp"
#include "mov/io/detail/text.hpp"

namespace mov::io::detail {

namespace {

constexpr bool is_sign(char c) noexcept { return c == '+' or c == '-'; }

constexpr bool is_exponent_letter(char c) noexcept {
  return c == 'e' or c == 'E';
}

// The longest token the Fortran forms are rewritten for: a real prints in
// 20 characters or so, and the rewrite needs one more.
constexpr std::size_t rewrite_capacity = 64;

// The token as parse_double's grammar spells it, when it is one of the forms
// Fortran writes: a `D` exponent letter, or a three-digit exponent whose
// letter is dropped. Returns the characters of the rewrite; an empty view
// means the token is not one of those forms and is judged as written. Only
// the shape is touched: parse_double still decides whether it is a number.
std::string_view fortran_spelling(std::string_view token,
                                  std::array<char, rewrite_capacity>& buffer) {
  if (token.size() >= buffer.size() or
      std::ranges::any_of(token, is_exponent_letter)) {
    return {};
  }
  const std::size_t letter = token.find_first_of("dD");
  if (letter != std::string_view::npos) {
    std::ranges::copy(token, buffer.begin());
    buffer[letter] = 'E';
    return {buffer.data(), token.size()};
  }
  // The exponent's sign is the first sign after the mantissa's first
  // character: "1.5-100" -> "1.5E-100", "-1.5-100" -> "-1.5E-100".
  const std::size_t lead =
      (not token.empty() and is_sign(token.front())) ? 1 : 0;
  const std::size_t sign =
      token.find_first_of("+-", std::min(lead + 1, token.size()));
  if (sign == std::string_view::npos) {
    return {};
  }
  const auto after = std::ranges::copy(token.substr(0, sign), buffer.begin());
  *after.out = 'E';
  std::ranges::copy(token.substr(sign), after.out + 1);
  return {buffer.data(), token.size() + 1};
}

}  // namespace

bool is_nonfinite_token(std::string_view token) noexcept {
  // An overflowed field is all asterisks: Fortran fills the whole field, sign
  // included.
  if (not token.empty() and
      std::ranges::all_of(token, [](char c) noexcept { return c == '*'; })) {
    return true;
  }
  static_cast<void>(strip_sign(token));
  return core::detail::equal_ignore_case(token, "nan") or
         core::detail::equal_ignore_case(token, "inf") or
         core::detail::equal_ignore_case(token, "infinity");
}

std::expected<ModelNumber, NumberError> parse_model_number(
    std::string_view token) {
  if (is_nonfinite_token(token)) {
    return ModelNumber{NonFinite{}};
  }
  std::array<char, rewrite_capacity> buffer{};
  const std::string_view rewritten = fortran_spelling(token, buffer);
  const auto parsed = parse_double(rewritten.empty() ? token : rewritten);
  if (parsed) {
    return ModelNumber{*parsed};
  }
  if (parsed.error() == NumberError::out_of_range) {
    return ModelNumber{NonFinite{}};
  }
  return std::unexpected{parsed.error()};
}

}  // namespace mov::io::detail
