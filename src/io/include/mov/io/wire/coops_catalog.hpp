// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The CO-OPS station catalog from the metadata API's station lists and each
// station's datums.json (docs/providers-design.md sections 4.8 and 7): the
// pure steps of the catalog build, which the CLI's `stations build` and the
// app's refresh run after the fetch.

#pragma once

#include <vector>

#include "mov/core/station.hpp"
#include "mov/io/read.hpp"
#include "mov/io/station_catalog.hpp"
#include "mov/io/wire/coops.hpp"

namespace mov::io::wire::coops {

/// The parsed station lists a CO-OPS catalog is built from
/// (station_list_target): water level, met and air gap give the stations,
/// the tide-prediction list only says which of them have predictions.
struct CatalogLists {
  std::vector<StationRecord> water_levels{};
  std::vector<StationRecord> met{};
  std::vector<StationRecord> air_gap{};
  std::vector<PredictionStation> tide_predictions{};
  friend bool operator==(const CatalogLists&, const CatalogLists&) = default;
};

using CatalogEntry = io::CatalogEntry<core::provider::Coops>;

/// The stations of the lists, by id, without datums.json (with_datums adds
/// it). A station in several lists (or twice in one) is one entry:
/// - its capabilities are the union of its records (`observed`, `met`,
///   `air_gap`, and the datums a list tells: IGLD85 at a Great Lakes
///   station);
/// - its name and position are the first record's, in the order water
///   level, met, air gap; a later record whose name or position as a catalog
///   holds it (rounded, docs/station-catalog.md section 7) differs makes the
///   station one that disagrees;
/// - its validity is the join of its records' (the earliest first day, the
///   latest last one, ongoing when any record is);
/// - its predictions are its type in the tide-prediction list: harmonic
///   `any_interval`, subordinate `hilo_only`, absent `none`. A station listed
///   twice there with different types keeps the first and disagrees too.
///   Prediction stations in no other list are not catalog stations (most
///   are subordinate stations without a gauge).
/// A station that offers nothing (no active water-level, met or air-gap
/// sensor, and no predictions) is left out: decision 6, active stations
/// only. Warnings: one `station_lists_disagree` (subject the smallest id that
/// disagrees, count the stations), one `station_skipped` (subject
/// `no_active_sensor`, count the stations left out). The entries come back
/// sorted by id.
[[nodiscard]] Read<std::vector<CatalogEntry>> merge_station_lists(
    const CatalogLists& lists);

/// The entry with its datums.json: the offered datums are the union of the
/// entry's and the file's, and the table is the file's (nullopt when the
/// file had no MSL). A station whose datums.json failed keeps the entry as
/// it is: no table (docs/providers-design.md section 7).
[[nodiscard]] CatalogEntry with_datums(CatalogEntry entry,
                                       const StationDatums& datums);

}  // namespace mov::io::wire::coops
