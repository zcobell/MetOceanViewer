// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/daily_series.hpp"

#include <chrono>
#include <cstddef>
#include <expected>
#include <utility>
#include <vector>

namespace mov::core {

std::expected<DailySeries, ConstructionError> DailySeries::make(
    SeriesMeta meta, std::vector<std::chrono::local_days> days,
    std::vector<Sample> samples, usgs::StatisticId statistic) {
  if (days.size() != samples.size()) {
    return std::unexpected{ConstructionError{
        LengthMismatch{.times = days.size(), .samples = samples.size()}}};
  }
  if (const auto bad = detail::first_not_increasing(days)) {
    return std::unexpected{ConstructionError{TimeNotIncreasing{.index = *bad}}};
  }
  return DailySeries{std::move(meta), std::move(days), std::move(samples),
                     statistic};
}

}  // namespace mov::core
