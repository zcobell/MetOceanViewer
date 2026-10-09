// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <chrono>
#include <cstddef>
#include <expected>
#include <span>
#include <vector>

#include "mov/core/meta.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/timeseries.hpp"

namespace mov::core {

/// The values of a station's local calendar days, one per day, each a
/// statistic (mean, minimum, maximum, ...) of that day (decision 34). A day
/// is not an instant, so this is not a TimeSeries, and no time-zone
/// conversion ever applies to it. Days are strictly increasing. A plain
/// value; there is no default one (a statistic has none).
class DailySeries {
 public:
  /// LengthMismatch if the sizes differ, then the first TimeNotIncreasing
  /// (the index of the first day not after its predecessor).
  [[nodiscard]] static std::expected<DailySeries, ConstructionError> make(
      SeriesMeta meta, std::vector<std::chrono::sys_days> days,
      std::vector<Sample> samples, usgs::StatisticId statistic);

  [[nodiscard]] const SeriesMeta& meta() const& noexcept { return meta_; }
  const SeriesMeta& meta() const&& = delete;
  [[nodiscard]] std::span<const std::chrono::sys_days> days() const& noexcept {
    return days_;
  }
  std::span<const std::chrono::sys_days> days() const&& = delete;
  [[nodiscard]] std::span<const Sample> samples() const& noexcept {
    return samples_;
  }
  std::span<const Sample> samples() const&& = delete;
  [[nodiscard]] usgs::StatisticId statistic() const noexcept {
    return statistic_;
  }

  [[nodiscard]] std::size_t size() const noexcept { return days_.size(); }
  [[nodiscard]] bool empty() const noexcept { return days_.empty(); }

  friend bool operator==(const DailySeries&, const DailySeries&) = default;

 private:
  DailySeries(SeriesMeta meta, std::vector<std::chrono::sys_days> days,
              std::vector<Sample> samples, usgs::StatisticId statistic)
      : meta_{std::move(meta)},
        days_{std::move(days)},
        samples_{std::move(samples)},
        statistic_{statistic} {}

  SeriesMeta meta_;
  std::vector<std::chrono::sys_days> days_;
  std::vector<Sample> samples_;
  usgs::StatisticId statistic_;
};

}  // namespace mov::core
