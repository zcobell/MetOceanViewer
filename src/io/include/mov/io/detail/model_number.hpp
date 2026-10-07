// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The numbers a Fortran model writes. Private to mov::io (the header is only
// public because the tests and fuzz targets include it).

#pragma once

#include <expected>
#include <string_view>
#include <variant>

#include "mov/io/detail/text.hpp"

namespace mov::io::detail {

/// A token that is a number in the file's grammar but not a finite double:
/// NaN, an infinity, a Fortran overflow field ("****"), or a magnitude no
/// double holds.
struct NonFinite {
  friend constexpr bool operator==(NonFinite, NonFinite) = default;
};

using ModelNumber = std::variant<double, NonFinite>;

/// True for the tokens a Fortran program prints for a value that is not a
/// number: an optional sign and `nan`, `inf` or `infinity` in any case, or
/// one or more asterisks and nothing else (an overflowed field).
[[nodiscard]] bool is_nonfinite_token(std::string_view token) noexcept;

/// One value of a model's text output. Everything parse_double accepts, with
/// the same value, plus what Fortran writes that it does not:
///  - a non-finite token (is_nonfinite_token), and a number that overflows or
///    underflows a double, are `NonFinite`: the caller decides what that means
///    (the model readers make it Missing and count it);
///  - a three-digit exponent printed without its letter (`1.5-100`: a sign and
///    exactly three digits end the token) and the `D` exponent letter of
///    double precision (`1.5D+02`).
/// Any other token is the NumberError parse_double gives for it.
[[nodiscard]] std::expected<ModelNumber, NumberError> parse_model_number(
    std::string_view token);

}  // namespace mov::io::detail
