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
#include "mov/io/read.hpp"

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

/// Parses a CF `units` attribute of a time variable (CF section 4.4, as
/// foreign files write it):
///
///   <unit> since <Y-M-D>[( |T)h:m[:s[.f...]]] [zone]
///
/// - `unit`: millisecond, second, minute, hour or day (singular or plural), or
///   an abbreviation `d`, `h`, `hr`, `min`, `sec`, `s` (also with a plural
///   `s`: `hrs`, `mins`, `secs`), in any ASCII case. `since` in any case.
/// - The year has four digits; month, day, hour, minute and second one or two
///   (`1800-1-1 0:0:0.0`). The fraction of a second has any number of digits
///   and is rounded to the nearest millisecond (halves up); when a nonzero
///   digit beyond the millisecond was dropped the result carries a
///   `time_precision_dropped` warning.
/// - The date and the clock are separated by exactly one space or a `T`.
///   (core::parse_utc_datetime, for ADCIRC's own text, stays strict.)
/// - The zone is none (UTC), `Z`, `UTC`, or an offset `+hh`, `+hh:mm`,
///   `+hhmm` (also `-`), with optional white space before it; the offset is
///   applied, so the epoch is in UTC.
/// - Surrounding white space is ignored, and so is any amount between words.
///
/// Errors: `bad_time_units` for a missing or unknown unit, a missing `since`
/// or a bad zone (the column of the sign for an offset); `bad_date` for the
/// reference time (the column of the character that does not fit, or of the
/// field whose value is out of range); `trailing_text` after the zone. The
/// line is 1; columns are byte offsets in `text`, which is also the context.
[[nodiscard]] std::expected<Read<CfTimeUnits>, ParseError> parse_cf_time_units(
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

/// Why a (units, calendar) pair cannot be turned into times at all. A reader
/// reports it once per variable as `FormatErrc::unsupported_calendar`; it is a
/// property of the file, not of any value.
enum class CfClockError : std::uint8_t {
  /// The `standard` calendar with an epoch before 1582-10-15: every time
  /// counted from it is a date in the mixed Julian/Gregorian calendar, which is
  /// not reproduced.
  epoch_before_gregorian_reform,
};

/// The times of one CF time variable: its units and calendar, checked once.
/// Making a CfClock is the reproducibility check; `at` is then only the range
/// check of one value.
class CfClock {
 public:
  [[nodiscard]] static constexpr std::expected<CfClock, CfClockError> make(
      const CfTimeUnits& units, CfCalendar calendar) noexcept {
    if (calendar == CfCalendar::standard and units.epoch < gregorian_reform) {
      return std::unexpected{CfClockError::epoch_before_gregorian_reform};
    }
    return CfClock{units, calendar};
  }

  /// The instant `value` units after the epoch: core::checked_time with the
  /// unit's factor, so a non-finite value, one beyond max_abs_time_ms, or a sum
  /// that leaves it is nullopt. Under the `standard` calendar a result before
  /// 1582-10-15 is nullopt too (an epoch after it and a negative value).
  template <class V>
    requires(std::floating_point<V> or
             (std::integral<V> and not std::same_as<V, bool>))
  [[nodiscard]] constexpr std::optional<core::Time> at(V value) const noexcept {
    const auto time = core::checked_time(
        value, std::chrono::milliseconds{unit_ms(units_.unit)}, units_.epoch);
    if (calendar_ == CfCalendar::standard and time and
        *time < gregorian_reform) {
      return std::nullopt;
    }
    return time;
  }

  [[nodiscard]] constexpr const CfTimeUnits& units() const noexcept {
    return units_;
  }
  [[nodiscard]] constexpr CfCalendar calendar() const noexcept {
    return calendar_;
  }

 private:
  constexpr CfClock(const CfTimeUnits& units, CfCalendar calendar) noexcept
      : units_{units}, calendar_{calendar} {}

  CfTimeUnits units_;
  CfCalendar calendar_;
};

}  // namespace mov::io
