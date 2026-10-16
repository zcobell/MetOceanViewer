// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// wire::Verbatim: a link the server handed out, checked before it is
// followed (docs/providers-design.md section 4.1).

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>

#include "mov/core/ascii.hpp"
#include "mov/io/wire/origin.hpp"

namespace mov::io::wire {

namespace {

[[nodiscard]] constexpr bool is_hex(char c) noexcept {
  return core::ascii::is_digit(c) or (c >= 'a' and c <= 'f') or
         (c >= 'A' and c <= 'F');
}

// RFC 3986's unreserved and reserved characters, without `#` (a fragment
// has no business in a link to follow), `[` and `]` (IP literals, which no
// origin is) and `%`, which is checked with its two digits.
[[nodiscard]] constexpr bool is_uri_char(char c) noexcept {
  constexpr std::string_view marks = "-._~:/?@!$&'()*+,;=";
  return core::ascii::is_alnum(c) or marks.find(c) != std::string_view::npos;
}

// Every byte a URI character, every `%` an escape of two hex digits.
[[nodiscard]] constexpr bool well_formed(std::string_view url) noexcept {
  for (std::size_t i = 0; i < url.size(); ++i) {
    if (url[i] == '%') {
      if (i + 2 >= url.size() or not is_hex(url[i + 1]) or
          not is_hex(url[i + 2])) {
        return false;
      }
      i += 2;
    } else if (not is_uri_char(url[i])) {
      return false;
    }
  }
  return true;
}

}  // namespace

std::expected<Verbatim, LinkError> Verbatim::make(Origin origin,
                                                  std::string_view pinned_path,
                                                  std::string_view url) {
  if (url.size() > max_bytes) {
    return std::unexpected{LinkError::too_long};
  }
  if (not well_formed(url)) {
    return std::unexpected{LinkError::bad_character};
  }
  const std::string_view root = base(origin);
  // The base and then the path's slash: no port, no user information, no
  // longer host name with the base as its prefix.
  if (not url.starts_with(root) or url.size() == root.size() or
      url[root.size()] != '/') {
    return std::unexpected{LinkError::other_origin};
  }
  const std::string_view rest = url.substr(root.size());
  const std::string_view path = rest.substr(0, rest.find('?'));
  if (path != pinned_path) {
    return std::unexpected{LinkError::other_path};
  }
  return Verbatim{origin, std::string{url}};
}

}  // namespace mov::io::wire
