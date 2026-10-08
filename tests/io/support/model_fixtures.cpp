// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "model_fixtures.hpp"

#include <netcdf.h>

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "nc_raw.hpp"

namespace mov::test::ncgen {

namespace {

nc_type nc_type_of(DataType t) {
  switch (t) {
    case DataType::float64:
      return NC_DOUBLE;
    case DataType::float32:
      return NC_FLOAT;
    case DataType::int8:
      return NC_BYTE;
    case DataType::int16:
      return NC_SHORT;
    case DataType::int32:
      return NC_INT;
    case DataType::int64:
      return NC_INT64;
    case DataType::uint8:
      return NC_UBYTE;
  }
  return NC_NAT;
}

void put_double_att(const Raw& f, int varid, const char* name, nc_type type,
                    double value) {
  check(nc_put_att_double(f.id(), varid, name, type, 1, &value), name);
}

void put_attributes(const Raw& f, int varid, const DflowVar& v) {
  const std::string_view units = v.units;
  const std::string_view standard_name = v.standard_name;
  const std::string_view long_name = v.long_name;
  if (v.numeric_attributes) {
    const int seven = 7;
    for (const char* name : {"units", "standard_name", "long_name"}) {
      check(nc_put_att_int(f.id(), varid, name, NC_INT, 1, &seven), name);
    }
    return;
  }
  if (not units.empty()) {
    f.text(varid, "units", units);
  }
  if (not standard_name.empty()) {
    f.text(varid, "standard_name", standard_name);
  }
  if (not long_name.empty()) {
    f.text(varid, "long_name", long_name);
  }
}

// Rows of `width` bytes: each row's bytes, padded with `pad`. A row that is
// longer than the width is a mistake in the test.
std::string padded_rows(const std::vector<std::string>& rows, std::size_t width,
                        char pad) {
  std::string bytes;
  bytes.reserve(rows.size() * width);
  for (const std::string& row : rows) {
    if (row.size() > width) {
      throw std::runtime_error{"netCDF fixture: a name is longer than its row"};
    }
    bytes += row;
    bytes.append(width - row.size(), pad);
  }
  return bytes;
}

void put_block(const Raw& f, int varid, const std::vector<std::size_t>& start,
               const std::vector<std::size_t>& count,
               const std::vector<double>& values) {
  check(nc_put_vara_double(f.id(), varid, start.data(), count.data(),
                           values.data()),
        "put block");
}

void put_times(const Raw& f, int varid, const std::vector<double>& times) {
  const std::vector<std::size_t> start{0};
  const std::vector<std::size_t> count{times.size()};
  put_block(f, varid, start, count, times);
}

std::vector<double> default_times(std::size_t steps, double dt) {
  std::vector<double> t(steps);
  for (std::size_t i = 0; i < steps; ++i) {
    t[i] = dt * static_cast<double>(i + 1);
  }
  return t;
}

}  // namespace

// ---- ADCIRC ----------------------------------------------------------------

void make_adcirc_nc(const std::filesystem::path& path, const AdcircNc& spec) {
  Raw f{path};
  int time_dim = 0;
  int station_dim = 0;
  const std::size_t time_length = spec.fixed_time ? spec.steps : NC_UNLIMITED;
  if (spec.station_dim_first) {
    station_dim = f.dim("station", spec.stations);
    time_dim = f.dim("time", time_length);
  } else {
    time_dim = f.dim("time", time_length);
    station_dim = f.dim("station", spec.stations);
  }
  f.text(NC_GLOBAL, "base_date", "seconds since Met");
  if (spec.model) {
    f.text(NC_GLOBAL, "model", *spec.model);
  }
  const double dt = 600.0;
  check(nc_put_att_double(f.id(), NC_GLOBAL, "dt", NC_DOUBLE, 1, &dt), "dt");

  int time = -1;
  if (spec.write_time) {
    const nc_type time_type =
        spec.time_type == TimeType::int64 ? NC_INT64 : NC_DOUBLE;
    time = f.var("time", time_type, {time_dim});
    if (spec.time_units) {
      f.text(time, "units", *spec.time_units);
    }
    if (spec.time_calendar) {
      f.text(time, "calendar", *spec.time_calendar);
    }
    if (spec.time_fill) {
      put_double_att(f, time, "_FillValue", time_type, *spec.time_fill);
    }
  }
  int x = -1;
  int y = -1;
  if (spec.write_coordinates) {
    x = f.var("x", NC_DOUBLE, {station_dim});
    y = f.var("y", NC_DOUBLE, {station_dim});
  }
  int names = -1;
  if (not spec.station_names.empty()) {
    const int namelen = f.dim("namelen", spec.name_len);
    names = f.var("station_name", NC_CHAR, {station_dim, namelen});
  }
  std::vector<int> data;
  for (const std::string& name : spec.variables) {
    const nc_type type = nc_type_of(spec.type);
    const int id =
        f.var(name.c_str(), type,
              spec.transposed_data ? std::vector<int>{station_dim, time_dim}
                                   : std::vector<int>{time_dim, station_dim});
    if (not spec.chunks.empty()) {
      check(nc_def_var_chunking(f.id(), id, NC_CHUNKED, spec.chunks.data()),
            "chunking");
    }
    if (spec.fill) {
      put_double_att(f, id, "_FillValue", type, *spec.fill);
    }
    if (spec.missing_value) {
      put_double_att(f, id, "missing_value", type, *spec.missing_value);
    }
    if (spec.scale_factor) {
      put_double_att(f, id, "scale_factor", NC_DOUBLE, *spec.scale_factor);
    }
    if (spec.add_offset) {
      put_double_att(f, id, "add_offset", NC_DOUBLE, *spec.add_offset);
    }
    data.push_back(id);
  }
  f.enddef();

  if (time >= 0) {
    put_times(f, time,
              spec.times.empty() ? default_times(spec.steps, dt) : spec.times);
  }
  if (spec.write_coordinates) {
    std::vector<double> xs = spec.x;
    std::vector<double> ys = spec.y;
    if (xs.empty() or ys.empty()) {
      xs.resize(spec.stations);
      ys.resize(spec.stations);
      for (std::size_t s = 0; s < spec.stations; ++s) {
        xs[s] = -90.0 - 0.5 * static_cast<double>(s);
        ys[s] = 29.0 - static_cast<double>(s);
      }
    }
    const std::vector<std::size_t> start{0};
    const std::vector<std::size_t> count{spec.stations};
    put_block(f, x, start, count, xs);
    put_block(f, y, start, count, ys);
  }
  if (names >= 0) {
    const std::string bytes =
        padded_rows(spec.station_names, spec.name_len, '\0');
    check(nc_put_var_text(f.id(), names, bytes.data()), "station_name");
  }
  const auto value = spec.value
                         ? spec.value
                         : [](std::size_t t, std::size_t s, std::size_t c) {
                             return 100.0 * static_cast<double>(c) +
                                    10.0 * static_cast<double>(s) +
                                    0.25 * static_cast<double>(t);
                           };
  for (std::size_t c = 0; c < data.size(); ++c) {
    if (spec.transposed_data) {
      std::vector<double> all(spec.stations * spec.steps);
      for (std::size_t s = 0; s < spec.stations; ++s) {
        for (std::size_t t = 0; t < spec.steps; ++t) {
          all[s * spec.steps + t] = value(t, s, c);
        }
      }
      put_block(f, data[c], {0, 0}, {spec.stations, spec.steps}, all);
      continue;
    }
    for (std::size_t t0 = 0; t0 < spec.steps; t0 += spec.write_rows) {
      const std::size_t rows = std::min(spec.write_rows, spec.steps - t0);
      std::vector<double> block(rows * spec.stations);
      for (std::size_t r = 0; r < rows; ++r) {
        for (std::size_t s = 0; s < spec.stations; ++s) {
          block[r * spec.stations + s] = value(t0 + r, s, c);
        }
      }
      put_block(f, data[c], {t0, 0}, {rows, spec.stations}, block);
    }
  }
  f.close();
}

// ---- D-Flow FM -------------------------------------------------------------

void make_dflow_nc(const std::filesystem::path& path, const DflowNc& spec) {
  Raw f{path};
  int time_dim = -1;
  int station_dim = -1;
  const auto define_time = [&] {
    if (not spec.omit_time_dim) {
      time_dim = f.dim("time", NC_UNLIMITED);
    }
  };
  const auto define_stations = [&] {
    if (not spec.omit_stations_dim) {
      station_dim = f.dim("stations", spec.stations);
    }
  };
  if (spec.stations_dim_first) {
    define_stations();
    define_time();
  } else {
    define_time();
    define_stations();
  }
  const int name_len = f.dim("name_len", spec.name_len);
  int laydim = -1;
  int laydimw = -1;
  if (spec.layers > 0) {
    laydim = f.dim("laydim", spec.layers);
  }
  if (spec.interfaces > 0) {
    laydimw = f.dim("laydimw", spec.interfaces);
  }

  // The dimensions of a variable: those of `wanted` that the file has.
  const auto dims_of = [](std::initializer_list<int> wanted) {
    std::vector<int> dims;
    std::ranges::copy_if(wanted, std::back_inserter(dims),
                         [](int id) { return id >= 0; });
    return dims;
  };
  int time = -1;
  if (not spec.omit_time_var) {
    time = f.var("time", NC_DOUBLE, dims_of({time_dim}));
    if (not spec.time_units.empty()) {
      f.text(time, "units", spec.time_units);
    }
    f.text(time, "standard_name", "time");
    if (spec.calendar) {
      f.text(time, "calendar", *spec.calendar);
    }
  }
  int names = -1;
  int xs = -1;
  int ys = -1;
  const nc_type coordinate_type = nc_type_of(spec.coordinate_type);
  if (not spec.omit_names) {
    names = f.var("station_name", NC_CHAR, dims_of({station_dim, name_len}));
  }
  if (not spec.omit_coordinates) {
    xs = f.var("station_x_coordinate", coordinate_type, dims_of({station_dim}));
    ys = f.var("station_y_coordinate", coordinate_type, dims_of({station_dim}));
  }
  std::vector<int> ids;
  for (const DflowVar& v : spec.vars) {
    std::vector<int> dims = dims_of({time_dim, station_dim});
    if (v.shape == 1) {
      dims.push_back(laydim);
    } else if (v.shape == 2) {
      dims.push_back(laydimw);
    }
    const nc_type type = nc_type_of(v.type);
    const int id = f.var(v.name.c_str(), type, dims);
    put_attributes(f, id, v);
    if (v.fill) {
      put_double_att(f, id, "_FillValue", type, *v.fill);
    }
    ids.push_back(id);
  }
  f.enddef();

  if (time >= 0) {
    put_times(
        f, time,
        spec.times.empty() ? default_times(spec.steps, 600.0) : spec.times);
  }
  if (names >= 0 and not spec.station_names.empty()) {
    const std::string bytes =
        padded_rows(spec.station_names, spec.name_len, spec.pad);
    check(nc_put_var_text(f.id(), names, bytes.data()), "station_name");
  }
  if (xs >= 0) {
    std::vector<double> x = spec.x;
    std::vector<double> y = spec.y;
    if (x.empty() or y.empty()) {
      x.resize(spec.stations);
      y.resize(spec.stations);
      for (std::size_t s = 0; s < spec.stations; ++s) {
        x[s] = 10.0 + static_cast<double>(s);
        y[s] = 40.0 + 0.5 * static_cast<double>(s);
      }
    }
    const std::vector<std::size_t> start{0};
    const std::vector<std::size_t> count{spec.stations};
    put_block(f, xs, start, count, x);
    put_block(f, ys, start, count, y);
  }
  const std::size_t rows = time_dim < 0 ? 1 : spec.steps;
  for (std::size_t k = 0; k < spec.vars.size(); ++k) {
    const DflowVar& v = spec.vars[k];
    const std::size_t depth = v.shape == 1   ? spec.layers
                              : v.shape == 2 ? spec.interfaces
                                             : 1;
    std::vector<double> block(rows * spec.stations * depth);
    for (std::size_t t = 0; t < rows; ++t) {
      for (std::size_t s = 0; s < spec.stations; ++s) {
        for (std::size_t l = 0; l < depth; ++l) {
          block[(t * spec.stations + s) * depth + l] =
              v.value ? v.value(t, s, l)
                      : 1000.0 * static_cast<double>(k) +
                            100.0 * static_cast<double>(l) +
                            10.0 * static_cast<double>(s) +
                            0.25 * static_cast<double>(t);
        }
      }
    }
    std::vector<std::size_t> start;
    std::vector<std::size_t> count;
    if (time_dim >= 0) {
      start.push_back(0);
      count.push_back(rows);
    }
    start.push_back(0);
    count.push_back(spec.stations);
    if (v.shape != 0) {
      start.push_back(0);
      count.push_back(depth);
    }
    put_block(f, ids[k], start, count, block);
  }
  f.close();
}

}  // namespace mov::test::ncgen
