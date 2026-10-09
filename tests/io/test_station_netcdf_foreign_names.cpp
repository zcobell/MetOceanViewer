// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Foreign CF station netCDF files: what the reader tolerates (packing, fills,
// valid ranges, strings, grid mappings), how it names the quantity of a
// variable (standard names that are exactly a registry quantity's,
// substitute names for the rest), which variables it skips, and the errors
// of a file whose structure is not a `timeSeries`.

#include <netcdf.h>

#include <algorithm>
#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "foreign_support.hpp"
#include "legacy_fixtures.hpp"
#include "model_fixtures.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/quantity.hpp"
#include "mov/io/error.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "nc_edit.hpp"
#include "station_nc_canonical.hpp"

namespace {

using namespace mov::test::foreign;  // NOLINT(google-build-using-namespace)
namespace core = mov::core;
namespace io = mov::io;
namespace station_nc = mov::test::station_nc;
using gen::Cdf;
using gen::CfKind;
using gen::CfSpec;
using io::FormatErrc;
using io::WarningCode;
using mov::test::ncgen::Editor;

/// 12 values of the 3 x 4 orthogonal matrix.
std::vector<double> matrix(double v) { return std::vector<double>(12, v); }

/// A spec whose file gets one more variable over (station, time).
CfSpec with(std::function<void(Cdf&, int, int)> customize) {
  CfSpec spec;
  spec.customize = std::move(customize);
  return spec;
}

std::vector<std::string> subjects(const std::vector<io::Warning>& warnings,
                                  WarningCode code) {
  std::vector<std::string> out;
  for (const io::Warning& w : warnings) {
    if (w.code == code) {
      out.push_back(w.subject);
    }
  }
  std::ranges::sort(out);
  return out;
}

}  // namespace

// ---- tolerated in foreign files (SN 12.5) -----------------------------------

TEST_CASE("foreign: float data with missing_value",
          "[io][station_nc][foreign]") {
  const auto read = read_spec(with([](Cdf& f, int station, int sample) {
    f.var("humid", NC_FLOAT, {station, sample});
    f.text("humid", "standard_name", "relative_humidity");
    f.text("humid", "units", "percent");
    f.num("humid", "missing_value", NC_FLOAT, {-1.0});
    f.put("humid", std::vector<double>{1, 2, -1, 4, 5, 6, 7, 8, 9, 10, 11, -1});
  }));
  const core::StationTable& t = read.value.table;
  REQUIRE(t.schema().size() == 2);
  CHECK(t.schema()[1].quantity() ==
        core::QuantityId{core::Quantity::relative_humidity});
  CHECK(t.schema()[1].unit() == unit("percent"));
  CHECK(at(t, 0, 1, 0) == v(1.0));
  CHECK(at(t, 0, 1, 2) == missing);
  CHECK(at(t, 2, 1, 3) == missing);
  CHECK(at(t, 2, 1, 2) == v(11.0));
}

TEST_CASE(
    "foreign: scale_factor and add_offset are applied after the fill test",
    "[io][station_nc][foreign]") {
  const auto read = read_spec(with([](Cdf& f, int station, int sample) {
    f.var("sal", NC_SHORT, {station, sample});
    f.num("sal", "_FillValue", NC_SHORT, {-32767.0});
    f.num("sal", "scale_factor", NC_DOUBLE, {0.5});
    f.num("sal", "add_offset", NC_DOUBLE, {10.0});
    f.put("sal", std::vector<double>{0, 2, -32767, 4, 0, 0, 0, 0, 0, 0, 0, 0});
  }));
  const core::StationTable& t = read.value.table;
  CHECK(at(t, 0, 1, 0) == v(10.0));
  CHECK(at(t, 0, 1, 1) == v(11.0));
  CHECK(at(t, 0, 1, 2) == missing);
  CHECK(at(t, 0, 1, 3) == v(12.0));
}

TEST_CASE("foreign: valid_range and the library's default fill",
          "[io][station_nc][foreign]") {
  const auto read = read_spec(with([](Cdf& f, int station, int sample) {
    f.var("depth", NC_DOUBLE, {station, sample});
    f.num("depth", "valid_range", NC_DOUBLE, {0.0, 100.0});
    f.put("depth",
          std::vector<double>{-5, 50, 150, 10, 1, 2, 3, 4, 5, 6, 7, 8});
    f.var("untouched", NC_FLOAT, {station, sample});  // never written: fill
  }));
  const core::StationTable& t = read.value.table;
  REQUIRE(t.schema().size() == 3);
  CHECK(at(t, 0, 1, 0) == missing);
  CHECK(at(t, 0, 1, 1) == v(50.0));
  CHECK(at(t, 0, 1, 2) == missing);
  CHECK(at(t, 0, 1, 3) == v(10.0));
  CHECK(at(t, 1, 2, 2) == missing);  // the default fill of float
}

TEST_CASE("foreign: NC_STRING and integer station ids",
          "[io][station_nc][foreign]") {
  CfSpec spec;
  spec.string_ids = true;
  auto read = read_spec(spec);
  CHECK(read.value.table.station(core::StationIndex{1}).id.view() == "B");
  CHECK(read.value.table.station(core::StationIndex{1}).name.empty());

  spec.string_ids = false;
  spec.integer_ids = true;
  spec.ids = {"101", "102", "103"};
  read = read_spec(spec);
  CHECK(read.value.table.station(core::StationIndex{0}).id.view() == "101");
  CHECK(read.value.table.station(core::StationIndex{2}).id.view() == "103");

  spec = ragged(CfKind::contiguous_ragged);
  spec.string_ids = true;
  check_ragged_table(read_spec(spec).value.table);
}

TEST_CASE("foreign: blanks around an id are trimmed, NUL padding is cut",
          "[io][station_nc][foreign]") {
  CfSpec spec;
  spec.id_width = 8;
  spec.ids = {"  A     ", std::string{"B\0zz", 4}, "C"};
  const auto read = read_spec(spec);
  CHECK(read.value.table.station(core::StationIndex{0}).id.view() == "A");
  CHECK(read.value.table.station(core::StationIndex{1}).id.view() == "B");
}

TEST_CASE("foreign: the platform name is the station name, else none",
          "[io][station_nc][foreign]") {
  const auto read = read_spec(with([](Cdf& f, int station, int /*sample*/) {
    const int wide = f.dim("name_width", 12);
    f.var("long_names", NC_CHAR, {station, wide});
    f.text("long_names", "standard_name", "platform_name");
    f.put_rows("long_names", 12, {"Alpha Pier", "Bravo Pier", ""});
  }));
  CHECK(read.value.table.station(core::StationIndex{0}).name.view() ==
        "Alpha Pier");
  CHECK(read.value.table.station(core::StationIndex{2}).id.view() == "C");
  CHECK(read.value.table.station(core::StationIndex{2}).name.empty());
}

TEST_CASE("foreign: bytes that are not UTF-8 in an id are replaced",
          "[io][station_nc][foreign]") {
  CfSpec spec;
  spec.ids = {"A\xe9", "B", "C"};
  const auto read = read_spec(spec);
  CHECK(read.value.table.station(core::StationIndex{0}).id.view() ==
        "A\xef\xbf\xbd");
  CHECK(count_of(read.warnings, WarningCode::invalid_utf8_replaced) == 1);
}

TEST_CASE("foreign: duplicate ids are made unique, an empty one is the index",
          "[io][station_nc][foreign]") {
  CfSpec spec;
  spec.ids = {"A", "A", "B"};
  const auto read = read_spec(spec);
  CHECK(read.value.table.station(core::StationIndex{0}).id.view() == "A");
  CHECK(read.value.table.station(core::StationIndex{1}).id.view() == "A#2");
  const io::Warning w =
      warning_of(read.warnings, WarningCode::duplicate_station_id_renamed);
  CHECK(w.subject == "A");
  CHECK(w.count == 1);

  // An id that is empty is the index of the station, said once for the file.
  spec.ids = {"A", "", "C"};
  const auto substituted = read_spec(spec);
  CHECK(substituted.value.table.station(core::StationIndex{1}).id.view() ==
        "1");
  CHECK(substituted.value.table.station(core::StationIndex{1}).name.empty());
  const io::Warning s =
      warning_of(substituted.warnings, WarningCode::station_id_substituted);
  CHECK(s.count == 1);
  CHECK(s.subject == "station_name");
  // ... and the index can collide with an id: that is a duplicate like any.
  spec.ids = {"1", "", "C"};
  const auto collide = read_spec(spec);
  CHECK(collide.value.table.station(core::StationIndex{1}).id.view() == "1#2");
}

TEST_CASE(
    "foreign: latitude and longitude by standard name when units are missing",
    "[io][station_nc][foreign]") {
  const auto read = read_spec(with([](Cdf& f, int /*station*/, int /*sample*/) {
    f.del_att("lat", "units");
    f.text("lat", "standard_name", "latitude");
    f.del_att("lon", "units");
    f.text("lon", "standard_name", "longitude");
  }));
  CHECK(read.value.table.station(core::StationIndex{2}).location.lat() ==
        Catch::Approx(30.0));
}

TEST_CASE("foreign: lon in 0..360 is wrapped", "[io][station_nc][foreign]") {
  const FixtureFile file{CfSpec{}};
  {
    Editor edit{file.path()};
    edit.put("lon", {0}, 270.0);
  }
  const auto read = file.read();
  CHECK(read.value.table.station(core::StationIndex{0}).location.lon() ==
        Catch::Approx(-90.0));
}

// ---- the grid mapping
// --------------------------------------------------------

namespace {

CfSpec mapped(std::function<void(Cdf&)> define, const char* name = "crs") {
  return with([define = std::move(define), name](Cdf& f, int /*station*/,
                                                 int /*sample*/) {
    f.var(name, NC_INT);
    define(f);
    f.text("temperature", "grid_mapping", name);
  });
}

}  // namespace

TEST_CASE("foreign: a geographic epsg_code keeps the native point",
          "[io][station_nc][foreign]") {
  const auto read = read_spec(mapped([](Cdf& f) {
    f.text("crs", "grid_mapping_name", "latitude_longitude");
    f.text("crs", "epsg_code", "EPSG:4269");
  }));
  const core::FileStation& s = read.value.table.station(core::StationIndex{0});
  if (not s.native) {
    FAIL("no native point");
    return;
  }
  CHECK(s.native->crs().code() == 4269);
  CHECK(count_of(read.warnings, WarningCode::crs_assumed) == 0);
}

TEST_CASE("foreign: epsg_code 4326 and the extended grid_mapping syntax",
          "[io][station_nc][foreign]") {
  auto read = read_spec(mapped([](Cdf& f) {
    f.text("crs", "grid_mapping_name", "latitude_longitude");
    f.text("crs", "epsg_code", "EPSG:4326");
  }));
  CHECK(count_of(read.warnings, WarningCode::crs_assumed) == 0);
  CHECK_FALSE(
      read.value.table.station(core::StationIndex{0}).native.has_value());
  read = read_spec(with([](Cdf& f, int /*station*/, int /*sample*/) {
    f.var("crs", NC_INT);
    f.text("crs", "grid_mapping_name", "latitude_longitude");
    f.text("crs", "epsg_code", "EPSG:4326");
    f.text("temperature", "grid_mapping", "crs: lat lon");
  }));
  CHECK(count_of(read.warnings, WarningCode::crs_assumed) == 0);
}

TEST_CASE(
    "foreign: a grid mapping the reader cannot use is assumed to be WGS 84",
    "[io][station_nc][foreign]") {
  SECTION("latitude_longitude without parameters") {
    const auto read = read_spec(mapped([](Cdf& f) {
      f.text("crs", "grid_mapping_name", "latitude_longitude");
    }));
    CHECK(warning_of(read.warnings, WarningCode::crs_assumed).subject == "crs");
  }
  SECTION("another ellipsoid") {
    const auto read = read_spec(mapped([](Cdf& f) {
      f.text("crs", "grid_mapping_name", "latitude_longitude");
      f.num("crs", "semi_major_axis", NC_DOUBLE, {6378137.0});
      f.num("crs", "inverse_flattening", NC_DOUBLE, {298.257222101});
    }));
    CHECK(warning_of(read.warnings, WarningCode::crs_assumed).subject == "crs");
  }
  SECTION("a projection") {
    const auto read = read_spec(mapped([](Cdf& f) {
      f.text("crs", "grid_mapping_name", "lambert_conformal_conic");
    }));
    CHECK(warning_of(read.warnings, WarningCode::crs_assumed).subject == "crs");
  }
  SECTION("a projected epsg_code: the positions are degrees all the same") {
    const auto read = read_spec(mapped([](Cdf& f) {
      f.text("crs", "grid_mapping_name", "transverse_mercator");
      f.text("crs", "epsg_code", "EPSG:26915");
    }));
    CHECK(warning_of(read.warnings, WarningCode::crs_assumed).subject ==
          "EPSG:26915");
    CHECK(read.value.table.station(core::StationIndex{0}).location.lat() ==
          Catch::Approx(29.0));
  }
  SECTION("a variable that does not exist") {
    const auto read =
        read_spec(with([](Cdf& f, int /*station*/, int /*sample*/) {
          f.text("temperature", "grid_mapping", "nothere");
        }));
    CHECK(warning_of(read.warnings, WarningCode::crs_assumed).subject ==
          "nothere");
  }
}

// ---- quantities -------------------------------------------------------------

TEST_CASE("foreign: standard names that are exactly a registry quantity",
          "[io][station_nc][foreign]") {
  const auto read = read_spec(with([](Cdf& f, int station, int sample) {
    const auto add = [&](const char* name, const char* standard,
                         const char* units) {
      f.var(name, NC_DOUBLE, {station, sample});
      f.text(name, "standard_name", standard);
      f.text(name, "units", units);
      f.put(name, matrix(1.0));
    };
    add("u", "eastward_sea_water_velocity", "m s-1");
    add("v", "northward_sea_water_velocity", "m s-1");
    add("ux", "sea_water_x_velocity", "m s-1");
    add("eta", "water_surface_height_above_reference_datum", "m");
    add("eta2", "water_surface_height_above_reference_datum", "m");
    add("wspd", "wind_speed", "knots");
    add("tair", "air_temperature", "K");
    add("p", "air_pressure", "mb");
    f.text("eta", "vertical_datum", "NAVD88");
  }));
  const core::StationTable& t = read.value.table;
  REQUIRE(t.schema().size() == 9);
  CHECK(t.schema()[0].quantity() == token("temperature"));
  // Eastward and northward components are the registry's; the
  // grid-relative x velocity is a generic quantity that keeps its name.
  CHECK(t.schema()[1].quantity() ==
        core::QuantityId{core::Quantity::current_u});
  CHECK(t.schema()[2].quantity() ==
        core::QuantityId{core::Quantity::current_v});
  CHECK(t.schema()[3].quantity() == token("ux", "sea_water_x_velocity"));
  // One water level per file: the second variable with the standard name is
  // generic, with the name it has.
  CHECK(t.schema()[4].quantity() ==
        core::QuantityId{core::Quantity::water_level});
  CHECK(t.schema()[4].datum() == core::VerticalDatum::navd88);
  CHECK(t.schema()[5].quantity() ==
        token("eta2", "water_surface_height_above_reference_datum"));
  CHECK(t.schema()[6].quantity() ==
        core::QuantityId{core::Quantity::wind_speed});
  CHECK(t.schema()[6].unit() == unit("knots"));  // converted by core, later
  // Kelvin converts to the canonical temperature unit.
  CHECK(t.schema()[7].quantity() ==
        core::QuantityId{core::Quantity::air_temperature});
  CHECK(t.schema()[7].unit() == unit("K"));
  CHECK(warning_of(read.warnings, WarningCode::unknown_quantity).subject ==
        "water_surface_height_above_reference_datum");
  CHECK(t.schema()[8].quantity() ==
        core::QuantityId{core::Quantity::air_pressure});
  CHECK(t.schema()[8].unit() == unit("mb"));
}

TEST_CASE("foreign: a variable name that is no token gets a substitute",
          "[io][station_nc][foreign]") {
  const auto read = read_spec(with([](Cdf& f, int station, int sample) {
    const auto add = [&](const char* name, const char* standard) {
      f.var(name, NC_DOUBLE, {station, sample});
      if (*standard != '\0') {
        f.text(name, "standard_name", standard);
      }
      f.text(name, "long_name", std::string{"label of "} + name);
      f.put(name, matrix(1.0));
    };
    add("Water Level (m)", "");
    add("1st", "");
    add("wind_gust", "something_else");  // a registry token, another meaning
    add("a_b", "");
    add("a b", "");
    add("a-b", "");
    add("cr\xc3\xa8me", "");
  }));
  const core::StationTable& t = read.value.table;
  REQUIRE(t.schema().size() == 8);
  CHECK(core::token(t.schema()[1].quantity()) == "Water_Level__m_");
  CHECK(core::token(t.schema()[2].quantity()) == "v1st");
  CHECK(t.schema()[3].quantity() == token("wind_gust_2", "something_else"));
  CHECK(core::token(t.schema()[4].quantity()) == "a_b");
  CHECK(core::token(t.schema()[5].quantity()) == "a_b_2");
  CHECK(core::token(t.schema()[6].quantity()) == "a_b_3");
  CHECK(core::token(t.schema()[7].quantity()) == "cr__me");
  // The label keeps the file's words.
  CHECK(t.schema()[1].label() == "label of Water Level (m)");
  // The warning names the variable; the schema has the token.
  CHECK(subjects(read.warnings, WarningCode::variable_renamed) ==
        std::vector<std::string>{"1st", "Water Level (m)", "a b", "a-b",
                                 "cr\xc3\xa8me", "wind_gust"});
}

TEST_CASE(
    "foreign: the substitute names are deterministic and the table writes",
    "[io][station_nc][foreign]") {
  const auto customize = [](Cdf& f, int station, int sample) {
    for (const char* name : {"x y", "x-y", "x.y"}) {
      f.var(name, NC_DOUBLE, {station, sample});
      f.put(name, matrix(2.0));
    }
  };
  const auto first = read_spec(with(customize));
  const auto second = read_spec(with(customize));
  CHECK(first.value.table == second.value.table);
  CHECK(first.warnings == second.warnings);
  // Every token is valid, so the v5 writer takes the table.
  CHECK(io::validate_station_netcdf(first.value.table, {}).has_value());
}

// ---- variables that are skipped ---------------------------------------------

TEST_CASE("foreign: variables that are not series of this file are skipped",
          "[io][station_nc][foreign]") {
  const auto read = read_spec(with([](Cdf& f, int station, int sample) {
    const int z = f.dim("z", 2);
    const int w = f.dim("w", 3);
    f.var("temperature_qc", NC_BYTE, {station, sample});
    f.text("temperature", "ancillary_variables", "temperature_qc");
    f.var("cube", NC_DOUBLE, {station, sample, z});
    f.var("big", NC_INT64, {station, sample});
    f.var("ub", NC_UBYTE, {station, sample});
    f.var("flagged", NC_BYTE, {station, sample});
    f.text("flagged", "_Unsigned", "true");
    f.var("tclock", NC_DOUBLE, {sample});
    f.var("tag", NC_CHAR, {station, w});  // text: not a series, no warning
    f.var("alt", NC_DOUBLE, {station});   // an instance variable: no warning
    f.text("alt", "positive", "up");
  }));
  const core::StationTable& t = read.value.table;
  REQUIRE(t.schema().size() == 1);
  CHECK(subjects(read.warnings, WarningCode::skipped_variable) ==
        std::vector<std::string>{"big", "cube", "flagged", "tclock", "ub"});
  // The quality variable has no scheme to read: ignored, and said so.
  CHECK(subjects(read.warnings, WarningCode::quality_flags_ignored) ==
        std::vector<std::string>{"temperature_qc"});
}

TEST_CASE(
    "foreign: bounds, coordinates and the grid mapping variable are not data",
    "[io][station_nc][foreign]") {
  const auto read = read_spec(with([](Cdf& f, int station, int sample) {
    const int nv = f.dim("nv", 2);
    f.var("time_bnds", NC_DOUBLE, {sample, nv});
    f.text("time", "bounds", "time_bnds");
    f.var("depth", NC_DOUBLE, {station, sample});
    f.text("temperature", "coordinates", "time lat lon depth");
    f.var("crs", NC_INT);
    f.text("temperature", "grid_mapping", "crs");
    f.text("crs", "grid_mapping_name", "latitude_longitude");
  }));
  REQUIRE(read.value.table.schema().size() == 1);
  CHECK(count_of(read.warnings, WarningCode::skipped_variable) == 0);
}

// ---- errors in the structure of the file ------------------------------------

TEST_CASE("foreign: what a timeSeries file must have",
          "[io][station_nc][foreign]") {
  SECTION("latitude") {
    const FixtureFile file{
        with([](Cdf& f, int, int) { f.del_att("lat", "units"); })};
    const auto e = file.error();
    CHECK(e.code == FormatErrc::missing_variable);
    CHECK(e.subject == "latitude");
    CHECK(file.inspect_error().code == FormatErrc::missing_variable);
  }
  SECTION("longitude") {
    const auto e = error_of_spec(
        with([](Cdf& f, int, int) { f.del_att("lon", "units"); }));
    CHECK(e.code == FormatErrc::missing_variable);
    CHECK(e.subject == "longitude");
  }
  SECTION("time") {
    const auto e = error_of_spec(with([](Cdf& f, int, int) {
      f.del_att("time", "units");
      f.del_att("time", "standard_name");
    }));
    CHECK(e.code == FormatErrc::missing_variable);
    CHECK(e.subject == "time");
  }
  SECTION("units of the time variable") {
    const FixtureFile file{
        with([](Cdf& f, int, int) { f.del_att("time", "units"); })};
    const auto e = file.error();
    CHECK(e.code == FormatErrc::missing_attribute);
    CHECK(e.subject == "time:units");
    CHECK(io::inspect_station_netcdf(file.path(), {}).has_value());
  }
  SECTION("one cf_role") {
    const auto e = error_of_spec(with(
        [](Cdf& f, int, int) { f.text("lat", "cf_role", "timeseries_id"); }));
    CHECK(e.code == FormatErrc::ambiguous_station_id);
    CHECK(e.subject == "station_name, lat");
  }
  SECTION("a data variable") {
    const auto e = error_of_spec(with(
        [](Cdf& f, int, int) { f.text("lat", "coordinates", "temperature"); }));
    CHECK(e.code == FormatErrc::no_data_variables);
  }
  SECTION("a supported calendar") {
    const FixtureFile file{
        with([](Cdf& f, int, int) { f.text("time", "calendar", "noleap"); })};
    const auto e = file.error();
    CHECK(e.code == FormatErrc::unsupported_calendar);
    CHECK(e.subject == "time:calendar");
  }
  SECTION("time units that parse") {
    const FixtureFile file{with([](Cdf& f, int, int) {
      f.text("time", "units", "furlongs since 2000-01-01");
      f.text("time", "standard_name", "time");
    })};
    const auto result =
        io::read_station_netcdf(file.path(), io::AllStations{}, {});
    REQUIRE_FALSE(result.has_value());
    const auto* parse = std::get_if<io::ParseError>(&result.error());
    REQUIRE(parse != nullptr);
    CHECK(parse->code() == io::ParseErrc::bad_time_units);
  }
}

TEST_CASE("foreign: a file that is not a timeSeries is not read as one",
          "[io][station_nc][foreign]") {
  const FixtureFile file{CfSpec{}};
  SECTION("another featureType") {
    {
      Editor edit{file.path()};
      edit.text("", "featureType", "profile");
    }
    CHECK(file.error().code == FormatErrc::not_this_format);
  }
  SECTION("no featureType") {
    {
      Editor edit{file.path()};
      edit.remove_att("", "featureType");
    }
    CHECK(file.error().code == FormatErrc::not_this_format);
  }
  SECTION("Conventions older than CF-1.6") {
    {
      Editor edit{file.path()};
      edit.text("", "Conventions", "CF-1.5");
    }
    CHECK(file.error().code == FormatErrc::not_this_format);
  }
  SECTION("CF-2 is another convention") {
    {
      Editor edit{file.path()};
      edit.text("", "Conventions", "CF-2.0");
    }
    CHECK(file.error().code == FormatErrc::not_this_format);
  }
  SECTION("CF-1.12 and a list of conventions") {
    {
      Editor edit{file.path()};
      edit.text("", "Conventions", "ACDD-1.3, CF-1.12");
    }
    const auto read = file.read();
    CHECK(origin_of(read.value).version ==
          io::CfVersion{.major = 1, .minor = 12});
  }
  SECTION("any case of timeSeries") {
    {
      Editor edit{file.path()};
      edit.text("", "featureType", "TimeSeries");
    }
    CHECK(file.read().value.table.size() == 3);
  }
}

// ---- a v5 file without its format attribute ---------------------------------

TEST_CASE("foreign: a v5 file whose format attribute is gone reads as CF",
          "[io][station_nc][foreign]") {
  for (const auto& canonical :
       {station_nc::orthogonal(), station_nc::incomplete()}) {
    const mov::test::ScratchDir dir;
    const auto path = station_nc::write(canonical, dir.path());
    {
      Editor edit{path};
      edit.remove_att("", "metoceanviewer_format");
    }
    const auto read = must_read(read_all(path));
    const auto& origin = origin_of(read.value);
    CHECK(origin.version == io::CfVersion{.major = 1, .minor = 11});
    CHECK(origin.layout == (canonical.name == "station_timeseries_orthogonal"
                                ? io::CfDsgLayout::orthogonal
                                : io::CfDsgLayout::incomplete));
    const core::StationTable& t = read.value.table;
    const core::StationTable& v5 = canonical.table;
    REQUIRE(t.size() == v5.size());
    for (std::size_t i = 0; i < t.size(); ++i) {
      CHECK(t.station(core::StationIndex{i}).id ==
            v5.station(core::StationIndex{i}).id);
      CHECK(t.times(core::StationIndex{i}).size() ==
            v5.times(core::StationIndex{i}).size());
    }
    CHECK(t.schema()[0].quantity() ==
          core::QuantityId{core::Quantity::water_level});
    CHECK(t.schema()[0].datum() == core::VerticalDatum::mllw);
    CHECK(t.schema()[1].quantity() ==
          core::QuantityId{core::Quantity::water_temperature});
    // The wet/dry status of v5 is a flag variable here, of a scheme that
    // masks nothing: no series, no warning.
    CHECK(count_of(read.warnings, WarningCode::skipped_variable) == 0);
    CHECK(count_of(read.warnings, WarningCode::quality_flags_ignored) == 0);
    CHECK(count_of(read.warnings, WarningCode::foreign_cf) == 1);
  }
}

// ---- limits and cancellation
// --------------------------------------------------

TEST_CASE("foreign: limits bound what is read", "[io][station_nc][foreign]") {
  for (const CfKind kind :
       {CfKind::orthogonal, CfKind::incomplete, CfKind::contiguous_ragged,
        CfKind::indexed_ragged}) {
    const CfSpec spec = kind == CfKind::orthogonal ? CfSpec{} : ragged(kind);
    const FixtureFile file{spec};
    io::ReadContext ctx;
    ctx.limits.max_elements = 4;
    const auto result = read_all(file.path(), ctx);
    INFO("layout " << static_cast<int>(kind));
    REQUIRE_FALSE(result.has_value());
    const auto* nc = std::get_if<io::NcError>(&result.error());
    REQUIRE(nc != nullptr);
    CHECK(nc->status == io::NcStatus{io::WrapperFault::too_large});

    io::ReadContext stop;
    stop.stop = io::StopToken{[] { return true; }};
    const auto cancelled = read_all(file.path(), stop);
    REQUIRE_FALSE(cancelled.has_value());
    CHECK(std::holds_alternative<io::Cancelled>(cancelled.error()));
  }
}

TEST_CASE("the station reader refuses the other netCDF kinds",
          "[io][station_nc][foreign]") {
  const mov::test::ScratchDir dir;
  gen::make_adcirc_nc(dir / "fort.61.nc", gen::AdcircNc{});
  gen::make_dflow_nc(dir / "his.nc", gen::DflowNc{});
  gen::make_crms_nc(dir / "crms.nc");
  for (const char* name : {"fort.61.nc", "his.nc", "crms.nc"}) {
    INFO(name);
    const auto read = read_all(dir / name);
    CHECK(format_error_of(read).code == FormatErrc::not_this_format);
    CHECK(format_error_of(io::inspect_station_netcdf(dir / name, {})).code ==
          FormatErrc::not_this_format);
  }
  CHECK(format_error_of(read_all(dir / "fort.61.nc")).subject == "model");
  CHECK(format_error_of(read_all(dir / "his.nc")).subject ==
        "station_x_coordinate");
}
