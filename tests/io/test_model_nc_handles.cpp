// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The model readers open their file once and close it on every path (B5: v4
// returned early from nearly every error path without nc_close). Linked with
// the --wrap shims of mov_io_netcdf_tests, so the counts are the library's.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

#include "model_fixtures.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/io/adcirc_netcdf.hpp"
#include "mov/io/dflow.hpp"
#include "mov/io/netcdf/name.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/test/scratch_dir.hpp"
#include "nc_counts.hpp"

namespace {

namespace counts = mov::test::nc_counts;
using mov::core::Epsg;
using mov::core::StationSelection;
using mov::io::AdcircKind;
using mov::io::ReadContext;
using mov::io::StopToken;

mov::io::AdcircNcRequest adcirc_request(AdcircKind kind,
                                        std::optional<mov::core::Time> start,
                                        StationSelection stations) {
  return {.kind = kind,
          .cold_start = start,
          .crs = Epsg::wgs84(),
          .stations = std::move(stations)};
}

mov::io::DflowRequest dflow_request(const std::string& name,
                                    StationSelection stations) {
  return {.choice = mov::io::Flat{.source = *mov::io::nc::NcName::make(name),
                                  .long_name = name},
          .stations = std::move(stations),
          .crs = Epsg::wgs84()};
}

}  // namespace

TEST_CASE("the model readers close their file on every path (B5)",
          "[io][netcdf][regression][B5][linux]") {
  if constexpr (not counts::available) {
    SKIP("needs the --wrap shims (Linux)");
  }
  const mov::test::ScratchDir dir;
  mov::test::ncgen::AdcircNc adcirc;
  adcirc.time_units = "seconds since Met";  // a cold start is needed
  make_adcirc_nc(dir / "adcirc.nc", adcirc);
  mov::test::ncgen::DflowNc dflow;
  mov::test::ncgen::DflowVar level;
  level.name = "waterlevel";
  level.units = "m";
  dflow.vars = {level};
  make_dflow_nc(dir / "his.nc", dflow);
  const auto start = std::chrono::time_point_cast<std::chrono::milliseconds>(
      std::chrono::sys_days{std::chrono::year{2010} / std::chrono::January /
                            1});

  const auto before = counts::counts();
  constexpr std::int64_t rounds = 100;
  std::int64_t succeeded = 0;
  for (std::int64_t i = 0; i < rounds; ++i) {
    const ReadContext stopped{.limits = {},
                              .stop = StopToken{[] { return true; }}};
    const auto adcirc_path = dir / "adcirc.nc";
    const auto his_path = dir / "his.nc";
    // Each of these opens the file, and each ends differently.
    succeeded += static_cast<std::int64_t>(
        mov::io::inspect_adcirc_netcdf(adcirc_path, Epsg::wgs84(), {})
            .has_value());
    succeeded += static_cast<std::int64_t>(
        mov::io::read_adcirc_netcdf(adcirc_path,
                                    adcirc_request(AdcircKind::elevation, start,
                                                   StationSelection::all(3)),
                                    {})
            .has_value());
    succeeded += static_cast<std::int64_t>(
        mov::io::read_adcirc_netcdf(  // cold_start_required
            adcirc_path,
            adcirc_request(AdcircKind::elevation, std::nullopt,
                           StationSelection::all(3)),
            {})
            .has_value());
    succeeded += static_cast<std::int64_t>(
        mov::io::read_adcirc_netcdf(  // missing_variable
            adcirc_path,
            adcirc_request(AdcircKind::pressure, start,
                           StationSelection::all(3)),
            {})
            .has_value());
    succeeded += static_cast<std::int64_t>(
        mov::io::read_adcirc_netcdf(  // station_count_mismatch
            adcirc_path,
            adcirc_request(AdcircKind::elevation, start,
                           StationSelection::all(7)),
            {})
            .has_value());
    succeeded += static_cast<std::int64_t>(
        mov::io::read_adcirc_netcdf(  // Cancelled
            adcirc_path,
            adcirc_request(AdcircKind::elevation, start,
                           StationSelection::all(3)),
            stopped)
            .has_value());
    succeeded += static_cast<std::int64_t>(
        mov::io::inspect_dflow(adcirc_path, Epsg::wgs84(), {}).has_value());
    succeeded += static_cast<std::int64_t>(
        mov::io::inspect_dflow(his_path, Epsg::wgs84(), {}).has_value());
    succeeded += static_cast<std::int64_t>(
        mov::io::read_dflow(
            his_path, dflow_request("waterlevel", StationSelection::all(3)), {})
            .has_value());
    succeeded += static_cast<std::int64_t>(
        mov::io::read_dflow(  // missing_variable
            his_path, dflow_request("salinity", StationSelection::all(3)), {})
            .has_value());
    succeeded += static_cast<std::int64_t>(
        mov::io::read_dflow(  // Cancelled
            his_path, dflow_request("waterlevel", StationSelection::all(3)),
            stopped)
            .has_value());
  }
  const auto now = counts::counts();
  // Four of the eleven calls end in a value.
  CHECK(succeeded == 4 * rounds);
  // Eleven opens a round (the cancelled reads do get as far as the clock).
  CHECK(now.opened - before.opened == 11 * rounds);
  CHECK(now.closed - before.closed == 11 * rounds);
  CHECK(now.close_calls - before.close_calls == 11 * rounds);
  CHECK(now.abort_calls == before.abort_calls);
}
