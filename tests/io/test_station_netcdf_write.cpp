// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The v5 station netCDF writer (docs/station-netcdf.md, SN 3-10, 12.8): what
// it writes, checked with raw netCDF-C, and what it refuses.

#include <netcdf.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <cfloat>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/station.hpp"
#include "mov/io/detail/atomic_file.hpp"
#include "mov/io/error.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "mov/test/scratch_dir.hpp"
#include "station_nc_support.hpp"

namespace {

using namespace mov::test::snc;  // NOLINT(google-build-using-namespace)
using mov::core::Quantity;
using mov::core::VerticalDatum;
using mov::io::FormatErrc;
using mov::io::StationNcLayout;
using mov::io::WarningCode;
using mov::test::ScratchDir;
namespace station_nc = mov::test::station_nc;
namespace core = mov::core;
using core_axis = mov::core::TimeAxis;

constexpr double fill = NC_FILL_DOUBLE;

TEST_CASE("the orthogonal file: format, dimensions, chunks and deflate",
          "[io][station_nc][write]") {
  const ScratchDir dir;
  const auto c = station_nc::orthogonal();
  const auto path = dir / "o.nc";
  const auto written = must_write(path, c.table, c.options);
  CHECK(written.layout == StationNcLayout::orthogonal);
  CHECK(written.warnings.empty());

  const Inspect f{path};
  int format = 0;
  REQUIRE(nc_inq_format(f.id(), &format) == NC_NOERR);
  CHECK(format == NC_FORMAT_NETCDF4);
  int nunlim = -1;
  REQUIRE(nc_inq_unlimdims(f.id(), &nunlim, nullptr) == NC_NOERR);
  CHECK(nunlim == 0);
  CHECK(f.dim("station") == 2);
  CHECK(f.dim("time") == 4);
  CHECK(not f.has_dim("obs"));
  CHECK(not f.has_var("obs_count"));
  CHECK(f.dim("station_id_len") == 7);
  CHECK(f.dim("station_name_len") == 28);
  CHECK(f.dim("station_provider_len") == 10);

  CHECK(f.dims_of("time") == std::vector<std::string>{"time"});
  CHECK(not(f.att("time", "_FillValue")));
  CHECK(not f.chunks("time"));  // the coordinate is contiguous
  CHECK(not f.chunks("lat"));
  CHECK(not f.chunks("station_id"));
  CHECK(f.chunks("water_level") == std::vector<std::size_t>{2, 4});
  CHECK(f.chunks("water_level_status") == std::vector<std::size_t>{2, 4});
  CHECK(f.deflate("water_level") == std::vector<int>{1, 1, 2});
  CHECK(f.deflate("water_level_status") == std::vector<int>{1, 1, 2});
  CHECK(f.deflate("lat") == std::vector<int>{0, 0, 0});

  CHECK(f.doubles("time") == std::vector<double>{1700000000000, 1700000360000,
                                                 1700000720000, 1700001080000});
  CHECK(f.doubles("water_level") ==
        std::vector<double>{0.5, 0.6, 0.7, 0.8, 1, fill, 1.2, 1.3});
  CHECK(f.bytes("water_level_status") ==
        std::vector<signed char>{1, 1, 1, 1, 1, 0, 1, 1});
  CHECK(f.doubles("water_temperature") ==
        std::vector<double>{20, 21, 22, 23.5, fill, fill, fill, fill});
}

TEST_CASE("the incomplete file: obs, obs_count and fill padding",
          "[io][station_nc][write]") {
  const ScratchDir dir;
  const auto c = station_nc::incomplete();
  const auto path = dir / "i.nc";
  CHECK(must_write(path, c.table, c.options).layout ==
        StationNcLayout::incomplete);

  const Inspect f{path};
  CHECK(f.dim("obs") == 5);
  CHECK(not f.has_dim("time"));
  CHECK(f.dims_of("time") == std::vector<std::string>{"station", "obs"});
  CHECK(f.type("time") == NC_DOUBLE);
  CHECK(f.att("time", "_FillValue") == std::pair{NC_DOUBLE, std::size_t{1}});
  CHECK(f.chunks("time") == std::vector<std::size_t>{2, 5});
  CHECK(f.deflate("time") == std::vector<int>{1, 1, 2});
  CHECK(f.type("obs_count") == NC_INT);
  CHECK(f.ints("obs_count") == std::vector<int>{3, 5});
  CHECK(f.doubles("time") ==
        std::vector<double>{1700000000000, 1700000360000, 1700000720000, fill,
                            fill, 1700000000000, 1700000900000, 1700001800000,
                            1700002700000, 1700003600000});
  CHECK(f.doubles("water_level") ==
        std::vector<double>{0.5, 0.6, 0.7, fill, fill, 1, fill, 1.2, 1.3, 1.4});
  CHECK(f.bytes("water_level_status") ==
        std::vector<signed char>{1, 1, 1, -128, -128, 1, 0, 1, 1, 1});
}

TEST_CASE("attributes: exact text, byte lengths and types (B8, B15)",
          "[io][station_nc][write][regression][B8][B15]") {
  const ScratchDir dir;
  const auto c = station_nc::incomplete();
  const auto path = dir / "i.nc";
  static_cast<void>(must_write(path, c.table, c.options));
  const Inspect f{path};

  CHECK(f.text("", "Conventions") == "CF-1.11");
  CHECK(f.text("", "featureType") == "timeSeries");
  CHECK(f.text("", "title") == "Example");
  CHECK(f.att("", "title") == std::pair{NC_CHAR, std::size_t{7}});
  CHECK(f.text("", "institution") == "X");
  CHECK(f.text("", "source") == "NOAA CO-OPS API");
  CHECK(not(f.att("", "references")));
  CHECK(not(f.att("", "comment")));
  CHECK(f.text("", "date_created") == "2026-10-06T12:00:00Z");
  const std::string history = f.text("", "history").value_or("");
  CHECK(
      history.starts_with("2026-10-06T12:00:00Z: created by MetOceanViewer "));
  CHECK(history.ends_with(" (station-timeseries 1.0)"));
  CHECK(f.text("", "metoceanviewer_format") == "station-timeseries");
  CHECK(f.text("", "metoceanviewer_format_version") == "1.0");

  CHECK(f.text("time", "units") == "milliseconds since 1970-01-01 00:00:00");
  CHECK(f.text("time", "calendar") == "proleptic_gregorian");
  CHECK(f.text("time", "axis") == "T");
  CHECK(f.text("station_id", "cf_role") == "timeseries_id");
  CHECK(f.text("station_id", "_Encoding") == "utf-8");
  CHECK(f.text("lat", "units") == "degrees_north");
  CHECK(f.text("lon", "units") == "degrees_east");
  CHECK(f.type("crs") == NC_INT);
  CHECK(f.dims_of("crs").empty());
  CHECK(f.text("crs", "grid_mapping_name") == "latitude_longitude");
  CHECK(f.text("crs", "epsg_code") == "EPSG:4326");
  const std::string wkt = f.text("crs", "crs_wkt").value_or("");
  CHECK(wkt.starts_with(R"(GEOGCRS["WGS 84",)"));
  CHECK(wkt.ends_with(R"(ID["EPSG",4326]])"));

  for (const char* var : {"water_level", "water_temperature"}) {
    CHECK(f.att(var, "_FillValue") == std::pair{NC_DOUBLE, std::size_t{1}});
    CHECK(f.text(var, "coordinates") == "time lat lon station_id station_name");
    CHECK(f.text(var, "grid_mapping") == "crs");
  }
  CHECK(f.text("water_level", "standard_name") ==
        "water_surface_height_above_reference_datum");
  CHECK(f.text("water_level", "long_name") == "water level");
  CHECK(f.text("water_level", "units") == "m");
  CHECK(f.text("water_level", "vertical_datum") == "MLLW");
  CHECK(f.text("water_level", "ancillary_variables") == "water_level_status");
  CHECK(not(f.att("water_level", "units_metadata")));
  CHECK(f.text("water_temperature", "units") == "degC");
  CHECK(f.text("water_temperature", "units_metadata") ==
        "temperature: on_scale");
  CHECK(not(f.att("water_temperature", "vertical_datum")));
  CHECK(not(f.att("water_temperature", "ancillary_variables")));
  CHECK(not f.has_var("water_temperature_status"));

  CHECK(f.type("water_level_status") == NC_BYTE);
  CHECK(f.att("water_level_status", "_FillValue") ==
        std::pair{NC_BYTE, std::size_t{1}});
  CHECK(f.att("water_level_status", "flag_values") ==
        std::pair{NC_BYTE, std::size_t{2}});
  CHECK(f.att("water_level_status", "valid_range") ==
        std::pair{NC_BYTE, std::size_t{2}});
  CHECK(f.text("water_level_status", "flag_meanings") == "dry wet");
  CHECK(f.text("water_level_status", "standard_name") == "status_flag");
  CHECK(f.text("water_level_status", "long_name") ==
        "water level wet/dry status");
}

TEST_CASE("layout: orthogonal iff every station has the same non-empty times",
          "[io][station_nc][write][layout]") {
  const ScratchDir dir;
  const auto layout_of = [&dir](const StationTable& table) {
    CHECK(mov::io::choose_layout(table) ==
          must_write(dir / "l.nc", table).layout);
    return mov::io::choose_layout(table);
  };
  const core_axis axis{t(0), t(1), t(2)};
  SECTION("one station") {
    CHECK(layout_of(water_levels({axis}, {{v(1), v(2), v(3)}})) ==
          StationNcLayout::orthogonal);
  }
  SECTION("identical axes, stations in either order") {
    CHECK(layout_of(water_levels({axis, axis},
                                 {{v(1), v(2), v(3)}, {v(4), v(5), v(6)}})) ==
          StationNcLayout::orthogonal);
  }
  SECTION("one time 1 ms later") {
    const core_axis later{t(0), t(1), t(2) + std::chrono::milliseconds{1}};
    CHECK(layout_of(water_levels({axis, later},
                                 {{v(1), v(2), v(3)}, {v(4), v(5), v(6)}})) ==
          StationNcLayout::incomplete);
  }
  SECTION("same length, other times") {
    CHECK(layout_of(water_levels({axis, {t(3), t(4), t(5)}},
                                 {{v(1), v(2), v(3)}, {v(4), v(5), v(6)}})) ==
          StationNcLayout::incomplete);
  }
  SECTION("a station without samples") {
    CHECK(layout_of(water_levels({axis, {}}, {{v(1), v(2), v(3)}, {}})) ==
          StationNcLayout::incomplete);
    const Inspect f{dir / "l.nc"};
    CHECK(f.ints("obs_count") == std::vector<int>{3, 0});
    CHECK(f.doubles("water_level") ==
          std::vector<double>{1, 2, 3, fill, fill, fill});
  }
}

TEST_CASE("chunks: (r, c) with c = min(n, 65536) and r = clamp(65536 / c)",
          "[io][station_nc][write][chunks]") {
  const ScratchDir dir;
  SECTION("a long axis: one station per chunk row, 65536 columns") {
    core_axis axis;
    core::Column column;
    for (std::int64_t k = 0; k < 70000; ++k) {
      axis.push_back(t(k));
      column.push_back(v(static_cast<double>(k)));
    }
    static_cast<void>(must_write(dir / "long.nc",
                                 water_levels({axis, axis}, {column, column})));
    const Inspect f{dir / "long.nc"};
    CHECK(f.chunks("water_level") == std::vector<std::size_t>{1, 65536});
  }
  SECTION("many stations with short axes: rows clamped to the stations") {
    const std::vector<core_axis> axes(300, core_axis{t(0), t(1)});
    const std::vector<core::Column> columns(300, core::Column{v(1), v(2)});
    static_cast<void>(must_write(dir / "wide.nc", water_levels(axes, columns)));
    const Inspect f{dir / "wide.nc"};
    CHECK(f.chunks("water_level") == std::vector<std::size_t>{300, 2});
  }
  SECTION("the rows of 65536 / c") {
    std::vector<core_axis> axes(40, core_axis{});
    std::vector<core::Column> columns(40, core::Column{});
    for (std::int64_t k = 0; k < 2000; ++k) {
      axes[0].push_back(t(k));
      columns[0].push_back(v(1));
    }
    static_cast<void>(must_write(dir / "rows.nc", water_levels(axes, columns)));
    const Inspect f{dir / "rows.nc"};
    CHECK(f.chunks("water_level") == std::vector<std::size_t>{32, 2000});
    CHECK(f.chunks("time") == std::vector<std::size_t>{32, 2000});
  }
}

TEST_CASE("the status variable exists iff a sample of the column is Dry",
          "[io][station_nc][write][wetdry]") {
  const ScratchDir dir;
  const core_axis axis{t(0), t(1)};
  static_cast<void>(
      must_write(dir / "wet.nc", water_levels({axis}, {{v(1), missing}})));
  const Inspect wet{dir / "wet.nc"};
  CHECK(not wet.has_var("water_level_status"));
  CHECK(not(wet.att("water_level", "ancillary_variables")));

  // Dry on a quantity that is not a water level is legal too (C1).
  const auto air =
      table({{.meta = meta(Quantity::air_temperature, "air", "degC"),
              .per_station = {{dry, v(3)}}}},
            {axis}, {{.station = station("A"), .axis = 0}});
  static_cast<void>(must_write(dir / "air.nc", air));
  const Inspect f{dir / "air.nc"};
  CHECK(f.bytes("air_temperature_status") == std::vector<signed char>{0, 1});
  CHECK(f.doubles("air_temperature") == std::vector<double>{fill, 3});
  CHECK(f.text("air_temperature_status", "long_name") == "air wet/dry status");
}

TEST_CASE("units: registry columns are converted to the canonical unit",
          "[io][station_nc][write][units]") {
  const ScratchDir dir;
  const core_axis axis{t(0)};
  const auto one = [&](core::SeriesMeta m, double x) {
    return table({{.meta = std::move(m), .per_station = {{v(x)}}}}, {axis},
                 {{.station = station("A"), .axis = 0}});
  };
  struct Case {
    core::SeriesMeta meta;
    double in;
    const char* var;
    const char* unit;
    double out;
  };
  const std::vector<Case> cases{
      {.meta = meta(Quantity::water_level, "wl", "ft", VerticalDatum::navd88),
       .in = 10.0,
       .var = "water_level",
       .unit = "m",
       .out = 3.048},
      {.meta = meta(Quantity::air_pressure, "p", "mH2O"),
       .in = 1.0,
       .var = "air_pressure",
       .unit = "hPa",
       .out = 98.0665},
      {.meta = meta(Quantity::discharge, "q", "ft3/s"),
       .in = 100.0,
       .var = "discharge",
       .unit = "m3 s-1",
       .out = 2.8316846592},
      {.meta = meta(Quantity::air_temperature, "t", "degF"),
       .in = 212.0,
       .var = "air_temperature",
       .unit = "degC",
       .out = 100.0},
      {.meta = meta(Quantity::wind_speed, "w", "kt"),
       .in = 1.0,
       .var = "wind_speed",
       .unit = "m s-1",
       .out = 1852.0 / 3600.0},
  };
  for (const Case& c : cases) {
    CAPTURE(c.var);
    const auto written = must_write(dir / "u.nc", one(c.meta, c.in));
    CHECK(written.warnings ==
          std::vector<mov::io::Warning>{{.code = WarningCode::unit_converted,
                                         .subject = c.var,
                                         .count = 1}});
    const Inspect f{dir / "u.nc"};
    CHECK(f.text(c.var, "units") == c.unit);
    CHECK_THAT(f.doubles(c.var).at(0),
               Catch::Matchers::WithinRel(c.out, 1e-12));
  }

  SECTION("a canonical unit is written as it is, bit for bit") {
    const auto written = must_write(
        dir / "c.nc", one(meta(Quantity::water_level, "wl", "m"), -0.0));
    CHECK(written.warnings.empty());
    const Inspect f{dir / "c.nc"};
    CHECK(std::signbit(f.doubles("water_level").at(0)));
  }
}

TEST_CASE("units: a value the conversion overflows is written as missing",
          "[io][station_nc][write][units]") {
  const ScratchDir dir;
  const auto big =
      table({{.meta = meta(Quantity::water_level, "wl", "nmi"),
              .per_station = {{v(DBL_MAX), v(1)}}}},
            {{t(0), t(1)}}, {{.station = station("A"), .axis = 0}});
  const auto written = must_write(dir / "big.nc", big);
  CHECK(written.warnings ==
        std::vector<mov::io::Warning>{
            {.code = WarningCode::unit_converted, .subject = "water_level"},
            {.code = WarningCode::value_reads_as_missing,
             .subject = "water_level",
             .count = 1}});
  const Inspect f{dir / "big.nc"};
  CHECK(f.doubles("water_level") == std::vector<double>{fill, 1852});
}

TEST_CASE("units: refused when a registry column cannot be canonical",
          "[io][station_nc][write][units]") {
  const ScratchDir dir;
  const core_axis axis{t(0)};
  const auto one = [&](core::SeriesMeta m) {
    return table({{.meta = std::move(m), .per_station = {{v(1)}}}}, {axis},
                 {{.station = station("A"), .axis = 0}});
  };
  const auto refused = [&](core::SeriesMeta m, const char* token) {
    const auto path = dir / "refused.nc";
    const auto e = format_error_of(write(path, one(std::move(m))));
    CHECK(e.code == FormatErrc::noncanonical_unit);
    CHECK(e.subject == token);
    CHECK(not std::filesystem::exists(path));
  };
  refused(meta(Quantity::water_level, "no unit"), "water_level");
  refused(meta(Quantity::water_level, "speed", "m/s"), "water_level");
  refused(meta(Quantity::relative_humidity, "angle", "degree"),
          "relative_humidity");
  refused(meta(Quantity::wind_direction, "other", "furlongs"),
          "wind_direction");
}

TEST_CASE("generic and difference columns: units, standard names, datums",
          "[io][station_nc][write][units]") {
  const ScratchDir dir;
  const core_axis axis{t(0)};
  const auto path = dir / "g.nc";
  static_cast<void>(must_write(
      path,
      table(
          {{.meta = meta(mov::core::GenericQuantity::value(), "gauge", "ft",
                         VerticalDatum::stnd),
            .per_station = {{v(1)}}},
           {.meta = meta(Quantity::difference, "residual", "degC"),
            .per_station = {{v(2)}}},
           {.meta = meta(generic("salinity", "sea_water_salinity"), "", "1e-3"),
            .per_station = {{v(35)}}},
           {.meta = meta(generic("mystery"), "?"), .per_station = {{v(4)}}}},
          {axis}, {{.station = station("A"), .axis = 0}})));
  const Inspect f{path};
  CHECK(f.text("value", "units") == "ft");
  CHECK(f.text("value", "vertical_datum") == "STND");
  CHECK(not(f.att("value", "standard_name")));
  CHECK(f.text("value", "long_name") == "gauge");
  CHECK(f.text("difference", "units") == "degC");
  CHECK(f.text("difference", "units_metadata") == "temperature: difference");
  CHECK(not(f.att("difference", "standard_name")));
  CHECK(not(f.att("difference", "vertical_datum")));
  CHECK(f.text("salinity", "standard_name") == "sea_water_salinity");
  CHECK(f.text("salinity", "long_name") == "salinity");  // empty label: token
  CHECK(f.text("salinity", "units") == "1e-3");
  CHECK(not(f.att("mystery", "units")));
  CHECK(not(f.att("mystery", "standard_name")));
}

TEST_CASE("an empty label is written as the registry's long name",
          "[io][station_nc][write]") {
  const ScratchDir dir;
  static_cast<void>(must_write(
      dir / "l.nc", table({{.meta = meta(Quantity::wind_gust, "", "m s-1"),
                            .per_station = {{v(1)}}}},
                          {{t(0)}}, {{.station = station("A"), .axis = 0}})));
  const Inspect f{dir / "l.nc"};
  CHECK(f.text("wind_gust", "long_name") == "Wind gust speed");
}

TEST_CASE("nothing to write: no stations, columns or samples",
          "[io][station_nc][write][errors]") {
  const ScratchDir dir;
  const auto path = dir / "e.nc";
  const auto refused = [&](const StationTable& table, FormatErrc code) {
    const auto e = format_error_of(write(path, table));
    CHECK(e.code == code);
    CHECK(not std::filesystem::exists(path));
  };
  SECTION("no stations") {
    refused(StationTable{}, FormatErrc::empty_collection);
  }
  SECTION("stations without samples") {
    refused(water_levels({{}, {}}, {{}, {}}), FormatErrc::no_samples);
  }
  SECTION("no columns") {
    refused(table({}, {{t(0)}}, {{.station = station("A"), .axis = 0}}),
            FormatErrc::no_data_variables);
  }
}

TEST_CASE("too_many_samples: more than obs_count can count (N1)",
          "[io][station_nc][write][errors]") {
  const auto t2 =
      water_levels({{t(0), t(1), t(2)}, {t(0)}}, {{v(1), v(2), v(3)}, {v(4)}});
  const auto at = [&](std::size_t limit) {
    return mov::io::detail::validate_station_netcdf(
        t2, {}, {.max_station_samples = limit});
  };
  CHECK(at(3).has_value());
  const auto e = format_error_of(at(2));
  CHECK(e.code == FormatErrc::too_many_samples);
  CHECK(e.subject == "S0");
}

TEST_CASE("options: UTF-8 without NUL, bounded, a title (N6)",
          "[io][station_nc][write][errors]") {
  const ScratchDir dir;
  const auto one = water_levels({{t(0)}}, {{v(1)}});
  const auto refused = [&](const mov::io::StationNcWriteOptions& o,
                           const std::string& subject) {
    const auto e = format_error_of(write(dir / "o.nc", one, o));
    CHECK(e.code == FormatErrc::bad_option);
    CHECK(e.subject == subject);
    CHECK(not std::filesystem::exists(dir / "o.nc"));
  };
  refused({.title = ""}, ":title");
  refused({.title = "bad \xFF"}, ":title");
  refused({.title = "x", .institution = std::string("a\0b", 3)},
          ":institution");
  refused({.title = "x",
           .comment = std::string(
               mov::io::StationNcWriteOptions::max_option_bytes + 1, 'c')},
          ":comment");
  SECTION("an empty optional text is absent") {
    static_cast<void>(must_write(
        dir / "o.nc", one, {.title = "t", .source = "", .references = "r"}));
    const Inspect f{dir / "o.nc"};
    CHECK(not f.att("", "source"));
    CHECK(f.text("", "references") == "r");
  }
}

TEST_CASE("validate_station_netcdf: the writer's verdict without a file",
          "[io][station_nc][write]") {
  const ScratchDir dir;
  const core::FileStation unnamed = station("A", "");
  const auto t2 = table({{.meta = meta(Quantity::water_level, "wl", "ft"),
                          .per_station = {{v(1)}}}},
                        {{t(0)}}, {{.station = unnamed, .axis = 0}});
  const auto preview = mov::io::validate_station_netcdf(t2, {});
  REQUIRE(preview.has_value());
  CHECK(preview == write(dir / "p.nc", t2));
  CHECK(format_error_of(mov::io::validate_station_netcdf(StationTable{}, {}))
            .code == FormatErrc::empty_collection);
}

TEST_CASE("a generic temperature is `temperature: unknown` (N2)",
          "[io][station_nc][write][units]") {
  const ScratchDir dir;
  static_cast<void>(must_write(
      dir / "t.nc",
      table({{.meta = meta(generic("probe_temperature"), "probe", "degF"),
              .per_station = {{v(50)}}}},
            {{t(0)}}, {{.station = station("A"), .axis = 0}})));
  const Inspect f{dir / "t.nc"};
  CHECK(f.text("probe_temperature", "units") == "degF");
  CHECK(f.text("probe_temperature", "units_metadata") ==
        "temperature: unknown");
}

TEST_CASE("invalid_variable_name: a generic token the format cannot use",
          "[io][station_nc][write][errors]") {
  const ScratchDir dir;
  const core_axis axis{t(0)};
  const auto with = [&](std::vector<core::Variable> vars) {
    return table(std::move(vars), {axis},
                 {{.station = station("A"), .axis = 0}});
  };
  const auto refused = [&](const StationTable& table,
                           const std::string& token) {
    const auto e = format_error_of(write(dir / "n.nc", table));
    CHECK(e.code == FormatErrc::invalid_variable_name);
    CHECK(e.subject == token);
  };
  for (const char* reserved :
       {"station", "station_id", "station_name", "station_provider", "lat",
        "lon", "elevation", "crs", "time", "obs", "obs_count", "station_id_len",
        "station_name_len", "station_provider_len"}) {
    CAPTURE(reserved);
    refused(
        with({{.meta = meta(generic(reserved), "x"), .per_station = {{v(1)}}}}),
        reserved);
  }
  SECTION("the status variable of another column") {
    refused(with({{.meta = meta(Quantity::water_level, "wl", "m"),
                   .per_station = {{dry}}},
                  {.meta = meta(generic("water_level_status"), "x"),
                   .per_station = {{v(1)}}}}),
            "water_level_status");
  }
  SECTION("the status name of a column without Dry samples too") {
    // Every column reserves its status name: whether a table can be written
    // depends on its schema, not on whether a sample is Dry (F2).
    refused(with({{.meta = meta(Quantity::water_level, "wl", "m"),
                   .per_station = {{v(2)}}},
                  {.meta = meta(generic("water_level_status"), "x"),
                   .per_station = {{v(1)}}}}),
            "water_level_status");
  }
  SECTION("longer than a netCDF name with _status appended") {
    const std::string longest(249, 'a');  // 249 + "_status" = 256
    static_cast<void>(
        must_write(dir / "ok.nc", with({{.meta = meta(generic(longest), "x"),
                                         .per_station = {{v(1)}}}})));
    const std::string too_long(250, 'b');
    refused(
        with({{.meta = meta(generic(too_long), "x"), .per_station = {{v(1)}}}}),
        too_long);
  }
}

TEST_CASE("warnings: names substituted, native points dropped, fill values",
          "[io][station_nc][write][warnings]") {
  const ScratchDir dir;
  const core::FileStation unnamed = station("8761724", "");
  core::FileStation projected = station("B");
  projected.native =
      mov::core::NativePoint::make({.x = 1, .y = 2}, mov::core::Epsg::wgs84())
          .value();
  const auto t1 = table(
      {{.meta = meta(Quantity::water_level, "wl", "m"),
        .per_station = {{v(fill), v(1)}, {v(2), v(fill)}}}},
      {{t(0), t(1)}},
      {{.station = unnamed, .axis = 0}, {.station = projected, .axis = 0}});
  const auto written = must_write(dir / "w.nc", t1);
  CHECK(written.warnings ==
        std::vector<mov::io::Warning>{
            {.code = WarningCode::station_name_substituted, .count = 1},
            {.code = WarningCode::native_position_dropped, .count = 1},
            {.code = WarningCode::value_reads_as_missing,
             .subject = "water_level",
             .count = 2}});
  const Inspect f{dir / "w.nc"};
  CHECK(f.rows("station_name").at(0) == "Station 8761724");
}

TEST_CASE("strings: UTF-8 bytes, sized to the longest, NUL-padded (B7, B15)",
          "[io][station_nc][write][strings][regression][B15]") {
  const ScratchDir dir;
  const std::string bmp =
      "Sainte-Ana\xC3\xAF\x73 \xE6\xB0\xB4";           // 2- and 3-byte
  const std::string astral = "\xF0\x9F\x8C\x8A wave";  // U+1F30A
  const std::string long_name(1000, 'n');
  const auto t1 =
      table({{.meta = meta(Quantity::water_level, "wl", "m"),
              .per_station = {{v(1)}, {v(2)}, {v(3)}, {v(4)}}}},
            {{t(0)}},
            {{.station = station("x", bmp), .axis = 0},
             {.station = station(astral, "trailing space "), .axis = 0},
             {.station = station("q\"uote\\", long_name), .axis = 0},
             {.station = station("1", "a", 0, 0, std::nullopt), .axis = 0}});
  static_cast<void>(must_write(dir / "s.nc", t1));
  const Inspect f{dir / "s.nc"};
  CHECK(f.dim("station_id_len") == astral.size());
  CHECK(f.dim("station_name_len") == 1000);
  CHECK(f.dim("station_provider_len") == 4);  // "user"
  const auto ids = f.rows("station_id");
  CHECK(ids.at(0) == std::string("x") + std::string(astral.size() - 1, '\0'));
  CHECK(ids.at(1) == astral);
  const auto names = f.rows("station_name");
  CHECK(names.at(0).substr(0, bmp.size()) == bmp);
  CHECK(names.at(1).substr(0, 16) == std::string("trailing space ") + '\0');
  const auto providers = f.rows("station_provider");
  CHECK(providers.at(0) == "user");
  CHECK(providers.at(3) == std::string(4, '\0'));  // no source
}

TEST_CASE("station_provider is left out when no station has a source",
          "[io][station_nc][write][strings]") {
  const ScratchDir dir;
  const auto t1 =
      table({{.meta = meta(Quantity::water_level, "wl", "m"),
              .per_station = {{v(1)}}}},
            {{t(0)}},
            {{.station = station("A", "a", 0, 0, std::nullopt), .axis = 0}});
  static_cast<void>(must_write(dir / "p.nc", t1));
  const Inspect f{dir / "p.nc"};
  CHECK(not f.has_var("station_provider"));
  CHECK(not f.has_dim("station_provider_len"));
}

TEST_CASE("determinism: the same table and time give the same bytes",
          "[io][station_nc][write][determinism]") {
  const ScratchDir dir;
  const auto c = station_nc::incomplete();
  static_cast<void>(must_write(dir / "a.nc", c.table, c.options));
  static_cast<void>(must_write(dir / "b.nc", c.table, c.options));
  CHECK(mov::test::read_bytes(dir / "a.nc") ==
        mov::test::read_bytes(dir / "b.nc"));
}

TEST_CASE("the write is atomic: a failure leaves the old file (B6, B19)",
          "[io][station_nc][write][atomic][regression][B6][B19]") {
  const ScratchDir dir;
  const auto c = station_nc::orthogonal();
  const auto path = dir / "target.nc";
  mov::test::write_bytes(path, "the old file");
  using mov::io::detail::AtomicStage;
  for (const AtomicStage stage :
       {AtomicStage::create, AtomicStage::body, AtomicStage::close,
        AtomicStage::fsync_file, AtomicStage::rename}) {
    CAPTURE(static_cast<int>(stage));
    const auto failed = mov::io::detail::write_station_netcdf(
        path, c.table, c.options, station_nc::canonical_now(),
        {.fail_at = stage});
    CHECK(not failed.has_value());
    CHECK(mov::test::read_bytes(path) == "the old file");
    std::size_t entries = 0;
    for (const auto& entry : std::filesystem::directory_iterator{dir.path()}) {
      static_cast<void>(entry);
      ++entries;
    }
    CHECK(entries == 1);  // no temporary file left
  }
  SECTION("a precondition fails before anything is created") {
    CHECK(not write(path, StationTable{}).has_value());
    CHECK(mov::test::read_bytes(path) == "the old file");
  }
  SECTION("an unwritable directory") {
    const auto missing_dir = dir / "no" / "such" / "dir.nc";
    const auto e = write(missing_dir, c.table);
    REQUIRE(not e.has_value());
    CHECK(not(std::filesystem::exists(dir / "no")));
  }
  SECTION("success replaces the old file") {
    static_cast<void>(must_write(path, c.table, c.options));
    CHECK(mov::test::read_bytes(path) != "the old file");
  }
}

}  // namespace
