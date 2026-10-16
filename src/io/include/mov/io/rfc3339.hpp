// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

#include "mov/core/time.hpp"

/// RFC 3339 timestamps (docs/providers-design.md section 4.3): the USGS Water
/// Data API's `time`, `begin` and `end` ("2026-10-01T00:00:00+00:00").
namespace mov::io {

namespace detail {

/// The milliseconds of a fraction's digits after the `.`: the first three
/// scaled, any further ones (at most nine digits in all) truncated, since a
/// core::Time has millisecond resolution.
[[nodiscard]] constexpr std::expected<int, core::DateTimeError> fraction_digits(
    core::DateTimeCursor& cursor) noexcept {
  constexpr std::size_t max_digits = 9;
  if (not cursor.next_is_digit()) {
    return std::unexpected{cursor.error_here()};
  }
  int ms = 0;
  int scale = 100;
  for (std::size_t n = 0; cursor.next_is_digit(); ++n) {
    if (n == max_digits) {
      return std::unexpected{cursor.error_here()};
    }
    const auto digit = cursor.field(1, 0, 9);
    if (not digit) {
      return std::unexpected{digit.error()};
    }
    ms += *digit * scale;
    scale /= 10;
  }
  return ms;
}

/// `Z` or `z` (zero), else `+hh:mm` or `-hh:mm` as signed minutes east of UTC.
[[nodiscard]] constexpr std::expected<int, core::DateTimeError> utc_offset(
    core::DateTimeCursor& cursor) noexcept {
  if (cursor.accept('Z') or cursor.accept('z')) {
    return 0;
  }
  const bool west = cursor.next_is('-');
  if (not(cursor.accept('+') or cursor.accept('-'))) {
    return std::unexpected{cursor.error_here()};
  }
  const auto hours = cursor.field(2, 0, 23);
  if (not hours) {
    return std::unexpected{hours.error()};
  }
  if (const auto colon = cursor.expect(':'); not colon) {
    return std::unexpected{colon.error()};
  }
  const auto minutes = cursor.field(2, 0, 59);
  if (not minutes) {
    return std::unexpected{minutes.error()};
  }
  const int east = (*hours * 60) + *minutes;
  return west ? -east : east;
}

}  // namespace detail

/// Parses an RFC 3339 date-time (section 5.6) as the UTC instant it names:
///
///   yyyy-mm-dd ("T" | "t") hh:mm:ss [ "." 1*9digit ] ( "Z" | "z" | ("+" | "-")
///   hh:mm )
///
/// Seconds are required, a leap second (60) is refused, and so is anything
/// RFC 3339 does not allow: a space for the `T`, a missing offset, `+hhmm`,
/// surrounding white space. The offset is subtracted (`-00:00`, "local
/// offset unknown", is UTC). A fraction's digits past the millisecond are
/// truncated. The error is where the text stopped fitting (the 0-based
/// offset), or the start of a field whose value is out of range.
[[nodiscard]] constexpr std::expected<core::Time, core::DateTimeError>
parse_rfc3339(std::string_view text) noexcept {
  using std::chrono::milliseconds;
  core::DateTimeCursor cursor{text};
  const auto date = core::parse_date(cursor);
  if (not date) {
    return std::unexpected{date.error()};
  }
  if (not(cursor.accept('T') or cursor.accept('t'))) {
    return std::unexpected{cursor.error_here()};
  }
  const auto hour = cursor.field(2, 0, 23);
  if (not hour) {
    return std::unexpected{hour.error()};
  }
  if (const auto colon = cursor.expect(':'); not colon) {
    return std::unexpected{colon.error()};
  }
  const auto minute = cursor.field(2, 0, 59);
  if (not minute) {
    return std::unexpected{minute.error()};
  }
  if (const auto colon = cursor.expect(':'); not colon) {
    return std::unexpected{colon.error()};
  }
  const auto second = cursor.field(2, 0, 59);
  if (not second) {
    return std::unexpected{second.error()};
  }
  int fraction = 0;
  if (cursor.accept('.')) {
    const auto ms = detail::fraction_digits(cursor);
    if (not ms) {
      return std::unexpected{ms.error()};
    }
    fraction = *ms;
  }
  const auto offset = detail::utc_offset(cursor);
  if (not offset) {
    return std::unexpected{offset.error()};
  }
  if (not cursor.at_end()) {
    return std::unexpected{cursor.error_here()};
  }
  // In 64-bit milliseconds throughout: MSVC's std::chrono::minutes and
  // hours have a 32-bit rep.
  constexpr std::int64_t millis_per_second = 1000;
  constexpr std::int64_t millis_per_minute = 60 * millis_per_second;
  const std::int64_t clock_ms =
      ((std::int64_t{*hour} * 60 + *minute) * millis_per_minute) +
      (std::int64_t{*second} * millis_per_second) + fraction;
  const std::int64_t offset_ms = std::int64_t{*offset} * millis_per_minute;
  return std::chrono::time_point_cast<milliseconds>(*date) +
         milliseconds{clock_ms - offset_ms};
}

}  // namespace mov::io
