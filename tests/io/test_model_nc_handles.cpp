// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The model readers open their file once and close it on every path (v4
// returned early from nearly every error path without nc_close). Linked with
// the --wrap shims of mov_io_netcdf_tests, so the counts are the library's.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "model_fixtures.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/io/adcirc_netcdf.hpp"
#include "mov/io/dflow.hpp"
#include "mov/io/error.hpp"
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
  return {
      .choice = mov::io::FlatChoice{.source = *mov::io::nc::NcName::make(name)},
      .stations = std::move(stations),
      .crs = Epsg::wgs84()};
}

// How a call ended, as text a test can compare: the value, the format error
// (by code) or the cancellation.
template <class R>
std::string outcome(const R& result) {
  if (result) {
    return "ok";
  }
  if (const auto* format = std::get_if<mov::io::FormatError>(&result.error())) {
    return "format:" + std::to_string(static_cast<int>(format->code));
  }
  if (std::holds_alternative<mov::io::Cancelled>(result.error())) {
    return "cancelled";
  }
  return "other:" + std::to_string(result.error().index());
}

std::string format_outcome(mov::io::FormatErrc code) {
  return "format:" + std::to_string(static_cast<int>(code));
}

}  // namespace

TEST_CASE("the model readers close their file on every path (B5)",
          "[io][netcdf][regression][B5][linux]") {
  if constexpr (not counts::available) {
    SKIP("needs the --wrap shims (Linux)");
  }
  using mov::io::FormatErrc;
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
  const auto adcirc_path = dir / "adcirc.nc";
  const auto his_path = dir / "his.nc";

  // Eleven calls that open a file; each ends differently.
  const auto one_round = [&] {
    const ReadContext stopped{.limits = {},
                              .stop = StopToken{[] { return true; }}};
    const auto read_adcirc =
        [&](AdcircKind kind, std::optional<mov::core::Time> cold_start,
            std::size_t station_count, const ReadContext& ctx) {
          return outcome(mov::io::read_adcirc_netcdf(
              adcirc_path,
              adcirc_request(kind, cold_start,
                             StationSelection::all(station_count)),
              ctx));
        };
    const auto read_his = [&](const std::string& name, const ReadContext& ctx) {
      return outcome(mov::io::read_dflow(
          his_path, dflow_request(name, StationSelection::all(3)), ctx));
    };
    return std::vector<std::string>{
        outcome(mov::io::inspect_adcirc_netcdf(adcirc_path, Epsg::wgs84(), {})),
        read_adcirc(AdcircKind::elevation, start, 3, {}),
        read_adcirc(AdcircKind::elevation, std::nullopt, 3, {}),
        read_adcirc(AdcircKind::pressure, start, 3, {}),
        read_adcirc(AdcircKind::elevation, start, 7, {}),
        read_adcirc(AdcircKind::elevation, start, 3, stopped),
        outcome(mov::io::inspect_dflow(adcirc_path, Epsg::wgs84(), {})),
        outcome(mov::io::inspect_dflow(his_path, Epsg::wgs84(), {})),
        read_his("waterlevel", {}),
        read_his("salinity", {}),
        read_his("waterlevel", stopped)};
  };
  const std::vector<std::string> expected{
      "ok",
      "ok",
      format_outcome(FormatErrc::cold_start_required),
      format_outcome(FormatErrc::missing_variable),
      format_outcome(FormatErrc::station_count_mismatch),
      "cancelled",
      format_outcome(FormatErrc::missing_dimension),
      "ok",
      "ok",
      format_outcome(FormatErrc::missing_variable),
      "cancelled"};

  // The first round says what each call ended in (so the counts below are
  // those of the paths named, not of paths that happened to fail early); the
  // others only have to end the same way.
  const auto before = counts::counts();
  constexpr std::int64_t rounds = 100;
  std::int64_t differing = 0;
  for (std::int64_t i = 0; i < rounds; ++i) {
    const auto got = one_round();
    if (i == 0) {
      CHECK(got == expected);
    }
    differing += got == expected ? 0 : 1;
  }
  const auto now = counts::counts();
  CHECK(differing == 0);
  // Eleven opens a round (the cancelled reads do get as far as the clock).
  CHECK(now.opened - before.opened == 11 * rounds);
  CHECK(now.closed - before.closed == 11 * rounds);
  CHECK(now.close_calls - before.close_calls == 11 * rounds);
  CHECK(now.abort_calls == before.abort_calls);
}
