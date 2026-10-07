// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// NcName, NcNameRef, AttTarget and the hyperslab helpers of the netCDF
// wrapper.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <expected>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

#include "mov/io/netcdf/name.hpp"
#include "mov/io/netcdf/types.hpp"

namespace {

using mov::io::nc::AttTarget;
using mov::io::nc::DimInfo;
using mov::io::nc::NcName;
using mov::io::nc::NcNameError;
using mov::io::nc::NcNameRef;
using mov::io::nc::rows_per_block;
using mov::io::nc::Slab;
using mov::io::nc::Type;
using mov::io::nc::VarInfo;

}  // namespace

TEST_CASE("NcName::make validates", "[io][netcdf]") {
  const auto time = NcName::make("time");
  REQUIRE(time.has_value());
  CHECK(time->view() == "time");
  CHECK(std::string_view{time->c_str()} == "time");
  CHECK(*time == "time");
  CHECK(*time == std::string_view{"time"});
  CHECK(not(*time == "times"));
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
  CHECK(AttTarget{owned} == AttTarget{"station_name"});
  CHECK(not(AttTarget{owned} == AttTarget{mov::io::nc::global}));
}

TEST_CASE("whole and unzip", "[io][netcdf]") {
  const VarInfo var{
      .id = 0,
      .name = NcName::make("v").value(),
      .type = Type::double_,
      .dims = {
          DimInfo{.id = 0, .name = NcName::make("a").value(), .length = 3},
          DimInfo{.id = 1, .name = NcName::make("b").value(), .length = 5}}};
  const Slab all = mov::io::nc::whole(var);
  CHECK(all == Slab{{.start = 0, .count = 3}, {.start = 0, .count = 5}});
  const auto h = mov::io::nc::detail::unzip(
      {{.start = 1, .count = 2}, {.start = 4, .count = 1}});
  CHECK(h.start == std::vector<std::size_t>{1, 4});
  CHECK(h.count == std::vector<std::size_t>{2, 1});
}

TEST_CASE("rows_per_block", "[io][netcdf]") {
  const Slab grid{{.start = 0, .count = 100}, {.start = 0, .count = 10}};
  CHECK(rows_per_block(grid, 50) == 5);
  CHECK(rows_per_block(grid, 55) == 5);
  CHECK(rows_per_block(grid, 10) == 1);
  // One outer index is more than a block: still one row per block.
  CHECK(rows_per_block(grid, 3) == 1);
  CHECK(rows_per_block(grid, 0) == 1);
  CHECK(rows_per_block({{.start = 0, .count = 7}}, 3) == 3);
  CHECK(rows_per_block({}, 3) == 1);
  // An empty inner range, and inner products beyond size_t.
  CHECK(rows_per_block({{.start = 0, .count = 4}, {.start = 0, .count = 0}},
                       3) == 1);
  constexpr std::size_t big = std::size_t{1} << 40U;
  CHECK(rows_per_block({{.start = 0, .count = 2},
                        {.start = 0, .count = big},
                        {.start = 0, .count = big}},
                       std::numeric_limits<std::size_t>::max()) == 1);
}
