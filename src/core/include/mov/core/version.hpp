// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <compare>
#include <cstddef>
#include <optional>
#include <string_view>

namespace mov::core {

/// A "major.minor.patch" version number.
struct VersionNumber {
  int major_version = 0;
  int minor_version = 0;
  int patch_version = 0;

  friend constexpr auto operator<=>(const VersionNumber&,
                                    const VersionNumber&) = default;
};

/// The MetOceanViewer version string, "major.minor.patch", from
/// project(VERSION) in the top-level CMakeLists.txt.
[[nodiscard]] std::string_view version() noexcept;

/// The MetOceanViewer version as numbers.
[[nodiscard]] VersionNumber version_number() noexcept;

namespace detail {

/// A non-empty run of at most 9 decimal digits, or nullopt.
[[nodiscard]] constexpr std::optional<int> parse_version_component(
    std::string_view text) noexcept {
  constexpr std::size_t max_digits = 9;  // fits in int without overflow
  if (text.empty() or text.size() > max_digits) {
    return std::nullopt;
  }
  int value = 0;
  for (const char c : text) {
    if (c < '0' or c > '9') {
      return std::nullopt;
    }
    value = (value * 10) + (c - '0');
  }
  return value;
}

}  // namespace detail

/// Parses exactly "major.minor.patch" (decimal, no sign, no whitespace).
[[nodiscard]] constexpr std::optional<VersionNumber> parse_version(
    std::string_view text) noexcept {
  const std::size_t first_dot = text.find('.');
  if (first_dot == std::string_view::npos) {
    return std::nullopt;
  }
  const std::size_t second_dot = text.find('.', first_dot + 1);
  if (second_dot == std::string_view::npos) {
    return std::nullopt;
  }
  const auto major = detail::parse_version_component(text.substr(0, first_dot));
  const auto minor = detail::parse_version_component(
      text.substr(first_dot + 1, second_dot - first_dot - 1));
  const auto patch =
      detail::parse_version_component(text.substr(second_dot + 1));
  if (not major or not minor or not patch) {
    return std::nullopt;
  }
  return VersionNumber{.major_version = *major,
                       .minor_version = *minor,
                       .patch_version = *patch};
}

}  // namespace mov::core
