// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target, the structure fuzzer of core-design.md 7.5: the input bytes
// choose a netCDF *schema* (dimensions, variables, attribute types and values,
// a little data), which is written to a file under the build directory with
// netCDF-C, and every netCDF reader of mov::io is run on it: detect_file_type,
// inspect_station_netcdf and read_station_netcdf (v5, foreign CF and legacy v4
// files), and the ADCIRC and D-Flow FM readers. The input is not netCDF bytes:
// the readers' danger is the structure of a well-formed file, and netCDF-C
// itself refuses a malformed one.
//
// A schema starts from a template (a v5 file, a foreign CF layout, a legacy v4
// file, an ADCIRC or D-Flow file) so that the readers get past their first
// checks, and the input then spoils it: a missing variable, another type, a
// vocabulary attribute value, a huge dimension on a chunked variable (no data
// is written for those).
//
// Oracle:
//  - no crash, no undefined behavior, no hang (libFuzzer's timeout);
//  - no allocation above 256 MB (ASan's max_allocation_size_mb, set below):
//    the ReadLimits in force are small, so a size taken from the file shows;
//  - every reader returns a value or an Error, and the values are consistent:
//    a table's columns are as long as its stations' times, which increase; an
//    inspection and a read of the same file agree on origin, stations and
//    schema; the file type detect_file_type names is the one the station
//    reader's origin names.

#include <netcdf.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <variant>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/station_table.hpp"
#include "mov/io/adcirc_netcdf.hpp"
#include "mov/io/dflow.hpp"
#include "mov/io/error.hpp"
#include "mov/io/file_type.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/station_netcdf.hpp"

#ifndef MOV_FUZZ_SCRATCH_DIR
#error "MOV_FUZZ_SCRATCH_DIR names the directory the fuzzer writes its files to"
#endif

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

// ASan: an allocation above this aborts instead of returning null.
extern "C" const char*
__asan_default_options();  // NOLINT(bugprone-reserved-identifier)
extern "C" const char*
__asan_default_options() {  // NOLINT(bugprone-reserved-identifier)
  return "max_allocation_size_mb=256:allocator_may_return_null=0";
}

namespace {

namespace core = mov::core;
namespace io = mov::io;

[[noreturn]] void fail() { std::abort(); }

// ---- the input as a stream of choices ---------------------------------------

class Choices {
 public:
  Choices(const std::uint8_t* data, std::size_t size) : bytes_{data, size} {}
  /// The next byte; 0 when the input is used up.
  unsigned u8() { return next_ < bytes_.size() ? bytes_[next_++] : 0U; }
  unsigned below(unsigned n) { return n == 0 ? 0U : u8() % n; }
  /// True once in `n` times.
  bool one_in(unsigned n) { return below(n) == 0; }
  template <class T, std::size_t N>
  const T& pick(const std::array<T, N>& options) {
    return options[below(static_cast<unsigned>(N))];
  }

 private:
  std::span<const std::uint8_t> bytes_;
  std::size_t next_{0};
};

// ---- vocabulary
// ---------------------------------------------------------------

constexpr auto dim_names = std::to_array<const char*>({"station",
                                                       "time",
                                                       "obs",
                                                       "numStations",
                                                       "stations",
                                                       "name_len",
                                                       "stationNameLen",
                                                       "station_id_len",
                                                       "strlen",
                                                       "z",
                                                       "nv",
                                                       "laydim",
                                                       "stationLength_0001",
                                                       "stationLength_0002",
                                                       "x",
                                                       "y",
                                                       "n",
                                                       "len",
                                                       "station_name_len",
                                                       "t",
                                                       "sample",
                                                       "row",
                                                       "col",
                                                       "layer",
                                                       "e",
                                                       "d"});
constexpr auto var_names = std::to_array<const char*>({"station_id",
                                                       "station_name",
                                                       "lat",
                                                       "lon",
                                                       "time",
                                                       "obs_count",
                                                       "crs",
                                                       "rowSize",
                                                       "stationIndex",
                                                       "water_level",
                                                       "temperature",
                                                       "stationXCoordinate",
                                                       "stationYCoordinate",
                                                       "stationName",
                                                       "stationId",
                                                       "time_station_0001",
                                                       "data_station_0001",
                                                       "time_station_0002",
                                                       "data_station_0002",
                                                       "zeta",
                                                       "x",
                                                       "y",
                                                       "station_x_coordinate",
                                                       "station_y_coordinate",
                                                       "waterlevel",
                                                       "u-vel",
                                                       "v-vel",
                                                       "Water Level",
                                                       "1x",
                                                       "wind_speed",
                                                       "alt",
                                                       "depth",
                                                       "time_bnds",
                                                       "flags",
                                                       "value",
                                                       "difference",
                                                       "temperature_status",
                                                       "time_station_000001",
                                                       "u",
                                                       "v",
                                                       "pressure",
                                                       "windx",
                                                       "windy",
                                                       "salinity"});
constexpr auto att_names =
    std::to_array<const char*>({"cf_role",
                                "standard_name",
                                "long_name",
                                "units",
                                "calendar",
                                "axis",
                                "positive",
                                "coordinates",
                                "ancillary_variables",
                                "bounds",
                                "grid_mapping",
                                "sample_dimension",
                                "instance_dimension",
                                "_FillValue",
                                "missing_value",
                                "valid_min",
                                "valid_max",
                                "valid_range",
                                "scale_factor",
                                "add_offset",
                                "_Unsigned",
                                "flag_values",
                                "flag_meanings",
                                "epsg_code",
                                "grid_mapping_name",
                                "semi_major_axis",
                                "inverse_flattening",
                                "longitude_of_prime_meridian",
                                "vertical_datum",
                                "HorizontalProjectionEPSG",
                                "referenceDate",
                                "timezone",
                                "datum",
                                "StationName",
                                "model",
                                "ics",
                                "base_date",
                                "metoceanviewer_format",
                                "metoceanviewer_format_version",
                                "Conventions",
                                "featureType",
                                "fileformat",
                                "title",
                                "history",
                                "comment",
                                "units_metadata"});
constexpr auto texts =
    std::to_array<const char*>({"timeseries_id",
                                "timeSeries",
                                "CF-1.8",
                                "CF-1.11",
                                "CF-1.5",
                                "CF-2.0",
                                "station-timeseries",
                                "station-profile",
                                "1.0",
                                "1.7",
                                "2.0",
                                "degrees_north",
                                "degrees_east",
                                "days since 2000-01-01",
                                "seconds since 1970-01-01 00:00:00",
                                "hours since 2000-01-01 00:00:00 +05:30",
                                "furlongs since 1",
                                "noleap",
                                "standard",
                                "proleptic_gregorian",
                                "latitude",
                                "longitude",
                                "time",
                                "platform_name",
                                "platform_id",
                                "water_surface_height_above_reference_datum",
                                "eastward_sea_water_velocity",
                                "sea_water_x_velocity",
                                "m",
                                "degC",
                                "K",
                                "percent",
                                "m s-1",
                                "knots",
                                "MLLW",
                                "NAVD88",
                                "none",
                                "EPSG:4326",
                                "EPSG:26915",
                                "EPSG:99999999999",
                                "latitude_longitude",
                                "X",
                                "Y",
                                "T",
                                "up",
                                "crs",
                                "time lat lon",
                                "temperature_status",
                                "dry wet",
                                "obs",
                                "station",
                                "ADCIRC",
                                "UTC",
                                "utc",
                                "EST",
                                "1970-01-01 00:00:00",
                                "2000-13-45",
                                "true",
                                "",
                                "time lat lon nothere",
                                "crs: lat lon",
                                "water_level",
                                "wind_speed",
                                "x y z",
                                "9999-99-99 99:99:99"});
constexpr auto numbers = std::to_array<double>(
    {0.0, 1.0, 2.0, 3.0, -1.0, -999.0, -99999.0, 9.969209968386869e36, 4326.0,
     26915.0, 6378137.0, 298.257223563, 1e300, 255.0, 65535.0, 0.5});
constexpr auto types = std::to_array<nc_type>(
    {NC_BYTE, NC_CHAR, NC_SHORT, NC_INT, NC_FLOAT, NC_DOUBLE, NC_UBYTE,
     NC_USHORT, NC_UINT, NC_INT64, NC_UINT64, NC_STRING});
constexpr auto small_lengths = std::to_array<std::size_t>(
    {0, 1, 1, 2, 2, 3, 3, 4, 5, 7, 12, 16, 100, 1000});
constexpr auto huge_lengths =
    std::to_array<std::size_t>({std::size_t{1} << 20U, std::size_t{70000},
                                std::size_t{1} << 31U, std::size_t{1} << 40U});

// ---- a schema
// ------------------------------------------------------------------

enum class Role : std::uint8_t {
  generic,
  id,
  name,
  lat,
  lon,
  time,
  count,
  index,
  data,
  coordinate
};

struct Att {
  std::string name;
  bool text{true};
  nc_type type{NC_DOUBLE};
  std::string value;
  std::vector<double> numbers;
};

struct Dim {
  std::string name;
  std::size_t length;
};

struct Var {
  std::string name;
  nc_type type;
  std::vector<std::size_t> dims;  // indices into Plan::dims
  Role role{Role::generic};
  std::vector<Att> atts;
};

struct Plan {
  int cmode{NC_NETCDF4};
  std::vector<Dim> dims;
  std::vector<Att> global;
  std::vector<Var> vars;
};

Att text_att(std::string name, std::string value) {
  return {.name = std::move(name),
          .text = true,
          .type = NC_CHAR,
          .value = std::move(value),
          .numbers = {}};
}

Att num_att(std::string name, nc_type type, std::vector<double> values) {
  return {.name = std::move(name),
          .text = false,
          .type = type,
          .value = {},
          .numbers = std::move(values)};
}

/// A template, spoiled by the input.
class Builder {
 public:
  explicit Builder(Choices& in) : in_{in} {}

  Plan plan() {
    switch (in_.below(11)) {
      case 0:
        free_form();
        break;
      case 1:
        cf(true, Layout::orthogonal);
        break;
      case 2:
        cf(true, Layout::incomplete);
        break;
      case 3:
        cf(false, Layout::orthogonal);
        break;
      case 4:
        cf(false, Layout::incomplete);
        break;
      case 5:
        cf(false, Layout::contiguous);
        break;
      case 6:
        cf(false, Layout::indexed);
        break;
      case 7:
        cf(false, Layout::single);
        break;
      case 8:
        legacy();
        break;
      case 9:
        adcirc();
        break;
      default:
        dflow();
        break;
    }
    return std::move(plan_);
  }

 private:
  enum class Layout : std::uint8_t {
    orthogonal,
    incomplete,
    contiguous,
    indexed,
    single
  };

  // ---- spoilers
  bool keep() { return not in_.one_in(14); }
  nc_type type(nc_type good) { return in_.one_in(10) ? in_.pick(types) : good; }
  std::string text(const char* good) {
    return in_.one_in(8) ? std::string{in_.pick(texts)} : std::string{good};
  }
  std::size_t length(std::size_t good) {
    if (in_.one_in(24)) {
      return in_.pick(huge_lengths);
    }
    return in_.one_in(8) ? in_.pick(small_lengths) : good;
  }

  std::size_t dim(std::string name, std::size_t good_length) {
    plan_.dims.push_back(
        {.name = std::move(name), .length = length(good_length)});
    return plan_.dims.size() - 1;
  }

  Var& var(std::string name, nc_type good, std::vector<std::size_t> dims,
           Role role) {
    plan_.vars.push_back({.name = std::move(name),
                          .type = type(good),
                          .dims = std::move(dims),
                          .role = role,
                          .atts = {}});
    return plan_.vars.back();
  }

  void header_attributes(bool v5) {
    plan_.global.push_back(text_att("Conventions", text("CF-1.11")));
    plan_.global.push_back(text_att("featureType", text("timeSeries")));
    if (v5) {
      plan_.global.push_back(
          text_att("metoceanviewer_format", text("station-timeseries")));
      plan_.global.push_back(
          text_att("metoceanviewer_format_version", text("1.0")));
    }
    if (in_.one_in(8)) {
      plan_.global.push_back(random_att());
    }
  }

  Att random_att() {
    const char* name = in_.pick(att_names);
    if (in_.one_in(3)) {
      std::vector<double> values;
      const unsigned n = in_.below(4);
      values.reserve(n);
      for (unsigned i = 0; i < n; ++i) {
        values.push_back(in_.pick(numbers));
      }
      return num_att(name, in_.pick(types), std::move(values));
    }
    return text_att(name, in_.pick(texts));
  }

  void spoil(Var& v) {
    for (int more = 0; more < 3 and in_.one_in(6); ++more) {
      v.atts.push_back(random_att());
    }
  }

  void free_form() {
    plan_.cmode = in_.one_in(4) ? 0 : NC_NETCDF4;
    const unsigned dims = 1 + in_.below(8);
    for (unsigned i = 0; i < dims; ++i) {
      plan_.dims.push_back({.name = in_.pick(dim_names),
                            .length = in_.one_in(16)
                                          ? in_.pick(huge_lengths)
                                          : in_.pick(small_lengths)});
    }
    const unsigned vars = in_.below(17);
    for (unsigned i = 0; i < vars; ++i) {
      Var v{.name = in_.pick(var_names),
            .type = in_.pick(types),
            .dims = {},
            .role = Role::generic,
            .atts = {}};
      const unsigned rank = in_.below(4);
      for (unsigned r = 0; r < rank; ++r) {
        v.dims.push_back(in_.below(dims));
      }
      const unsigned atts = in_.below(5);
      for (unsigned a = 0; a < atts; ++a) {
        v.atts.push_back(random_att());
      }
      plan_.vars.push_back(std::move(v));
    }
    const unsigned globals = in_.below(5);
    for (unsigned g = 0; g < globals; ++g) {
      plan_.global.push_back(random_att());
    }
  }

  // ---- foreign CF and v5
  void cf(bool v5, Layout layout) {
    plan_.cmode = in_.one_in(6) ? 0 : NC_NETCDF4;
    header_attributes(v5);
    const std::size_t stations = 1 + in_.below(4);
    const std::size_t times = 1 + in_.below(4);
    const bool single = layout == Layout::single;
    const bool ragged =
        layout == Layout::contiguous or layout == Layout::indexed;
    std::size_t station = 0;
    if (not single) {
      station = dim("station", stations);
    }
    const std::size_t strlen_dim = dim("strlen", 3);
    const std::size_t sample =
        dim(ragged or layout == Layout::incomplete ? "obs" : "time",
            ragged ? stations * times : times);
    if (keep()) {
      Var& id = var(v5 ? "station_id" : "station_name", NC_CHAR,
                    single ? std::vector<std::size_t>{strlen_dim}
                           : std::vector<std::size_t>{station, strlen_dim},
                    Role::id);
      id.atts.push_back(text_att("cf_role", text("timeseries_id")));
      spoil(id);
    }
    if (v5 and keep()) {
      var("station_name", NC_CHAR,
          single ? std::vector<std::size_t>{strlen_dim}
                 : std::vector<std::size_t>{station, strlen_dim},
          Role::name);
    }
    for (const auto& [name, units, role] :
         {std::tuple{"lat", "degrees_north", Role::lat},
          std::tuple{"lon", "degrees_east", Role::lon}}) {
      if (not keep()) {
        continue;
      }
      Var& v = var(name, NC_DOUBLE,
                   single ? std::vector<std::size_t>{}
                          : std::vector<std::size_t>{station},
                   role);
      v.atts.push_back(text_att("units", text(units)));
      spoil(v);
    }
    cf_samples(layout, station, sample, stations);
    if (in_.one_in(4)) {
      Var& crs = var("crs", NC_INT, {}, Role::generic);
      crs.atts.push_back(
          text_att("grid_mapping_name", text("latitude_longitude")));
      if (in_.one_in(2)) {
        crs.atts.push_back(text_att("epsg_code", text("EPSG:4326")));
      }
    }
  }

  void cf_samples(Layout layout, std::size_t station, std::size_t sample,
                  std::size_t stations) {
    const bool matrix =
        layout == Layout::orthogonal or layout == Layout::incomplete;
    const bool transposed = in_.one_in(5);
    const auto shape = [&](bool two_d) {
      if (not two_d) {
        return std::vector<std::size_t>{sample};
      }
      return transposed ? std::vector<std::size_t>{sample, station}
                        : std::vector<std::size_t>{station, sample};
    };
    if (keep()) {
      Var& t = var("time", NC_DOUBLE, shape(layout == Layout::incomplete),
                   Role::time);
      t.atts.push_back(text_att("units", text("days since 2000-01-01")));
      if (layout == Layout::incomplete and in_.one_in(2)) {
        t.atts.push_back(num_att("_FillValue", t.type, {-1.0}));
      }
      spoil(t);
    }
    if (keep()) {
      Var& d = var("temperature", NC_DOUBLE, shape(matrix), Role::data);
      d.atts.push_back(text_att("units", text("degC")));
      if (in_.one_in(2)) {
        d.atts.push_back(num_att("_FillValue", d.type, {-999.0}));
      }
      if (in_.one_in(4)) {
        d.atts.push_back(text_att("standard_name", text("time")));
      }
      spoil(d);
    }
    if (layout == Layout::incomplete and keep()) {
      var("obs_count", NC_INT, {station}, Role::count);
    }
    if (layout == Layout::contiguous and keep()) {
      Var& r = var("rowSize", NC_INT, {station}, Role::count);
      r.atts.push_back(text_att("sample_dimension", text("obs")));
    }
    if (layout == Layout::indexed and keep()) {
      Var& i = var("stationIndex", NC_INT, {sample}, Role::index);
      i.atts.push_back(text_att("instance_dimension", text("station")));
    }
    static_cast<void>(stations);
  }

  // ---- legacy v4
  void legacy() {
    plan_.cmode = in_.one_in(6) ? 0 : NC_NETCDF4;
    if (in_.one_in(2)) {
      plan_.global.push_back(text_att("fileformat", "20180123"));
    }
    const std::size_t stations = 1 + in_.below(3);
    const std::size_t numStations = dim("numStations", stations);
    const std::size_t name_len = dim("stationNameLen", 8);
    for (const char* name : {"stationXCoordinate", "stationYCoordinate"}) {
      if (keep()) {
        Var& v =
            var(name, NC_DOUBLE, {numStations},
                std::string_view{name} == "stationXCoordinate" ? Role::lon
                                                               : Role::lat);
        if (std::string_view{name} == "stationXCoordinate" and keep()) {
          v.atts.push_back(
              num_att("HorizontalProjectionEPSG", NC_INT, {in_.pick(numbers)}));
        }
        spoil(v);
      }
    }
    if (keep()) {
      var("stationName", NC_CHAR, {numStations, name_len}, Role::name);
    }
    if (in_.one_in(2) and keep()) {
      var("stationId", NC_CHAR, {numStations, name_len}, Role::id);
    }
    for (std::size_t s = 0; s < stations; ++s) {
      const std::string number = std::to_string(s + 1);
      const std::string suffix =
          std::string(4 - std::min<std::size_t>(4, number.size()), '0') +
          number;
      const std::size_t len = dim("stationLength_" + suffix, 1 + in_.below(4));
      if (keep()) {
        Var& t = var("time_station_" + suffix, NC_INT64, {len}, Role::time);
        if (in_.one_in(2)) {
          t.atts.push_back(
              text_att("referenceDate", text("1970-01-01 00:00:00")));
        }
        if (in_.one_in(3)) {
          t.atts.push_back(text_att("timezone", text("utc")));
        }
      }
      if (keep()) {
        Var& d = var("data_station_" + suffix, NC_DOUBLE, {len}, Role::data);
        if (in_.one_in(2)) {
          d.atts.push_back(text_att("units", text("m")));
        }
        if (in_.one_in(2)) {
          d.atts.push_back(text_att("datum", text("MLLW")));
        }
        spoil(d);
      }
    }
  }

  // ---- ADCIRC and D-Flow FM
  void adcirc() {
    plan_.cmode = in_.one_in(6) ? 0 : NC_NETCDF4;
    plan_.global.push_back(text_att("model", text("ADCIRC")));
    if (in_.one_in(2)) {
      plan_.global.push_back(num_att("ics", NC_INT, {in_.pick(numbers)}));
    }
    const std::size_t time = dim("time", 1 + in_.below(4));
    const std::size_t station = dim("station", 1 + in_.below(4));
    const std::size_t namelen = dim("namelen", 6);
    Var& t = var("time", NC_DOUBLE, {time}, Role::time);
    if (in_.one_in(2)) {
      t.atts.push_back(
          text_att("units", text("seconds since 2010-01-01 00:00:00")));
    }
    for (const char* name : {"x", "y"}) {
      if (keep()) {
        var(name, NC_DOUBLE, {station},
            std::string_view{name} == "x" ? Role::lon : Role::lat);
      }
    }
    if (keep()) {
      var("station_name", NC_CHAR, {station, namelen}, Role::name);
    }
    for (const char* name : {"zeta", "u-vel", "v-vel", "pressure", "windx"}) {
      if (in_.one_in(3)) {
        Var& d = var(name, NC_DOUBLE, {time, station}, Role::data);
        if (in_.one_in(2)) {
          d.atts.push_back(num_att("_FillValue", d.type, {-99999.0}));
        }
      }
    }
  }

  void dflow() {
    plan_.cmode = in_.one_in(6) ? 0 : NC_NETCDF4;
    const std::size_t time = dim("time", 1 + in_.below(4));
    const std::size_t stations = dim("stations", 1 + in_.below(4));
    const std::size_t name_len = dim("name_len", 6);
    const std::size_t laydim = dim("laydim", 1 + in_.below(3));
    Var& t = var("time", NC_DOUBLE, {time}, Role::time);
    t.atts.push_back(
        text_att("units", text("seconds since 2001-01-01 00:00:00")));
    for (const char* name : {"station_x_coordinate", "station_y_coordinate"}) {
      if (keep()) {
        var(name, NC_DOUBLE, {stations},
            std::string_view{name} == "station_x_coordinate" ? Role::lon
                                                             : Role::lat);
      }
    }
    if (keep()) {
      var("station_name", NC_CHAR, {stations, name_len}, Role::name);
    }
    for (const char* name :
         {"waterlevel", "x_velocity", "y_velocity", "windx", "salinity"}) {
      if (in_.one_in(2)) {
        Var& d =
            var(name, NC_DOUBLE,
                in_.one_in(3) ? std::vector<std::size_t>{time, stations, laydim}
                              : std::vector<std::size_t>{time, stations},
                Role::data);
        d.atts.push_back(num_att("_FillValue", d.type, {-999.0}));
        spoil(d);
      }
    }
  }

  Choices& in_;
  Plan plan_;
};

// ---- writing the schema
// ----------------------------------------------------------

/// The element count of a variable of fixed dimensions, or nullopt.
std::optional<std::size_t> elements_of(const Plan& plan, const Var& v) {
  std::size_t n = 1;
  for (const std::size_t d : v.dims) {
    const std::size_t len = plan.dims[d].length;
    if (len == 0 or n > 256 / len) {
      return std::nullopt;
    }
    n *= len;
  }
  return n;
}

void put_att(int ncid, int varid, const Att& a) {
  if (a.text) {
    nc_put_att_text(ncid, varid, a.name.c_str(), a.value.size(),
                    a.value.data());
  } else if (a.type == NC_STRING) {
    const char* s = "x";
    nc_put_att_string(ncid, varid, a.name.c_str(), 1, &s);
  } else {
    nc_put_att_double(ncid, varid, a.name.c_str(), a.type, a.numbers.size(),
                      a.numbers.data());
  }
}

/// Data for a variable of `n` elements, shaped by its role.
std::vector<double> values_for(const Var& v, std::size_t n, Choices& in) {
  std::vector<double> out(n);
  for (std::size_t i = 0; i < n; ++i) {
    switch (v.role) {
      case Role::lat:
        out[i] =
            in.one_in(12) ? in.pick(numbers) : 29.0 + static_cast<double>(i);
        break;
      case Role::lon:
        out[i] =
            in.one_in(12) ? in.pick(numbers) : -90.0 + static_cast<double>(i);
        break;
      case Role::time:
        out[i] = in.one_in(12) ? in.pick(numbers) : static_cast<double>(i);
        break;
      case Role::count:
        out[i] = static_cast<double>(in.below(5));
        break;
      case Role::index:
        out[i] = static_cast<double>(in.below(4));
        break;
      default:
        out[i] = in.one_in(3) ? in.pick(numbers)
                              : static_cast<double>(in.below(100));
        break;
    }
  }
  return out;
}

void put_data(int ncid, int varid, const Plan& plan, const Var& v,
              Choices& in) {
  const auto n = elements_of(plan, v);
  if (not n or *n == 0) {
    return;
  }
  if (v.type == NC_CHAR) {
    std::string chars(*n, '\0');
    for (char& c : chars) {
      c = in.one_in(4) ? '\0' : static_cast<char>('A' + in.below(26));
    }
    nc_put_var_text(ncid, varid, chars.data());
  } else if (v.type == NC_STRING) {
    std::vector<std::string> owned(*n);
    std::vector<const char*> pointers(*n, nullptr);
    for (std::size_t i = 0; i < *n; ++i) {
      owned[i] = in.pick(texts);
      pointers[i] = in.one_in(5) ? nullptr : owned[i].c_str();
    }
    nc_put_var_string(ncid, varid, pointers.data());
  } else if (v.type == NC_INT64) {
    const std::vector<double> d = values_for(v, *n, in);
    std::vector<long long> wide;
    wide.reserve(d.size());
    for (const double x : d) {
      // (a double beyond the range of long long is undefined to convert)
      wide.push_back(x > -9e18 and x < 9e18 ? static_cast<long long>(x) : 0);
    }
    nc_put_var_longlong(ncid, varid, wide.data());
  } else {
    const std::vector<double> d = values_for(v, *n, in);
    nc_put_var_double(ncid, varid, d.data());
  }
}

/// Writes `plan` to `path`; netCDF-C's refusals (a name used twice, a
/// _FillValue of another type) leave that piece out.
void write(const std::filesystem::path& path, const Plan& plan, Choices& in) {
  int ncid = -1;
  if (nc_create(path.c_str(), plan.cmode | NC_CLOBBER, &ncid) != NC_NOERR) {
    return;
  }
  std::vector<int> dim_ids(plan.dims.size(), -1);
  for (std::size_t i = 0; i < plan.dims.size(); ++i) {
    nc_def_dim(ncid, plan.dims[i].name.c_str(), plan.dims[i].length,
               &dim_ids[i]);
  }
  for (const Att& a : plan.global) {
    put_att(ncid, NC_GLOBAL, a);
  }
  std::vector<int> var_ids(plan.vars.size(), -1);
  for (std::size_t i = 0; i < plan.vars.size(); ++i) {
    const Var& v = plan.vars[i];
    std::vector<int> ids;
    ids.reserve(v.dims.size());
    for (const std::size_t d : v.dims) {
      ids.push_back(dim_ids[d]);
    }
    if (nc_def_var(ncid, v.name.c_str(), v.type, static_cast<int>(ids.size()),
                   ids.data(), &var_ids[i]) != NC_NOERR) {
      continue;
    }
    if (plan.cmode == NC_NETCDF4 and not v.dims.empty()) {
      std::vector<std::size_t> chunks;
      for (const std::size_t d : v.dims) {
        const std::size_t len = plan.dims[d].length;
        chunks.push_back(len >= 70000 ? 4096
                                      : std::clamp<std::size_t>(len, 1, 8));
      }
      nc_def_var_chunking(ncid, var_ids[i], NC_CHUNKED, chunks.data());
    }
    for (const Att& a : v.atts) {
      put_att(ncid, var_ids[i], a);
    }
  }
  if (nc_enddef(ncid) == NC_NOERR) {
    for (std::size_t i = 0; i < plan.vars.size(); ++i) {
      if (var_ids[i] >= 0) {
        put_data(ncid, var_ids[i], plan, plan.vars[i], in);
      }
    }
  }
  nc_close(ncid);
}

// ---- the oracle
// ---------------------------------------------------------------------

io::ReadContext small_limits() {
  io::ReadContext ctx;
  ctx.limits = {.max_elements = std::size_t{1} << 14U,
                .max_att_bytes = std::size_t{1} << 12U,
                .max_text_bytes = std::uintmax_t{1} << 20U,
                .slab_elements = std::size_t{1} << 10U,
                .max_result_bytes = std::size_t{1} << 22U};
  return ctx;
}

bool increasing(std::span<const core::Time> times) {
  return std::ranges::adjacent_find(times, [](core::Time a, core::Time b) {
           return not(a < b);
         }) == times.end();
}

void check_warnings(const std::vector<io::Warning>& warnings) {
  for (const io::Warning& w : warnings) {
    if (w.count == 0 or w.subject.size() > 4096) {
      fail();
    }
  }
}

void check_table(const core::StationTable& t, const io::ReadLimits& limits) {
  std::size_t samples = 0;
  for (const core::StationIndex i : t.stations()) {
    const auto times = t.times(i);
    if (not increasing(times)) {
      fail();
    }
    for (std::size_t k = 0; k < t.schema().size(); ++k) {
      if (t.column(i, core::ColumnIndex{k}).size() != times.size()) {
        fail();
      }
    }
    samples += times.size();
  }
  if (samples * std::max<std::size_t>(1, t.schema().size()) >
      limits.max_elements * 2) {
    fail();
  }
}

io::FileType expected_type(const io::StationFileOrigin& origin) {
  if (std::holds_alternative<io::V5Origin>(origin)) {
    return io::FileType::station_netcdf;
  }
  if (std::holds_alternative<io::ForeignCfOrigin>(origin)) {
    return io::FileType::foreign_cf_netcdf;
  }
  return io::FileType::legacy_station_netcdf;
}

void run_station_readers(const std::filesystem::path& path,
                         const io::ReadContext& ctx,
                         const std::optional<io::FileType>& detected) {
  const auto catalog = io::inspect_station_netcdf(path, ctx);
  if (catalog) {
    check_warnings(catalog->warnings);
    if (detected and *detected != expected_type(catalog->value.origin)) {
      fail();
    }
  }
  for (const io::PaddingCheck padding :
       {io::PaddingCheck::boundary, io::PaddingCheck::whole}) {
    const auto read =
        io::read_station_netcdf(path, io::AllStations{}, ctx,
                                io::StationNcReadOptions{.padding = padding});
    if (not read) {
      const auto* format = std::get_if<io::FormatError>(&read.error());
      const bool refused =
          format != nullptr and format->code == io::FormatErrc::not_this_format;
      const bool station_file =
          detected and (*detected == io::FileType::station_netcdf or
                        *detected == io::FileType::foreign_cf_netcdf or
                        *detected == io::FileType::legacy_station_netcdf);
      if (refused and station_file) {
        fail();  // detect_file_type named a station kind the reader refuses
      }
      continue;
    }
    check_warnings(read->warnings);
    check_table(read->value.table, ctx.limits);
    if (detected and *detected != expected_type(read->value.origin)) {
      fail();
    }
    if (catalog and padding == io::PaddingCheck::boundary) {
      const auto& c = catalog->value;
      const core::StationTable& t = read->value.table;
      if (c.origin != read->value.origin or c.stations.size() != t.size() or
          c.schema.size() != t.schema().size()) {
        fail();
      }
      for (std::size_t i = 0; i < t.size(); ++i) {
        if (not(c.stations[i].station == t.station(core::StationIndex{i})) or
            c.stations[i].samples != t.times(core::StationIndex{i}).size()) {
          fail();
        }
      }
      for (std::size_t k = 0; k < c.schema.size(); ++k) {
        if (not(c.schema[k] == t.schema()[k])) {
          fail();
        }
      }
    }
  }
}

void run_model_readers(const std::filesystem::path& path,
                       const io::ReadContext& ctx) {
  const auto adcirc = io::inspect_adcirc_netcdf(path, core::Epsg::wgs84(), ctx);
  if (adcirc) {
    const io::AdcircNcRequest request{
        .kind = adcirc->value.kind,
        .cold_start = std::chrono::time_point_cast<std::chrono::milliseconds>(
            std::chrono::sys_days{std::chrono::year{2010} /
                                  std::chrono::January / 1}),
        .crs = core::Epsg::wgs84(),
        .stations = core::StationSelection::all(adcirc->value.stations.size())};
    const auto read = io::read_adcirc_netcdf(path, request, ctx);
    if (read) {
      check_table(read->value, ctx.limits);
    }
  }
  const auto dflow = io::inspect_dflow(path, core::Epsg::wgs84(), ctx);
  if (dflow and not dflow->value.variables.empty()) {
    const io::DflowVariable& first = dflow->value.variables.front();
    std::optional<io::DflowChoice> choice;
    if (const auto* flat = std::get_if<io::Flat>(&first)) {
      choice = io::FlatChoice{.source = flat->source};
    } else if (const auto* layered = std::get_if<io::Layered>(&first)) {
      if (auto at = io::AtLayer::make(*layered, 1)) {
        choice = *std::move(at);
      }
    }
    if (not choice) {
      return;
    }
    const auto read = io::read_dflow(
        path,
        {.choice = *choice,
         .stations = core::StationSelection::all(dflow->value.stations.size()),
         .crs = core::Epsg::wgs84()},
        ctx);
    if (read) {
      check_table(read->value, ctx.limits);
    }
  }
}

std::filesystem::path scratch_file() {
  static const std::filesystem::path path = [] {
    // MOV_FUZZ_SCRATCH names a faster directory than the build tree's when
    // that one is on a network file system (a file is created and removed per
    // execution).
    const char* chosen = std::getenv("MOV_FUZZ_SCRATCH");
    const std::filesystem::path dir{chosen != nullptr ? chosen
                                                      : MOV_FUZZ_SCRATCH_DIR};
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    return dir / ("structure-" + std::to_string(::getpid()) + ".nc");
  }();
  return path;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  Choices in{data, size};
  Plan plan = Builder{in}.plan();
  if (plan.cmode != NC_NETCDF4) {
    // A classic file stores (and fills) every element of its variables.
    for (Dim& d : plan.dims) {
      d.length = std::min<std::size_t>(d.length, 16);
    }
  }
  const std::filesystem::path path = scratch_file();
  write(path, plan, in);

  const io::ReadContext ctx = small_limits();
  std::optional<io::FileType> detected;
  if (const auto type = io::detect_file_type(path, ctx.limits)) {
    detected = *type;
  }
  run_station_readers(path, ctx, detected);
  run_model_readers(path, ctx);

  std::error_code ignored;
  std::filesystem::remove(path, ignored);
  return 0;
}
