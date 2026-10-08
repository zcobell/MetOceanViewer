// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "legacy_fixtures.hpp"

#include <netcdf.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <span>
#include <string>
#include <vector>

#include "nc_build.hpp"

namespace mov::test::ncgen {

std::string legacy_name(const std::string& prefix, std::size_t n, int width) {
  return std::format("{}{:0{}}", prefix, n, width);
}

namespace {

nc_type time_nc_type(LegacyTime t) {
  switch (t) {
    case LegacyTime::int64:
      return NC_INT64;
    case LegacyTime::int32:
      return NC_INT;
    case LegacyTime::float64:
      return NC_DOUBLE;
  }
  return NC_INT64;
}

// The text variables of a legacy file: one row per station.
std::vector<std::string> rows_of(const std::vector<LegacyStation>& stations,
                                 std::string LegacyStation::* field) {
  std::vector<std::string> rows;
  rows.reserve(stations.size());
  for (const LegacyStation& s : stations) {
    rows.push_back(s.*field);
  }
  return rows;
}

void define_coordinate(Cdf& f, const LegacyNc& spec, const std::string& name,
                       int stations) {
  f.var(name, NC_DOUBLE, {stations});
  f.text(name, "HorizontalProjectionName", "WGS84");
  if (name == "stationXCoordinate" and spec.epsg) {
    if (spec.epsg_as_text) {
      f.text(name, "HorizontalProjectionEPSG", std::to_string(*spec.epsg));
    } else {
      f.num(name, "HorizontalProjectionEPSG",
            static_cast<nc_type>(spec.epsg_type),
            {static_cast<double>(*spec.epsg)});
    }
  }
}

}  // namespace

void make_legacy_nc(const std::filesystem::path& path, const LegacyNc& spec) {
  Cdf f{path};
  const std::size_t n = spec.stations.size();
  const int stations =
      spec.omit_num_stations ? f.dim("other", n) : f.dim("numStations", n);
  const int name_dim = f.dim("stationNameLen", spec.name_len);
  std::vector<int> length_dims;
  length_dims.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    // A zero-length dimension is the unlimited one: what v4 gets for a station
    // without samples.
    length_dims.push_back(
        f.dim(legacy_name("stationLength_", i + 1, spec.width),
              spec.stations[i].seconds.size()));
  }

  if (not spec.omit_x) {
    define_coordinate(f, spec, "stationXCoordinate", stations);
  }
  if (not spec.omit_y) {
    define_coordinate(f, spec, "stationYCoordinate", stations);
  }
  if (not spec.omit_names) {
    f.var("stationName", NC_CHAR, {stations, name_dim});
  }
  if (spec.write_station_id) {
    f.var("stationId", NC_CHAR, {stations, name_dim});
  }

  for (std::size_t i = 0; i < n; ++i) {
    const LegacyStation& s = spec.stations[i];
    const std::size_t number = i + 1;
    const std::string time_name =
        legacy_name("time_station_", number, spec.width);
    const std::string data_name =
        legacy_name("data_station_", number, spec.width);
    const int own = length_dims[i];
    const int time_dim =
        spec.wrong_dim_for == number and i + 1 < n ? length_dims[i + 1] : own;
    if (spec.omit_time_of != number) {
      f.var(time_name, time_nc_type(spec.time_type), {time_dim});
      if (spec.reference_date) {
        f.text(time_name, "referenceDate", *spec.reference_date);
      }
      if (spec.timezone) {
        f.text(time_name, "timezone", *spec.timezone);
      }
      f.text(time_name, "units", "sec");
    }
    if (spec.omit_data_of != number) {
      f.var(data_name,
            spec.data_type == LegacyData::float64 ? NC_DOUBLE : NC_FLOAT,
            {own});
      if (not s.units.empty()) {
        f.text(data_name, "units", s.units);
      }
      if (not s.datum.empty()) {
        f.text(data_name, "datum", s.datum);
      }
      f.text(data_name, "StationName", s.name);
      f.text(data_name, "StationID", s.id);
      if (s.fill) {
        f.num(data_name, "_FillValue",
              spec.data_type == LegacyData::float64 ? NC_DOUBLE : NC_FLOAT,
              {*s.fill});
      }
    }
  }
  f.text("", "source", "MetOceanViewer");
  if (spec.write_fileformat) {
    f.text("", "fileformat", "20180123");
  }

  if (not spec.omit_x) {
    std::vector<double> x;
    x.reserve(n);
    for (const LegacyStation& s : spec.stations) {
      x.push_back(s.x);
    }
    f.put("stationXCoordinate", x);
  }
  if (not spec.omit_y) {
    std::vector<double> y;
    y.reserve(n);
    for (const LegacyStation& s : spec.stations) {
      y.push_back(s.y);
    }
    f.put("stationYCoordinate", y);
  }
  if (not spec.omit_names) {
    f.put_rows("stationName", spec.name_len,
               rows_of(spec.stations, &LegacyStation::name));
  }
  if (spec.write_station_id) {
    f.put_rows("stationId", spec.name_len,
               rows_of(spec.stations, &LegacyStation::id));
  }
  for (std::size_t i = 0; i < n; ++i) {
    const LegacyStation& s = spec.stations[i];
    const std::size_t number = i + 1;
    if (s.seconds.empty()) {
      continue;
    }
    if (spec.omit_time_of != number) {
      const std::string time_name =
          legacy_name("time_station_", number, spec.width);
      if (spec.time_type == LegacyTime::int64) {
        f.put_i64(time_name, s.seconds);
      } else {
        const std::vector<double> t(s.seconds.begin(), s.seconds.end());
        f.put(time_name, t);
      }
    }
    if (spec.omit_data_of != number and not s.values.empty()) {
      const std::string data_name =
          legacy_name("data_station_", number, spec.width);
      const std::size_t k =
          std::min(s.written.value_or(s.values.size()), s.values.size());
      if (k > 0) {
        f.put_range(data_name, 0, std::span<const double>{s.values}.first(k));
      }
    }
  }
  f.close();
}

void make_crms_nc(const std::filesystem::path& path) {
  Cdf f{path};
  const int stations = f.dim("nstation", 1);
  const int strings = f.dim("stringsize", 200);
  const int params = f.dim("numParam", 2);
  const int length = f.dim("stationLength_000001", 3);
  f.var("sensors", NC_CHAR, {params, strings});
  f.var("time_station_000001", NC_INT64, {length});
  f.text("time_station_000001", "reference",
         "seconds since 1970/01/01 00:00:00 UTC");
  f.var("data_station_000001", NC_FLOAT, {params, length});
  f.num("data_station_000001", "_FillValue", NC_FLOAT, {-9999.0});
  f.text("data_station_000001", "station_name", "Test station");
  static_cast<void>(stations);
  f.put_rows("sensors", 200, {"Salinity", "Stage"});
  const std::vector<std::int64_t> t{0, 3600, 7200};
  f.put_i64("time_station_000001", t);
  const std::vector<double> v{1, 2, 3, 4, 5, 6};
  f.put("data_station_000001", v);
  f.close();
}

}  // namespace mov::test::ncgen
