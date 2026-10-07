// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "adcirc_test_support.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/io/cf_time.hpp"
#include "mov/io/dflow.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "model_fixtures.hpp"
#include "model_nc_support.hpp"

namespace {

using mov::core::Epsg;
using mov::core::Missing;
using mov::core::Sample;
using mov::core::StationIndex;
using mov::core::StationSelection;
using mov::core::StationTable;
using mov::core::Time;
using mov::io::AtLayer;
using mov::io::Cancelled;
using mov::io::DflowChoice;
using mov::io::DflowDerived;
using mov::io::DflowRequest;
using mov::io::Flat;
using mov::io::FormatErrc;
using mov::io::Layer;
using mov::io::Layered;
using mov::io::ParseErrc;
using mov::io::ReadContext;
using mov::io::StopToken;
using mov::io::WarningCode;
using mov::test::format_error_of;
using mov::test::nc_error_in;
using mov::test::number;
using mov::test::parse_error_of;
using mov::test::sample;
using mov::test::samples_of;
using mov::test::warning_count;
using mov::test::ncgen::DataType;
using mov::test::ncgen::DflowNc;
using mov::test::ncgen::DflowVar;
using namespace std::string_literals;

constexpr double not_a_number = std::numeric_limits<double>::quiet_NaN();
constexpr double default_fill = 9.9692099683868690e+36;

// The ids of the layered variables of the tests: u(l) = l + 1, etc.
using Value = std::function<double(std::size_t t, std::size_t s, std::size_t l)>;

DflowVar variable(std::string name, int shape, std::string units,
                  Value value = {}) {
  DflowVar v;
  v.name = std::move(name);
  v.shape = shape;
  v.units = std::move(units);
  v.long_name = v.name + " (long)";
  v.value = std::move(value);
  return v;
}

DflowNc basic() {
  DflowNc spec;
  spec.vars = {variable("waterlevel", 0, "m")};
  return spec;
}

DflowRequest flat_request(const std::string& name, StationSelection stations,
                          Epsg crs = Epsg::wgs84()) {
  return {.choice = Flat{.source = *mov::io::nc::NcName::make(name),
                         .long_name = name},
          .stations = std::move(stations),
          .crs = crs};
}

DflowRequest derived_request(DflowDerived d, StationSelection stations,
                             std::optional<std::size_t> layers = std::nullopt,
                             std::size_t one_based = 1) {
  if (layers) {
    const Layered layered{.source = d, .long_name = "", .layers = *layers};
    return {.choice = AtLayer{.variable = layered,
                              .layer = Layer::make(layered, one_based).value()},
            .stations = std::move(stations),
            .crs = Epsg::wgs84()};
  }
  return {.choice = Flat{.source = d, .long_name = ""},
          .stations = std::move(stations),
          .crs = Epsg::wgs84()};
}

DflowRequest layered_request(const std::string& name, std::size_t layers,
                             std::size_t one_based, StationSelection stations) {
  const Layered layered{.source = *mov::io::nc::NcName::make(name),
                        .long_name = name,
                        .layers = layers};
  return {.choice = AtLayer{.variable = layered,
                            .layer = Layer::make(layered, one_based).value()},
          .stations = std::move(stations),
          .crs = Epsg::wgs84()};
}

StationSelection everything(std::size_t n) { return StationSelection::all(n); }

mov::io::Read<StationTable> read_ok(const std::filesystem::path& path,
                                    const DflowRequest& request,
                                    const ReadContext& ctx = {}) {
  auto read = mov::io::read_dflow(path, request, ctx);
  INFO((read ? std::string{} : mov::test::what(read.error())));
  REQUIRE(read.has_value());
  return *std::move(read);
}

mov::io::Read<mov::io::DflowCatalog> inspect_ok(
    const std::filesystem::path& path) {
  auto inspected = mov::io::inspect_dflow(path, Epsg::wgs84(), {});
  INFO((inspected ? std::string{} : mov::test::what(inspected.error())));
  REQUIRE(inspected.has_value());
  return *std::move(inspected);
}

Time utc(std::string_view text) {
  return mov::core::parse_utc_datetime(text).value();
}

// The source of a catalog entry as a string: the variable's name or the
// derived variable's token.
std::string name_of(const mov::io::DflowVariable& entry) {
  const mov::io::DflowSource& source = std::visit(
      [](const auto& v) -> const mov::io::DflowSource& { return v.source; },
      entry);
  if (const auto* name = std::get_if<mov::io::nc::NcName>(&source)) {
    return std::string{name->view()};
  }
  return std::string{mov::io::to_token(std::get<DflowDerived>(source))};
}

std::vector<std::string> names_of(const mov::io::DflowCatalog& catalog) {
  std::vector<std::string> names;
  names.reserve(catalog.variables.size());
  for (const auto& entry : catalog.variables) {
    names.push_back(name_of(entry));
  }
  return names;
}

}  // namespace

// ---- the catalog ------------------------------------------------------------------------

TEST_CASE("inspect: stations, variables, time", "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.steps = 5;
  spec.vars = {variable("waterlevel", 0, "m"), variable("x_velocity", 0, "m s-1"),
               variable("y_velocity", 0, "m s-1"), variable("windx", 0, "m s-1"),
               variable("windy", 0, "m s-1"), variable("bedlevel", 0, "m")};
  spec.station_names = {"Alpha", "Beta", "Gamma"};
  spec.x = {10.0, 20.0, 30.0};
  spec.y = {40.0, 41.0, 42.0};
  make_dflow_nc(dir / "his.nc", spec);
  const auto inspected = inspect_ok(dir / "his.nc");
  const auto& catalog = inspected.value;
  REQUIRE(catalog.stations.size() == 3);
  CHECK(catalog.stations[1].name.view() == "Beta");
  CHECK(catalog.stations[1].id.view() == "1");
  CHECK(catalog.stations[1].location.lon() == 20.0);
  CHECK(catalog.stations[1].location.lat() == 41.0);
  CHECK(catalog.stations[1].source == mov::core::DataSource::dflowfm);
  CHECK(catalog.times == 5);
  CHECK(catalog.time_units.unit == mov::io::CfTimeUnit::second);
  CHECK(catalog.time_units.epoch == utc("2001-01-01 00:00:00"));
  CHECK(catalog.calendar == mov::io::CfCalendar::standard);
  CHECK(names_of(catalog) ==
        std::vector<std::string>{"waterlevel", "x_velocity", "y_velocity",
                                 "windx", "windy", "bedlevel",
                                 "2D_current_speed", "2D_current_direction",
                                 "wind_speed", "wind_direction"});
  const auto* level = std::get_if<Flat>(catalog.variables.data());
  REQUIRE(level != nullptr);
  CHECK(level->long_name == "waterlevel (long)");
  CHECK(std::holds_alternative<Flat>(catalog.variables[6]));
}

TEST_CASE("inspect: a 3-D file offers layered variables, not those on laydimw",
          "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.layers = 3;
  spec.interfaces = 4;
  spec.vars = {variable("waterlevel", 0, "m"),
               variable("x_velocity", 1, "m s-1"),
               variable("y_velocity", 1, "m s-1"),
               variable("z_velocity", 1, "m s-1"),
               variable("zcoordinate_w", 2, "m"),
               variable("windx", 0, "m s-1"),
               variable("windy", 0, "m s-1")};
  make_dflow_nc(dir / "his.nc", spec);
  const auto inspected = inspect_ok(dir / "his.nc");
  CHECK(names_of(inspected.value) ==
        std::vector<std::string>{"waterlevel", "x_velocity", "y_velocity",
                                 "z_velocity", "windx", "windy",
                                 "3D_current_speed", "2D_current_speed",
                                 "2D_current_direction", "wind_speed",
                                 "wind_direction"});
  const auto& v = inspected.value.variables;
  CHECK(std::holds_alternative<Flat>(v[0]));
  REQUIRE(std::holds_alternative<Layered>(v[1]));
  CHECK(std::get<Layered>(v[1]).layers == 3);  // laydim's length, not laydimw's
  CHECK(std::holds_alternative<Layered>(v[6]));  // derived from layered inputs
  CHECK(std::holds_alternative<Layered>(v[7]));
  CHECK(std::holds_alternative<Flat>(v[9]));  // the wind has no layers (N16)
}

TEST_CASE("laydim and laydimw are told apart (B12)",
          "[io][dflow][regression][B12]") {
  // v4 decided "3-D" by laydimw and counted layers in laydim, both through a
  // map that gives 0 for a name it does not have.
  const mov::test::ScratchDir dir;
  SECTION("laydimw without laydim: no layered variable, no layer count") {
    DflowNc spec = basic();
    spec.layers = 0;
    spec.interfaces = 4;
    spec.vars = {variable("waterlevel", 0, "m"),
                 variable("zcoordinate_w", 2, "m")};
    make_dflow_nc(dir / "his.nc", spec);
    const auto inspected = inspect_ok(dir / "his.nc");
    CHECK(names_of(inspected.value) == std::vector<std::string>{"waterlevel"});
  }
  SECTION("laydim without laydimw: the layered variable is offered") {
    DflowNc spec = basic();
    spec.layers = 2;
    spec.interfaces = 0;
    spec.vars = {variable("x_velocity", 1, "m s-1")};
    make_dflow_nc(dir / "his.nc", spec);
    const auto inspected = inspect_ok(dir / "his.nc");
    REQUIRE(names_of(inspected.value).size() == 1);
    CHECK(std::get<Layered>(inspected.value.variables[0]).layers == 2);
  }
}

TEST_CASE("inspect: other files are not history files (B2)",
          "[io][dflow][regression][B2]") {
  // v4 set "initialized" when the error flag was set: inverted.
  const mov::test::ScratchDir dir;
  const mov::test::ncgen::AdcircNc adcirc;
  make_adcirc_nc(dir / "adcirc.nc", adcirc);
  const auto other = mov::io::inspect_dflow(dir / "adcirc.nc", Epsg::wgs84(), {});
  CHECK(format_error_of(other).code == FormatErrc::missing_dimension);
  CHECK(format_error_of(other).subject == "stations");

  make_dflow_nc(dir / "his.nc", basic());
  CHECK(mov::io::inspect_dflow(dir / "his.nc", Epsg::wgs84(), {}).has_value());

  mov::test::write_bytes(dir / "text.nc", "not netCDF");
  const auto text = mov::io::inspect_dflow(dir / "text.nc", Epsg::wgs84(), {});
  REQUIRE(not(text.has_value()));
  CHECK(nc_error_in(text.error()) != nullptr);
}

TEST_CASE("a missing dimension or variable is an error, not dimension 0 (B12)",
          "[io][dflow][regression][B12]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  const auto inspect = [&] {
    make_dflow_nc(dir / "his.nc", spec);
    return mov::io::inspect_dflow(dir / "his.nc", Epsg::wgs84(), {});
  };
  SECTION("no time dimension") {
    spec.omit_time_dim = true;
    const auto result = inspect();
    CHECK(format_error_of(result).code == FormatErrc::missing_dimension);
    CHECK(format_error_of(result).subject == "time");
  }
  SECTION("no stations dimension") {
    spec.omit_stations_dim = true;
    spec.omit_names = true;
    spec.omit_coordinates = true;
    spec.vars.clear();
    const auto result = inspect();
    CHECK(format_error_of(result).code == FormatErrc::missing_dimension);
    CHECK(format_error_of(result).subject == "stations");
  }
  SECTION("no time variable") {
    spec.omit_time_var = true;
    const auto result = inspect();
    CHECK(format_error_of(result).code == FormatErrc::missing_variable);
    CHECK(format_error_of(result).subject == "time");
  }
  SECTION("no coordinates") {
    spec.omit_coordinates = true;
    const auto result = inspect();
    CHECK(format_error_of(result).code == FormatErrc::missing_variable);
    CHECK(format_error_of(result).subject == "station_x_coordinate");
  }
  SECTION("no station names") {
    spec.omit_names = true;
    const auto result = inspect();
    CHECK(format_error_of(result).code == FormatErrc::missing_variable);
    CHECK(format_error_of(result).subject == "station_name");
  }
  SECTION("`stations` before `time` reads the same") {
    spec.stations_dim_first = true;
    spec.vars = {variable("waterlevel", 0, "m")};
    make_dflow_nc(dir / "his.nc", spec);
    const auto read = read_ok(dir / "his.nc",
                              flat_request("waterlevel", everything(3)));
    CHECK(samples_of(read.value, 2, 0)[3] == sample(20.75));
  }
}

// ---- stations ---------------------------------------------------------------------------

TEST_CASE("station names: the stride is name_len, never 200 (B11)",
          "[io][dflow][regression][B11]") {
  const mov::test::ScratchDir dir;
  struct Case {
    std::size_t name_len;
    char pad;
  };
  for (const Case c : {Case{.name_len = 64, .pad = ' '},
                       Case{.name_len = 20, .pad = '\0'},
                       Case{.name_len = 300, .pad = ' '},
                       Case{.name_len = 200, .pad = '\0'},
                       Case{.name_len = 9, .pad = ' '}}) {
    DflowNc spec = basic();
    spec.name_len = c.name_len;
    spec.pad = c.pad;
    spec.station_names = {"Station  One", "B"s, " C  D "s};
    if (c.name_len < 12) {
      spec.station_names[0] = "Station 1";
    }
    make_dflow_nc(dir / "his.nc", spec);
    const auto inspected = inspect_ok(dir / "his.nc");
    CHECK(inspected.value.stations[0].name.view() ==
          (c.name_len < 12 ? "Station 1" : "Station One"));
    CHECK(inspected.value.stations[1].name.view() == "B");
    CHECK(inspected.value.stations[2].name.view() == "C D");
  }
}

TEST_CASE("station names: junk after a NUL is dropped, bad UTF-8 replaced",
          "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.name_len = 20;
  spec.pad = '\0';
  spec.station_names = {"Alpha"s + '\0' + "junk", "caf\xE9"s, ""s};
  make_dflow_nc(dir / "his.nc", spec);
  const auto inspected = inspect_ok(dir / "his.nc");
  CHECK(inspected.value.stations[0].name.view() == "Alpha");
  CHECK(inspected.value.stations[1].name.view() == "caf\xEF\xBF\xBD");
  CHECK(inspected.value.stations[2].name.view() == "Station 2");
  CHECK(warning_count(inspected.warnings, WarningCode::invalid_utf8_replaced) ==
        1);
}

TEST_CASE("float coordinates are read as the float values (B4)",
          "[io][dflow][regression][B4]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.coordinate_type = DataType::float32;
  spec.x = {10.1, 20.2, 30.3};
  spec.y = {40.4, 41.5, 42.6};
  make_dflow_nc(dir / "his.nc", spec);
  const auto inspected = inspect_ok(dir / "his.nc");
  CHECK(inspected.value.stations[0].location.lon() ==
        static_cast<double>(10.1F));
  CHECK(inspected.value.stations[2].location.lat() ==
        static_cast<double>(42.6F));
}

TEST_CASE("coordinates in a projected CRS are converted",
          "[io][dflow][projection]") {
  mov::test::configure_projection_database();
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.x = {500000.0, 510000.0, 520000.0};
  spec.y = {3000000.0, 3010000.0, 3020000.0};
  make_dflow_nc(dir / "his.nc", spec);
  const auto crs = mov::test::epsg(32615);  // WGS 84 / UTM zone 15N
  const auto inspected = mov::io::inspect_dflow(dir / "his.nc", crs, {});
  REQUIRE(inspected.has_value());
  const auto& station = inspected->value.stations[0];
  CHECK(station.location.lon() == Catch::Approx(-93.0).margin(1e-7));
  const auto native = station.native;
  REQUIRE(native.has_value());
  if (native) {
    CHECK(native->crs() == crs);
  }
  CHECK(warning_count(inspected->warnings, WarningCode::crs_approximate) == 0);
}

// ---- time -------------------------------------------------------------------------------

TEST_CASE("time units: the unit, the epoch and the zone are parsed (B11)",
          "[io][dflow][regression][B11]") {
  // v4 cut the attribute at character 14 for 19 characters and added seconds.
  const mov::test::ScratchDir dir;
  struct Case {
    const char* units;
    std::vector<double> times;
    const char* first;  // the first time
  };
  const std::vector<Case> cases{
      {.units = "seconds since 2001-01-01 00:00:00",
       .times = {0, 600, 1200, 1800},
       .first = "2001-01-01 00:00:00"},
      {.units = "minutes since 2001-02-03 04:05:06 +00:00",
       .times = {1, 2, 3, 4},
       .first = "2001-02-03 04:06:06"},
      {.units = "hours since 1990-5-6",
       .times = {1, 2, 3, 4},
       .first = "1990-05-06 01:00:00"},
      {.units = "days since 2000-01-01T12:00:00Z",
       .times = {0.5, 1, 2, 3},
       .first = "2000-01-02 00:00:00"},
      {.units = "seconds since 2000-01-01 00:00:00 +00:00",
       .times = {60, 120, 180, 240},
       .first = "2000-01-01 00:01:00"},
      {.units = "seconds since 2000-01-01 06:00:00 -06:00",
       .times = {0, 1, 2, 3},
       .first = "2000-01-01 12:00:00"},
  };
  for (const Case& c : cases) {
    DflowNc spec = basic();
    spec.time_units = c.units;
    spec.times = c.times;
    make_dflow_nc(dir / "his.nc", spec);
    const auto read = read_ok(dir / "his.nc",
                              flat_request("waterlevel", everything(3)));
    CHECK(read.value.times(StationIndex{0})[0] == utc(c.first));
    CHECK(read.value.times(StationIndex{0}).size() == 4);
  }
}

TEST_CASE("time units that cannot be used", "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  SECTION("an unknown unit") {
    spec.time_units = "furlongs since 2000-01-01";
    make_dflow_nc(dir / "his.nc", spec);
    const auto result = mov::io::inspect_dflow(dir / "his.nc", Epsg::wgs84(), {});
    CHECK(parse_error_of(result).code() == ParseErrc::bad_time_units);
  }
  SECTION("no date") {
    spec.time_units = "seconds since";
    make_dflow_nc(dir / "his.nc", spec);
    const auto result = mov::io::inspect_dflow(dir / "his.nc", Epsg::wgs84(), {});
    REQUIRE(not(result.has_value()));
    CHECK(std::holds_alternative<mov::io::ParseError>(result.error()));
  }
  SECTION("no units attribute") {
    spec.time_units = "";  // the generator writes no attribute
    make_dflow_nc(dir / "his.nc", spec);
    const auto result = mov::io::inspect_dflow(dir / "his.nc", Epsg::wgs84(), {});
    CHECK(format_error_of(result).code == FormatErrc::missing_attribute);
    CHECK(format_error_of(result).subject == "time:units");
  }
  SECTION("a calendar that is not reproduced") {
    spec.calendar = "360_day";
    make_dflow_nc(dir / "his.nc", spec);
    const auto result = mov::io::inspect_dflow(dir / "his.nc", Epsg::wgs84(), {});
    CHECK(format_error_of(result).code == FormatErrc::unsupported_calendar);
  }
  SECTION("an epoch before the Gregorian reform") {
    spec.time_units = "seconds since 1500-01-01 00:00:00";
    make_dflow_nc(dir / "his.nc", spec);
    const auto result = mov::io::inspect_dflow(dir / "his.nc", Epsg::wgs84(), {});
    CHECK(format_error_of(result).code == FormatErrc::unsupported_calendar);
  }
  SECTION("times that are not increasing") {
    spec.times = {0, 600, 600, 1200};
    make_dflow_nc(dir / "his.nc", spec);
    const auto result = mov::io::read_dflow(
        dir / "his.nc", flat_request("waterlevel", everything(3)), {});
    CHECK(format_error_of(result).code == FormatErrc::time_not_increasing);
    CHECK(format_error_of(result).index == 2U);
  }
}

// ---- values ------------------------------------------------------------------------------

TEST_CASE("values: only the variable's own attributes make a value missing",
          "[io][dflow][regression][B12]") {
  // v4 compared with a hard-coded -999.0; and bed levels below -999 m are real.
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.vars = {variable("waterlevel", 0, "m",
                        [](std::size_t t, std::size_t s, std::size_t) {
                          if (s == 0) {
                            constexpr std::array<double, 4> column{
                                -999.0, -1500.0, not_a_number, 2.5};
                            return column.at(t);
                          }
                          return 1.0 + static_cast<double>(t);
                        })};
  make_dflow_nc(dir / "his.nc", spec);
  const auto read = read_ok(dir / "his.nc",
                            flat_request("waterlevel", everything(3)));
  // _FillValue is -999: that is missing; -1500 is a value (a deep bed level).
  CHECK(samples_of(read.value, 0, 0) ==
        std::vector<Sample>{Missing{}, sample(-1500.0), Missing{}, sample(2.5)});
  CHECK(warning_count(read.warnings, WarningCode::nonfinite_masked) == 1);
}

TEST_CASE("values: a -999 is a value when the fill is something else",
          "[io][dflow][regression][B12]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  DflowVar v = variable("waterlevel", 0, "m",
                        [](std::size_t t, std::size_t s, std::size_t) {
                          return (s == 0 and t == 1) ? -999.0
                                 : (s == 0 and t == 2) ? -9999.0
                                                       : 1.0;
                        });
  v.fill = -9999.0;
  spec.vars = {v};
  make_dflow_nc(dir / "his.nc", spec);
  const auto read = read_ok(dir / "his.nc",
                            flat_request("waterlevel", everything(3)));
  CHECK(samples_of(read.value, 0, 0) ==
        std::vector<Sample>{sample(1.0), sample(-999.0), Missing{}, sample(1.0)});
}

TEST_CASE("values: no _FillValue means the library's default fill",
          "[io][dflow]") {
  const mov::test::ScratchDir dir;
  for (const DataType type : {DataType::float64, DataType::float32}) {
    DflowNc spec = basic();
    DflowVar v = variable("waterlevel", 0, "m",
                          [](std::size_t t, std::size_t s, std::size_t) {
                            return (s == 1 and t == 0) ? default_fill : -999.0;
                          });
    v.fill = std::nullopt;
    v.type = type;
    spec.vars = {v};
    make_dflow_nc(dir / "his.nc", spec);
    const auto read = read_ok(dir / "his.nc",
                              flat_request("waterlevel", everything(3)));
    const auto column = samples_of(read.value, 1, 0);
    REQUIRE(column.size() == 4);
    CHECK(column.at(0) == Sample{Missing{}});
    CHECK(column.at(1) == sample(-999.0));
  }
}

TEST_CASE("float variables read as the float values (B4)",
          "[io][dflow][regression][B4]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  DflowVar v = variable("waterlevel", 0, "m",
                        [](std::size_t t, std::size_t s, std::size_t) {
                          return 0.1 * static_cast<double>(1 + t + s);
                        });
  v.type = DataType::float32;
  spec.vars = {v};
  make_dflow_nc(dir / "his.nc", spec);
  const auto read = read_ok(dir / "his.nc",
                            flat_request("waterlevel", everything(3)));
  CHECK(samples_of(read.value, 2, 0)[3] ==
        sample(static_cast<double>(static_cast<float>(0.1 * 6.0))));
}

TEST_CASE("the table: schema, selection order, shared axis", "[io][dflow]") {
  const mov::test::ScratchDir dir;
  make_dflow_nc(dir / "his.nc", basic());
  const auto read = read_ok(
      dir / "his.nc",
      flat_request("waterlevel",
                   StationSelection::make({2, 0}, 3).value()));
  const StationTable& table = read.value;
  REQUIRE(table.size() == 2);
  CHECK(table.station(StationIndex{0}).id.view() == "2");
  CHECK(samples_of(table, 0, 0)[1] == sample(20.25));
  CHECK(samples_of(table, 1, 0)[1] == sample(0.25));
  CHECK(table.single_axis());
  REQUIRE(table.schema().size() == 1);
  CHECK(mov::core::token(table.schema()[0].quantity()) == "water_level");
  CHECK(table.schema()[0].label() == "waterlevel (long)");
  CHECK(table.schema()[0].unit() ==
        std::optional<mov::core::Unit>{mov::core::LengthUnit::meter});
}

// ---- layers ----------------------------------------------------------------------------------

TEST_CASE("layers: counted from 1, checked against the variable (N16)",
          "[io][dflow][regression][N16]") {
  const Layered v{.source = *mov::io::nc::NcName::make("x_velocity"),
                  .long_name = "x velocity",
                  .layers = 3};
  CHECK(Layer::make(v, 1).value().zero_based() == 0);
  CHECK(Layer::make(v, 3).value().zero_based() == 2);
  const auto zero = Layer::make(v, 0);
  REQUIRE(not(zero.has_value()));
  CHECK(zero.error().code == FormatErrc::layer_out_of_range);
  CHECK(zero.error().index == 0U);
  const auto beyond = Layer::make(v, 4);
  REQUIRE(not(beyond.has_value()));
  CHECK(beyond.error().code == FormatErrc::layer_out_of_range);
  CHECK(beyond.error().index == 4U);
}

TEST_CASE("layers: each layer of a layered variable, by the layer asked for",
          "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.layers = 3;
  spec.interfaces = 4;
  spec.vars = {variable("x_velocity", 1, "m s-1",
                        [](std::size_t t, std::size_t s, std::size_t l) {
                          return 100.0 * static_cast<double>(l) +
                                 10.0 * static_cast<double>(s) +
                                 static_cast<double>(t);
                        }),
               variable("waterlevel", 0, "m")};
  make_dflow_nc(dir / "his.nc", spec);
  const auto path = dir / "his.nc";
  const auto first =
      read_ok(path, layered_request("x_velocity", 3, 1, everything(3)));
  const auto third =
      read_ok(path, layered_request("x_velocity", 3, 3, everything(3)));
  CHECK(samples_of(first.value, 1, 0)[2] == sample(12.0));
  CHECK(samples_of(third.value, 1, 0)[2] == sample(212.0));

  SECTION("a layer for a flat variable, and none for a layered one") {
    const auto at = mov::io::read_dflow(
        path, layered_request("waterlevel", 3, 1, everything(3)), {});
    CHECK(format_error_of(at).code == FormatErrc::dimension_mismatch);
    CHECK(format_error_of(at).subject == "waterlevel");
    const auto flat = mov::io::read_dflow(
        path, flat_request("x_velocity", everything(3)), {});
    CHECK(format_error_of(flat).code == FormatErrc::dimension_mismatch);
  }
  SECTION("a layer beyond the file's layers (a stale catalog)") {
    const auto stale = mov::io::read_dflow(
        path, layered_request("x_velocity", 5, 5, everything(3)), {});
    CHECK(format_error_of(stale).code == FormatErrc::layer_out_of_range);
    CHECK(format_error_of(stale).index == 5U);
  }
  SECTION("a variable the file does not have") {
    const auto missing = mov::io::read_dflow(
        path, flat_request("salinity", everything(3)), {});
    CHECK(format_error_of(missing).code == FormatErrc::missing_variable);
    CHECK(format_error_of(missing).subject == "salinity");
  }
}

// ---- derived variables ------------------------------------------------------------------------

TEST_CASE("derived: current speed and direction (3-4-5, and straight south)",
          "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.vars = {
      variable("x_velocity", 0, "m s-1",
               [](std::size_t t, std::size_t, std::size_t) {
                 constexpr std::array<double, 4> u{3.0, 0.0, -999.0, 0.0};
                 return u.at(t);
               }),
      variable("y_velocity", 0, "m s-1",
               [](std::size_t t, std::size_t, std::size_t) {
                 constexpr std::array<double, 4> v{4.0, -1.0, 1.0, 0.0};
                 return v.at(t);
               })};
  make_dflow_nc(dir / "his.nc", spec);
  const auto path = dir / "his.nc";

  const auto speed = read_ok(
      path, derived_request(DflowDerived::current_speed_2d, everything(3)));
  CHECK(samples_of(speed.value, 0, 0) ==
        std::vector<Sample>{sample(5.0), sample(1.0), Missing{}, sample(0.0)});
  CHECK(speed.value.schema()[0].label() == "current speed");
  CHECK(speed.value.schema()[0].unit() ==
        std::optional<mov::core::Unit>{mov::core::SpeedUnit::meter_per_second});

  const auto direction = read_ok(
      path,
      derived_request(DflowDerived::current_direction_2d, everything(3)));
  const auto angles = samples_of(direction.value, 1, 0);
  CHECK(number(angles[0]) == Catch::Approx(53.13010235415598).epsilon(1e-12));
  CHECK(number(angles[1]) == Catch::Approx(-90.0).epsilon(1e-12));
  CHECK(angles[2] == Sample{Missing{}});
  CHECK(angles[3] == Sample{Missing{}});  // no direction of a zero vector
}

TEST_CASE("derived: wind, and the 3-D speed at a layer", "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.layers = 2;
  spec.interfaces = 3;
  spec.vars = {
      variable("x_velocity", 1, "m s-1",
               [](std::size_t, std::size_t, std::size_t l) {
                 return l == 0 ? 2.0 : 20.0;
               }),
      variable("y_velocity", 1, "m s-1",
               [](std::size_t, std::size_t, std::size_t l) {
                 return l == 0 ? 3.0 : 30.0;
               }),
      variable("z_velocity", 1, "m s-1",
               [](std::size_t, std::size_t, std::size_t l) {
                 return l == 0 ? 6.0 : 60.0;
               }),
      variable("windx", 0, "m s-1"), variable("windy", 0, "m s-1")};
  make_dflow_nc(dir / "his.nc", spec);
  const auto path = dir / "his.nc";

  const auto speed3 = read_ok(
      path, derived_request(DflowDerived::current_speed_3d, everything(3), 2, 1));
  CHECK(number(samples_of(speed3.value, 0, 0)[0]) ==
        Catch::Approx(7.0).epsilon(1e-12));  // hypot(2, 3, 6)
  CHECK(speed3.value.schema()[0].label() == "3D current speed");
  const auto speed3b = read_ok(
      path, derived_request(DflowDerived::current_speed_3d, everything(3), 2, 2));
  CHECK(number(samples_of(speed3b.value, 0, 0)[0]) ==
        Catch::Approx(70.0).epsilon(1e-12));

  // N16: the wind of a 3-D file is flat, and is read without a layer.
  const auto wind = read_ok(
      path, derived_request(DflowDerived::wind_speed, everything(3)));
  CHECK(number(samples_of(wind.value, 1, 0)[0]) ==
        Catch::Approx(std::hypot(1000.0 * 3.0 + 10.0, 1000.0 * 4.0 + 10.0))
            .epsilon(1e-12));
  CHECK(mov::core::token(wind.value.schema()[0].quantity()) == "wind_speed");
  const auto bearing = read_ok(
      path, derived_request(DflowDerived::wind_direction, everything(3)));
  CHECK(bearing.value.schema()[0].label().starts_with("wind direction"));
}

TEST_CASE("derived: the schema exists for an empty selection",
          "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.vars = {variable("windx", 0, "m s-1"), variable("windy", 0, "m s-1")};
  make_dflow_nc(dir / "his.nc", spec);
  const auto read = read_ok(
      dir / "his.nc",
      derived_request(DflowDerived::wind_speed,
                      StationSelection::make({}, 3).value()));
  CHECK(read.value.size() == 0);
  REQUIRE(read.value.schema().size() == 1);
  CHECK(mov::core::token(read.value.schema()[0].quantity()) == "wind_speed");
}

TEST_CASE("derived: components in different units are refused",
          "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.vars = {variable("x_velocity", 0, "m s-1"),
               variable("y_velocity", 0, "knots")};
  make_dflow_nc(dir / "his.nc", spec);
  const auto read = mov::io::read_dflow(
      dir / "his.nc",
      derived_request(DflowDerived::current_speed_2d, everything(3)), {});
  CHECK(format_error_of(read).code == FormatErrc::noncanonical_unit);
}

TEST_CASE("derived: the vertical component in another unit is refused",
          "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.layers = 2;
  spec.vars = {variable("x_velocity", 1, "m s-1"),
               variable("y_velocity", 1, "m s-1"),
               variable("z_velocity", 1, "knots")};
  make_dflow_nc(dir / "his.nc", spec);
  const auto read = mov::io::read_dflow(
      dir / "his.nc",
      derived_request(DflowDerived::current_speed_3d, everything(3), 2, 1), {});
  CHECK(format_error_of(read).code == FormatErrc::noncanonical_unit);
  CHECK(format_error_of(read).subject == "x_velocity, y_velocity, z_velocity");
}

TEST_CASE("derived: a component the file lacks", "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.vars = {variable("x_velocity", 0, "m s-1")};
  make_dflow_nc(dir / "his.nc", spec);
  const auto read = mov::io::read_dflow(
      dir / "his.nc",
      derived_request(DflowDerived::current_speed_2d, everything(3)), {});
  CHECK(format_error_of(read).code == FormatErrc::missing_variable);
  CHECK(format_error_of(read).subject == "y_velocity");
}

TEST_CASE("derived tokens round trip", "[io][dflow]") {
  for (const DflowDerived d :
       {DflowDerived::current_speed_2d, DflowDerived::current_direction_2d,
        DflowDerived::current_speed_3d, DflowDerived::wind_speed,
        DflowDerived::wind_direction}) {
    CHECK(mov::io::parse_dflow_derived(mov::io::to_token(d)) == d);
  }
  CHECK(mov::io::to_token(DflowDerived::current_speed_2d) == "2D_current_speed");
  CHECK(not(mov::io::parse_dflow_derived("current_speed").has_value()));
}

// ---- quantities and units ------------------------------------------------------------------------

TEST_CASE("quantities: registry names, tokens, and the unknown",
          "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  const DflowVar no_unit = variable("waterlevel", 0, "");
  DflowVar salinity = variable("salinity", 0, "ppt");
  salinity.standard_name = "sea_water_salinity";
  const DflowVar odd = variable("sea-water temperature", 0, "degC");
  const DflowVar discharge =
      variable("cross_section_discharge", 0, "m3/s");
  spec.vars = {no_unit, salinity, odd, discharge};
  make_dflow_nc(dir / "his.nc", spec);
  const auto path = dir / "his.nc";

  const auto level = read_ok(path, flat_request("waterlevel", everything(3)));
  CHECK(mov::core::token(level.value.schema()[0].quantity()) == "water_level");
  // No unit attribute: the registry's own.
  CHECK(level.value.schema()[0].unit() ==
        std::optional<mov::core::Unit>{mov::core::LengthUnit::meter});
  INFO((level.warnings.empty() ? std::string_view{} : mov::io::to_token(level.warnings[0].code)));
  CHECK(level.warnings.empty());

  const auto sal = read_ok(path, flat_request("salinity", everything(3)));
  CHECK(mov::core::token(sal.value.schema()[0].quantity()) == "salinity");
  const auto* generic =
      std::get_if<mov::core::GenericQuantity>(&sal.value.schema()[0].quantity());
  REQUIRE(generic != nullptr);
  CHECK(generic->standard_name() == "sea_water_salinity");
  CHECK(warning_count(sal.warnings, WarningCode::unrecognized_unit) == 1);

  const auto strange = read_ok(
      path, flat_request("sea-water temperature", everything(3)));
  CHECK(mov::core::token(strange.value.schema()[0].quantity()) == "value");
  CHECK(warning_count(strange.warnings, WarningCode::unknown_quantity) == 1);

  const auto flow = read_ok(
      path, flat_request("cross_section_discharge", everything(3)));
  CHECK(flow.value.schema()[0].unit() ==
        std::optional<mov::core::Unit>{
            mov::core::DischargeUnit::cubic_meter_per_second});
  CHECK(flow.warnings.empty());
}

TEST_CASE("quantities: a name that is a registry token is that quantity",
          "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.vars = {variable("wave_height", 0, "m"),
               variable("air_pressure", 0, "hPa")};
  make_dflow_nc(dir / "his.nc", spec);
  const auto read = read_ok(dir / "his.nc",
                            flat_request("wave_height", everything(3)));
  CHECK(mov::core::token(read.value.schema()[0].quantity()) == "wave_height");
  CHECK(read.value.schema()[0].unit() ==
        std::optional<mov::core::Unit>{mov::core::LengthUnit::meter});
  const auto pressure = read_ok(dir / "his.nc",
                                flat_request("air_pressure", everything(3)));
  CHECK(pressure.value.schema()[0].unit() ==
        std::optional<mov::core::Unit>{mov::core::PressureUnit::hectopascal});
}

TEST_CASE("attributes that are not text are not labels or units",
          "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  DflowVar odd = variable("waterlevel", 0, "m");
  odd.numeric_attributes = true;
  spec.vars = {odd};
  make_dflow_nc(dir / "his.nc", spec);
  const auto read = read_ok(dir / "his.nc",
                            flat_request("waterlevel", everything(3)));
  // The label is the name; the unit is the registry's own.
  CHECK(read.value.schema()[0].label() == "waterlevel");
  CHECK(read.value.schema()[0].unit() ==
        std::optional<mov::core::Unit>{mov::core::LengthUnit::meter});
  const auto inspected = inspect_ok(dir / "his.nc");
  CHECK(std::get<Flat>(inspected.value.variables[0]).long_name == "waterlevel");
}

TEST_CASE("coordinates that cannot be used", "[io][dflow]") {
  mov::test::configure_projection_database();
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  SECTION("64-bit integer coordinates are refused like 64-bit data") {
    spec.coordinate_type = DataType::int64;
    make_dflow_nc(dir / "his.nc", spec);
    const auto result = mov::io::inspect_dflow(dir / "his.nc", Epsg::wgs84(), {});
    REQUIRE(not(result.has_value()));
    const auto* error = nc_error_in(result.error());
    REQUIRE(error != nullptr);
    CHECK(error->status ==
          mov::io::NcStatus{mov::io::WrapperFault::type_mismatch});
  }
  SECTION("a point that PROJ cannot transform names its station") {
    spec.x = {500000.0, 1e15, 500000.0};
    spec.y = {3000000.0, 3000000.0, 3000000.0};
    make_dflow_nc(dir / "his.nc", spec);
    const auto result =
        mov::io::inspect_dflow(dir / "his.nc", mov::test::epsg(32615), {});
    CHECK(format_error_of(result).code == FormatErrc::bad_coordinates);
    CHECK(format_error_of(result).station == 1U);
  }
}

TEST_CASE("inspect: a variable of a type that is not read is skipped",
          "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  DflowVar counts = variable("counts", 0, "1");
  counts.type = DataType::int64;
  counts.fill = std::nullopt;
  spec.vars = {variable("waterlevel", 0, "m"), counts};
  make_dflow_nc(dir / "his.nc", spec);
  const auto inspected = inspect_ok(dir / "his.nc");
  CHECK(names_of(inspected.value) == std::vector<std::string>{"waterlevel"});
  CHECK(warning_count(inspected.warnings, WarningCode::skipped_variable) == 1);
}

// ---- selection, limits, cancellation -----------------------------------------------------------------

TEST_CASE("a selection for another station count", "[io][dflow]") {
  const mov::test::ScratchDir dir;
  make_dflow_nc(dir / "his.nc", basic());
  const auto read = mov::io::read_dflow(
      dir / "his.nc", flat_request("waterlevel", everything(5)), {});
  CHECK(format_error_of(read).code == FormatErrc::station_count_mismatch);
}

TEST_CASE("limits and cancellation", "[io][dflow]") {
  const mov::test::ScratchDir dir;
  DflowNc spec = basic();
  spec.steps = 100;
  make_dflow_nc(dir / "his.nc", spec);
  const auto request = flat_request("waterlevel", everything(3));
  SECTION("stopped") {
    ReadContext ctx;
    ctx.stop = StopToken{[] { return true; }};
    const auto read = mov::io::read_dflow(dir / "his.nc", request, ctx);
    REQUIRE(not(read.has_value()));
    CHECK(std::holds_alternative<Cancelled>(read.error()));
  }
  SECTION("too many samples") {
    ReadContext ctx;
    ctx.limits.max_elements = 299;
    const auto read = mov::io::read_dflow(dir / "his.nc", request, ctx);
    REQUIRE(not(read.has_value()));
    const auto* error = nc_error_in(read.error());
    REQUIRE(error != nullptr);
    CHECK(error->status ==
          mov::io::NcStatus{mov::io::WrapperFault::too_large});
  }
  SECTION("blocks of any size give the same table") {
    ReadContext small;
    small.limits.slab_elements = 7;
    CHECK(read_ok(dir / "his.nc", request, small).value ==
          read_ok(dir / "his.nc", request).value);
  }
}

TEST_CASE("the readers close their file: it can be opened again at once",
          "[io][dflow]") {
  const mov::test::ScratchDir dir;
  make_dflow_nc(dir / "his.nc", basic());
  const auto path = dir / "his.nc";
  for (int round = 0; round < 3; ++round) {
    CHECK(mov::io::inspect_dflow(path, Epsg::wgs84(), {}).has_value());
    CHECK(mov::io::read_dflow(path, flat_request("waterlevel", everything(3)), {})
              .has_value());
    const auto absent = mov::io::read_dflow(
        path, flat_request("salinity", everything(3)), {});
    CHECK(not absent.has_value());
    CHECK(mov::io::nc::File::open(path, {}).has_value());
  }
}
