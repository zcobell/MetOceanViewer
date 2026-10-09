// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <chrono>
#include <cstdint>

#include "mov/core/station.hpp"
#include "mov/core/time.hpp"

/// The request vocabulary of NDBC (docs/providers-design.md section 3.5).
namespace mov::core::ndbc {

/// Standard meteorological data of one station over a time range: every
/// column the files have; the caller picks the ones it shows.
struct StdMet {
  StationId<provider::Ndbc> station;
  TimeRange range;
  friend constexpr bool operator==(const StdMet&, const StdMet&) = default;
};

/// The kind of file a series came from, in precedence order: on equal times
/// an earlier kind's row wins (the quality-controlled one).
enum class FileKind : std::uint8_t {
  historical,      // the year's h{yyyy} file
  monthly,         // a finalized month of the current year (.txt.gz)
  monthly_latest,  // the latest month, not yet finalized (.txt)
  realtime,        // realtime2: the last 45 days
};

/// The grain the files' times are on: whole minutes.
inline constexpr std::chrono::minutes grain{1};

}  // namespace mov::core::ndbc
