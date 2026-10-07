// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The measurement behind detail::default_grouping (design review finding S7,
// docs/wp-notes/WP9.md): reading some stations of a model output one column
// at a time (v4's strided reads) against reading time blocks of every station
// the selection spans, and against the policy the readers use (which groups
// the selected stations by the chunk columns of the file). Hidden (`[.measure]`); run it from a release build:
//
//   mov_io_model_netcdf_tests "[.measure]"
//
// It writes a 1000-station x 10000-step file in each chunk layout (80 MB of
// doubles) into a scratch directory, reads the file once to warm the page
// cache, and prints the best of three reads for each selection and policy.

#include <catch2/catch_test_macros.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "adcirc_test_support.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/station_table.hpp"
#include "mov/io/adcirc_netcdf.hpp"
#include "mov/io/detail/station_groups.hpp"
#include "model_fixtures.hpp"
#include "model_nc_support.hpp"

namespace {

using Clock = std::chrono::steady_clock;

struct Layout {
  const char* name;
  std::vector<std::size_t> chunks;  // (time, station); empty: netCDF-C's default
};

struct Selection {
  const char* name;
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

double best_ms(const std::filesystem::path& path,
               const mov::io::AdcircNcRequest& request,
               std::optional<mov::io::detail::GroupingPolicy> policy) {
  double best = 1e300;
  for (int round = 0; round < 3; ++round) {
    const auto start = Clock::now();
    const auto read = mov::io::detail::read_adcirc_netcdf(path, request, {}, policy);
    const auto stop = Clock::now();
    INFO((read ? std::string{} : mov::test::what(read.error())));
    REQUIRE(read.has_value());
    best = std::min(
        best, std::chrono::duration<double, std::milli>(stop - start).count());
  }
  return best;
}

}  // namespace

TEST_CASE("per-column reads against time-block reads", "[.measure]") {
  constexpr std::size_t stations = 1000;
  constexpr std::size_t steps = 10000;
  const std::vector<Layout> layouts{
      {.name = "default (netCDF-C: 1 step x 1000 stations)", .chunks = {}},
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
      "\n{} stations x {} steps, doubles; best of 3, ms (read of the selected "
      "stations into a StationTable)\n",
      stations, steps);
  for (const Layout& layout : layouts) {
    mov::test::ncgen::AdcircNc spec;
    spec.variables = {"zeta"};
    spec.stations = stations;
    spec.steps = steps;
    spec.chunks = layout.chunks;
    spec.x.resize(stations, -90.0);
    spec.y.resize(stations, 29.0);
    spec.fill = std::nullopt;
    spec.value = [](std::size_t t, std::size_t s, std::size_t) {
      return static_cast<double>((t * 31) + s) * 0.001;
    };
    const auto path = dir / "measure.nc";
    make_adcirc_nc(path, spec);
    std::cout << std::format("\nchunks: {}\n{:<26} {:>12} {:>12} {:>12}\n",
                             layout.name, "selection", "per column",
                             "one block", "chunk-aware");
    for (const Selection& selection : selections) {
      const mov::io::AdcircNcRequest request{
          .kind = mov::io::AdcircKind::elevation,
          .cold_start = mov::test::cold_start(),
          .crs = mov::core::Epsg::wgs84(),
          .stations =
              mov::core::StationSelection::make(selection.stations, stations)
                  .value()};
      const double columns = best_ms(path, request,
                                   mov::io::detail::GroupingPolicy{.stride = 0});
      const double block = best_ms(
          path, request, mov::io::detail::GroupingPolicy{.stride = stations});
      const double aware = best_ms(path, request, std::nullopt);
      std::cout << std::format("{:<26} {:>12.1f} {:>12.1f} {:>12.1f}\n",
                               selection.name, columns, block, aware);
    }
    std::filesystem::remove(path);
  }
}
