// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/quantity.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "mov/core/ascii.hpp"

namespace mov::core {

namespace {

// ^[A-Za-z][A-Za-z0-9_]*$
constexpr bool is_cf_name(std::string_view text) noexcept {
  return not text.empty() and ascii::is_alpha(text.front()) and
         std::ranges::all_of(text, [](char c) noexcept {
           return ascii::is_alnum(c) or c == '_';
         });
}

}  // namespace

std::optional<GenericQuantity> GenericQuantity::parse(Spec spec) {
  if (not is_cf_name(spec.token) or parse_quantity_token(spec.token)) {
    return std::nullopt;
  }
  return GenericQuantity{std::string{spec.token},
                         std::string{spec.standard_name}};
}

std::optional<Unit> canonical_unit(Quantity q) {
  const std::string_view text = info(q).canonical_unit;
  if (text.empty()) {  // `difference`
    return std::nullopt;
  }
  // Every other registry spelling is non-blank (static_assert in quantity.hpp).
  return detail::unit_of_nonblank(text);
}

}  // namespace mov::core
