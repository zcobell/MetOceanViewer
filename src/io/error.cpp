// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/error.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace mov::io {

namespace detail {

namespace {

// A UTF-8 continuation byte is 0b10xxxxxx.
constexpr bool is_continuation(char c) noexcept {
  return (static_cast<unsigned char>(c) & 0xC0U) == 0x80U;
}

// A sequence has at most three continuation bytes after its lead byte.
constexpr std::size_t max_continuation_bytes = 3;

}  // namespace

std::string_view truncate_utf8(std::string_view text,
                               std::size_t max_bytes) noexcept {
  if (text.size() <= max_bytes) {
    return text;
  }
  // text[cut] is the first byte dropped. While it is a continuation byte the
  // cut would split a sequence, so move it back to that sequence's lead byte.
  std::size_t cut = max_bytes;
  std::size_t steps = 0;
  while (cut > 0 and steps < max_continuation_bytes and
         is_continuation(text[cut])) {
    --cut;
    ++steps;
  }
  if (is_continuation(text[cut])) {
    cut = max_bytes;  // not UTF-8: no boundary within reach
  }
  return text.substr(0, cut);
}

}  // namespace detail

ParseError ParseError::make(ParseErrc code, Where where,
                            std::string_view context) {
  return ParseError{
      code, where,
      std::string{detail::truncate_utf8(context, max_context_bytes)}};
}

}  // namespace mov::io
