// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/detail/model_number.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <expected>
#include <optional>
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

// The token as parse_double's grammar spells it, if it is one of the forms
// Fortran writes: a `D` exponent letter (`1.5D+02`), or a three-digit
// exponent whose letter is dropped because it does not fit (`1.5-100`: a sign
// and exactly three digits end the token). nullopt means the token is not
// one of those and is judged as written. Only the shape is touched:
// parse_double still decides whether the result is a number.
std::optional<std::string_view> fortran_spelling(
    std::string_view token, std::array<char, rewrite_capacity>& buffer) {
  if (token.size() >= buffer.size() or
      std::ranges::any_of(token, is_exponent_letter)) {
    return std::nullopt;
  }
  const std::size_t letter = token.find_first_of("dD");
  if (letter != std::string_view::npos) {
    std::ranges::copy(token, buffer.begin());
    buffer[letter] = 'E';
    return std::string_view{buffer.data(), token.size()};
  }
  constexpr std::size_t exponent_digits = 3;
  const std::size_t lead =
      (not token.empty() and is_sign(token.front())) ? 1 : 0;
  const std::size_t sign = token.find_last_of("+-");
  // A sign after at least one mantissa character, and three digits to the end.
  if (sign == std::string_view::npos or sign <= lead or
      token.size() - sign - 1 != exponent_digits or
      not std::ranges::all_of(token.substr(sign + 1), [](char c) noexcept {
        return c >= '0' and c <= '9';
      })) {
    return std::nullopt;
  }
  const auto after = std::ranges::copy(token.substr(0, sign), buffer.begin());
  *after.out = 'E';
  std::ranges::copy(token.substr(sign), after.out + 1);
  return std::string_view{buffer.data(), token.size() + 1};
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
  const auto parsed =
      parse_double(fortran_spelling(token, buffer).value_or(token));
  if (parsed) {
    return ModelNumber{*parsed};
  }
  if (parsed.error() == NumberError::out_of_range) {
    return ModelNumber{NonFinite{}};
  }
  return std::unexpected{parsed.error()};
}

}  // namespace mov::io::detail
