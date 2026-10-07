// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// NcName, NcNameRef and the slab block iteration of the netCDF wrapper.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <numeric>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "internal.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/name.hpp"
#include "mov/io/netcdf/types.hpp"
#include "mov/io/read_limits.hpp"

namespace {

using mov::io::Cancelled;
using mov::io::Error;
using mov::io::StopToken;
using mov::io::nc::DimRange;
using mov::io::nc::NcName;
using mov::io::nc::NcNameError;
using mov::io::nc::NcNameRef;
using mov::io::nc::Slab;
using mov::io::nc::detail::Block;
using mov::io::nc::detail::for_each_block;

// Every block of `slab` in order.
struct Seen {
  std::vector<std::vector<std::size_t>> starts;
  std::vector<std::vector<std::size_t>> counts;
  std::vector<std::size_t> offsets;
  std::vector<std::size_t> sizes;
};

Seen blocks_of(const Slab& slab, std::size_t max_block) {
  Seen seen;
  const auto done =
      for_each_block(slab, max_block, StopToken{},
                     [&](const Block& b) -> std::expected<void, Error> {
                       seen.starts.emplace_back(b.start.begin(), b.start.end());
                       seen.counts.emplace_back(b.count.begin(), b.count.end());
                       seen.offsets.push_back(b.offset);
                       seen.sizes.push_back(b.size);
                       return {};
                     });
  REQUIRE(done.has_value());
  return seen;
}

// The row-major offset of `pos` inside `slab`.
std::size_t offset_of(const Slab& slab, const std::vector<std::size_t>& pos) {
  std::size_t offset = 0;
  for (std::size_t i = 0; i < slab.size(); ++i) {
    offset = offset * slab[i].count + (pos[i] - slab[i].start);
  }
  return offset;
}

}  // namespace

TEST_CASE("NcName::make validates", "[io][netcdf]") {
  const auto time = NcName::make("time");
  REQUIRE(time.has_value());
  CHECK(time->view() == "time");
  CHECK(std::string_view{time->c_str()} == "time");
  CHECK(NcName::make("") == std::unexpected{NcNameError::empty});
  CHECK(NcName::make(std::string(256, 'x')).has_value());
  CHECK(NcName::make(std::string(257, 'x')) ==
        std::unexpected{NcNameError::too_long});
  CHECK(NcName::make(std::string_view{"ti\0me", 5}) ==
        std::unexpected{NcNameError::embedded_nul});
  // UTF-8 is bytes here: 128 two-byte characters are 256 bytes.
  std::string accents;
  for (int i = 0; i < 128; ++i) {
    accents += "\xc3\xa9";
  }
  CHECK(NcName::make(accents).has_value());
  CHECK(NcName::make(accents + "x") == std::unexpected{NcNameError::too_long});
}

TEST_CASE("NcNameRef views an NcName and copies back", "[io][netcdf]") {
  const NcName owned = NcName::make("station_name").value();
  const NcNameRef ref{owned};
  CHECK(ref.view() == "station_name");
  CHECK(ref.c_str() == owned.c_str());
  CHECK(NcName{ref} == owned);
  const NcName zeta{NcNameRef{"zeta"}};
  CHECK(zeta.view() == "zeta");
}

TEST_CASE("for_each_block covers a slab once, in row-major order",
          "[io][netcdf]") {
  const std::vector<Slab> slabs{
      {},                           // scalar
      {{.start = 0, .count = 10}},  // 1-D
      {{.start = 2, .count = 5}},   // 1-D, offset
      {{.start = 0, .count = 3}, {.start = 0, .count = 4}},
      {{.start = 1, .count = 2}, {.start = 1, .count = 3}},
      {{.start = 0, .count = 2},
       {.start = 1, .count = 2},
       {.start = 0, .count = 4}},
      {{.start = 0, .count = 1},
       {.start = 0, .count = 1},
       {.start = 3, .count = 7}},
  };
  for (const Slab& slab : slabs) {
    std::size_t total = 1;
    for (const DimRange& r : slab) {
      total *= r.count;
    }
    for (const std::size_t max_block :
         {0UZ, 1UZ, 2UZ, 3UZ, 5UZ, 7UZ, 12UZ, 100UZ}) {
      CAPTURE(slab.size(), max_block);
      const Seen seen = blocks_of(slab, max_block);
      // Contiguous, ascending, complete.
      std::size_t next = 0;
      for (std::size_t i = 0; i < seen.offsets.size(); ++i) {
        CHECK(seen.offsets[i] == next);
        CHECK(seen.offsets[i] == offset_of(slab, seen.starts[i]));
        CHECK(seen.sizes[i] <= std::max(max_block, 1UZ));
        CHECK(seen.sizes[i] == std::accumulate(seen.counts[i].begin(),
                                               seen.counts[i].end(), 1UZ,
                                               std::multiplies<>{}));
        next += seen.sizes[i];
      }
      CHECK(next == total);
    }
  }
}

TEST_CASE("for_each_block: empty slab, stop and visitor errors",
          "[io][netcdf]") {
  const Slab empty{{.start = 0, .count = 4}, {.start = 0, .count = 0}};
  CHECK(blocks_of(empty, 2).offsets.empty());

  const Slab slab{{.start = 0, .count = 10}};
  int visits = 0;
  const auto visit = [&](const Block&) -> std::expected<void, Error> {
    ++visits;
    return {};
  };
  // A stop requested before the read: nothing is read.
  auto stopped = for_each_block(slab, 2, StopToken{[] { return true; }}, visit);
  REQUIRE(not stopped.has_value());
  CHECK(std::holds_alternative<Cancelled>(stopped.error()));
  CHECK(visits == 0);

  // A stop between blocks: the blocks before it were read.
  int polls = 0;
  stopped =
      for_each_block(slab, 2, StopToken{[&] { return ++polls > 3; }}, visit);
  REQUIRE(not stopped.has_value());
  CHECK(std::holds_alternative<Cancelled>(stopped.error()));
  CHECK(visits == 3);

  // A visitor error ends the walk.
  visits = 0;
  const auto failing = for_each_block(
      slab, 2, StopToken{}, [&](const Block&) -> std::expected<void, Error> {
        ++visits;
        return std::unexpected{Error{Cancelled{}}};
      });
  CHECK(not failing.has_value());
  CHECK(visits == 1);
}
