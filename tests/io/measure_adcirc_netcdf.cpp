// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The measurements behind the grouping of the selected stations into reads
// (detail::grouping_for; design review findings S7 and 6 of the second WP9
// review; docs/wp-notes/WP9.md). Hidden (`[.measure]`); run them from a release
// build, one at a time:
//
//   mov_io_model_netcdf_tests "[measure][chunks]"
//   mov_io_model_netcdf_tests "[measure][contiguous]"
//   mov_io_model_netcdf_tests "[measure][deflate]"
//
// Each writes its files into a scratch directory. Every figure is the median
// of seven timed reads (min-max beside it) after one untimed read of the same
// request, so the page cache is warm and the first-read costs are out of the
// figure. The sizes are small enough to run in a minute or two; read the
// table as orders of magnitude, not as constants of the library.

#include <algorithm>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "adcirc_test_support.hpp"
#include "model_fixtures.hpp"
#include "model_nc_support.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/station_table.hpp"
#include "mov/io/adcirc_netcdf.hpp"
#include "mov/io/detail/station_groups.hpp"

namespace {

using Clock = std::chrono::steady_clock;
using mov::io::detail::GroupingPolicy;

constexpr int timed_runs = 7;
constexpr int slow_timed_runs = 5;  // the deflated files, where a read is slow

struct Timing {
  double median;
  double lowest;
  double highest;
  int runs;
};

// A read slower than this (the warm-up decides) is timed fewer times, so that
// a table of 90 s reads ends: the figure then says how many runs it is.
constexpr double slow_ms = 5000.0;
constexpr int runs_when_slow = 3;

// One untimed read, then `runs` timed ones.
Timing measure(const std::filesystem::path& path,
               const mov::io::AdcircNcRequest& request,
               const mov::io::ReadContext& ctx,
               std::optional<GroupingPolicy> policy, int runs = timed_runs) {
  const auto once = [&] {
    const auto start = Clock::now();
    const auto read =
        mov::io::detail::read_adcirc_netcdf(path, request, ctx, policy);
    const auto stop = Clock::now();
    INFO((read ? std::string{} : mov::test::what(read.error())));
    REQUIRE(read.has_value());
    return std::chrono::duration<double, std::milli>(stop - start).count();
  };
  if (once() > slow_ms) {  // the warm-up
    runs = std::min(runs, runs_when_slow);
  }
  std::vector<double> ms;
  ms.reserve(static_cast<std::size_t>(runs));
  for (int i = 0; i < runs; ++i) {
    ms.push_back(once());
  }
  std::ranges::sort(ms);
  return {.median = ms[ms.size() / 2],
          .lowest = ms.front(),
          .highest = ms.back(),
          .runs = runs};
}

std::string show(const Timing& t) {
  const std::string text =
      std::format("{:.1f} ({:.1f}-{:.1f})", t.median, t.lowest, t.highest);
  return t.runs == runs_when_slow ? std::format("{} n={}", text, t.runs) : text;
}

struct Selection {
  std::string name;
  std::vector<std::size_t> stations;
};

std::vector<std::size_t> every(std::size_t step, std::size_t count,
                               std::size_t first = 0) {
  std::vector<std::size_t> out;
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    out.push_back(first + (i * step));
  }
  return out;
}

mov::test::ncgen::AdcircNc base_spec(std::size_t stations, std::size_t steps) {
  mov::test::ncgen::AdcircNc spec;
  spec.variables = {"zeta"};
  spec.stations = stations;
  spec.steps = steps;
  spec.x.resize(stations, -90.0);
  spec.y.resize(stations, 29.0);
  spec.fill = std::nullopt;
  spec.value = [](std::size_t t, std::size_t s, std::size_t) {
    return static_cast<double>((t * 31) + s) * 0.001;
  };
  return spec;
}

mov::io::AdcircNcRequest request_for(const Selection& selection,
                                     std::size_t stations) {
  return {.kind = mov::io::AdcircKind::elevation,
          .cold_start = mov::test::cold_start(),
          .crs = mov::core::Epsg::wgs84(),
          .stations =
              mov::core::StationSelection::make(selection.stations, stations)
                  .value()};
}

// One table: each selection read per column, as one block, and as the
// readers do.
void table(const std::filesystem::path& path, std::size_t stations,
           const std::vector<Selection>& selections,
           const mov::io::ReadContext& ctx = {}) {
  std::cout << std::format("{:<28} {:>30} {:>30} {:>30}\n", "selection",
                           "per column", "one block", "the readers'");
  for (const Selection& selection : selections) {
    const auto request = request_for(selection, stations);
    const Timing columns =
        measure(path, request, ctx, GroupingPolicy{.stride = 0});
    const Timing block = measure(
        path, request, ctx,
        GroupingPolicy{.stride = std::numeric_limits<std::size_t>::max()});
    const Timing aware = measure(path, request, ctx, std::nullopt);
    std::cout << std::format("{:<28} {:>30} {:>30} {:>30}\n", selection.name,
                             show(columns), show(block), show(aware));
  }
}

}  // namespace

// ---- chunked storage: 1000 stations x 10000 steps, four layouts
// -------------------

TEST_CASE("per-column reads against time-block reads, by chunk layout",
          "[.measure][chunks]") {
  constexpr std::size_t stations = 1000;
  constexpr std::size_t steps = 10000;
  struct Layout {
    std::string name;
    std::vector<std::size_t> chunks;  // (time, station); empty: netCDF-C's
  };
  const std::vector<Layout> layouts{
      {.name = "netCDF-C default (1 step x 1000 stations)", .chunks = {}},
      {.name = "100 steps x 100 stations", .chunks = {100, 100}},
      {.name = "1000 steps x 10 stations", .chunks = {1000, 10}},
      {.name = "10000 steps x 1 station (columns)", .chunks = {10000, 1}},
  };
  const std::vector<Selection> selections{
      {.name = "1 station", .stations = {500}},
      {.name = "10 neighbours", .stations = every(1, 10, 500)},
      {.name = "10 spread (every 100th)", .stations = every(100, 10)},
      {.name = "100 neighbours", .stations = every(1, 100, 450)},
      {.name = "100 spread (every 10th)", .stations = every(10, 100)},
      {.name = "all 1000", .stations = every(1, 1000)},
  };
  const mov::test::ScratchDir dir;
  std::cout << std::format(
      "\n{} stations x {} steps of doubles, ms: median (min-max) of {} reads\n",
      stations, steps, timed_runs);
  for (const Layout& layout : layouts) {
    auto spec = base_spec(stations, steps);
    spec.chunks = layout.chunks;
    make_adcirc_nc(dir / "m.nc", spec);
    std::cout << std::format("\nchunks: {}\n", layout.name);
    table(dir / "m.nc", stations, selections);
    std::filesystem::remove(dir / "m.nc");
  }
}

// ---- contiguous storage: 10000 stations
// -------------------------------------------

TEST_CASE("contiguous storage: two far-apart stations of 10000",
          "[.measure][contiguous]") {
  constexpr std::size_t stations = 10000;
  constexpr std::size_t steps = 2000;
  struct Storage {
    std::string name;
    mov::test::ncgen::FileFormat format;
    bool fixed_time;
  };
  const std::vector<Storage> storages{
      {.name = "netCDF-4, fixed dimensions, no chunks (contiguous)",
       .format = mov::test::ncgen::FileFormat::netcdf4,
       .fixed_time = true},
      {.name = "classic CDF-1, record variable",
       .format = mov::test::ncgen::FileFormat::classic,
       .fixed_time = false},
      {.name = "64-bit offset CDF-2, fixed dimensions",
       .format = mov::test::ncgen::FileFormat::offset64,
       .fixed_time = true},
  };
  const std::vector<Selection> selections{
      {.name = "2 far apart (0, 9999)", .stations = {0, 9999}},
      {.name = "2 apart by 1000", .stations = {4000, 5000}},
      {.name = "2 apart by 100", .stations = {4000, 4100}},
      {.name = "2 apart by 10", .stations = {4000, 4010}},
      {.name = "2 apart by 1 (neighbours)", .stations = {4000, 4001}},
      {.name = "10 neighbours", .stations = every(1, 10, 4000)},
      {.name = "10 spread (every 1000th)", .stations = every(1000, 10)},
      {.name = "100 neighbours", .stations = every(1, 100, 4000)},
  };
  const mov::test::ScratchDir dir;
  std::cout << std::format(
      "\n{} stations x {} steps of doubles, ms: median (min-max) of {} reads\n",
      stations, steps, timed_runs);
  for (const Storage& storage : storages) {
    auto spec = base_spec(stations, steps);
    spec.format = storage.format;
    spec.fixed_time = storage.fixed_time;
    make_adcirc_nc(dir / "m.nc", spec);
    std::cout << std::format("\nstorage: {}\n", storage.name);
    table(dir / "m.nc", stations, selections);
    std::filesystem::remove(dir / "m.nc");
  }
}

// ---- deflated storage with long time chunks
// -----------------------------------------

TEST_CASE("deflated chunks longer than a block", "[.measure][deflate]") {
  constexpr std::size_t stations = 1000;
  constexpr std::size_t steps = 10000;
  struct Layout {
    std::string name;
    std::vector<std::size_t> chunks;
  };
  // A block is slab_elements (2^20) elements: 1048 steps of 1000 stations. A
  // chunk of 10000 x 100 is 1M elements, one of 10000 x 1000 is ten blocks.
  const std::vector<Layout> layouts{
      {.name = "deflated 10000 steps x 100 stations (1.0 M elements)",
       .chunks = {10000, 100}},
      {.name = "deflated 2000 steps x 1000 stations (2 M elements)",
       .chunks = {2000, 1000}},
      {.name = "deflated 10000 steps x 1000 stations (10 M elements)",
       .chunks = {10000, 1000}},
  };
  const std::vector<Selection> selections{
      {.name = "1 station", .stations = {500}},
      {.name = "all 1000", .stations = every(1, 1000)},
  };
  const mov::test::ScratchDir dir;
  std::cout << std::format(
      "\n{} stations x {} steps of doubles, deflate level 1, the readers' "
      "grouping, ms: median (min-max) of {} reads\n",
      stations, steps, slow_timed_runs);
  for (const Layout& layout : layouts) {
    auto spec = base_spec(stations, steps);
    spec.chunks = layout.chunks;
    spec.deflate = 1;
    make_adcirc_nc(dir / "m.nc", spec);
    std::cout << std::format("\nchunks: {}\n", layout.name);
    for (const std::size_t block :
         {std::size_t{1} << 20U, std::size_t{1} << 23U,
          std::size_t{1} << 25U}) {
      mov::io::ReadContext ctx;
      ctx.limits.slab_elements = block;
      std::cout << std::format("slab_elements = 2^{}\n",
                               std::countr_zero(block));
      for (const Selection& selection : selections) {
        const Timing t = measure(dir / "m.nc", request_for(selection, stations),
                                 ctx, std::nullopt, slow_timed_runs);
        std::cout << std::format("  {:<12} {}\n", selection.name, show(t));
      }
    }
    std::filesystem::remove(dir / "m.nc");
  }
}
