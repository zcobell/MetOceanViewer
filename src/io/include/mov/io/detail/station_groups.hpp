// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// How a netCDF model reader cuts a station selection into reads (design 5.4,
// the chunk-aware reads of review finding S7). Private to mov::io (public
// only because the tests include it).

#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <optional>
#include <span>
#include <vector>

namespace mov::io::detail {

/// One selected station: its index in the file and its position in the
/// selection (the order the caller asked for, which the table keeps).
struct SelectedStation {
  std::size_t station;
  std::size_t position;
  friend constexpr bool operator==(const SelectedStation&,
                                   const SelectedStation&) = default;
};

/// Selected stations that one read covers: the stations first..last of the
/// file, `members` of them selected (in file order).
struct StationGroup {
  std::size_t first;
  std::size_t last;  // inclusive
  std::vector<SelectedStation> members;

  [[nodiscard]] std::size_t width() const noexcept { return last - first + 1; }
  friend bool operator==(const StationGroup&, const StationGroup&) = default;
};

/// Which selected stations share a read. A read of a group covers every
/// station between its first and last, selected or not, in every time step,
/// and netCDF-C reads (and decompresses) whole chunks, so what a group costs
/// depends on how the file is chunked along the stations:
///  - chunks as wide as the file (netCDF-C's default for a variable with an
///    unlimited time dimension, and what ADCIRC writes): every station is in
///    the one chunk column, and one read of the span beats a read per station
///    by the number of stations (84 ms against 82 s for 1000 of 1000);
///  - chunks of one station: a read per station reads only that station, and
///    one read of the span reads all stations in between (5 ms against
///    700 ms for ten stations a hundred apart).
/// docs/wp-notes/WP9.md has the measurement.
///
/// `stride` is the largest difference of two neighbouring selected station
/// indices that is bridged by one read: 0 never groups (a read per station,
/// the strided column reads of v4), 1 groups only stations that are next to
/// each other, and `stride - 1` unselected stations between two selected ones
/// is the most a group skips over. `chunk`, when engaged, is the chunk size
/// along the stations: stations in different chunk columns are never grouped,
/// because the group would read the columns in between as well, which costs
/// what separate reads cost and adds the discarded stations.
struct GroupingPolicy {
  std::size_t stride;
  std::optional<std::size_t> chunk{};
  friend constexpr bool operator==(const GroupingPolicy&,
                                   const GroupingPolicy&) = default;
};

/// The grouping a reader uses for a variable whose chunk sizes are
/// `chunk_shape` (nullopt: not chunked, which reads like one chunk column),
/// stations on dimension `station_axis`: as many stations as one chunk column
/// holds share a read.
[[nodiscard]] inline GroupingPolicy grouping_for(
    const std::optional<std::vector<std::size_t>>& chunk_shape,
    std::size_t station_axis) {
  constexpr std::size_t unbounded = std::numeric_limits<std::size_t>::max();
  if (chunk_shape and station_axis < chunk_shape->size() and
      (*chunk_shape)[station_axis] > 0) {
    return {.stride = unbounded, .chunk = (*chunk_shape)[station_axis]};
  }
  return {.stride = unbounded};
}

/// Cuts `selected` (distinct station indices in the caller's order) into
/// groups: sorted by station, a new group starts where the next station is
/// more than `policy.stride` after the previous one or, with a chunk size,
/// in another chunk column. Every selected station is in exactly one group,
/// with its position in `selected`. An empty selection has no groups.
[[nodiscard]] inline std::vector<StationGroup> station_groups(
    std::span<const std::size_t> selected, GroupingPolicy policy) {
  std::vector<SelectedStation> sorted(selected.size());
  for (std::size_t p = 0; p < selected.size(); ++p) {
    sorted[p] = {.station = selected[p], .position = p};
  }
  std::ranges::sort(sorted, {}, &SelectedStation::station);
  const auto column_of = [&policy](std::size_t station) {
    return policy.chunk ? station / *policy.chunk : std::size_t{0};
  };
  std::vector<StationGroup> groups;
  for (const SelectedStation& s : sorted) {
    if (groups.empty() or s.station - groups.back().last > policy.stride or
        column_of(s.station) != column_of(groups.back().last)) {
      groups.push_back({.first = s.station, .last = s.station, .members = {}});
    }
    groups.back().last = s.station;
    groups.back().members.push_back(s);
  }
  return groups;
}

}  // namespace mov::io::detail
