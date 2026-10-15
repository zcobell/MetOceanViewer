// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <type_traits>

#include "mov/core/utf8.hpp"

// Source ids and the station-id grammar of a harmonics file
// (docs/harmonics-json.md sections 6 and 8.1). Kept apart from source.hpp so
// that mov/core/station.hpp can use the grammar for provider::Harmonics.

namespace mov::core::tide {

enum class SourceTokenError : std::uint8_t { empty, too_long, bad_char };

/// A source id: ^[a-z][a-z0-9-]{0,31}$, stored inline (trivially copyable,
/// constexpr). It is the namespace of its stations' ids.
class SourceToken {
 public:
  static constexpr std::size_t max_size = 32;

  [[nodiscard]] static constexpr std::expected<SourceToken, SourceTokenError>
  make(std::string_view s) noexcept {
    if (s.empty()) {
      return std::unexpected(SourceTokenError::empty);
    }
    if (s.size() > max_size) {
      return std::unexpected(SourceTokenError::too_long);
    }
    if (not is_lower(s.front()) or
        not std::ranges::all_of(s.substr(1), is_tail_char)) {
      return std::unexpected(SourceTokenError::bad_char);
    }
    return SourceToken{s};
  }

  [[nodiscard]] constexpr std::string_view view() const& noexcept {
    return {chars_.data(), size_};
  }
  std::string_view view() const&& = delete;  // would dangle

  friend constexpr auto operator<=>(const SourceToken&,
                                    const SourceToken&) = default;
  friend constexpr bool operator==(const SourceToken&,
                                   const SourceToken&) = default;

 private:
  // Only make() calls it, with checked text (MSVC 19.44 cannot use a private
  // defaulted default constructor from the class's own static member).
  explicit constexpr SourceToken(std::string_view validated) noexcept
      : size_{static_cast<std::uint8_t>(validated.size())} {
    std::ranges::copy(validated, chars_.begin());
  }

  [[nodiscard]] static constexpr bool is_lower(char c) noexcept {
    return c >= 'a' and c <= 'z';
  }
  [[nodiscard]] static constexpr bool is_tail_char(char c) noexcept {
    return is_lower(c) or (c >= '0' and c <= '9') or c == '-';
  }

  // Unused bytes stay 0, so the defaulted <=> is byte order of view().
  std::array<char, max_size> chars_{};
  std::uint8_t size_{};
};
static_assert(std::is_trivially_copyable_v<SourceToken>);

/// A source id written in the source code; an invalid literal does not
/// compile.
[[nodiscard]] consteval SourceToken source_token(std::string_view s) {
  return SourceToken::make(s).value();
}

/// The namespace of a harmonics station id `<source>:<local>`
/// (docs/harmonics-json.md 8.1): at most 255 bytes of well-formed UTF-8, a
/// SourceToken, a ':', then a non-empty local part without control
/// characters (U+0000-U+001F, U+007F). nullopt if `id` does not have that
/// form. provider::Harmonics accepts exactly these ids.
[[nodiscard]] constexpr std::optional<SourceToken> station_namespace(
    std::string_view id) noexcept {
  constexpr std::size_t max_id_bytes = 255;
  const std::size_t colon = id.find(':');
  if (id.size() > max_id_bytes or colon == std::string_view::npos or
      colon + 1 == id.size() or not is_valid_utf8(id)) {
    return std::nullopt;
  }
  const auto is_control = [](char c) {
    const auto byte = static_cast<unsigned char>(c);
    return byte < 0x20 or byte == 0x7F;
  };
  if (std::ranges::any_of(id.substr(colon + 1), is_control)) {
    return std::nullopt;
  }
  const auto token = SourceToken::make(id.substr(0, colon));
  return token ? std::optional<SourceToken>{*token} : std::nullopt;
}

}  // namespace mov::core::tide
