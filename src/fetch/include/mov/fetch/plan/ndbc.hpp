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
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/provider/ndbc.hpp"
#include "mov/core/time.hpp"
#include "mov/fetch/error.hpp"

namespace mov::fetch::ndbc {

/// How far back realtime2 reaches.
inline constexpr std::chrono::days realtime_reach{45};

/// A year's quality-controlled file, `h{yyyy}`. Last year's may not be
/// published yet: `months_when_absent` says its absence falls back to the
/// year's monthly files.
struct Historical {
  std::chrono::year year;
  bool months_when_absent;
  friend constexpr bool operator==(Historical, Historical) = default;
};

/// Which spelling of a monthly file's month: the primary one (1–9, and
/// a/b/c for 10–12, believed but not verified, §1.3) or, for 10–12 only,
/// the decimal fallback (10, 11, 12).
enum class MonthSpelling : std::uint8_t { primary, decimal };

/// A finalized month, `{Mon}/{id}{m}{yyyy}.txt.gz`. Only plan functions
/// build one, so `decimal` is never paired with a month below October.
struct Monthly {
  std::chrono::year year;
  std::chrono::month month;
  MonthSpelling spelling;
  friend constexpr bool operator==(Monthly, Monthly) = default;
};

/// The latest, not yet finalized month, `{Mon}/{id}.txt`.
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
using Source = std::variant<Historical, Monthly, MonthlyLatest, Realtime>;

/// The sources of a request at `now`, in precedence order (historical by
/// year, then the current year's past months, then realtime):
/// - `h{y}` for every year of the range before the current one;
/// - each past month of the current year in the range;
/// - realtime2 when the range began before `now` and ends after
///   `now - realtime_reach`.
/// Nothing for a range entirely after `now`.
[[nodiscard]] std::vector<Source> initial(const core::ndbc::StdMet& request,
                                          core::Time now);

/// The sources tried when `source` was absent, in plan order: last year's
/// `h{y}` falls back to that year's months in the range; a month's primary
/// spelling to its decimal one (October to December), then to the latest
/// file; everything else to nothing. A source that had data has no
/// successor.
[[nodiscard]] std::vector<Source> fallback(const core::ndbc::StdMet& request,
                                           const Source& source);

/// The file kind of a source.
[[nodiscard]] constexpr core::ndbc::FileKind kind(
    const Source& source) noexcept {
  using core::ndbc::FileKind;
  if (std::holds_alternative<Historical>(source)) {
    return FileKind::historical;
  }
  if (std::holds_alternative<Monthly>(source)) {
    return FileKind::monthly;
  }
  if (std::holds_alternative<MonthlyLatest>(source)) {
    return FileKind::monthly_latest;
  }
  return FileKind::realtime;
}

[[nodiscard]] NdbcFile label(const Source& source);

/// The month part of a monthly file's name: "1"–"9", "a"–"c" or "10"–"12".
[[nodiscard]] constexpr std::string_view month_token(
    const Monthly& m) noexcept {
  constexpr std::array<std::string_view, 12> primary{
      "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c"};
  constexpr std::array<std::string_view, 3> decimal{"10", "11", "12"};
  const auto index =
      static_cast<std::size_t>(static_cast<unsigned>(m.month)) - 1;
  constexpr std::size_t first_two_digit = 9;
  if (m.spelling == MonthSpelling::decimal and index >= first_two_digit and
      index - first_two_digit < decimal.size()) {
    return decimal[index - first_two_digit];
  }
  return primary[index % primary.size()];
}

}  // namespace mov::fetch::ndbc
