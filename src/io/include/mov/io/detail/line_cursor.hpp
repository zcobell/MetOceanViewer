// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <optional>
#include <string_view>

#include "mov/io/detail/text.hpp"

namespace mov::io::detail {

/// Reads a text line by line without copying. Lines are numbered from 1.
/// The line ends at '\n'; one '\r' before it (CRLF), or at the very end of
/// the text, is not part of the line. A final line without a terminator is
/// returned, a trailing '\n' does not produce an empty line after it, and
/// empty text has no lines. One UTF-8 byte order mark at the start of the
/// text is skipped; a second one is ordinary content.
///
/// The cursor holds a view of the text, which must outlive it.
class LineCursor {
 public:
  struct Line {
    std::size_t number;  // 1-based
    std::string_view text;
    friend bool operator==(const Line&, const Line&) = default;
  };

  explicit LineCursor(std::string_view text MOV_LIFETIMEBOUND) noexcept
      : text_{skip_bom(text)} {}
  template <TemporaryString S>
  explicit LineCursor(S&&) = delete;

  /// The next line, or nullopt at the end of the text.
  [[nodiscard]] std::optional<Line> next() noexcept {
    if (at_end()) {
      return std::nullopt;
    }
    const std::string_view rest = text_.substr(position_);
    const std::size_t newline = rest.find('\n');
    const bool terminated = newline != std::string_view::npos;
    const std::string_view line =
        rest.substr(0, terminated ? newline : rest.size());
    position_ += line.size() + (terminated ? 1U : 0U);
    return Line{.number = ++number_, .text = without_cr(line)};
  }

  /// True when `next()` would return nullopt.
  [[nodiscard]] bool at_end() const noexcept {
    return position_ >= text_.size();
  }

  /// The bytes of the text that `next()` has not consumed (after any BOM).
  [[nodiscard]] std::size_t remaining_bytes() const noexcept {
    return text_.size() - std::min(position_, text_.size());
  }

  /// How many lines `next()` has returned; the number of the last one.
  [[nodiscard]] std::size_t lines_read() const noexcept { return number_; }

 private:
  static constexpr std::string_view utf8_bom{"\xEF\xBB\xBF"};

  // One CR before the terminator is part of a CRLF, not of the line.
  [[nodiscard]] static std::string_view without_cr(
      std::string_view line) noexcept {
    return line.ends_with('\r') ? line.substr(0, line.size() - 1) : line;
  }

  [[nodiscard]] static std::string_view skip_bom(
      std::string_view text) noexcept {
    return text.starts_with(utf8_bom) ? text.substr(utf8_bom.size()) : text;
  }

  std::string_view text_;
  std::size_t position_{0};
  std::size_t number_{0};
};

}  // namespace mov::io::detail
