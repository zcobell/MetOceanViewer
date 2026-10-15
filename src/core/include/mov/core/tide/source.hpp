// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include "mov/core/station.hpp"
#include "mov/core/tide/licence.hpp"
#include "mov/core/utf8.hpp"

// The sources of a harmonics file and the station ids they namespace
// (docs/harmonics-json.md sections 6 and 8.1).

namespace mov::core::detail {
struct HarmonicsAccess;  // builds the file's records (src/core/tide)
}  // namespace mov::core::detail

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
    if (not is_lower(s.front())) {
      return std::unexpected(SourceTokenError::bad_char);
    }
    for (const char c : s.substr(1)) {
      if (not(is_lower(c) or (c >= '0' and c <= '9') or c == '-')) {
        return std::unexpected(SourceTokenError::bad_char);
      }
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
    for (std::size_t i = 0; i < validated.size(); ++i) {
      chars_[i] = validated[i];
    }
  }

  [[nodiscard]] static constexpr bool is_lower(char c) noexcept {
    return c >= 'a' and c <= 'z';
  }

  // Unused bytes stay 0, so the defaulted <=> is byte order of view().
  std::array<char, max_size> chars_{};
  std::uint8_t size_{};
};

/// A source id written in the source code; an invalid literal does not
/// compile.
[[nodiscard]] consteval SourceToken source_token(std::string_view s) {
  return SourceToken::make(s).value();
}

/// The namespace of a harmonics station id `<source>:<local>`
/// (docs/harmonics-json.md 8.1): at most 255 bytes of well-formed UTF-8, a
/// SourceToken, a ':', then a non-empty local part without control
/// characters (U+0000-U+001F, U+007F). nullopt if `id` does not have that
/// form.
[[nodiscard]] constexpr std::optional<SourceToken> station_namespace(
    std::string_view id) noexcept {
  constexpr std::size_t max_id_bytes = 255;
  const std::size_t colon = id.find(':');
  if (id.size() > max_id_bytes or colon == std::string_view::npos or
      colon + 1 == id.size() or not is_valid_utf8(id)) {
    return std::nullopt;
  }
  for (const char c : id.substr(colon + 1)) {
    const auto byte = static_cast<unsigned char>(c);
    if (byte < 0x20 or byte == 0x7F) {
      return std::nullopt;
    }
  }
  const auto token = SourceToken::make(id.substr(0, colon));
  return token ? std::optional<SourceToken>{*token} : std::nullopt;
}

/// The position of a source in a HarmonicsFile (sorted by id). Only the
/// file makes one.
class SourceIndex {
 public:
  [[nodiscard]] constexpr std::size_t value() const noexcept { return v_; }
  friend constexpr auto operator<=>(SourceIndex, SourceIndex) = default;

 private:
  friend struct detail::HarmonicsAccess;
  explicit constexpr SourceIndex(std::size_t v) noexcept : v_{v} {}
  std::size_t v_;
};

/// One element of `sources` (docs/harmonics-json.md section 6).
struct Source {
  SourceToken id;
  StationText name;
  std::optional<std::string> url;
  Licence licence;  // the default licence of the source's stations
  std::string attribution;
  std::chrono::sys_days retrieved;
  std::optional<std::string> version;
  std::optional<std::string> sha256;
  std::optional<std::string> notes;
  friend bool operator==(const Source&, const Source&) = default;
};

}  // namespace mov::core::tide
