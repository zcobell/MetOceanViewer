// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Foreign CF discrete-sampling-geometry `timeSeries` files (docs/
// station-netcdf.md section 12 "Foreign", CF 9.3): the five representations,
// each written with the raw netCDF-C API (support/foreign_fixtures.hpp) and
// read back through read_station_netcdf. The tolerance for packing, fills and
// strings, the standard-name mapping and the errors are in
// test_station_netcdf_foreign_names.cpp.

#include <netcdf.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "foreign_support.hpp"
#include "mov/io/error.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "nc_edit.hpp"

namespace {

using namespace mov::test::foreign;  // NOLINT(google-build-using-namespace)
namespace core = mov::core;
namespace io = mov::io;
using gen::CfKind;
using gen::CfSpec;
using io::FormatErrc;
using io::WarningCode;
using mov::test::ncgen::Editor;

}  // namespace

// ---- CF 9.3.1, orthogonal ---------------------------------------------------

TEST_CASE("foreign orthogonal (CF H.2.1): data over (station, time)",
          "[io][station_nc][foreign]") {
  const auto read = read_spec({});
  const core::StationTable& t = read.value.table;

  CHECK(origin_of(read.value).layout == io::CfDsgLayout::orthogonal);
  CHECK(origin_of(read.value).version == io::CfVersion{.major = 1, .minor = 8});
  REQUIRE(t.size() == 3);
  REQUIRE(t.schema().size() == 1);
  CHECK(t.schema()[0].quantity() == token("temperature"));
  CHECK(t.schema()[0].label() == "temperature");
  CHECK(t.schema()[0].unit() == unit("degC"));

  CHECK(t.station(core::StationIndex{0}).id.view() == "A");
  CHECK(t.station(core::StationIndex{1}).name.view() == "B");
  CHECK_FALSE(t.station(core::StationIndex{1}).source.has_value());
  CHECK(t.station(core::StationIndex{1}).location.lat() == Catch::Approx(29.5));
  CHECK(t.station(core::StationIndex{1}).location.lon() ==
        Catch::Approx(-89.5));

  CHECK(t.single_axis());
  const auto times = t.times(core::StationIndex{0});
  REQUIRE(times.size() == 4);
  CHECK(times[0] == hours(0));
  CHECK(times[3] == hours(3));
  CHECK(at(t, 0, 0, 0) == v(20.0));
  CHECK(at(t, 2, 0, 3) == v(43.0));
  CHECK(at(t, 1, 0, 2) == missing);  // _FillValue
  CHECK(at(t, 1, 0, 1) == v(31.0));

  CHECK(count_of(read.warnings, WarningCode::foreign_cf) == 1);
  CHECK(warning_of(read.warnings, WarningCode::foreign_cf).subject == "CF-1.8");
  // No grid_mapping: WGS 84 is assumed.
  CHECK(count_of(read.warnings, WarningCode::crs_assumed) == 1);
}

TEST_CASE(
    "foreign orthogonal: the transposed (time, station) matrix reads the same",
    "[io][station_nc][foreign]") {
  CfSpec spec;
  const auto major = read_spec(spec);
  spec.transposed = true;
  const auto minor = read_spec(spec);
  CHECK(major.value.table == minor.value.table);
  CHECK(major.warnings == minor.warnings);
}

TEST_CASE("foreign orthogonal: a classic (CDF-1) file reads",
          "[io][station_nc][foreign]") {
  CfSpec spec;
  const auto netcdf4 = read_spec(spec);
  spec.cmode = 0;
  const auto classic = read_spec(spec);
  CHECK(netcdf4.value.table == classic.value.table);
}

TEST_CASE("foreign orthogonal: unsorted times are put in order, and said so",
          "[io][station_nc][foreign]") {
  CfSpec spec;
  spec.times = {{0, 2, 1, 3}, {0, 2, 1, 3}, {0, 2, 1, 3}};
  spec.values = {
      {20, 22, 21, 23}, {30, 32, gen::cf_missing, 33}, {40, 42, 41, 43}};
  const FixtureFile file{spec};
  const auto read = file.read();
  const core::StationTable& t = read.value.table;
  // One shared axis, in order; every column moved with it.
  const auto axis = t.times(core::StationIndex{0});
  REQUIRE(axis.size() == 4);
  for (std::size_t i = 0; i < 4; ++i) {
    CHECK(axis[i] == hours(static_cast<double>(i)));
  }
  CHECK(at(t, 0, 0, 1) == v(21.0));
  CHECK(at(t, 0, 0, 2) == v(22.0));
  CHECK(at(t, 1, 0, 1) == missing);
  CHECK(at(t, 1, 0, 2) == v(32.0));
  const io::Warning w = warning_of(read.warnings, WarningCode::times_reordered);
  CHECK(w.count == 1);
  CHECK(w.subject == "time");
  CHECK(count_of(read.warnings, WarningCode::duplicate_times_dropped) == 0);
  // The catalog needs no times.
  CHECK(io::inspect_station_netcdf(file.path(), {}).has_value());
}

TEST_CASE("foreign orthogonal: a repeated time keeps its first row",
          "[io][station_nc][foreign]") {
  CfSpec spec;
  spec.times = {{0, 1, 1, 3}, {0, 1, 1, 3}, {0, 1, 1, 3}};
  spec.values = {{20, 21, 99, 23}, {30, 31, 31, 33}, {40, 41, 41, 43}};
  const auto read = read_spec(spec);
  const core::StationTable& t = read.value.table;
  REQUIRE(t.times(core::StationIndex{0}).size() == 3);
  CHECK(at(t, 0, 0, 1) == v(21.0));  // the first of the two
  CHECK(at(t, 0, 0, 2) == v(23.0));
  CHECK(warning_of(read.warnings, WarningCode::duplicate_times_dropped).count ==
        1);
  // Station 0's two rows differ.
  CHECK(warning_of(read.warnings, WarningCode::conflicting_duplicate_times)
            .count == 1);
  CHECK(count_of(read.warnings, WarningCode::times_reordered) == 0);
}

TEST_CASE("foreign incomplete: each station is put in order on its own",
          "[io][station_nc][foreign]") {
  CfSpec spec = ragged(CfKind::incomplete);
  spec.times = {{2, 0, 1}, {0, 1, 2, 3}, {6, 5}};
  spec.values = {{22, 20, 21}, {30, 31, gen::cf_missing, 33}, {41, 40}};
  const auto read = read_spec(spec);
  check_ragged_table(read.value.table);
  CHECK(warning_of(read.warnings, WarningCode::times_reordered).count == 2);
}

TEST_CASE("foreign: the selection picks and orders stations",
          "[io][station_nc][foreign]") {
  for (const CfKind kind :
       {CfKind::orthogonal, CfKind::incomplete, CfKind::contiguous_ragged,
        CfKind::indexed_ragged}) {
    const CfSpec spec = kind == CfKind::orthogonal ? CfSpec{} : ragged(kind);
    const FixtureFile file{spec};
    const auto selection = core::StationSelection::make({2, 0}, 3);
    REQUIRE(selection.has_value());
    auto read = io::read_station_netcdf(file.path(),
                                        io::StationNcSelection{*selection}, {});
    INFO("layout " << static_cast<int>(kind));
    REQUIRE(read.has_value());
    const core::StationTable& t = read->value.table;
    REQUIRE(t.size() == 2);
    CHECK(t.station(core::StationIndex{0}).id.view() == "C");
    CHECK(t.station(core::StationIndex{1}).id.view() == "A");
    CHECK(at(t, 0, 0, 0) == v(40.0));
    CHECK(at(t, 1, 0, 0) == v(20.0));
    CHECK(t.times(core::StationIndex{1})[0] == hours(0));

    const auto wrong = core::StationSelection::make({0}, 5);
    REQUIRE(wrong.has_value());
    CHECK(format_error_of(io::read_station_netcdf(
                              file.path(), io::StationNcSelection{*wrong}, {}))
              .code == FormatErrc::station_count_mismatch);
  }
}

TEST_CASE("foreign inspect agrees with read", "[io][station_nc][foreign]") {
  for (const CfKind kind :
       {CfKind::orthogonal, CfKind::incomplete, CfKind::contiguous_ragged,
        CfKind::indexed_ragged, CfKind::single_station}) {
    CfSpec spec = kind == CfKind::orthogonal ? CfSpec{} : ragged(kind);
    if (kind == CfKind::single_station) {
      spec.ids = {"S"};
      spec.times = {{0, 1}};
      spec.values = {{1, 2}};
    }
    const FixtureFile file{spec};
    auto catalog = io::inspect_station_netcdf(file.path(), {});
    REQUIRE(catalog.has_value());
    const auto read = file.read();
    INFO("layout " << static_cast<int>(kind));
    CHECK(catalog->value.origin == read.value.origin);
    REQUIRE(catalog->value.stations.size() == read.value.table.size());
    for (std::size_t i = 0; i < read.value.table.size(); ++i) {
      CHECK(catalog->value.stations[i].samples ==
            read.value.table.times(core::StationIndex{i}).size());
    }
    CHECK(catalog->value.schema.size() == 1);
    CHECK(catalog->value.schema[0] == read.value.table.schema()[0]);
    CHECK(catalog->warnings == read.warnings);
  }
}

// ---- CF 9.3.2, incomplete ---------------------------------------------------

TEST_CASE("foreign incomplete (CF H.2.2): the leading non-missing times",
          "[io][station_nc][foreign]") {
  for (const bool transposed : {false, true}) {
    for (const bool declared_fill : {true, false}) {
      CfSpec spec = ragged(CfKind::incomplete, transposed);
      if (not declared_fill) {
        spec.time_fill = std::nullopt;  // the default fill pads the times
      }
      INFO("transposed " << transposed << ", declared fill " << declared_fill);
      const auto read = read_spec(spec);
      CHECK(origin_of(read.value).layout == io::CfDsgLayout::incomplete);
      check_ragged_table(read.value.table);
    }
  }
}

TEST_CASE("foreign incomplete: obs_count is honoured and checked",
          "[io][station_nc][foreign]") {
  CfSpec spec = ragged(CfKind::incomplete);
  spec.obs_count = std::vector<std::int64_t>{3, 4, 2};
  check_ragged_table(read_spec(spec).value.table);

  spec.obs_count = std::vector<std::int64_t>{3, 5, 2};
  const auto e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::bad_obs_count);
  CHECK(e.station == 1);
  spec.obs_count = std::vector<std::int64_t>{3, -1, 2};
  CHECK(error_of_spec(spec).code == FormatErrc::bad_obs_count);
}

TEST_CASE(
    "foreign incomplete: a time after the padding began is padding_not_missing",
    "[io][station_nc][foreign]") {
  for (const bool transposed : {false, true}) {
    const FixtureFile file{ragged(CfKind::incomplete, transposed)};
    {
      // Station C has two samples; its third time is padding and its fourth is
      // a time again.
      Editor edit{file.path()};
      if (transposed) {
        edit.put("time", {3, 2}, 9.0);
      } else {
        edit.put("time", {2, 3}, 9.0);
      }
    }
    const auto e = file.error();
    INFO("transposed " << transposed);
    CHECK(e.code == FormatErrc::padding_not_missing);
    CHECK(e.station == 2);
    CHECK(e.index == 3);
    CHECK(file.inspect_error().code == FormatErrc::padding_not_missing);
  }
}

TEST_CASE(
    "foreign incomplete: a missing time before a valid one is the same error",
    "[io][station_nc][foreign]") {
  const FixtureFile file{ragged(CfKind::incomplete)};
  {
    Editor edit{file.path()};
    edit.put("time", {1, 1}, -1.0);  // B's second time is the fill value
  }
  const auto e = file.error();
  CHECK(e.code == FormatErrc::padding_not_missing);
  CHECK(e.station == 1);
  CHECK(e.index == 2);
}

TEST_CASE("foreign incomplete: padding of the data variables is not looked at",
          "[io][station_nc][foreign]") {
  const FixtureFile file{ragged(CfKind::incomplete)};
  {
    Editor edit{file.path()};
    edit.put("temperature", {0, 3}, 99.0);  // A's padding holds a value
  }
  check_ragged_table(file.read().value.table);
}

// ---- CF 9.3.3, contiguous ragged --------------------------------------------

TEST_CASE("foreign contiguous ragged (CF H.2.4)", "[io][station_nc][foreign]") {
  const auto read = read_spec(ragged(CfKind::contiguous_ragged));
  CHECK(origin_of(read.value).layout == io::CfDsgLayout::contiguous_ragged);
  check_ragged_table(read.value.table);
  // rowSize and the observation dimension are not data.
  CHECK(read.value.table.schema().size() == 1);
  CHECK(count_of(read.warnings, WarningCode::skipped_variable) == 0);
}

TEST_CASE("foreign contiguous ragged: a station without samples",
          "[io][station_nc][foreign]") {
  CfSpec spec = ragged(CfKind::contiguous_ragged);
  spec.times[1].clear();
  spec.values[1].clear();
  const auto read = read_spec(spec);
  const core::StationTable& t = read.value.table;
  CHECK(t.times(core::StationIndex{1}).empty());
  CHECK(t.times(core::StationIndex{0}).size() == 3);
  CHECK(t.times(core::StationIndex{2}).size() == 2);
  CHECK(at(t, 2, 0, 1) == v(41.0));
}

TEST_CASE("foreign contiguous ragged: the counts must add up to the dimension",
          "[io][station_nc][foreign]") {
  const FixtureFile file{ragged(CfKind::contiguous_ragged)};
  const auto edit = [&file](std::size_t station, int value) {
    Editor e{file.path()};
    e.put_int("rowSize", {station}, value);
  };
  SECTION("a sum below the dimension") {
    edit(2, 1);
    const auto e = file.error();
    CHECK(e.code == FormatErrc::bad_row_size);
    CHECK(e.subject == "rowSize");
  }
  SECTION("a sum above the dimension") {
    edit(2, 5);
    const auto e = file.error();
    CHECK(e.code == FormatErrc::bad_row_size);
    CHECK(e.station == 2);
  }
  SECTION("a negative count") {
    edit(1, -1);
    const auto e = file.error();
    CHECK(e.code == FormatErrc::bad_row_size);
    CHECK(e.station == 1);
  }
  CHECK(file.inspect_error().code == FormatErrc::bad_row_size);
}

// ---- CF 9.3.4, indexed ragged -----------------------------------------------

TEST_CASE("foreign indexed ragged (CF H.2.5)", "[io][station_nc][foreign]") {
  const auto read = read_spec(ragged(CfKind::indexed_ragged));
  CHECK(origin_of(read.value).layout == io::CfDsgLayout::indexed_ragged);
  check_ragged_table(read.value.table);
  CHECK(count_of(read.warnings, WarningCode::skipped_variable) == 0);
}

TEST_CASE("foreign: every ragged and incomplete layout gives one table",
          "[io][station_nc][foreign]") {
  const auto a = read_spec(ragged(CfKind::incomplete));
  const auto b = read_spec(ragged(CfKind::incomplete, true));
  const auto c = read_spec(ragged(CfKind::contiguous_ragged));
  const auto d = read_spec(ragged(CfKind::indexed_ragged));
  CHECK(a.value.table == b.value.table);
  CHECK(a.value.table == c.value.table);
  CHECK(a.value.table == d.value.table);
}

TEST_CASE("foreign indexed ragged: an index outside the stations",
          "[io][station_nc][foreign]") {
  const FixtureFile file{ragged(CfKind::indexed_ragged)};
  const auto edit = [&file](std::size_t sample, int value) {
    Editor e{file.path()};
    e.put_int("stationIndex", {sample}, value);
  };
  SECTION("past the last station") {
    edit(4, 3);
    const auto e = file.error();
    CHECK(e.code == FormatErrc::bad_ragged_index);
    CHECK(e.index == 4);
    CHECK(e.subject == "stationIndex");
  }
  SECTION("negative") {
    edit(0, -1);
    const auto e = file.error();
    CHECK(e.code == FormatErrc::bad_ragged_index);
    CHECK(e.index == 0);
  }
  CHECK(file.inspect_error().code == FormatErrc::bad_ragged_index);
}

// ---- CF 9.2, a single station -----------------------------------------------

TEST_CASE("foreign single station (CF 9.2): no station dimension",
          "[io][station_nc][foreign]") {
  CfSpec spec;
  spec.kind = CfKind::single_station;
  spec.ids = {"BUOY1"};
  spec.id_width = 8;
  spec.times = {{0, 1, 2}};
  spec.values = {{20, 21, gen::cf_missing}};
  const auto read = read_spec(spec);
  const core::StationTable& t = read.value.table;
  CHECK(origin_of(read.value).layout == io::CfDsgLayout::single_station);
  REQUIRE(t.size() == 1);
  CHECK(t.station(core::StationIndex{0}).id.view() == "BUOY1");
  CHECK(t.station(core::StationIndex{0}).location.lat() == Catch::Approx(29.0));
  CHECK(t.times(core::StationIndex{0}).size() == 3);
  CHECK(t.times(core::StationIndex{0})[2] == hours(2));
  CHECK(at(t, 0, 0, 1) == v(21.0));
  CHECK(at(t, 0, 0, 2) == missing);
}

// ---- time types -------------------------------------------------------------

TEST_CASE("foreign: int and int64 times in every layout",
          "[io][station_nc][foreign]") {
  for (const nc_type type : {NC_INT, NC_INT64, NC_FLOAT, NC_SHORT}) {
    for (const CfKind kind : {CfKind::incomplete, CfKind::contiguous_ragged,
                              CfKind::indexed_ragged}) {
      CfSpec spec = ragged(kind);
      spec.time_type = type;
      spec.time_units = "seconds since 2000-01-01 00:00:00 +00:00";
      spec.times = {{0, 3600, 7200}, {0, 3600, 7200, 10800}, {18000, 21600}};
      spec.time_fill = -1.0;
      INFO("layout " << static_cast<int>(kind) << " type " << type);
      check_ragged_table(read_spec(spec).value.table);
    }
  }
}

TEST_CASE("foreign: an orthogonal int64 time reads",
          "[io][station_nc][foreign]") {
  CfSpec spec;
  spec.time_type = NC_INT64;
  spec.time_units = "minutes since 2000-01-01 00:00:00";
  spec.times = {{0, 60, 120, 180}, {0, 60, 120, 180}, {0, 60, 120, 180}};
  const auto read = read_spec(spec);
  CHECK(read.value.table.times(core::StationIndex{0})[3] == hours(3));
}

TEST_CASE("foreign: days, hours and a zone in the time units",
          "[io][station_nc][foreign]") {
  CfSpec spec;
  spec.time_units = "days since 2000-01-01";
  spec.times = {{0, 0.5, 1, 1.5}, {0, 0.5, 1, 1.5}, {0, 0.5, 1, 1.5}};
  const auto days = read_spec(spec);
  CHECK(days.value.table.times(core::StationIndex{0})[1] == hours(12));
  spec.time_units = "hours since 2000-01-01 06:00:00 +06:00";
  spec.times = {{0, 1, 2, 3}, {0, 1, 2, 3}, {0, 1, 2, 3}};
  const auto zoned = read_spec(spec);
  CHECK(zoned.value.table.times(core::StationIndex{0})[0] == hours(0));
}

TEST_CASE("foreign: data and integer helpers of every type read the same",
          "[io][station_nc][foreign]") {
  for (const CfKind kind : {CfKind::incomplete, CfKind::contiguous_ragged,
                            CfKind::indexed_ragged}) {
    const auto reference = read_spec(ragged(kind));
    for (const nc_type data : {NC_FLOAT, NC_SHORT, NC_INT, NC_DOUBLE}) {
      for (const nc_type helper : {NC_BYTE, NC_SHORT, NC_INT, NC_INT64}) {
        CfSpec spec = ragged(kind);
        spec.data_type = data;
        spec.helper_type = helper;
        spec.obs_count = std::vector<std::int64_t>{3, 4, 2};
        INFO("layout " << static_cast<int>(kind) << " data " << data
                       << " helper " << helper);
        const auto read = read_spec(spec);
        CHECK(read.value.table == reference.value.table);
      }
    }
  }
  for (const bool transposed : {false, true}) {
    CfSpec spec;
    spec.transposed = transposed;
    const auto reference = read_spec(spec);
    for (const nc_type data : {NC_FLOAT, NC_SHORT, NC_INT}) {
      spec.data_type = data;
      CHECK(read_spec(spec).value.table == reference.value.table);
    }
  }
}
