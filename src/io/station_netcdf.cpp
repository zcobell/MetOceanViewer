// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/station_netcdf.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>

#include "mov/core/station_table.hpp"
#include "station_netcdf_format.hpp"

namespace mov::io {

namespace {

constexpr std::size_t max_version_digits = 4;

constexpr bool is_digit(char c) noexcept { return c >= '0' and c <= '9'; }

/// 1 to 4 decimal digits without a leading zero (except "0" itself).
constexpr std::optional<unsigned> version_number(std::string_view text) {
  if (text.empty() or text.size() > max_version_digits or
      not std::ranges::all_of(text, is_digit) or
      (text.size() > 1 and text.front() == '0')) {
    return std::nullopt;
  }
  unsigned value = 0;
  for (const char c : text) {
    value = (value * 10U) + static_cast<unsigned>(c - '0');
  }
  return value;
}

/// The (major, minor) of a "CF-<major>.<minor>" token, or nullopt.
constexpr std::optional<StationNcVersion> cf_version(std::string_view token) {
  constexpr std::string_view prefix = "CF-";
  if (not token.starts_with(prefix)) {
    return std::nullopt;
  }
  return parse_station_nc_version(token.substr(prefix.size()));
}

}  // namespace

std::optional<StationNcVersion> parse_station_nc_version(
    std::string_view text) noexcept {
  const std::size_t dot = text.find('.');
  if (dot == std::string_view::npos) {
    return std::nullopt;
  }
  const auto major = version_number(text.substr(0, dot));
  const auto minor = version_number(text.substr(dot + 1));
  if (not major or not minor) {
    return std::nullopt;
  }
  return StationNcVersion{.major = *major, .minor = *minor};
}

StationNcLayout choose_layout(const core::StationTable& table) noexcept {
  return table.single_axis() ? StationNcLayout::orthogonal
                             : StationNcLayout::incomplete;
}

namespace detail::station_nc {

bool has_cf_1_6_or_later(std::string_view text) noexcept {
  constexpr StationNcVersion oldest{.major = 1, .minor = 6};
  constexpr std::string_view separators = " \t\n\r,";
  while (not text.empty()) {
    const std::size_t start = text.find_first_not_of(separators);
    if (start == std::string_view::npos) {
      break;
    }
    text.remove_prefix(start);
    const std::size_t end = text.find_first_of(separators);
    const auto version = cf_version(text.substr(0, end));
    if (version and *version >= oldest) {
      return true;
    }
    text.remove_prefix(end == std::string_view::npos ? text.size() : end);
  }
  return false;
}

}  // namespace detail::station_nc

}  // namespace mov::io
