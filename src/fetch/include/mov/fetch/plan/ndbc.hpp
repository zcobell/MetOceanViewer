// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// The files of an NDBC standard-met fetch (docs/providers-design.md §5.2,
// §1.3): one unit per source in precedence order, and the fallbacks when a
// file is absent (404).

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/overloaded.hpp"
#include "mov/core/provider/ndbc.hpp"
#include "mov/core/time.hpp"
#include "mov/fetch/error.hpp"

namespace mov::fetch::ndbc {

/// How far back realtime2 reaches.
inline constexpr std::chrono::days realtime_reach{45};

/// The first year NDBC has standard meteorological data for: the earliest
/// files in https://www.ndbc.noaa.gov/data/historical/stdmet/ are
/// eb01h1970.txt.gz and eb01h1971.txt.gz (listing read 2026-10-15; nothing
/// before 1970). A range is clipped to it.
inline constexpr std::chrono::year earliest_year{1970};

/// A year's quality-controlled file, `h{yyyy}`, of a year before last.
struct Historical {
  std::chrono::year year;
  friend constexpr bool operator==(Historical, Historical) = default;
};

/// Last year's `h{yyyy}`, which may not be published yet: its absence falls
/// back to the year's monthly files.
struct HistoricalLastYear {
  std::chrono::year year;
  friend constexpr bool operator==(HistoricalLastYear,
                                   HistoricalLastYear) = default;
};

/// A finalized month, `{Mon}/{id}{m}{yyyy}.txt.gz`, its month spelled 1–9
/// or, for October to December, a–c (believed, not yet verified: §1.3).
struct Monthly {
  std::chrono::year year;
  std::chrono::month month;
  friend constexpr bool operator==(Monthly, Monthly) = default;
};

/// October to December, the only months with a second spelling.
enum class LateMonth : std::uint8_t { october, november, december };

/// A finalized October, November or December spelled in decimal (10–12),
/// tried when the letter is absent.
struct MonthlyDecimal {
  std::chrono::year year;
  LateMonth month;
  friend constexpr bool operator==(MonthlyDecimal, MonthlyDecimal) = default;
};

/// The latest, not yet finalized month, `{Mon}/{id}.txt`. Its name has no
/// year: P7's spec clips its rows to extent() before the merge.
struct MonthlyLatest {
  std::chrono::year year;
  std::chrono::month month;
  friend constexpr bool operator==(MonthlyLatest, MonthlyLatest) = default;
};

/// `realtime2/{ID}.txt`: about the last 45 days. `year` is the fetch's
/// `now`'s, for the label.
struct Realtime {
  std::chrono::year year;
  friend constexpr bool operator==(Realtime, Realtime) = default;
};

/// One file to try: the unit of an NDBC fetch.
using Source = std::variant<Historical, HistoricalLastYear, Monthly,
                            MonthlyDecimal, MonthlyLatest, Realtime>;

/// The sources of a request at `now`, in precedence order (historical by
/// year, then the current year's past months, then realtime):
/// - `h{y}` for every year of the range from 1970 (earliest_year) before
///   the current one, last year's as HistoricalLastYear;
/// - each past month of the current year in the range;
/// - realtime2 when the range began before `now` and ends after
///   `now - realtime_reach`.
/// Nothing for a range entirely after `now` or before 1970.
/// UnsupportedRange when the range leaves the years std::chrono::year can
/// name (±32767).
[[nodiscard]] std::expected<std::vector<Source>, Error> initial(
    const core::ndbc::StdMet& request, core::Time now);

/// The sources tried when `source` was absent, in plan order: last year's
/// `h{y}` falls back to that year's months in the range; a month's letter
/// spelling to its decimal one (October to December), then to the latest
/// file; everything else to nothing.
[[nodiscard]] std::vector<Source> fallback(const core::ndbc::StdMet& request,
                                           const Source& source);

/// The part of the request a source can hold: the request's range clipped
/// to the source's year or month (realtime: the range). P7 clips the rows of
/// a MonthlyLatest file (whose name has no year) to it, so rows of another
/// year neither reach the merge nor count as dropped duplicates.
[[nodiscard]] core::TimeRange extent(const core::ndbc::StdMet& request,
                                     const Source& source);

[[nodiscard]] constexpr std::chrono::month month_of(LateMonth m) noexcept {
  constexpr unsigned october = 10;
  return std::chrono::month{october + static_cast<unsigned>(m)};
}

/// The file kind of a source.
[[nodiscard]] constexpr core::ndbc::FileKind kind(const Source& source) {
  using core::ndbc::FileKind;
  return std::visit(
      core::Overloaded{
          [](const Historical&) { return FileKind::historical; },
          [](const HistoricalLastYear&) { return FileKind::historical; },
          [](const Monthly&) { return FileKind::monthly; },
          [](const MonthlyDecimal&) { return FileKind::monthly; },
          [](const MonthlyLatest&) { return FileKind::monthly_latest; },
          [](const Realtime&) { return FileKind::realtime; }},
      source);
}

[[nodiscard]] NdbcFile label(const Source& source);

/// The month part of a finalized month's file name: "1"–"9" or "a"–"c";
/// nullopt for a month that is not ok().
[[nodiscard]] constexpr std::optional<std::string_view> month_token(
    const Monthly& m) noexcept {
  if (not m.month.ok()) {
    return std::nullopt;
  }
  constexpr std::array<std::string_view, 12> tokens{
      "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c"};
  return tokens[static_cast<unsigned>(m.month) - 1];
}

/// "10", "11" or "12".
[[nodiscard]] constexpr std::string_view month_token(
    const MonthlyDecimal& m) noexcept {
  switch (m.month) {
    case LateMonth::october:
      return "10";
    case LateMonth::november:
      return "11";
    case LateMonth::december:
      break;
  }
  return "12";
}

}  // namespace mov::fetch::ndbc
