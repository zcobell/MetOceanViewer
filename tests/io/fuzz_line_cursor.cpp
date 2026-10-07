// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target for the text helpers: LineCursor, split_on, split_ws,
// simplified, and the UTF-8 truncation of error contexts. The first input
// byte picks the delimiter and the truncation limit; the rest is the text.
// Oracles are the structural laws of each helper.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "mov/core/detail/ascii.hpp"
#include "mov/io/detail/line_cursor.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace detail = mov::io::detail;

[[noreturn]] void fail() { std::abort(); }

bool inside(std::string_view piece, std::string_view whole) {
  return piece.data() >= whole.data() and
         piece.data() + piece.size() <= whole.data() + whole.size();
}

std::string join(const std::vector<std::string_view>& parts, char separator) {
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) {
      out += separator;
    }
    out += parts[i];
  }
  return out;
}

void check_lines(std::string_view text) {
  detail::LineCursor cursor{text};
  std::size_t count = 0;
  std::size_t consumed = 0;
  while (const auto line = cursor.next()) {
    ++count;
    if (line->number != count or not inside(line->text, text) or
        line->text.find('\n') != std::string_view::npos) {
      fail();
    }
    consumed += line->text.size() + 1;
  }
  // Each line took its terminator (the last one may have had none) and its
  // optional CR; the text can hold no more than that plus a byte order mark.
  constexpr std::size_t bom_size = 3;
  if (cursor.lines_read() != count or not cursor.at_end() or
      cursor.next().has_value() or consumed > text.size() + 1 or
      (count == 0 and text.size() > bom_size)) {
    fail();
  }
}

void check_split_on(std::string_view text, char delimiter) {
  const auto fields = detail::split_on(text, delimiter);
  const auto delimiters =
      static_cast<std::size_t>(std::ranges::count(text, delimiter));
  if (fields.size() != delimiters + 1 or join(fields, delimiter) != text) {
    fail();
  }
  for (const std::string_view field : fields) {
    if (not inside(field, text) or
        field.find(delimiter) != std::string_view::npos) {
      fail();
    }
  }
}

void check_split_ws(std::string_view text) {
  const auto tokens = detail::split_ws(text);
  const char* previous_end = text.data();
  for (const std::string_view token : tokens) {
    if (token.empty() or not inside(token, text) or
        token.data() < previous_end or
        std::ranges::any_of(token, mov::core::detail::is_space)) {
      fail();
    }
    previous_end = token.data() + token.size();
  }
  if (detail::simplified(text) != join(tokens, ' ')) {
    fail();
  }
  // simplified is idempotent.
  const std::string once = detail::simplified(text);
  if (detail::simplified(once) != once) {
    fail();
  }
  const std::string lower = detail::to_lower_ascii(text);
  if (lower.size() != text.size() or detail::to_lower_ascii(lower) != lower) {
    fail();
  }
}

void check_truncation(std::string_view text, std::size_t limit) {
  const std::string_view cut = mov::io::detail::truncate_utf8(text, limit);
  if (cut.size() > limit or not text.starts_with(cut)) {
    fail();
  }
  // The cut is not short by more than a sequence's worth, and an error built
  // from the text respects the documented bound.
  if (text.size() > limit and cut.size() + 3 < limit) {
    // Only text with a long run of stray continuation bytes may be cut early;
    // those are not backed over.
    fail();
  }
  const auto error = mov::io::ParseError::make(
      mov::io::ParseErrc::corrupt_record, {.line = 1}, text);
  if (error.context().size() > mov::io::ParseError::max_context_bytes) {
    fail();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size == 0) {
    return 0;
  }
  const auto selector = static_cast<char>(data[0]);
  const std::string text(data + 1, data + size);
  check_lines(text);
  check_split_on(text, selector);
  check_split_ws(text);
  check_truncation(text, static_cast<unsigned char>(selector));
  return 0;
}
