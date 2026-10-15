// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <cstdint>
#include <expected>
#include <vector>

#include "mov/core/provider/coops.hpp"
#include "mov/core/windows.hpp"
#include "mov/fetch/error.hpp"
#include "mov/fetch/plan/coops.hpp"

namespace mov::fetch::coops {

std::expected<std::vector<Unit>, Error> initial(
    const core::coops::Request& request) {
  const auto cut = windows(request);
  if (not cut) {
    return std::unexpected{Error{cut.error()}};
  }
  std::vector<Unit> units;
  units.reserve(cut->size());
  for (const core::TimeRange window : *cut) {
    units.push_back(Unit{.window = window});
  }
  return units;
}

std::vector<DatumsUnit> initial(const DatumsRequest& request) {
  std::vector<DatumsUnit> units;
  units.reserve(request.stations.size());
  for (std::uint32_t index = 0; const auto& station : request.stations) {
    units.push_back(DatumsUnit{.index = index++, .station = station});
  }
  return units;
}

}  // namespace mov::fetch::coops
