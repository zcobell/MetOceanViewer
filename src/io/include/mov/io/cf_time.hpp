// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <chrono>
#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>

#include "mov/core/detail/ascii.hpp"
#include "mov/core/time.hpp"
#include "mov/io/error.hpp"

namespace mov::io {

/// The units of a CF time variable (B11: v4 cut the attribute at a fixed
/// offset and assumed seconds). Stored as an enumerator, so the factor to
/// milliseconds is an integer.
enum class CfTimeUnit : std::uint8_t { millisecond, second, minute, hour, day };

/// Milliseconds in one `u`: 1, 1000, 60000, 3600000, 86400000.
[[nodiscard]] constexpr std::int64_t unit_ms(CfTimeUnit u) noexcept {
  switch (u) {
    case CfTimeUnit::millisecond:
      return 1;
    case CfTimeUnit::second:
      return 1'000;
    case CfTimeUnit::minute:
      return 60'000;
    case CfTimeUnit::hour:
      return 3'600'000;
    case CfTimeUnit::day:
      return 86'400'000;
  }
  return 1;  // unreachable: every enumerator is handled
}

/// A parsed "<unit> since <reference time>". The epoch is in UTC; a zone
/// offset in the text has been applied.
struct CfTimeUnits {
  CfTimeUnit unit;
  core::Time epoch;
  friend constexpr bool operator==(const CfTimeUnits&,
                                   const CfTimeUnits&) = default;
};

/// Parses a CF `units` attribute of a time variable:
///
///   <unit> since <yyyy-mm-dd>[( |T)hh:mm[:ss[.f[f[f]]]]] [zone]
///
/// `unit` is millisecond, second, minute, hour or day (singular or plural,
/// any ASCII case), `since` any case, and the date and time are those of
/// core::parse_utc_datetime (four-digit year, two-digit fields). The zone is
/// `Z`, `UTC`, or an offset `+hh`, `+hh:mm` or `+hhmm` (also `-`), with
/// optional white space before it; the offset is applied, so the epoch is in
/// UTC. No zone means UTC. Surrounding white space is ignored.
///
/// Errors: `bad_time_units` for a missing or unknown unit, a missing `since`
/// or a bad zone; `bad_date` for the reference date and time, with the column
/// of the first bad character; `trailing_text` after the zone. Columns are
/// byte offsets in `text`; the line is 1.
[[nodiscard]] std::expected<CfTimeUnits, ParseError> parse_cf_time_units(
    std::string_view text);

/// The calendars a reader accepts. `standard` is CF's mixed Julian/Gregorian
/// calendar (`gregorian` is its old name); `proleptic_gregorian` is the civil
/// calendar of std::chrono.
enum class CfCalendar : std::uint8_t { standard, proleptic_gregorian };

/// The `calendar` attribute: absent means `standard`; `standard`,
/// `gregorian` and `proleptic_gregorian` (any ASCII case, white space around
/// it ignored) are the supported calendars; anything else is nullopt.
[[nodiscard]] constexpr std::optional<CfCalendar> parse_cf_calendar(
    std::optional<std::string_view> attribute) noexcept {
  if (not attribute) {
    return CfCalendar::standard;
  }
  const std::string_view name = core::detail::trim(*attribute);
  if (core::detail::equal_ignore_case(name, "standard") or
      core::detail::equal_ignore_case(name, "gregorian")) {
    return CfCalendar::standard;
  }
  if (core::detail::equal_ignore_case(name, "proleptic_gregorian")) {
    return CfCalendar::proleptic_gregorian;
  }
  return std::nullopt;
}

/// The first day of the Gregorian calendar, 1582-10-15. Before it the
/// `standard` calendar counts Julian days, which std::chrono does not
/// reproduce.
inline constexpr core::Time gregorian_reform =
    std::chrono::time_point_cast<std::chrono::milliseconds>(
        std::chrono::sys_days{std::chrono::year{1582} / std::chrono::October /
                              std::chrono::day{15}});

/// The instant `value` units after the epoch: core::checked_time with the
/// unit's factor, so a non-finite value, one beyond max_abs_time_ms, or a
/// sum that leaves it is nullopt. Under the `standard` calendar an epoch or a
/// result before 1582-10-15 is nullopt too: a mixed-calendar date is not
/// reproduced.
template <class V>
  requires(std::floating_point<V> or
           (std::integral<V> and not std::same_as<V, bool>))
[[nodiscard]] constexpr std::optional<core::Time> to_time(
    const CfTimeUnits& units, CfCalendar calendar, V value) noexcept {
  const bool mixed = calendar == CfCalendar::standard;
  if (mixed and units.epoch < gregorian_reform) {
    return std::nullopt;
  }
  const auto time = core::checked_time(
      value, std::chrono::milliseconds{unit_ms(units.unit)}, units.epoch);
  if (mixed and time and *time < gregorian_reform) {
    return std::nullopt;
  }
  return time;
}

}  // namespace mov::io
