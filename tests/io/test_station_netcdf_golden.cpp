// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Golden CDL: the header of each canonical file (docs/station-netcdf.md
// section 9) equals the committed CDL in tests/fixtures/io/station_netcdf/.
// The header is printed the way `ncdump -h` prints it (nc_header_dump); the
// format-compliance job (tools/check_cf.py) compares the real `ncdump -h`
// with the same files.

#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <string>

#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"
#include "nc_header_dump.hpp"
#include "station_nc_canonical.hpp"

namespace {

namespace station_nc = mov::test::station_nc;
using mov::test::ScratchDir;

TEST_CASE("the canonical headers equal the committed CDL",
          "[io][station_nc][golden]") {
  const ScratchDir dir;
  for (const station_nc::Canonical& c : station_nc::all()) {
    CAPTURE(c.name);
    const auto path = station_nc::write(c, dir.path());
    const std::string golden = mov::test::read_bytes(mov::test::fixture(
        "io/station_netcdf/" + std::string{c.name} + ".cdl"));
    REQUIRE(not golden.empty());
    CHECK(mov::test::ncgen::without_app_version(mov::test::ncgen::ncdump_header(
              path, c.name)) == mov::test::ncgen::without_app_version(golden));
  }
}

TEST_CASE("the header dumper prints the probe as ncdump does",
          "[io][station_nc][golden]") {
  // Every atomic type, NaN, fill values, escapes: the committed CDL is what
  // the real ncdump -h prints for this file (tools/check_cf.py checks it).
  const ScratchDir dir;
  const auto path = dir / "ncdump_probe.nc";
  mov::test::ncgen::make_ncdump_probe(path);
  const std::string golden = mov::test::read_bytes(
      mov::test::fixture("io/station_netcdf/ncdump_probe.cdl"));
  REQUIRE(not golden.empty());
  CHECK(mov::test::ncgen::ncdump_header(path, "ncdump_probe") == golden);
}

TEST_CASE("without_app_version hides only the application version",
          "[io][station_nc][golden]") {
  CHECK(mov::test::ncgen::without_app_version(
            "2026-10-06T12:00:00Z: created by MetOceanViewer 5.12.3 "
            "(station-timeseries 1.0)") ==
        "2026-10-06T12:00:00Z: created by MetOceanViewer X.Y.Z "
        "(station-timeseries 1.0)");
}

}  // namespace
