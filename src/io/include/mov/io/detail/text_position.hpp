// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Where a byte offset of a text is, as a ParseError locates it. Private to
// mov::io (public only because tests include it).

#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

#include "mov/io/error.hpp"

namespace mov::io::detail {

/// The 1-based line and 0-based byte column of `offset` in `text`; an offset
/// past the end is taken as the end.
[[nodiscard]] constexpr ParseError::Where where_of(
    std::string_view text, std::size_t offset) noexcept {
  const std::string_view before = text.substr(0, std::min(offset, text.size()));
  const std::size_t last_newline = before.rfind('\n');
  const auto newlines =
      static_cast<std::size_t>(std::ranges::count(before, '\n'));
  const std::size_t column = last_newline == std::string_view::npos
                                 ? before.size()
                                 : before.size() - last_newline - 1;
  return {.line = newlines + 1, .column = column};
}

/// A ParseError of `code` at `offset` of `text`, whose context is the text
/// from there (cut by ParseError::make).
[[nodiscard]] inline ParseError error_at_offset(ParseErrc code,
                                                std::string_view text,
                                                std::size_t offset) {
  const std::size_t at = std::min(offset, text.size());
  return ParseError::make(code, where_of(text, at), text.substr(at));
}

}  // namespace mov::io::detail
