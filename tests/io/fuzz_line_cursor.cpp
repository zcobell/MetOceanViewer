// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target for the text helpers. The first input byte picks the
// delimiter and the truncation limit; the rest is the text. The oracles are
// models, not just invariants:
//  - LineCursor against split_on('\n') with the BOM skipped, the empty last
//    field dropped and one CR chomped per line;
//  - split_ws is complete (its tokens concatenate to exactly the non-space
//    characters), maximal (each token is bounded by space or the text's ends),
//    and agrees with the bounded split_ws_into;
//  - split_on rejoins to the text;
//  - truncate_utf8 never cuts inside a sequence, and valid UTF-8 stays valid.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iterator>
#include <optional>
#include <span>
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
using mov::core::detail::is_space;

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

// ---- LineCursor against its model ------------------------------------------

std::vector<std::string_view> model_lines(std::string_view text) {
  constexpr std::string_view bom{"\xEF\xBB\xBF"};
  if (text.starts_with(bom)) {
    text.remove_prefix(bom.size());
  }
  std::vector<std::string_view> lines = detail::split_on(text, '\n');
  if (lines.back().empty()) {
    lines.pop_back();  // the text ended with a newline, or was empty
  }
  for (std::string_view& line : lines) {
    if (line.ends_with('\r')) {
      line.remove_suffix(1);
    }
  }
  return lines;
}

void check_lines(std::string_view text) {
  const std::vector<std::string_view> expected = model_lines(text);
  detail::LineCursor cursor{text};
  std::size_t count = 0;
  while (const auto line = cursor.next()) {
    ++count;
    if (count > expected.size() or line->number != count or
        line->text != expected[count - 1] or not inside(line->text, text)) {
      fail();
    }
  }
  if (count != expected.size() or cursor.lines_read() != count or
      not cursor.at_end() or cursor.next().has_value()) {
    fail();
  }
}

// ---- splitting -------------------------------------------------------------

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

void check_token_bounds(std::string_view text, std::string_view token) {
  if (token.empty() or not inside(token, text) or
      std::ranges::any_of(token, is_space)) {
    fail();
  }
  // Maximal: the characters on both sides are whitespace or the ends.
  const auto start = static_cast<std::size_t>(token.data() - text.data());
  const std::size_t end = start + token.size();
  if ((start > 0 and not is_space(text[start - 1])) or
      (end < text.size() and not is_space(text[end]))) {
    fail();
  }
}

void check_split_ws(std::string_view text) {
  const auto tokens = detail::split_ws(text);
  std::string concatenated;
  const char* previous_end = text.data();
  for (const std::string_view token : tokens) {
    check_token_bounds(text, token);
    if (token.data() < previous_end) {
      fail();
    }
    previous_end = token.data() + token.size();
    concatenated += token;
  }
  std::string non_space;
  std::ranges::copy_if(text, std::back_inserter(non_space),
                       [](char c) { return not is_space(c); });
  if (concatenated != non_space) {
    fail();  // complete: no character lost or invented
  }
  if (detail::simplified(text) != join(tokens, ' ')) {
    fail();
  }
  // The bounded form agrees: same words, and a count that stops at N + 1.
  std::array<std::string_view, 4> room{};
  const std::size_t counted = detail::split_ws_into(text, room);
  if (counted != std::min(tokens.size(), room.size() + 1)) {
    fail();
  }
  for (std::size_t i = 0; i < std::min(counted, room.size()); ++i) {
    if (room[i] != tokens[i]) {
      fail();
    }
  }
  const std::string lower = detail::to_lower_ascii(text);
  if (lower.size() != text.size() or detail::to_lower_ascii(lower) != lower) {
    fail();
  }
}

// ---- UTF-8 truncation ------------------------------------------------------

// The length of the sequence that starts with `lead`, or 0 if it cannot.
std::size_t sequence_length(unsigned char lead) {
  if (lead < 0x80U) {
    return 1;
  }
  if (lead >= 0xC2U and lead < 0xE0U) {
    return 2;
  }
  if (lead >= 0xE0U and lead < 0xF0U) {
    return 3;
  }
  return (lead >= 0xF0U and lead < 0xF5U) ? 4 : 0;
}

// Structurally valid UTF-8: every lead byte has its continuation bytes. (Not
// the full standard: overlongs and surrogates are not rejected.)
bool structurally_utf8(std::string_view text) {
  for (std::size_t i = 0; i < text.size();) {
    const std::size_t length =
        sequence_length(static_cast<unsigned char>(text[i]));
    if (length == 0 or i + length > text.size()) {
      return false;
    }
    for (std::size_t k = 1; k < length; ++k) {
      if ((static_cast<unsigned char>(text[i + k]) & 0xC0U) != 0x80U) {
        return false;
      }
    }
    i += length;
  }
  return true;
}

void check_truncation(std::string_view text, std::size_t limit) {
  const std::string_view cut = detail::truncate_utf8(text, limit);
  if (cut.size() > limit or not text.starts_with(cut)) {
    fail();
  }
  if (structurally_utf8(text)) {
    // No cut inside a sequence: the prefix is valid, and the next byte (if any)
    // starts a sequence.
    if (not structurally_utf8(cut) or
        (cut.size() < text.size() and
         (static_cast<unsigned char>(text[cut.size()]) & 0xC0U) == 0x80U)) {
      fail();
    }
    // As long as it can be: at most three bytes (one sequence) short.
    if (text.size() > limit and cut.size() + 3 < limit) {
      fail();
    }
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
