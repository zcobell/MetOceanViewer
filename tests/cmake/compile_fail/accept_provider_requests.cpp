// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The well-formed twins of reject_coops_met_datum.cpp,
// reject_usgs_daily_time_range.cpp and reject_usgs_continuous_statistic.cpp.

#include <chrono>

#include "mov/core/provider/coops.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/windows.hpp"

int main() {
  namespace core = mov::core;
  namespace coops = mov::core::coops;
  namespace usgs = mov::core::usgs;
  const coops::Product met = coops::Meteorological{
      .product = coops::Met::wind, .interval = coops::MetInterval::hourly};
  const coops::Product level = coops::WaterLevel{
      .kind = coops::ObservedLevel{.series = coops::Observed::six_minute},
      .datum = coops::Datum::mllw};

  const auto range =
      core::TimeRange::make(core::Time{std::chrono::milliseconds{0}},
                            core::Time{std::chrono::milliseconds{1}});
  const auto days =
      core::DayRange::make(std::chrono::sys_days{std::chrono::days{0}},
                           std::chrono::sys_days{std::chrono::days{1}});
  const auto station =
      core::StationId<core::provider::Usgs>::make("USGS-07374000");
  const auto code = usgs::ParameterCode::make("00060");
  const usgs::Daily d{
      .station = *station,
      .series = usgs::DailyByParameter{.parameter = *code,
                                       .statistic = usgs::StatisticId::mean()},
      .days = *days};
  const usgs::Continuous c{.station = *station,
                           .series = usgs::ByParameter{.parameter = *code},
                           .range = *range};
  return (met != level and d == d and c == c) ? 0 : 1;
}
