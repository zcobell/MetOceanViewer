// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Calendar arithmetic on 64-bit integers for the text formats. std::chrono's
// year_month_day holds a 16-bit year (+-32767), but a core::Time reaches
// +-2^53 ms, about +-285,000 years; the text writers must format those, and
// the text readers must refuse years the format cannot hold, without ever
// building a year_month_day out of range.
//
// days_from_civil and civil_from_days are Howard Hinnant's public-domain
// algorithms (http://howardhinnant.github.io/date_algorithms.html), proleptic
// Gregorian, on std::int64_t.

#pragma once

#include <chrono>
#include <cstdint>

#include "mov/core/time.hpp"

namespace mov::io::detail {

inline constexpr std::int64_t ms_per_second = 1000;
inline constexpr std::int64_t ms_per_day = 86'400'000;

/// Days from 1970-01-01 to the date (month 1-12, day 1-31).
[[nodiscard]] constexpr std::int64_t days_from_civil(std::int64_t year,
                                                     unsigned month,
                                                     unsigned day) noexcept {
  year -= month <= 2 ? 1 : 0;
  const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
  const auto year_of_era = static_cast<unsigned>(year - era * 400);
  const unsigned day_of_year =
      (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned day_of_era =
      year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
  return era * 146097 + static_cast<std::int64_t>(day_of_era) - 719468;
}

struct CivilDate {
  std::int64_t year;
  unsigned month;
  unsigned day;
  friend constexpr bool operator==(const CivilDate&,
                                   const CivilDate&) = default;
};

/// The date `days` after 1970-01-01 (before it when negative).
[[nodiscard]] constexpr CivilDate civil_from_days(std::int64_t days) noexcept {
  days += 719468;
  const std::int64_t era = (days >= 0 ? days : days - 146096) / 146097;
  const auto day_of_era = static_cast<unsigned>(days - era * 146097);
  const unsigned year_of_era = (day_of_era - day_of_era / 1460 +
                                day_of_era / 36524 - day_of_era / 146096) /
                               365;
  const std::int64_t year = static_cast<std::int64_t>(year_of_era) + era * 400;
  const unsigned day_of_year =
      day_of_era - (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
  const unsigned shifted_month = (5 * day_of_year + 2) / 153;
  const unsigned day = day_of_year - (153 * shifted_month + 2) / 5 + 1;
  const unsigned month =
      shifted_month < 10 ? shifted_month + 3 : shifted_month - 9;
  return {.year = year + (month <= 2 ? 1 : 0), .month = month, .day = day};
}

[[nodiscard]] constexpr bool is_leap_year(std::int64_t year) noexcept {
  return year % 4 == 0 and (year % 100 != 0 or year % 400 == 0);
}

/// 28-31, for month 1-12.
[[nodiscard]] constexpr unsigned days_in_month(std::int64_t year,
                                               unsigned month) noexcept {
  constexpr unsigned lengths[12] = {31, 28, 31, 30, 31, 30,
                                    31, 31, 30, 31, 30, 31};
  return month == 2 and is_leap_year(year) ? 29U : lengths[month - 1];
}

/// A core::Time as calendar fields.
struct CivilTime {
  std::int64_t year;
  unsigned month;
  unsigned day;
  unsigned hour;
  unsigned minute;
  unsigned second;
  unsigned millisecond;
  friend constexpr bool operator==(const CivilTime&,
                                   const CivilTime&) = default;
};

/// Exact for every core::Time (a time before the epoch is floored, so the
/// clock fields are never negative).
[[nodiscard]] constexpr CivilTime civil_fields(core::Time t) noexcept {
  const std::int64_t ms = t.time_since_epoch().count();
  std::int64_t days = ms / ms_per_day;
  std::int64_t of_day = ms % ms_per_day;
  if (of_day < 0) {
    of_day += ms_per_day;
    --days;
  }
  const CivilDate date = civil_from_days(days);
  const auto seconds_of_day = static_cast<unsigned>(of_day / ms_per_second);
  return {.year = date.year,
          .month = date.month,
          .day = date.day,
          .hour = seconds_of_day / 3600,
          .minute = seconds_of_day / 60 % 60,
          .second = seconds_of_day % 60,
          .millisecond = static_cast<unsigned>(of_day % ms_per_second)};
}

/// The inverse of civil_fields for fields that are a real date and time (the
/// caller checked; the arithmetic is exact whatever the year).
[[nodiscard]] constexpr core::Time time_of(const CivilTime& f) noexcept {
  const std::int64_t seconds_of_day =
      (static_cast<std::int64_t>(f.hour) * 60 + f.minute) * 60 + f.second;
  const std::int64_t ms = days_from_civil(f.year, f.month, f.day) * ms_per_day +
                          seconds_of_day * ms_per_second + f.millisecond;
  return core::Time{std::chrono::milliseconds{ms}};
}

/// The times IMEDS can hold: its four-digit years, [0000-01-01 00:00:00,
/// 10000-01-01 00:00:00). Compare against these, not against a year_month_day.
inline constexpr core::Time imeds_first = time_of({.year = 0,
                                                   .month = 1,
                                                   .day = 1,
                                                   .hour = 0,
                                                   .minute = 0,
                                                   .second = 0,
                                                   .millisecond = 0});
inline constexpr core::Time imeds_end = time_of({.year = 10000,
                                                 .month = 1,
                                                 .day = 1,
                                                 .hour = 0,
                                                 .minute = 0,
                                                 .second = 0,
                                                 .millisecond = 0});

[[nodiscard]] constexpr bool in_imeds_range(core::Time t) noexcept {
  return t >= imeds_first and t < imeds_end;
}

}  // namespace mov::io::detail
