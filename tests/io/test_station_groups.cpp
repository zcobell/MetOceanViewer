// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <limits>
#include <optional>
#include <vector>

#include "mov/io/detail/station_groups.hpp"

namespace {

using mov::io::detail::grouping_for;
using mov::io::detail::GroupingPolicy;
using mov::io::detail::SelectedStation;
using mov::io::detail::station_groups;
using mov::io::detail::StationGroup;

std::vector<StationGroup> groups_of(const std::vector<std::size_t>& selected,
                                    std::size_t stride,
                                    std::optional<std::size_t> chunk = {}) {
  return station_groups(selected,
                        GroupingPolicy{.stride = stride, .chunk = chunk});
}

}  // namespace

TEST_CASE("an empty selection has no groups", "[io][netcdf][groups]") {
  CHECK(groups_of({}, 0).empty());
  CHECK(groups_of({}, 100).empty());
}

TEST_CASE("stride 0 reads each station alone", "[io][netcdf][groups]") {
  const auto groups = groups_of({4, 5, 6}, 0);
  REQUIRE(groups.size() == 3);
  for (const StationGroup& g : groups) {
    CHECK(g.first == g.last);
    CHECK(g.members.size() == 1);
    CHECK(g.width() == 1);
  }
}

TEST_CASE("stride 1 groups stations that are next to each other",
          "[io][netcdf][groups]") {
  const auto groups = groups_of({10, 3, 4, 5, 8, 9}, 1);
  REQUIRE(groups.size() == 2);
  CHECK(groups[0].first == 3);
  CHECK(groups[0].last == 5);
  CHECK(groups[1].first == 8);
  CHECK(groups[1].last == 10);
  // Positions are the caller's order, not the file's.
  CHECK(groups[0].members ==
        std::vector<SelectedStation>{{.station = 3, .position = 1},
                                     {.station = 4, .position = 2},
                                     {.station = 5, .position = 3}});
  CHECK(groups[1].members ==
        std::vector<SelectedStation>{{.station = 8, .position = 4},
                                     {.station = 9, .position = 5},
                                     {.station = 10, .position = 0}});
}

TEST_CASE("a larger stride bridges the stations between",
          "[io][netcdf][groups]") {
  // 0 and 3 have two stations between them: bridged by stride 3, not by 2.
  CHECK(groups_of({0, 3}, 2).size() == 2);
  const auto bridged = groups_of({0, 3}, 3);
  REQUIRE(bridged.size() == 1);
  CHECK(bridged[0].width() == 4);
  CHECK(bridged[0].members.size() == 2);
}

TEST_CASE("every selected station is in exactly one group",
          "[io][netcdf][groups]") {
  const std::vector<std::size_t> selected{90, 2, 50, 51, 7, 3, 99, 0};
  for (const std::size_t stride : {0U, 1U, 2U, 5U, 40U, 1000U}) {
    std::vector<std::size_t> positions(selected.size(), 0);
    std::size_t members = 0;
    for (const StationGroup& g : groups_of(selected, stride)) {
      for (const SelectedStation& m : g.members) {
        CHECK(selected[m.position] == m.station);
        CHECK(m.station >= g.first);
        CHECK(m.station <= g.last);
        ++positions[m.position];
        ++members;
      }
    }
    CHECK(members == selected.size());
    for (const std::size_t count : positions) {
      CHECK(count == 1);
    }
  }
}

TEST_CASE("stations in different chunk columns are not grouped",
          "[io][netcdf][groups]") {
  // Chunks of 10 stations: 0-9, 10-19, ...
  const auto groups = groups_of({3, 25, 8, 11, 19, 12}, 1000, 10);
  REQUIRE(groups.size() == 3);
  CHECK(groups[0].first == 3);
  CHECK(groups[0].last == 8);
  CHECK(groups[0].members.size() == 2);
  CHECK(groups[1].first == 11);
  CHECK(groups[1].last == 19);
  CHECK(groups[1].members.size() == 3);
  CHECK(groups[2].first == 25);
  // Chunks as wide as the file: one group, whatever the stride allows.
  CHECK(groups_of({3, 25, 8, 11, 19, 12}, 1000, 1000).size() == 1);
  // The stride still applies inside a chunk column.
  CHECK(groups_of({0, 9}, 1, 10).size() == 2);
}

TEST_CASE("the grouping for a variable follows its chunk shape",
          "[io][netcdf][groups]") {
  constexpr std::size_t unbounded = std::numeric_limits<std::size_t>::max();
  // (time, station) chunked 100 x 25: chunk columns of 25 stations.
  CHECK(grouping_for(std::vector<std::size_t>{100, 25}, 1) ==
        GroupingPolicy{.stride = unbounded, .chunk = 25});
  // Not chunked: one chunk column.
  CHECK(grouping_for(std::nullopt, 1) == GroupingPolicy{.stride = unbounded});
  // A shape without the station axis, or a zero chunk, says nothing.
  CHECK(grouping_for(std::vector<std::size_t>{100}, 1) ==
        GroupingPolicy{.stride = unbounded});
  CHECK(grouping_for(std::vector<std::size_t>{100, 0}, 1) ==
        GroupingPolicy{.stride = unbounded});
}
