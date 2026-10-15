// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/wire/body.hpp"

#include <cstddef>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "mov/io/detail/text.hpp"
#include "mov/io/detail/text_position.hpp"
#include "mov/io/error.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"

namespace mov::io::wire {

std::expected<void, ParseError> check_json_depth(std::string_view body,
                                                 std::size_t max_depth) {
  std::size_t depth = 0;
  bool in_string = false;
  bool escaped = false;
  for (std::size_t i = 0; i < body.size(); ++i) {
    const char c = body[i];
    if (in_string) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    switch (c) {
      case '"':
        in_string = true;
        break;
      case '[':
      case '{':
        if (++depth > max_depth) {
          return std::unexpected{
              ParseError::make(ParseErrc::too_large, detail::where_of(body, i),
                               std::to_string(depth) + " levels, limit " +
                                   std::to_string(max_depth))};
        }
        break;
      case ']':
      case '}':
        // Unbalanced closers are the parser's to refuse; never below zero.
        depth -= depth > 0 ? 1 : 0;
        break;
      default:
        break;
    }
  }
  return {};
}

std::optional<ServiceError> sniff_markup(Origin origin, std::string_view body,
                                         std::string_view content_type) {
  const std::string_view rest = detail::skip_space(body);
  if (rest.empty() or rest.front() != '<') {
    return std::nullopt;
  }
  return ServiceError::make(origin, ServiceErrc::unexpected_content,
                            content_type);
}

}  // namespace mov::io::wire
