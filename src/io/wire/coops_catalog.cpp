// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/wire/coops_catalog.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/io/detail/station_catalog_text.hpp"
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

// The days both ranges cover between them: the earliest known first day and
// the latest last one (ongoing when either is).
core::ValidRange joined(const core::ValidRange& a, const core::ValidRange& b) {
  std::optional<std::chrono::sys_days> first = a.first();
  if (const auto other = b.first()) {
    first = std::min(first.value_or(*other), *other);
  }
  std::optional<std::chrono::sys_days> last;
  const auto a_last = a.last();
  const auto b_last = b.last();
  if (a_last and b_last) {
    last = std::max(*a_last, *b_last);
  }
  // first <= one of the lasts <= their maximum: never inverted.
  return core::ValidRange::make({.first = first, .last = last}).value_or(a);
}

// Whether two lists describe one station differently: its name, or its
// position as a catalog holds it (rounded, so a list's extra digits do not
// count).
bool differ(const core::GaugeStation<core::provider::Coops>& a,
            const core::GaugeStation<core::provider::Coops>& b) {
  return a.name != b.name or detail::catalog::canonical(a.location) !=
                                 detail::catalog::canonical(b.location);
}

// A station that offers no product: no active water-level, met or air-gap
// sensor and no tide predictions.
bool offers_nothing(const core::Capabilities<core::provider::Coops>& caps) {
  return caps.observed.empty() and caps.met.empty() and not caps.air_gap and
         caps.predictions == core::coops::Predictions::none;
}

}  // namespace

Read<std::vector<CatalogEntry>> merge_station_lists(const CatalogLists& lists) {
  std::map<StationId, CatalogEntry, std::less<>> entries;
  std::set<StationId, std::less<>> disagree;
  for (const auto* list : {&lists.water_levels, &lists.met, &lists.air_gap}) {
    for (const StationRecord& r : *list) {
      const auto [at, inserted] = entries.try_emplace(
          r.station.id, CatalogEntry{.station = r.station, .caps = {}});
      core::GaugeStation<core::provider::Coops>& station = at->second.station;
      if (not inserted) {
        if (differ(station, r.station)) {
          disagree.insert(r.station.id);
        }
        station.validity = joined(station.validity, r.station.validity);
      }
      core::Capabilities<core::provider::Coops>& caps = at->second.caps;
      caps.observed = caps.observed | r.observed;
      caps.met = caps.met | r.met;
      caps.air_gap = caps.air_gap or r.air_gap;
      caps.datums_offered = caps.datums_offered | r.datums;
    }
  }
  std::map<StationId, PredictionType, std::less<>> types;
  for (const PredictionStation& p : lists.tide_predictions) {
    const auto [at, inserted] = types.try_emplace(p.id, p.type);
    if (not inserted and at->second != p.type and entries.contains(p.id)) {
      disagree.insert(p.id);
    }
  }
  for (const auto& [id, type] : types) {
    if (const auto e = entries.find(id); e != entries.end()) {
      e->second.caps.predictions = predictions_of(type);
    }
  }

  Read<std::vector<CatalogEntry>> out;
  out.value.reserve(entries.size());
  std::size_t skipped = 0;
  for (auto& [id, entry] : entries) {
    if (offers_nothing(entry.caps)) {
      ++skipped;
      disagree.erase(id);
      continue;
    }
    out.value.push_back(std::move(entry));
  }
  if (not disagree.empty()) {
    out.warnings.push_back({.code = WarningCode::station_lists_disagree,
                            .subject = std::string{disagree.begin()->value()},
                            .count = disagree.size()});
  }
  append_if_counted(out.warnings, {.code = WarningCode::station_skipped,
                                   .subject = "no_active_sensor",
                                   .count = skipped});
  return out;
}

CatalogEntry with_datums(CatalogEntry entry, const StationDatums& datums) {
  entry.caps.datums_offered = entry.caps.datums_offered | datums.offered;
  entry.caps.datums = datums.table;
  return entry;
}

}  // namespace mov::io::wire::coops
