// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// A continuous request's statistic is 00011 by construction: its selector
// cannot name another.

#include <chrono>

#include "mov/core/provider/usgs.hpp"

int main() {
  namespace core = mov::core;
  namespace usgs = mov::core::usgs;
  const auto range =
      core::TimeRange::make(core::Time{std::chrono::milliseconds{0}},
                            core::Time{std::chrono::milliseconds{1}});
  const auto station =
      core::StationId<core::provider::Usgs>::make("USGS-07374000");
  const auto code = usgs::ParameterCode::make("00060");
  const usgs::Continuous c{
      .station = *station,
      .series = usgs::DailyByParameter{.parameter = *code,
                                       .statistic = usgs::StatisticId::mean()},
      .range = *range};
  return c == c ? 0 : 1;
}
