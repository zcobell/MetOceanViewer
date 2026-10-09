// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The station netCDF readers and detect_file_type open their file once and
// close it on every path (v4 returned early from nearly every error path
// without nc_close), and a file that cannot be opened closes nothing (v4
// called nc_close on an uninitialised id). Linked with the --wrap shims of
// mov_io_netcdf_tests, so the counts are the library's.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

#include "foreign_support.hpp"
#include "legacy_fixtures.hpp"
#include "mov/io/error.hpp"
#include "mov/io/file_type.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"
#include "nc_build.hpp"
#include "nc_counts.hpp"
#include "nc_edit.hpp"

namespace {

namespace counts = mov::test::nc_counts;
namespace gen = mov::test::ncgen;
namespace io = mov::io;
using gen::CfKind;
using gen::CfSpec;
using io::ReadContext;
using io::StopToken;

template <class R>
std::string outcome(const R& result) {
  if (result) {
    return "ok";
  }
  if (const auto* format = std::get_if<io::FormatError>(&result.error())) {
    return "format:" + std::to_string(static_cast<int>(format->code));
  }
  if (std::holds_alternative<io::Cancelled>(result.error())) {
    return "cancelled";
  }
  return "other:" + std::to_string(result.error().index());
}

std::string format_outcome(io::FormatErrc code) {
  return "format:" + std::to_string(static_cast<int>(code));
}

}  // namespace

TEST_CASE("the station readers close their file on every path (B5, B6)",
          "[io][netcdf][regression][B5][B6][linux]") {
  if constexpr (not counts::available) {
    SKIP("needs the --wrap shims (Linux)");
  }
  using io::FormatErrc;
  const mov::test::ScratchDir dir;

  gen::LegacyNc legacy;
  gen::LegacyStation st;
  st.name = "A";
  st.id = "A";
  st.seconds = {0, 60};
  st.values = {1.0, 2.0};
  legacy.stations = {st, st};
  legacy.stations[1].name = "B";
  legacy.stations[1].id = "B";
  gen::make_legacy_nc(dir / "legacy.nc", legacy);
  legacy.omit_time_of = 2;
  gen::make_legacy_nc(dir / "legacy_bad.nc", legacy);

  gen::make_cf(dir / "orthogonal.nc", CfSpec{});
  CfSpec no_latitude;
  no_latitude.customize = [](gen::Cdf& f, int, int) {
    f.del_att("lat", "units");
  };
  gen::make_cf(dir / "no_latitude.nc", no_latitude);
  gen::make_cf(dir / "contiguous.nc",
               mov::test::foreign::ragged(CfKind::contiguous_ragged));
  {
    gen::Editor edit{dir / "contiguous.nc"};
    edit.put_int("rowSize", {2}, 1);
  }
  gen::make_cf(dir / "indexed.nc",
               mov::test::foreign::ragged(CfKind::indexed_ragged));
  CfSpec v5;
  v5.conventions = "CF-1.11";
  v5.customize = [](gen::Cdf& f, int, int) {
    f.text("", "metoceanviewer_format", "station-timeseries");
    f.text("", "metoceanviewer_format_version", "1.0");
  };
  gen::make_cf(dir / "v5.nc", v5);
  mov::test::write_bytes(dir / "text.imeds", "% IMEDS generic format\n");

  // Ten calls that open a file; each ends differently; two that open none.
  const auto one_round = [&] {
    const ReadContext stopped{.limits = {},
                              .stop = StopToken{[] { return true; }}};
    const auto read = [](const std::filesystem::path& path,
                         const ReadContext& ctx = {}) {
      return outcome(io::read_station_netcdf(path, io::AllStations{}, ctx));
    };
    return std::vector<std::string>{
        read(dir / "legacy.nc"), read(dir / "legacy_bad.nc"),
        read(dir / "legacy.nc", stopped), read(dir / "orthogonal.nc"),
        read(dir / "no_latitude.nc"), read(dir / "orthogonal.nc", stopped),
        read(dir / "contiguous.nc"), read(dir / "v5.nc"),
        outcome(io::inspect_station_netcdf(dir / "indexed.nc", {})),
        outcome(io::detect_file_type(dir / "orthogonal.nc")),
        // These two open no netCDF file.
        outcome(io::detect_file_type(dir / "text.imeds")),
        read(dir / "does_not_exist.nc")};
  };
  const std::vector<std::string> expected{
      "ok",
      format_outcome(FormatErrc::missing_variable),
      "cancelled",
      "ok",
      format_outcome(FormatErrc::missing_variable),
      "cancelled",
      format_outcome(FormatErrc::bad_row_size),
      "ok",
      "ok",
      "ok",
      "ok",
      "other:2"};  // an NcError: the open failed (B6)

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
  constexpr std::int64_t opens_per_round = 10;
  CHECK(now.opened - before.opened == opens_per_round * rounds);
  CHECK(now.closed - before.closed == opens_per_round * rounds);
  CHECK(now.close_calls - before.close_calls == opens_per_round * rounds);
  CHECK(now.abort_calls == before.abort_calls);
}
