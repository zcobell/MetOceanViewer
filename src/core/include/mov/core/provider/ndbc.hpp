// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <chrono>
#include <cstdint>

#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/windows.hpp"

/// The request vocabulary of NDBC (docs/providers-design.md section 3.5).
namespace mov::core::ndbc {

/// Standard meteorological data of one station over a time range: every
/// column the files have; the caller picks the ones it shows.
struct StdMet {
  StationId<provider::Ndbc> station;
  TimeRange range;
  friend constexpr bool operator==(const StdMet&, const StdMet&) = default;
};

/// The kind of file a series came from.
enum class FileKind : std::uint8_t {
  historical,      // the year's h{yyyy} file
  monthly,         // a finalized month of the current year (.txt.gz)
  monthly_latest,  // the latest month, not yet finalized (.txt)
  realtime,        // realtime2: the last 45 days
};

/// The rank of a file kind when two files have a row at one time: the lower
/// rank's row wins, so the quality-controlled file beats the provisional
/// one (historical 0, monthly 1, monthly_latest 2, realtime 3). Spelled out
/// rather than read from the enumerator order, which is not a contract.
[[nodiscard]] constexpr std::uint8_t precedence(FileKind kind) noexcept {
  switch (kind) {
    case FileKind::historical:
      return 0;
    case FileKind::monthly:
      return 1;
    case FileKind::monthly_latest:
      return 2;
    case FileKind::realtime:
      break;
  }
  return 3;
}

/// The grain the files' times are on: whole minutes.
inline constexpr WindowSpan grain = WindowSpan::of<1, std::chrono::minutes>();

}  // namespace mov::core::ndbc
