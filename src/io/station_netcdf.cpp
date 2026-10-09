// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/station_netcdf.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>

#include "mov/core/ascii.hpp"
#include "mov/core/station_table.hpp"

namespace mov::io {

namespace {

constexpr std::size_t max_version_digits = 4;

/// 1 to 4 decimal digits without a leading zero (except "0" itself).
constexpr std::optional<unsigned> version_number(std::string_view text) {
  if (text.empty() or text.size() > max_version_digits or
      not std::ranges::all_of(text, core::ascii::is_digit) or
      (text.size() > 1 and text.front() == '0')) {
    return std::nullopt;
  }
  unsigned value = 0;
  for (const char c : text) {
    value = (value * 10U) + static_cast<unsigned>(c - '0');
  }
  return value;
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

std::optional<CfVersion> parse_cf_conventions(
    std::string_view conventions) noexcept {
  constexpr std::string_view separators = " \t\n\r,";
  constexpr std::string_view prefix = "CF-";
  while (not conventions.empty()) {
    const std::size_t start = conventions.find_first_not_of(separators);
    if (start == std::string_view::npos) {
      break;
    }
    conventions.remove_prefix(start);
    const std::size_t end = conventions.find_first_of(separators);
    const std::string_view token = conventions.substr(0, end);
    if (token.starts_with(prefix)) {
      if (const auto v =
              parse_station_nc_version(token.substr(prefix.size()))) {
        return CfVersion{.major = v->major, .minor = v->minor};
      }
    }
    conventions.remove_prefix(end == std::string_view::npos ? conventions.size()
                                                            : end);
  }
  return std::nullopt;
}

}  // namespace mov::io
