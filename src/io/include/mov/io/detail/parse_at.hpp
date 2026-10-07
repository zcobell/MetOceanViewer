// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// What a text reader needs to turn one token of one line into a number or a
// ParseError that says where it was: the NumberError -> ParseError mapping,
// written once.

#pragma once

#include <concepts>
#include <cstddef>
#include <expected>
#include <optional>
#include <string_view>

#include "mov/io/detail/line_cursor.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"

namespace mov::io::detail {

/// A ParseError of `code` for `token`, a view into `line.text` (as the words of
/// split_ws are): the line number is the line's, the column the token's byte
/// offset in the line, the context the whole line. A token that is not inside
/// the line has no column.
[[nodiscard]] inline ParseError at(const LineCursor::Line& line,
                                   std::string_view token, ParseErrc code) {
  const char* const first = line.text.data();
  const char* const last = first + line.text.size();
  const bool inside = not token.empty() and token.data() >= first and
                      token.data() + token.size() <= last;
  const std::optional<std::size_t> column =
      inside ? std::optional<std::size_t>{static_cast<std::size_t>(
                   token.data() - first)}
             : std::nullopt;
  return ParseError::make(code, {.line = line.number, .column = column},
                          line.text);
}

/// `token` as a double (parse_double): a malformed token is `bad_number`, one
/// that does not fit a double `out_of_range`.
[[nodiscard]] inline std::expected<double, ParseError> double_at(
    const LineCursor::Line& line, std::string_view token) {
  const auto value = parse_double(token);
  if (value) {
    return *value;
  }
  return std::unexpected{at(line, token,
                            value.error() == NumberError::out_of_range
                                ? ParseErrc::out_of_range
                                : ParseErrc::bad_number)};
}

/// `token` as an integer of type `I` (parse_int): `bad_integer` or
/// `out_of_range`.
template <std::integral I>
  requires(not std::same_as<I, bool>)
[[nodiscard]] std::expected<I, ParseError> int_at(const LineCursor::Line& line,
                                                  std::string_view token) {
  const auto value = parse_int<I>(token);
  if (value) {
    return *value;
  }
  return std::unexpected{at(line, token,
                            value.error() == NumberError::out_of_range
                                ? ParseErrc::out_of_range
                                : ParseErrc::bad_integer)};
}

}  // namespace mov::io::detail
