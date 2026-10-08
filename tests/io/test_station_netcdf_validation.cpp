// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The v5 reader's validation (docs/station-netcdf.md section 12, the v5
// column): each test breaks exactly one rule of a valid file with raw
// netCDF-C (nc_edit.hpp) and asserts the one error, or warning, it gives.

#include <netcdf.h>

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/station.hpp"
#include "mov/io/error.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "mov/test/scratch_dir.hpp"
#include "nc_edit.hpp"
#include "station_nc_support.hpp"

namespace {

using namespace mov::test::snc;  // NOLINT(google-build-using-namespace)
namespace core = mov::core;
namespace io = mov::io;
namespace station_nc = mov::test::station_nc;
using io::FormatErrc;
using io::WarningCode;
using mov::test::ScratchDir;
using mov::test::ncgen::Editor;

/// A valid canonical file in a scratch directory, to break.
class Broken {
 public:
  explicit Broken(const station_nc::Canonical& c)
      : path_{station_nc::write(c, dir_.path())} {}
  [[nodiscard]] const std::filesystem::path& path() const { return path_; }
  [[nodiscard]] Editor edit() const { return Editor{path_}; }

  /// The read's FormatError (the inspection reads no samples).
  [[nodiscard]] io::FormatError read_error() const {
    return format_error_of(read_all(path_));
  }
  [[nodiscard]] io::FormatError inspect_error() const {
    return format_error_of(io::inspect_station_netcdf(path_, {}));
  }
  [[nodiscard]] io::Read<io::V5StationFile> read() const {
    return must_read(read_all(path_));
  }

 private:
  ScratchDir dir_;
  std::filesystem::path path_;
};

io::FormatError expect(const Broken& f, FormatErrc code, std::string subject) {
  const io::FormatError e = f.read_error();
  CHECK(e.code == code);
  CHECK(e.subject == subject);
  return e;
}

/// What make_skeleton leaves out or changes.
struct Skeleton {
  bool incomplete{false};
  const char* station_dim{"station"};
  bool time_over_station{false};  // orthogonal dims, time(station, time)
  bool data{true};
  std::vector<std::string> omit{};
};

/// A minimal v5 file written with raw netCDF-C: two stations "A" and "B" at
/// (0, 0), two times, one `value` column. A structure that editing cannot
/// make (netCDF-4 can neither delete a variable nor rename one safely: a
/// renamed variable leaves an HDF5 file that netCDF-C 4.9 cannot reopen).
void make_skeleton(const std::filesystem::path& path, const Skeleton& k) {
  using mov::test::ncgen::check;
  mov::test::ncgen::Raw raw{path};
  const auto omitted = [&k](std::string_view name) {
    return std::ranges::find(k.omit, name) != k.omit.end();
  };
  const int station = raw.dim(k.station_dim, 2);
  const int len = raw.dim("station_id_len", 1);
  const int sample = raw.dim(k.incomplete ? "obs" : "time", 2);
  raw.text(NC_GLOBAL, "Conventions", "CF-1.11");
  raw.text(NC_GLOBAL, "featureType", "timeSeries");
  raw.text(NC_GLOBAL, "metoceanviewer_format", "station-timeseries");
  raw.text(NC_GLOBAL, "metoceanviewer_format_version", "1.0");
  raw.text(raw.var("station_id", NC_CHAR, {station, len}), "cf_role",
           "timeseries_id");
  for (const char* name : {"station_name", "lat", "lon", "time", "obs_count"}) {
    if (omitted(name)) {
      continue;
    }
    const std::string_view n{name};
    if (n == "station_name") {
      raw.var(name, NC_CHAR, {station, len});
    } else if (n == "time") {
      const bool two_d = k.incomplete or k.time_over_station;
      raw.text(raw.var(name, NC_DOUBLE,
                       two_d ? std::vector<int>{station, sample}
                             : std::vector<int>{sample}),
               "units", "milliseconds since 1970-01-01 00:00:00");
    } else if (n == "obs_count") {
      if (k.incomplete) {
        raw.var(name, NC_INT, {station});
      }
    } else {
      raw.var(name, NC_DOUBLE, {station});
    }
  }
  if (k.data) {
    raw.var("value", NC_DOUBLE, {station, sample});
  }
  raw.enddef();
  const auto put = [&raw](const char* var, std::span<const double> values) {
    int varid = 0;
    if (nc_inq_varid(raw.id(), var, &varid) == NC_NOERR) {
      check(nc_put_var_double(raw.id(), varid, values.data()), var);
    }
  };
  const std::array<double, 2> zeros{0, 0};
  const std::array<double, 4> four{0, 1, 0, 1};
  put("lat", zeros);
  put("lon", zeros);
  put("time", k.incomplete or k.time_over_station ? std::span{four}
                                                  : std::span{four}.first(2));
  put("value", four);
  check(nc_put_var_text(raw.id(), raw.varid("station_id"), "AB"), "ids");
  int count = 0;
  if (nc_inq_varid(raw.id(), "obs_count", &count) == NC_NOERR) {
    const std::array<int, 2> counts{2, 2};
    check(nc_put_var_int(raw.id(), count, counts.data()), "obs_count");
  }
  raw.close();
}

/// The FormatError of reading a skeleton.
io::FormatError skeleton_error(const Skeleton& k) {
  const ScratchDir dir;
  make_skeleton(dir / "s.nc", k);
  return format_error_of(read_all(dir / "s.nc"));
}

TEST_CASE("the skeleton is a valid file", "[io][station_nc][validation]") {
  const ScratchDir dir;
  for (const bool incomplete : {false, true}) {
    make_skeleton(dir / "s.nc", {.incomplete = incomplete});
    const auto read = must_read(read_all(dir / "s.nc"));
    CHECK(read.value.table.size() == 2);
    CHECK(read.value.layout == (incomplete ? io::StationNcLayout::incomplete
                                           : io::StationNcLayout::orthogonal));
    CHECK(count_of(read.warnings, WarningCode::crs_assumed) == 1);
    std::filesystem::remove(dir / "s.nc");
  }
}

// ---- header
// ------------------------------------------------------------------

TEST_CASE("the format attribute decides what the file is",
          "[io][station_nc][validation][header]") {
  const Broken f{station_nc::orthogonal()};
  const auto refused = [&f] {
    CHECK(
        expect(f, FormatErrc::not_this_format, ":metoceanviewer_format").code ==
        f.inspect_error().code);
  };
  SECTION("absent") {
    f.edit().remove_att("", "metoceanviewer_format");
    refused();
  }
  SECTION("another format") {
    f.edit().text("", "metoceanviewer_format", "station-profile");
    refused();
  }
}

TEST_CASE("the format version: bad, unsupported, newer minor",
          "[io][station_nc][validation][header][version]") {
  const Broken f{station_nc::orthogonal()};
  SECTION("missing") {
    f.edit().remove_att("", "metoceanviewer_format_version");
    expect(f, FormatErrc::bad_version, ":metoceanviewer_format_version");
  }
  SECTION("unparsable") {
    f.edit().text("", "metoceanviewer_format_version", "1");
    expect(f, FormatErrc::bad_version, "1");
  }
  SECTION("not text") {
    f.edit().ints("", "metoceanviewer_format_version", {1});
    expect(f, FormatErrc::bad_version, ":metoceanviewer_format_version");
  }
  SECTION("a newer major") {
    f.edit().text("", "metoceanviewer_format_version", "2.0");
    expect(f, FormatErrc::unsupported_version, "2.0");
  }
  SECTION("an older major") {
    f.edit().text("", "metoceanviewer_format_version", "0.9");
    expect(f, FormatErrc::unsupported_version, "0.9");
  }
  SECTION("a newer minor reads with a warning") {
    f.edit().text("", "metoceanviewer_format_version", "1.7");
    f.edit().text("water_level", "some_future_attribute", "ignored");
    const auto read = f.read();
    CHECK(read.value.version == io::StationNcVersion{.major = 1, .minor = 7});
    CHECK(warning_of(read.warnings, WarningCode::minor_newer).subject == "1.7");
    CHECK(read.value.table == station_nc::orthogonal().table);
  }
}

TEST_CASE("Conventions: a CF-1.6 or later token",
          "[io][station_nc][validation][header]") {
  const Broken f{station_nc::orthogonal()};
  for (const char* good : {"CF-1.6", "CF-1.12", "CF-1.8 ACDD-1.3",
                           "CF-1.11,ACDD-1.3", "ACDD-1.3, CF-1.10"}) {
    CAPTURE(good);
    f.edit().text("", "Conventions", good);
    CHECK(read_all(f.path()).has_value());
  }
  SECTION("absent") {
    f.edit().remove_att("", "Conventions");
    expect(f, FormatErrc::missing_attribute, ":Conventions");
  }
  for (const char* bad : {"CF-1.5", "COARDS", "CF-1", "CF-2.x", "UGRID-1.0"}) {
    CAPTURE(bad);
    f.edit().text("", "Conventions", bad);
    expect(f, FormatErrc::unsupported_version, ":Conventions");
  }
}

TEST_CASE("featureType: timeSeries in any case",
          "[io][station_nc][validation][header]") {
  const Broken f{station_nc::orthogonal()};
  f.edit().text("", "featureType", "TIMESERIES");
  CHECK(read_all(f.path()).has_value());
  SECTION("absent") {
    f.edit().remove_att("", "featureType");
    expect(f, FormatErrc::missing_attribute, ":featureType");
  }
  SECTION("another feature type") {
    f.edit().text("", "featureType", "profile");
    expect(f, FormatErrc::unsupported_layout, ":featureType");
  }
}

// ---- structure
// -----------------------------------------------------------------

TEST_CASE("the station dimension and the station id variable",
          "[io][station_nc][validation][structure]") {
  const Broken f{station_nc::orthogonal()};
  SECTION("no station dimension") {
    const auto e = skeleton_error({.station_dim = "stations"});
    CHECK(e.code == FormatErrc::missing_dimension);
    CHECK(e.subject == "station");
  }
  SECTION("no cf_role") {
    f.edit().remove_att("station_id", "cf_role");
    expect(f, FormatErrc::no_station_id, "cf_role");
    CHECK(f.inspect_error().code == FormatErrc::no_station_id);
  }
  SECTION("two variables with cf_role timeseries_id") {
    f.edit().text("station_name", "cf_role", "timeseries_id");
    expect(f, FormatErrc::no_station_id, "cf_role");
  }
  SECTION("the id variable is not char") {
    f.edit().text("lat", "cf_role", "timeseries_id");
    f.edit().remove_att("station_id", "cf_role");
    expect(f, FormatErrc::bad_encoding, "lat");
  }
  SECTION("the id variable is not over the station dimension") {
    f.edit().add_var("other_id", NC_CHAR, {"time", "station_id_len"});
    f.edit().text("other_id", "cf_role", "timeseries_id");
    f.edit().remove_att("station_id", "cf_role");
    expect(f, FormatErrc::dimension_mismatch, "other_id");
  }
  SECTION("a variable missing") {
    for (const char* name : {"station_name", "lat", "lon", "time"}) {
      CAPTURE(name);
      const auto e = skeleton_error({.omit = {name}});
      CHECK(e.code == FormatErrc::missing_variable);
      CHECK(e.subject == name);
    }
  }
}

TEST_CASE("station values: ids, names and coordinates",
          "[io][station_nc][validation][stations]") {
  const Broken f{station_nc::orthogonal()};
  SECTION("duplicate ids") {
    f.edit().put_chars("station_id", 1, "8761724");
    const auto e = expect(f, FormatErrc::duplicate_station_id, "8761724");
    CHECK(e.station == 1);
  }
  SECTION("an empty id") {
    f.edit().put_chars("station_id", 0, std::string(7, '\0'));
    CHECK(expect(f, FormatErrc::no_station_id, "station_id").station == 0);
  }
  SECTION("a NUL inside an id") {
    f.edit().put_chars("station_id", 0,
                       std::string("87\0"
                                   "1724",
                                   7));
    CHECK(expect(f, FormatErrc::bad_encoding, "station_id").station == 0);
  }
  SECTION("an id that is not UTF-8") {
    f.edit().put_chars("station_id", 1, "\xFF\xFE");
    CHECK(expect(f, FormatErrc::bad_encoding, "station_id").station == 1);
  }
  SECTION("a name that is not UTF-8") {
    f.edit().put_chars("station_name", 0, "\xC3");
    CHECK(expect(f, FormatErrc::bad_encoding, "station_name").station == 0);
  }
  SECTION("a latitude out of range") {
    f.edit().put("lat", {1}, 95.0);
    CHECK(expect(f, FormatErrc::bad_coordinates, "lat").station == 1);
  }
  SECTION("a missing longitude") {
    f.edit().put("lon", {0}, NC_FILL_DOUBLE);
    CHECK(expect(f, FormatErrc::bad_coordinates, "lon").station == 0);
  }
  SECTION("a NaN latitude") {
    f.edit().put("lat", {0}, std::nan(""));
    CHECK(expect(f, FormatErrc::bad_coordinates, "lat").station == 0);
  }
  SECTION("a longitude in [0, 360] is normalized") {
    f.edit().put("lon", {0}, 270.0);
    const auto read = f.read();
    CHECK(read.value.table.station(core::StationIndex{0}).location.lon() ==
          -90.0);
  }
}

TEST_CASE("station_provider: unknown tokens are dropped with a warning",
          "[io][station_nc][validation][stations]") {
  const Broken f{station_nc::orthogonal()};
  f.edit().put_chars("station_provider", 1,
                     std::string_view{"martian\0\0\0", 10});
  const auto read = f.read();
  CHECK(warning_of(read.warnings, WarningCode::unknown_provider).subject ==
        "martian");
  CHECK(not read.value.table.station(core::StationIndex{1}).source);
  CHECK(read.value.table.station(core::StationIndex{0}).source ==
        core::DataSource::noaa_coops);
}

TEST_CASE("time: units, calendar, values (orthogonal)",
          "[io][station_nc][validation][time]") {
  const Broken f{station_nc::orthogonal()};
  SECTION("no units") {
    f.edit().remove_att("time", "units");
    expect(f, FormatErrc::missing_attribute, "time:units");
  }
  SECTION("units that are no CF time unit") {
    f.edit().text("time", "units", "milliseconds after lunch");
    const auto read = read_all(f.path());
    REQUIRE(not read.has_value());
    CHECK(std::holds_alternative<io::ParseError>(read.error()));
  }
  SECTION("other CF units are converted") {
    f.edit().text("time", "units", "seconds since 2000-01-01 00:00:00 +00:00");
    const auto read = f.read();
    CHECK(read.value.table.times(core::StationIndex{0})[0] ==
          ms(946684800000 + 1700000000000000));
  }
  SECTION("an unsupported calendar") {
    f.edit().text("time", "calendar", "noleap");
    expect(f, FormatErrc::unsupported_calendar, "time:calendar");
  }
  SECTION("the standard calendar is read the same after 1582") {
    f.edit().text("time", "calendar", "gregorian");
    CHECK(f.read().value.table == station_nc::orthogonal().table);
  }
  SECTION("not increasing") {
    f.edit().put("time", {2}, 1700000360000.0);
    CHECK(expect(f, FormatErrc::time_not_increasing, "time").index == 2);
  }
  SECTION("missing") {
    f.edit().put("time", {1}, NC_FILL_DOUBLE);
    CHECK(expect(f, FormatErrc::time_missing, "time").index == 1);
  }
  SECTION("beyond 2^53 ms") {
    f.edit().put("time", {3}, 9007199254740992.0);
    CHECK(expect(f, FormatErrc::time_out_of_range, "time").index == 3);
  }
  SECTION("a time over (station, time) is not orthogonal") {
    const auto e = skeleton_error({.time_over_station = true});
    CHECK(e.code == FormatErrc::unsupported_layout);
    CHECK(e.subject == "time");
  }
}

TEST_CASE("time and padding (incomplete)",
          "[io][station_nc][validation][time][padding]") {
  const Broken f{station_nc::incomplete()};
  SECTION("obs_count below 0") {
    f.edit().put_int("obs_count", {0}, -1);
    CHECK(expect(f, FormatErrc::bad_obs_count, "obs_count").station == 0);
  }
  SECTION("obs_count above obs") {
    f.edit().put_int("obs_count", {1}, 6);
    CHECK(expect(f, FormatErrc::bad_obs_count, "obs_count").station == 1);
    CHECK(f.inspect_error().code == FormatErrc::bad_obs_count);
  }
  SECTION("no obs_count") {
    const auto e = skeleton_error({.incomplete = true, .omit = {"obs_count"}});
    CHECK(e.code == FormatErrc::missing_variable);
    CHECK(e.subject == "obs_count");
  }
  SECTION("a time inside the count is missing") {
    f.edit().put("time", {1, 2}, NC_FILL_DOUBLE);
    const auto e = expect(f, FormatErrc::time_missing, "time");
    CHECK(e.station == 1);
    CHECK(e.index == 2);
  }
  SECTION("not increasing inside the count") {
    f.edit().put("time", {0, 2}, 1.0);
    const auto e = expect(f, FormatErrc::time_not_increasing, "time");
    CHECK(e.station == 0);
    CHECK(e.index == 2);
  }
  SECTION("a time in the padding: at the boundary and further on") {
    const std::size_t index = GENERATE(std::size_t{3}, std::size_t{4});
    f.edit().put("time", {0, index}, 1.0);
    const auto e = expect(f, FormatErrc::padding_not_missing, "time");
    CHECK(e.station == 0);
    CHECK(e.index == index);
  }
  SECTION("a value in the data padding") {
    f.edit().put("water_temperature", {0, 4}, 1.0);
    const auto e =
        expect(f, FormatErrc::padding_not_missing, "water_temperature");
    CHECK(e.station == 0);
    CHECK(e.index == 4);
  }
  SECTION("a flag in the status padding") {
    f.edit().put_byte("water_level_status", {0, 3}, 1);
    const auto e =
        expect(f, FormatErrc::padding_not_missing, "water_level_status");
    CHECK(e.station == 0);
    CHECK(e.index == 3);
  }
  SECTION("the counts decide; a short count keeps the first samples") {
    f.edit().put_int("obs_count", {1}, 4);
    f.edit().put("time", {1, 4}, NC_FILL_DOUBLE);
    f.edit().put("water_level", {1, 4}, NC_FILL_DOUBLE);
    f.edit().put_byte("water_level_status", {1, 4}, -128);
    const auto read = f.read();
    CHECK(read.value.table.times(core::StationIndex{1}).size() == 4);
  }
}

TEST_CASE("wet/dry status: flags and consistency",
          "[io][station_nc][validation][wetdry]") {
  const Broken f{station_nc::orthogonal()};
  SECTION("a flag outside flag_values") {
    f.edit().put_byte("water_level_status", {0, 2}, 2);
    const auto e = expect(f, FormatErrc::bad_flag, "water_level_status");
    CHECK(e.station == 0);
    CHECK(e.index == 2);
  }
  SECTION("dry with a value") {
    f.edit().put_byte("water_level_status", {0, 1}, 0);
    const auto e = expect(f, FormatErrc::wet_dry_inconsistent, "water_level");
    CHECK(e.station == 0);
    CHECK(e.index == 1);
  }
  SECTION("wet without a value") {
    f.edit().put_byte("water_level_status", {1, 1}, 1);
    const auto e = expect(f, FormatErrc::wet_dry_inconsistent, "water_level");
    CHECK(e.station == 1);
    CHECK(e.index == 1);
  }
  SECTION("unclassified: a value, or missing") {
    f.edit().put_byte("water_level_status", {0, 0}, -128);
    f.edit().put_byte("water_level_status", {1, 1}, -128);
    const auto read = f.read();
    const auto first =
        read.value.table.column(core::StationIndex{0}, core::ColumnIndex{0});
    const auto second =
        read.value.table.column(core::StationIndex{1}, core::ColumnIndex{0});
    CHECK(first[0] == v(0.5));
    CHECK(second[1] == missing);
  }
  SECTION("an ancillary variable that is not a wet/dry status is ignored") {
    f.edit().text("water_level_status", "flag_meanings", "bad good");
    const auto read = f.read();
    CHECK(read.value.table.column(core::StationIndex{1},
                                  core::ColumnIndex{0})[1] == missing);
  }
  SECTION("an ancillary target over other dimensions") {
    f.edit().add_var("quality", NC_BYTE, {"station"});
    f.edit().text("water_level", "ancillary_variables",
                  "water_level_status quality");
    expect(f, FormatErrc::bad_ancillary, "quality");
  }
  SECTION("a target that does not exist is ignored") {
    f.edit().text("water_level", "ancillary_variables",
                  "water_level_status nowhere");
    CHECK(f.read().value.table == station_nc::orthogonal().table);
  }
}

TEST_CASE("data variables: shapes and presence",
          "[io][station_nc][validation][data]") {
  SECTION("a variable over the sample dimension in another shape") {
    const Broken f{station_nc::orthogonal()};
    f.edit().add_var("transposed", NC_DOUBLE, {"time", "station"});
    expect(f, FormatErrc::dimension_mismatch, "transposed");
  }
  SECTION("over the sample dimension only") {
    const Broken f{station_nc::incomplete()};
    f.edit().add_var("per_obs", NC_DOUBLE, {"obs"});
    expect(f, FormatErrc::dimension_mismatch, "per_obs");
  }
  SECTION("other variables are ignored (a newer minor may add them)") {
    const Broken f{station_nc::orthogonal()};
    f.edit().add_var("station_height", NC_DOUBLE, {"station"});
    f.edit().add_var("scalar", NC_INT, {});
    CHECK(f.read().value.table == station_nc::orthogonal().table);
  }
  SECTION("a data variable of a type the reader refuses") {
    const Broken f{station_nc::orthogonal()};
    f.edit().add_var("counts", NC_INT64, {"station", "time"});
    const auto read = read_all(f.path());
    REQUIRE(not read.has_value());
    const auto* nc = std::get_if<io::NcError>(&read.error());
    REQUIRE(nc != nullptr);
    CHECK(nc->status == io::NcStatus{io::WrapperFault::type_mismatch});
  }
}

TEST_CASE("no data variables", "[io][station_nc][validation][data]") {
  CHECK(skeleton_error({.data = false}).code == FormatErrc::no_data_variables);
}

TEST_CASE("quantities, units and datums of a data variable",
          "[io][station_nc][validation][meta]") {
  const Broken f{station_nc::registry()};
  SECTION("an unknown datum token") {
    f.edit().text("value", "vertical_datum", "MOON");
    const auto read = f.read();
    CHECK(warning_of(read.warnings, WarningCode::datum_unknown).subject ==
          "MOON");
    const auto k = read.value.table.column_of(core::GenericQuantity::value());
    REQUIRE(k.has_value());
    if (k) {
      CHECK(not read.value.table.schema()[k->value()].datum());
    }
  }
  SECTION("a datum on a quantity that cannot carry one") {
    f.edit().text("wind_speed", "vertical_datum", "NAVD88");
    const auto read = f.read();
    CHECK(warning_of(read.warnings, WarningCode::datum_unknown).subject ==
          "wind_speed:vertical_datum");
    CHECK(read.value.table == station_nc::registry().table);
  }
  SECTION("an unrecognized unit is kept, with a warning") {
    f.edit().text("value", "units", "smoots");
    const auto read = f.read();
    CHECK(warning_of(read.warnings, WarningCode::unrecognized_unit).subject ==
          "smoots");
  }
  SECTION("a generic token other than value is reported") {
    const auto read = f.read();
    CHECK(warning_of(read.warnings, WarningCode::unknown_quantity).subject ==
          "sea_water_x_velocity");
    CHECK(count_of(read.warnings, WarningCode::unknown_quantity) == 1);
  }
  SECTION("a variable without long_name is labelled with its token") {
    f.edit().remove_att("air_pressure", "long_name");
    const auto read = f.read();
    const auto k = read.value.table.column_of(core::Quantity::air_pressure);
    REQUIRE(k.has_value());
    if (k) {
      CHECK(read.value.table.schema()[k->value()].label() == "air_pressure");
    }
  }
}

TEST_CASE("the horizontal CRS (SN 10.1)", "[io][station_nc][validation][crs]") {
  mov::test::configure_projection_database();
  const Broken f{station_nc::orthogonal()};
  const auto original = station_nc::orthogonal().table;
  SECTION("no epsg_code: latitude_longitude on WGS 84") {
    f.edit().remove_att("crs", "epsg_code");
    CHECK(f.read().value.table == original);
  }
  SECTION("no grid_mapping at all: assumed, with a warning") {
    f.edit().remove_att("water_level", "grid_mapping");
    f.edit().remove_att("water_temperature", "grid_mapping");
    const auto read = f.read();
    CHECK(count_of(read.warnings, WarningCode::crs_assumed) == 1);
    CHECK(read.value.table == original);
  }
  SECTION("another geographic CRS is projected; the native point is kept") {
    f.edit().text("crs", "epsg_code", "EPSG:4269");
    const auto read = f.read();
    const auto& s = read.value.table.station(core::StationIndex{0});
    REQUIRE(s.native.has_value());
    if (const auto& native = s.native) {
      CHECK(native->crs().code() == 4269);
      CHECK(native->x() == -89.9567);
      CHECK(native->y() == 29.2633);
    }
    CHECK(std::abs(s.location.lat() - 29.2633) < 1e-4);
  }
  SECTION("codes that are not a geographic EPSG code") {
    for (const char* bad : {"EPSG:26915", "EPSG:0", "EPSG:", "4326",
                            "EPSG:4326x", "EPSG:999999999"}) {
      CAPTURE(bad);
      f.edit().text("crs", "epsg_code", bad);
      CHECK(f.read_error().code == FormatErrc::unsupported_crs);
    }
  }
  SECTION("another ellipsoid") {
    f.edit().remove_att("crs", "epsg_code");
    f.edit().doubles("crs", "semi_major_axis", {6378206.4});
    expect(f, FormatErrc::unsupported_crs, "crs");
  }
  SECTION("a projected grid mapping") {
    f.edit().remove_att("crs", "epsg_code");
    f.edit().text("crs", "grid_mapping_name", "transverse_mercator");
    expect(f, FormatErrc::unsupported_crs, "crs");
  }
  SECTION("a grid_mapping that names no variable") {
    f.edit().text("water_level", "grid_mapping", "nowhere");
    f.edit().text("water_temperature", "grid_mapping", "nowhere");
    expect(f, FormatErrc::unsupported_crs, "nowhere");
  }
  SECTION("two grid mappings") {
    f.edit().text("water_temperature", "grid_mapping", "lat");
    expect(f, FormatErrc::unsupported_crs, "lat");
  }
}

}  // namespace
