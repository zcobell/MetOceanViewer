// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/daily_series.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <expected>
#include <utility>
#include <vector>

namespace mov::core {

std::expected<DailySeries, ConstructionError> DailySeries::make(
    SeriesMeta meta, std::vector<std::chrono::sys_days> days,
    std::vector<Sample> samples, usgs::StatisticId statistic) {
  if (days.size() != samples.size()) {
    return std::unexpected{ConstructionError{
        LengthMismatch{.times = days.size(), .samples = samples.size()}}};
  }
  const auto bad = std::ranges::adjacent_find(
      days, [](std::chrono::sys_days a, std::chrono::sys_days b) noexcept {
        return not(a < b);
      });
  if (bad != days.end()) {
    return std::unexpected{ConstructionError{TimeNotIncreasing{
        .index = static_cast<std::size_t>(bad - days.begin()) + 1}}};
  }
  return DailySeries{std::move(meta), std::move(days), std::move(samples),
                     statistic};
}

}  // namespace mov::core
