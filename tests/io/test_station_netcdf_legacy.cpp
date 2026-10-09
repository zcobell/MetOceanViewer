// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The legacy v4 station netCDF (docs/legacy-formats.md section 5, docs/
// station-netcdf.md section 11): dialects A and B read through
// read_station_netcdf, with v4's defects as regressions: closing an id that
// never opened, a name stride of 200, a fixed referenceDate length, the
// default fill read as data, an EPSG getter that returned error codes, and
// heap junk after the NUL of a name.

#include <netcdf.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "legacy_fixtures.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/station.hpp"
#include "mov/io/detail/legacy_station_names.hpp"
#include "mov/io/error.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "mov/test/scratch_dir.hpp"
#include "station_nc_support.hpp"

namespace {

using namespace mov::test::snc;  // NOLINT(google-build-using-namespace)
namespace core = mov::core;
namespace io = mov::io;
namespace gen = mov::test::ncgen;
using io::FormatErrc;
using io::WarningCode;
using mov::test::ScratchDir;

constexpr double fill_double = 9.9692099683868690e+36;

gen::LegacyStation st(std::string name, std::string id,
                      std::vector<std::int64_t> seconds,
                      std::vector<double> values) {
  gen::LegacyStation s;
  s.name = std::move(name);
  s.id = std::move(id);
  s.seconds = std::move(seconds);
  s.values = std::move(values);
  return s;
}

/// Three stations with the names A writes (NUL-padded to 200).
gen::LegacyNc three_stations() {
  gen::LegacyNc spec;
  spec.stations.push_back(
      st("Grand Isle", "8761724", {0, 360, 720}, {0.5, 0.6, 0.7}));
  spec.stations.back().x = -89.9567;
  spec.stations.back().y = 29.2633;
  spec.stations.push_back(
      st("Pilots Station East", "8760922", {0, 900}, {1.0, 1.5}));
  spec.stations.back().x = -89.4067;
  spec.stations.back().y = 28.9322;
  spec.stations.push_back(st("Shell Beach", "8761305", {60}, {2.0}));
  spec.stations.back().x = -89.6733;
  spec.stations.back().y = 29.8683;
  return spec;
}

io::Read<io::StationFile> read_spec(const gen::LegacyNc& spec,
                                    const io::ReadContext& ctx = {}) {
  const ScratchDir dir;
  const auto path = dir / "legacy.nc";
  gen::make_legacy_nc(path, spec);
  mov::test::configure_projection_database();
  return must_read(read_all(path, ctx));
}

io::FormatError error_of_spec(const gen::LegacyNc& spec) {
  const ScratchDir dir;
  const auto path = dir / "legacy.nc";
  gen::make_legacy_nc(path, spec);
  return format_error_of(read_all(path));
}

core::Sample at(const core::StationTable& t, std::size_t station,
                std::size_t column, std::size_t i) {
  return t.column(core::StationIndex{station}, core::ColumnIndex{column})[i];
}

}  // namespace

TEST_CASE("legacy A: three stations read into one table",
          "[io][station_nc][legacy]") {
  const auto read = read_spec(three_stations());
  const core::StationTable& t = read.value.table;
  REQUIRE(t.size() == 3);
  REQUIRE(t.schema().size() == 1);

  CHECK(t.schema()[0] == meta(core::GenericQuantity::value(), "", "m",
                              core::VerticalDatum::mllw));
  CHECK(t.station(core::StationIndex{0}).id.view() == "8761724");
  CHECK(t.station(core::StationIndex{0}).name.view() == "Grand Isle");
  CHECK(t.station(core::StationIndex{1}).id.view() == "8760922");
  CHECK(t.station(core::StationIndex{2}).name.view() == "Shell Beach");
  CHECK(t.station(core::StationIndex{0}).location.lat() ==
        Catch::Approx(29.2633));
  CHECK(t.station(core::StationIndex{0}).location.lon() ==
        Catch::Approx(-89.9567));
  CHECK_FALSE(t.station(core::StationIndex{0}).native.has_value());
  CHECK_FALSE(t.station(core::StationIndex{0}).source.has_value());

  const auto times0 = t.times(core::StationIndex{0});
  REQUIRE(times0.size() == 3);
  CHECK(times0[0] == ms(0));
  CHECK(times0[1] == ms(360000));
  CHECK(times0[2] == ms(720000));
  CHECK(t.times(core::StationIndex{1}).size() == 2);
  CHECK(t.times(core::StationIndex{2})[0] == ms(60000));
  CHECK(at(t, 0, 0, 1) == v(0.6));
  CHECK(at(t, 1, 0, 1) == v(1.5));
  CHECK(at(t, 2, 0, 0) == v(2.0));

  const auto* origin = std::get_if<io::LegacyOrigin>(&read.value.origin);
  REQUIRE(origin != nullptr);
  CHECK(origin->fileformat == "20180123");
  CHECK(origin->has_station_ids);
  CHECK(origin->width == io::StationNumberWidth::four);
  CHECK(count_of(read.warnings, WarningCode::legacy_dialect) == 1);
  CHECK(count_of(read.warnings, WarningCode::crs_assumed) == 0);
  CHECK(count_of(read.warnings, WarningCode::tz_assumed_utc) == 0);
  CHECK(count_of(read.warnings, WarningCode::epoch_used) == 0);
}

TEST_CASE("legacy: a file with neither stationId nor fileformat",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.write_station_id = false;
  spec.write_fileformat = false;
  const auto read = read_spec(spec);
  const auto* origin = std::get_if<io::LegacyOrigin>(&read.value.origin);
  REQUIRE(origin != nullptr);
  CHECK_FALSE(origin->fileformat.has_value());
  CHECK_FALSE(origin->has_station_ids);
  // The id is the name when the file has no stationId.
  CHECK(read.value.table.station(core::StationIndex{0}).id.view() ==
        "Grand Isle");
  CHECK(read.value.table.station(core::StationIndex{0}).name.view() ==
        "Grand Isle");
}

TEST_CASE("legacy: an empty stationId falls back to the name",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.stations[1].id = "";
  const auto read = read_spec(spec);
  CHECK(read.value.table.station(core::StationIndex{1}).id.view() ==
        "Pilots Station East");
}

TEST_CASE("legacy: the origin records what the file has, one fact each",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.write_fileformat = false;
  auto read = read_spec(spec);
  auto origin = std::get<io::LegacyOrigin>(read.value.origin);
  CHECK_FALSE(origin.fileformat.has_value());
  CHECK(origin.has_station_ids);
  spec.write_station_id = false;
  spec.write_fileformat = true;
  read = read_spec(spec);
  origin = std::get<io::LegacyOrigin>(read.value.origin);
  CHECK(origin.fileformat == "20180123");
  CHECK_FALSE(origin.has_station_ids);
  spec.width = 6;
  read = read_spec(spec);
  CHECK(std::get<io::LegacyOrigin>(read.value.origin).width ==
        io::StationNumberWidth::six);
}

TEST_CASE("legacy: the selection picks and orders stations",
          "[io][station_nc][legacy]") {
  const ScratchDir dir;
  const auto path = dir / "legacy.nc";
  gen::make_legacy_nc(path, three_stations());
  const auto selection = core::StationSelection::make({2, 0}, 3);
  REQUIRE(selection.has_value());
  auto read = io::read_station_netcdf(path, io::StationNcSelection{*selection},
                                      io::ReadContext{});
  REQUIRE(read.has_value());
  const core::StationTable& t = read->value.table;
  REQUIRE(t.size() == 2);
  CHECK(t.station(core::StationIndex{0}).id.view() == "8761305");
  CHECK(t.station(core::StationIndex{1}).id.view() == "8761724");
  CHECK(t.times(core::StationIndex{1}).size() == 3);

  const auto wrong = core::StationSelection::make({0}, 5);
  REQUIRE(wrong.has_value());
  CHECK(format_error_of(
            io::read_station_netcdf(path, io::StationNcSelection{*wrong}, {}))
            .code == FormatErrc::station_count_mismatch);
}

TEST_CASE("legacy inspect agrees with read", "[io][station_nc][legacy]") {
  const ScratchDir dir;
  const auto path = dir / "legacy.nc";
  gen::make_legacy_nc(path, three_stations());
  auto catalog = io::inspect_station_netcdf(path, {});
  REQUIRE(catalog.has_value());
  CHECK(std::get<io::LegacyOrigin>(catalog->value.origin).has_station_ids);
  REQUIRE(catalog->value.stations.size() == 3);
  CHECK(catalog->value.stations[0].samples == 3);
  CHECK(catalog->value.stations[1].samples == 2);
  CHECK(catalog->value.stations[2].samples == 1);
  CHECK(catalog->value.stations[1].station.id.view() == "8760922");
  const auto read = must_read(read_all(path));
  CHECK(catalog->value.schema.size() == read.value.table.schema().size());
  CHECK(catalog->value.schema[0] == read.value.table.schema()[0]);
  CHECK(catalog->warnings == read.warnings);
}

// ---- the row stride is the file's, not 200 ----------------------------------

TEST_CASE("legacy B7: stationNameLen 50 and 300 and 7 give the right names",
          "[io][station_nc][legacy][regression]") {
  for (const std::size_t len :
       {std::size_t{7}, std::size_t{50}, std::size_t{300}}) {
    auto spec = three_stations();
    spec.name_len = len;
    spec.stations[0].name = "Grand";
    spec.stations[0].id = "A1";
    spec.stations[1].name = "Pilots";
    spec.stations[1].id = "A2";
    spec.stations[2].name = "Shell";
    spec.stations[2].id = "A3";
    const auto read = read_spec(spec);
    INFO("stationNameLen " << len);
    CHECK(read.value.table.station(core::StationIndex{0}).name.view() ==
          "Grand");
    CHECK(read.value.table.station(core::StationIndex{1}).name.view() ==
          "Pilots");
    CHECK(read.value.table.station(core::StationIndex{2}).name.view() ==
          "Shell");
    CHECK(read.value.table.station(core::StationIndex{2}).id.view() == "A3");
  }
}

TEST_CASE("legacy: a long name fills its whole row",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.name_len = 150;
  const std::string long_name(150, 'q');
  spec.stations[0].name = long_name;
  spec.stations[0].id = long_name;
  const auto read = read_spec(spec);
  CHECK(read.value.table.station(core::StationIndex{0}).name.view() ==
        long_name);
  CHECK(read.value.table.station(core::StationIndex{1}).name.view() ==
        "Pilots Station East");
}

TEST_CASE("legacy S1: heap junk after the NUL of a name is cut",
          "[io][station_nc][legacy][regression]") {
  auto spec = three_stations();
  spec.stations[0].name = std::string{"Grand Isle\0\x7f\xfe junk", 17};
  spec.stations[0].id = std::string{"8761724\0zzz", 11};
  const auto read = read_spec(spec);
  CHECK(read.value.table.station(core::StationIndex{0}).name.view() ==
        "Grand Isle");
  CHECK(read.value.table.station(core::StationIndex{0}).id.view() == "8761724");
  CHECK(count_of(read.warnings, WarningCode::invalid_utf8_replaced) == 0);
}

TEST_CASE("legacy: names collapse internal white space (parity with v4)",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.stations[0].name = "  Grand   Isle \t LA ";
  const auto read = read_spec(spec);
  CHECK(read.value.table.station(core::StationIndex{0}).name.view() ==
        "Grand Isle LA");
}

TEST_CASE("legacy: bytes that are not UTF-8 in a name become U+FFFD",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.stations[0].name = "Gr\xe9nd";
  const auto read = read_spec(spec);
  CHECK(read.value.table.station(core::StationIndex{0}).name.view() ==
        "Gr\xef\xbf\xbdnd");
  CHECK(count_of(read.warnings, WarningCode::invalid_utf8_replaced) == 1);
}

TEST_CASE("legacy: UTF-8 names round trip", "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.stations[0].name = "Gen\xc3\xa8ve \xe2\x9c\x93";
  const auto read = read_spec(spec);
  CHECK(read.value.table.station(core::StationIndex{0}).name.view() ==
        "Gen\xc3\xa8ve \xe2\x9c\x93");
}

TEST_CASE("legacy: no name and no id gives the decimal index",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.stations[1].name = "";
  spec.stations[1].id = "";
  const auto read = read_spec(spec);
  CHECK(read.value.table.station(core::StationIndex{1}).id.view() == "1");
  // The name stays empty: only the station netCDF writer substitutes one.
  CHECK(read.value.table.station(core::StationIndex{1}).name.empty());
  const io::Warning w =
      warning_of(read.warnings, WarningCode::station_id_substituted);
  CHECK(w.subject == "stationId");
  CHECK(w.count == 1);
}

TEST_CASE("legacy: an empty stationId is the name, and said so",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.stations[0].id = "";
  spec.stations[2].id = "";
  const auto read = read_spec(spec);
  const core::StationTable& t = read.value.table;
  CHECK(t.station(core::StationIndex{0}).id.view() ==
        t.station(core::StationIndex{0}).name.view());
  CHECK(t.station(core::StationIndex{2}).id.view() ==
        t.station(core::StationIndex{2}).name.view());
  const io::Warning w =
      warning_of(read.warnings, WarningCode::station_id_substituted);
  CHECK(w.subject == "stationId");
  CHECK(w.count == 2);

  // Without a stationId variable the name is the id by the dialect's rule:
  // nothing was substituted.
  spec.write_station_id = false;
  const auto bare = read_spec(spec);
  CHECK(count_of(bare.warnings, WarningCode::station_id_substituted) == 0);
}

TEST_CASE("legacy: duplicate ids are made unique", "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.stations[1].id = "8761724";
  spec.stations[2].id = "8761724";
  const auto read = read_spec(spec);
  const core::StationTable& t = read.value.table;
  CHECK(t.station(core::StationIndex{0}).id.view() == "8761724");
  CHECK(t.station(core::StationIndex{1}).id.view() == "8761724#2");
  CHECK(t.station(core::StationIndex{2}).id.view() == "8761724#3");
  const io::Warning w =
      warning_of(read.warnings, WarningCode::duplicate_station_id_renamed);
  CHECK(w.subject == "8761724");
  CHECK(w.count == 2);
}

// ---- float data and the fill ------------------------------------------------

TEST_CASE("legacy B4: a float data variable reads exactly",
          "[io][station_nc][legacy][regression]") {
  auto spec = three_stations();
  spec.data_type = gen::LegacyData::float32;
  spec.stations[0].values = {0.5, 0.25, -1.5};
  const auto read = read_spec(spec);
  CHECK(at(read.value.table, 0, 0, 0) == v(0.5));
  CHECK(at(read.value.table, 0, 0, 1) == v(0.25));
  CHECK(at(read.value.table, 0, 0, 2) == v(-1.5));
}

TEST_CASE("legacy B9: the default fill value is masked",
          "[io][station_nc][legacy][regression]") {
  auto spec = three_stations();
  // No _FillValue is written by v4: unwritten elements read as the default.
  spec.stations[0].written = 2;
  auto read = read_spec(spec);
  CHECK(at(read.value.table, 0, 0, 0) == v(0.5));
  CHECK(at(read.value.table, 0, 0, 1) == v(0.6));
  CHECK(at(read.value.table, 0, 0, 2) == missing);

  // A value that is the default fill is missing even when it was written.
  spec = three_stations();
  spec.stations[0].values = {0.5, fill_double, 0.7};
  read = read_spec(spec);
  CHECK(at(read.value.table, 0, 0, 1) == missing);
}

TEST_CASE(
    "legacy B9: an explicit _FillValue and NaN are masked, -99999 is a value",
    "[io][station_nc][legacy][regression]") {
  auto spec = three_stations();
  spec.stations[0].fill = -9999.0;
  spec.stations[0].values = {-9999.0, -99999.0,
                             std::numeric_limits<double>::quiet_NaN()};
  const auto read = read_spec(spec);
  CHECK(at(read.value.table, 0, 0, 0) == missing);
  CHECK(at(read.value.table, 0, 0, 1) == v(-99999.0));  // no magic sentinel
  CHECK(at(read.value.table, 0, 0, 2) == missing);
}

TEST_CASE("legacy: a float _FillValue is compared in float",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.data_type = gen::LegacyData::float32;
  spec.stations[0].fill = -9999.0;
  spec.stations[0].values = {-9999.0, 1.0, 2.0};
  const auto read = read_spec(spec);
  CHECK(at(read.value.table, 0, 0, 0) == missing);
  CHECK(at(read.value.table, 0, 0, 1) == v(1.0));
}

// ---- the EPSG ---------------------------------------------------------------

TEST_CASE("legacy B10: a missing EPSG is assumed to be 4326 with a warning",
          "[io][station_nc][legacy][regression]") {
  auto spec = three_stations();
  spec.epsg = std::nullopt;
  const auto read = read_spec(spec);
  const io::Warning w = warning_of(read.warnings, WarningCode::crs_assumed);
  CHECK(w.subject == "EPSG:4326");
  CHECK(read.value.table.station(core::StationIndex{0}).location.lon() ==
        Catch::Approx(-89.9567));
}

TEST_CASE("legacy B10: an EPSG stored as text is an error, never a code",
          "[io][station_nc][legacy][regression]") {
  auto spec = three_stations();
  spec.epsg_as_text = true;
  const ScratchDir dir;
  const auto path = dir / "legacy.nc";
  gen::make_legacy_nc(path, spec);
  const auto result = read_all(path);
  REQUIRE_FALSE(result.has_value());
  const auto* nc = std::get_if<io::NcError>(&result.error());
  REQUIRE(nc != nullptr);
  CHECK(nc->status == io::NcStatus{io::WrapperFault::type_mismatch});
  CHECK(nc->object == "stationXCoordinate:HorizontalProjectionEPSG");
}

TEST_CASE("legacy: an EPSG that is not a CRS is unsupported_crs",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.epsg = 0;
  const auto e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::unsupported_crs);
  spec.epsg = 999999;
  CHECK(error_of_spec(spec).code == FormatErrc::unsupported_crs);
}

TEST_CASE(
    "legacy: a projected EPSG is projected to WGS 84 and the point is kept",
    "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.epsg = 26915;  // NAD83 / UTM zone 15N
  spec.stations[0].x = 500000.0;
  spec.stations[0].y = 3250000.0;
  spec.stations[1].x = 500000.0;
  spec.stations[1].y = 3260000.0;
  spec.stations[2].x = 500000.0;
  spec.stations[2].y = 3270000.0;
  const auto read = read_spec(spec);
  const core::FileStation& s = read.value.table.station(core::StationIndex{0});
  CHECK(s.location.lon() == Catch::Approx(-93.0).margin(1e-6));
  CHECK(s.location.lat() == Catch::Approx(29.3792).margin(1e-3));
  REQUIRE(s.native.has_value());
  if (s.native) {
    CHECK(s.native->x() == 500000.0);
    CHECK(s.native->y() == 3250000.0);
    CHECK(s.native->crs().code() == 26915);
  }
  CHECK(count_of(read.warnings, WarningCode::crs_assumed) == 0);
}

TEST_CASE("legacy: a geographic EPSG other than 4326 keeps the native point",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.epsg = 4269;  // NAD83
  const auto read = read_spec(spec);
  const core::FileStation& s = read.value.table.station(core::StationIndex{0});
  REQUIRE(s.native.has_value());
  if (s.native) {
    CHECK(s.native->crs().code() == 4269);
  }
  CHECK(s.location.lon() == Catch::Approx(-89.9567).margin(1e-4));
}

TEST_CASE("legacy: coordinates that are not places are bad_coordinates",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.stations[1].y = 123.0;  // latitude out of range
  const auto e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::bad_coordinates);
  CHECK(e.station == 1);
  spec = three_stations();
  spec.stations[2].x = std::numeric_limits<double>::quiet_NaN();
  const auto e2 = error_of_spec(spec);
  CHECK(e2.code == FormatErrc::bad_coordinates);
  CHECK(e2.station == 2);
}

// ---- referenceDate ----------------------------------------------------------

TEST_CASE("legacy B8: referenceDate of 19, 20 and 120 bytes",
          "[io][station_nc][legacy][regression]") {
  const std::string date19 = "2000-01-01 00:00:00";
  auto spec = three_stations();
  for (const std::string& text :
       {date19, date19 + std::string(1, '\0'),
        date19 + std::string(1, '\0') + std::string(100, 'x'),
        date19 + std::string(101, 'x')}) {
    spec.reference_date = text;
    const auto read = read_spec(spec);
    INFO("referenceDate of " << text.size() << " bytes");
    CHECK(read.value.table.times(core::StationIndex{0})[0] == ms(946684800000));
    CHECK(read.value.table.times(core::StationIndex{0})[1] ==
          ms(946684800000 + 360000));
    CHECK(count_of(read.warnings, WarningCode::epoch_used) == 0);
  }
}

TEST_CASE("legacy: a T between date and time is accepted",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.reference_date = "2000-01-01T00:00:10";
  const auto read = read_spec(spec);
  CHECK(read.value.table.times(core::StationIndex{0})[0] == ms(946684810000));
}

TEST_CASE("legacy: no referenceDate means 1970-01-01 with epoch_used",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.reference_date = std::nullopt;
  const auto read = read_spec(spec);
  CHECK(read.value.table.times(core::StationIndex{0})[1] == ms(360000));
  const io::Warning w = warning_of(read.warnings, WarningCode::epoch_used);
  CHECK(w.subject == "1970-01-01 00:00:00");
}

TEST_CASE("legacy: a referenceDate that is not a date is a ParseError",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  for (const char* text : {"not a date at all!", "2000-13-01 00:00:00", ""}) {
    spec.reference_date = std::string{text};
    const ScratchDir dir;
    const auto path = dir / "legacy.nc";
    gen::make_legacy_nc(path, spec);
    const auto result = read_all(path);
    REQUIRE_FALSE(result.has_value());
    const auto* parse = std::get_if<io::ParseError>(&result.error());
    INFO(text);
    REQUIRE(parse != nullptr);
    CHECK(parse->code() == io::ParseErrc::bad_date);
  }
}

TEST_CASE("legacy: the time zone attribute is only checked",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  for (const char* tz : {"utc", "UTC", "gmt", "GMT"}) {
    spec.timezone = std::string{tz};
    CHECK(count_of(read_spec(spec).warnings, WarningCode::tz_assumed_utc) == 0);
  }
  spec.timezone = std::string{"EST"};
  const auto read = read_spec(spec);
  CHECK(warning_of(read.warnings, WarningCode::tz_assumed_utc).subject ==
        "EST");
  CHECK(read.value.table.times(core::StationIndex{0})[1] == ms(360000));
  spec.timezone = std::nullopt;
  CHECK(count_of(read_spec(spec).warnings, WarningCode::tz_assumed_utc) == 0);
}

TEST_CASE("legacy: int and double time variables are read",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.time_type = gen::LegacyTime::int32;
  const auto ints = read_spec(spec);
  CHECK(ints.value.table.times(core::StationIndex{0})[2] == ms(720000));
  spec.time_type = gen::LegacyTime::float64;
  const auto doubles = read_spec(spec);
  CHECK(doubles.value.table.times(core::StationIndex{0})[2] == ms(720000));
}

TEST_CASE("legacy: a repeated time keeps its first row, and says so",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.stations[1].seconds = {900, 900};
  spec.stations[1].values = {1.0, 2.0};
  const auto read = read_spec(spec);
  const core::StationTable& t = read.value.table;
  REQUIRE(t.times(core::StationIndex{1}).size() == 1);
  CHECK(at(t, 1, 0, 0) == v(1.0));
  const io::Warning w =
      warning_of(read.warnings, WarningCode::duplicate_times_dropped);
  CHECK(w.count == 1);
  CHECK(warning_of(read.warnings, WarningCode::conflicting_duplicate_times)
            .count == 1);
}

TEST_CASE("legacy: times out of order are put in order, with their values",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.stations[0].seconds = {720, 0, 360};
  spec.stations[0].values = {3.0, 1.0, 2.0};
  const auto read = read_spec(spec);
  const core::StationTable& t = read.value.table;
  const auto times = t.times(core::StationIndex{0});
  REQUIRE(times.size() == 3);
  CHECK(times[0] == ms(0));
  CHECK(times[2] == ms(720000));
  CHECK(at(t, 0, 0, 0) == v(1.0));
  CHECK(at(t, 0, 0, 2) == v(3.0));
  CHECK(warning_of(read.warnings, WarningCode::times_reordered).count == 1);
}

TEST_CASE("legacy: a time that is the fill value is time_missing",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.stations[0].seconds = {0, std::numeric_limits<std::int64_t>::min() + 2,
                              720};  // NC_FILL_INT64
  const auto e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::time_missing);
  CHECK(e.station == 0);
  CHECK(e.index == 1);
}

TEST_CASE("legacy: a time beyond the clock is time_out_of_range",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.stations[2].seconds = {std::int64_t{1} << 60};
  const auto e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::time_out_of_range);
  CHECK(e.station == 2);
}

// ---- the schema -------------------------------------------------------------

TEST_CASE("legacy: units and datum come from the data variables",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  for (auto& s : spec.stations) {
    s.units = "ft";
    s.datum = "mhw";  // v4 could not name MHW
  }
  auto read = read_spec(spec);
  CHECK(read.value.table.schema()[0] == meta(core::GenericQuantity::value(), "",
                                             "ft", core::VerticalDatum::mhw));

  for (auto& s : spec.stations) {
    s.units = "";
    s.datum = "none";
  }
  read = read_spec(spec);
  CHECK(read.value.table.schema()[0] ==
        core::SeriesMeta::make({.quantity = core::GenericQuantity::value()}));

  for (auto& s : spec.stations) {
    s.datum = "";
  }
  read = read_spec(spec);
  CHECK_FALSE(read.value.table.schema()[0].datum().has_value());
}

TEST_CASE("legacy: unknown units and datums warn", "[io][station_nc][legacy]") {
  auto spec = three_stations();
  for (auto& s : spec.stations) {
    s.units = "furlongs";
    s.datum = "FOO";
  }
  const auto read = read_spec(spec);
  CHECK(warning_of(read.warnings, WarningCode::unrecognized_unit).subject ==
        "furlongs");
  CHECK(warning_of(read.warnings, WarningCode::datum_unknown).subject == "FOO");
  CHECK_FALSE(read.value.table.schema()[0].datum().has_value());
}

TEST_CASE("legacy: stations that disagree on units or datum are refused",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.stations[2].units = "ft";
  auto e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::inconsistent_metadata);
  CHECK(e.subject == "units");
  CHECK(e.station == 2);
  spec = three_stations();
  spec.stations[1].datum = "NAVD88";
  e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::inconsistent_metadata);
  CHECK(e.subject == "datum");
  CHECK(e.station == 1);
}

// ---- stations and their variables -------------------------------------------

TEST_CASE("legacy: a station without samples has an empty series",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.stations[1].seconds.clear();
  spec.stations[1].values.clear();
  const auto read = read_spec(spec);
  CHECK(read.value.table.times(core::StationIndex{1}).empty());
  CHECK(read.value.table.times(core::StationIndex{0}).size() == 3);
  CHECK(read.value.table.times(core::StationIndex{2}).size() == 1);
}

TEST_CASE("legacy: six-digit station numbers are accepted",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.width = 6;
  const auto read = read_spec(spec);
  REQUIRE(read.value.table.size() == 3);
  CHECK(at(read.value.table, 1, 0, 1) == v(1.5));
}

TEST_CASE("legacy: the number widens beyond the padding",
          "[io][station_nc][legacy]") {
  CHECK(io::detail::legacy_variable_name("time_station_", 1, 4) ==
        "time_station_0001");
  CHECK(io::detail::legacy_variable_name("time_station_", 9999, 4) ==
        "time_station_9999");
  CHECK(io::detail::legacy_variable_name("time_station_", 10000, 4) ==
        "time_station_10000");
  CHECK(io::detail::legacy_variable_name("time_station_", 1, 6) ==
        "time_station_000001");
  CHECK(io::detail::legacy_variable_name("time_station_", 1234567, 6) ==
        "time_station_1234567");
}

TEST_CASE("legacy: a missing piece is named", "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.omit_x = true;
  auto e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::missing_variable);
  CHECK(e.subject == "stationXCoordinate");

  spec = three_stations();
  spec.omit_y = true;
  e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::missing_variable);
  CHECK(e.subject == "stationYCoordinate");

  spec = three_stations();
  spec.omit_names = true;
  e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::missing_variable);
  CHECK(e.subject == "stationName");

  spec = three_stations();
  spec.omit_time_of = 2;
  e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::missing_variable);
  CHECK(e.subject == "time_station_0002");
  CHECK(e.station == 1);

  spec = three_stations();
  spec.omit_data_of = 3;
  e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::missing_variable);
  CHECK(e.subject == "data_station_0003");
  CHECK(e.station == 2);
}

TEST_CASE("legacy: a time variable on another station's dimension is refused",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.wrong_dim_for = 1;  // time_station_0001 over stationLength_0002
  const auto e = error_of_spec(spec);
  CHECK(e.code == FormatErrc::dimension_mismatch);
  CHECK(e.subject == "time_station_0001");
}

// ---- opening ----------------------------------------------------------------

TEST_CASE("legacy B6: a missing file is an error, not a crash",
          "[io][station_nc][legacy][regression]") {
  const ScratchDir dir;
  const auto result = read_all(dir / "nope.nc");
  REQUIRE_FALSE(result.has_value());
  const auto* nc = std::get_if<io::NcError>(&result.error());
  REQUIRE(nc != nullptr);
  CHECK(nc->op == io::NcOp::open);
}

TEST_CASE("legacy: a CRMS file (dialect C) is not read",
          "[io][station_nc][legacy]") {
  const ScratchDir dir;
  const auto path = dir / "crms.nc";
  gen::make_crms_nc(path);
  CHECK(format_error_of(read_all(path)).code == FormatErrc::not_this_format);
  CHECK(format_error_of(io::inspect_station_netcdf(path, {})).code ==
        FormatErrc::not_this_format);
}

TEST_CASE("legacy: limits bound the station count and the samples",
          "[io][station_nc][legacy]") {
  const ScratchDir dir;
  const auto path = dir / "legacy.nc";
  auto spec = three_stations();
  spec.name_len = 1;  // a row costs one element
  for (auto& st : spec.stations) {
    st.name = "a";
    st.id = "a";
  }
  gen::make_legacy_nc(path, spec);
  io::ReadContext ctx;
  ctx.limits.max_elements = 2;  // three stations
  const auto result = read_all(path, ctx);
  REQUIRE_FALSE(result.has_value());
  const auto* nc = std::get_if<io::NcError>(&result.error());
  REQUIRE(nc != nullptr);
  CHECK(nc->status == io::NcStatus{io::WrapperFault::too_large});

  ctx.limits.max_elements = 5;  // six samples
  const auto result2 = read_all(path, ctx);
  REQUIRE_FALSE(result2.has_value());
  CHECK(std::get_if<io::NcError>(&result2.error()) != nullptr);

  ctx.limits.max_elements = 6;
  CHECK(read_all(path, ctx).has_value());
}

TEST_CASE("legacy: a stop request is honoured", "[io][station_nc][legacy]") {
  const ScratchDir dir;
  const auto path = dir / "legacy.nc";
  gen::make_legacy_nc(path, three_stations());
  io::ReadContext ctx;
  ctx.stop = io::StopToken{[] { return true; }};
  const auto result = read_all(path, ctx);
  REQUIRE_FALSE(result.has_value());
  CHECK(std::holds_alternative<io::Cancelled>(result.error()));
}

TEST_CASE("legacy: the EPSG may be any integer type",
          "[io][station_nc][legacy]") {
  for (const int type : {NC_BYTE, NC_SHORT, NC_INT, NC_INT64}) {
    auto spec = three_stations();
    spec.epsg = type == NC_BYTE ? 120 : 26915;  // 120: not a CRS PROJ has
    spec.epsg_type = type;
    if (type == NC_BYTE) {
      const auto e = error_of_spec(spec);
      CHECK(e.code == FormatErrc::unsupported_crs);
      continue;
    }
    spec.stations[0].x = 500000.0;
    spec.stations[0].y = 3250000.0;
    spec.stations[1].x = 500000.0;
    spec.stations[1].y = 3260000.0;
    spec.stations[2].x = 500000.0;
    spec.stations[2].y = 3270000.0;
    INFO(type);
    const auto read = read_spec(spec);
    CHECK(read.value.table.station(core::StationIndex{0}).native.has_value());
  }
}

TEST_CASE("legacy: an unsigned EPSG is not a code",
          "[io][station_nc][legacy]") {
  for (const int type : {NC_UBYTE, NC_USHORT, NC_UINT}) {
    auto spec = three_stations();
    spec.epsg_type = type;
    spec.epsg = type == NC_UBYTE ? 120 : 4326;
    const ScratchDir dir;
    gen::make_legacy_nc(dir / "legacy.nc", spec);
    const auto result = read_all(dir / "legacy.nc");
    INFO(type);
    REQUIRE_FALSE(result.has_value());
    const auto* nc = std::get_if<io::NcError>(&result.error());
    REQUIRE(nc != nullptr);
    CHECK(nc->status == io::NcStatus{io::WrapperFault::type_mismatch});
    CHECK(nc->object == "stationXCoordinate:HorizontalProjectionEPSG");
  }
}

TEST_CASE("legacy: text after the date of referenceDate is said",
          "[io][station_nc][legacy]") {
  const auto read_with = [](std::string reference) {
    auto spec = three_stations();
    spec.reference_date = std::move(reference);
    return read_spec(spec);
  };
  SECTION("a zone letter, a UTC offset") {
    const auto z = read_with("2000-01-01T00:00:00Z");
    const io::Warning w = warning_of(z.warnings, WarningCode::tz_assumed_utc);
    CHECK(w.subject == "Z");
    // The date itself is read.
    CHECK(z.value.table.times(core::StationIndex{0})[0] == ms(946684800000));
    const auto offset = read_with("2000-01-01 00:00:00 +02:00");
    CHECK(warning_of(offset.warnings, WarningCode::tz_assumed_utc).subject ==
          "+02:00");
  }
  SECTION("blanks, NULs and UTC after the date say nothing") {
    for (const char* reference :
         {"2000-01-01 00:00:00   ", "2000-01-01 00:00:00 UTC",
          "2000-01-01 00:00:00 gmt"}) {
      INFO(reference);
      CHECK(count_of(read_with(reference).warnings,
                     WarningCode::tz_assumed_utc) == 0);
    }
    CHECK(count_of(
              read_with(std::string{"2000-01-01 00:00:00\0junk", 24}).warnings,
              WarningCode::tz_assumed_utc) == 0);
  }
  SECTION("the same text on every station is one warning, counted") {
    const auto z = read_with("2000-01-01 00:00:00 local");
    CHECK(count_of(z.warnings, WarningCode::tz_assumed_utc) == 1);
    CHECK(warning_of(z.warnings, WarningCode::tz_assumed_utc).count == 3);
  }
}

TEST_CASE("legacy: the warnings come in the order SN 11 documents",
          "[io][station_nc][legacy]") {
  auto spec = three_stations();
  spec.epsg.reset();
  spec.timezone = "EST";
  spec.reference_date.reset();
  spec.stations[0].name = "Gr\xe9";
  spec.stations[1].name = "Gr\xe9";
  spec.stations[0].id = "X";
  spec.stations[1].id = "X";
  spec.stations[2].id = "";
  spec.stations[0].units = "frobs";
  spec.stations[1].units = "frobs";
  spec.stations[2].units = "frobs";
  spec.stations[0].seconds = {720, 0, 360};
  const auto read = read_spec(spec);
  std::vector<WarningCode> codes;
  for (const io::Warning& w : read.warnings) {
    if (codes.empty() or codes.back() != w.code) {
      codes.push_back(w.code);
    }
  }
  const std::vector<WarningCode> documented{
      WarningCode::legacy_dialect,
      WarningCode::crs_assumed,
      WarningCode::tz_assumed_utc,
      WarningCode::epoch_used,
      WarningCode::station_id_substituted,
      WarningCode::invalid_utf8_replaced,
      WarningCode::duplicate_station_id_renamed,
      WarningCode::unrecognized_unit,
      WarningCode::times_reordered};
  CHECK(codes == documented);
}
