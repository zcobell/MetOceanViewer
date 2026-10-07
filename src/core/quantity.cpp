// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/quantity.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace mov::core {

namespace {

constexpr bool is_ascii_letter(char c) noexcept {
  return (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z');
}

constexpr bool is_ascii_digit(char c) noexcept { return c >= '0' and c <= '9'; }

// ^[A-Za-z][A-Za-z0-9_]*$
constexpr bool is_cf_name(std::string_view text) noexcept {
  return not text.empty() and is_ascii_letter(text.front()) and
         std::ranges::all_of(text, [](char c) noexcept {
           return is_ascii_letter(c) or is_ascii_digit(c) or c == '_';
         });
}

}  // namespace

GenericQuantity GenericQuantity::value() {
  return GenericQuantity{"value", ""};
}

std::optional<GenericQuantity> GenericQuantity::parse(
    std::string_view token, std::string_view standard_name) {
  if (not is_cf_name(token) or parse_quantity_token(token)) {
    return std::nullopt;
  }
  return GenericQuantity{std::string{token}, std::string{standard_name}};
}

std::string_view token(const QuantityId& q) noexcept {
  if (const Quantity* registry = std::get_if<Quantity>(&q)) {
    return info(*registry).token;
  }
  const GenericQuantity* generic = std::get_if<GenericQuantity>(&q);
  return generic != nullptr ? generic->token() : std::string_view{};
}

}  // namespace mov::core
