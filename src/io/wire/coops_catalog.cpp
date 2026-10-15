// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/wire/coops_catalog.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/station.hpp"
#include "mov/io/read.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/coops.hpp"

namespace mov::io::wire::coops {

namespace {

using StationId = core::StationId<core::provider::Coops>;

core::coops::Predictions predictions_of(PredictionType type) noexcept {
  switch (type) {
    case PredictionType::harmonic:
      return core::coops::Predictions::any_interval;
    case PredictionType::subordinate:
      break;
  }
  return core::coops::Predictions::hilo_only;
}

}  // namespace

Read<std::vector<CatalogEntry>> merge_station_lists(const CatalogLists& lists) {
  std::map<StationId, CatalogEntry, std::less<>> entries;
  std::map<StationId, std::size_t, std::less<>> disagreements;
  for (const auto* list : {&lists.water_levels, &lists.met, &lists.air_gap}) {
    for (const StationRecord& r : *list) {
      const auto [at, inserted] = entries.try_emplace(
          r.station.id, CatalogEntry{.station = r.station, .caps = {}});
      core::Capabilities<core::provider::Coops>& caps = at->second.caps;
      caps.observed = caps.observed | r.observed;
      caps.met = caps.met | r.met;
      caps.air_gap = caps.air_gap or r.air_gap;
      caps.datums_offered = caps.datums_offered | r.datums;
      if (not inserted and at->second.station != r.station) {
        ++disagreements[r.station.id];
      }
    }
  }
  std::map<StationId, PredictionType, std::less<>> types;
  for (const PredictionStation& p : lists.tide_predictions) {
    const auto [at, inserted] = types.try_emplace(p.id, p.type);
    if (not inserted and at->second != p.type and entries.contains(p.id)) {
      ++disagreements[p.id];
    }
  }
  for (const auto& [id, type] : types) {
    if (const auto e = entries.find(id); e != entries.end()) {
      e->second.caps.predictions = predictions_of(type);
    }
  }

  Read<std::vector<CatalogEntry>> out;
  out.value.reserve(entries.size());
  for (auto& [id, entry] : entries) {
    out.value.push_back(std::move(entry));
  }
  for (const auto& [id, count] : disagreements) {
    out.warnings.push_back({.code = WarningCode::station_lists_disagree,
                            .subject = std::string{id.value()},
                            .count = count});
  }
  return out;
}

CatalogEntry with_datums(CatalogEntry entry, const StationDatums& datums) {
  entry.caps.datums_offered = entry.caps.datums_offered | datums.offered;
  entry.caps.datums = datums.table;
  return entry;
}

}  // namespace mov::io::wire::coops
