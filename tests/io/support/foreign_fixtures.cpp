// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "foreign_fixtures.hpp"

#include <netcdf.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "nc_build.hpp"

namespace mov::test::ncgen {

namespace {

constexpr double default_fill = 9.9692099683868690e+36;

std::size_t longest(const std::vector<std::vector<double>>& rows) {
  std::size_t n = 0;
  for (const auto& row : rows) {
    n = std::max(n, row.size());
  }
  return n;
}

std::size_t total(const std::vector<std::vector<double>>& rows) {
  std::size_t n = 0;
  for (const auto& row : rows) {
    n += row.size();
  }
  return n;
}

void define_positions(Cdf& f, std::optional<int> station) {
  for (const auto& [name, units] :
       {std::pair<const char*, const char*>{"lat", "degrees_north"},
        {"lon", "degrees_east"}}) {
    if (station) {
      f.var(name, NC_DOUBLE, {station.value()});
    } else {
      f.var(name, NC_DOUBLE);
    }
    f.text(name, "units", units);
  }
}

void put_positions(Cdf& f, std::size_t stations) {
  std::vector<double> lat;
  std::vector<double> lon;
  for (std::size_t s = 0; s < stations; ++s) {
    lat.push_back(29.0 + (0.5 * static_cast<double>(s)));
    lon.push_back(-90.0 + (0.5 * static_cast<double>(s)));
  }
  f.put("lat", lat);
  f.put("lon", lon);
}

/// `time` and `temperature`: over the sample dimension alone, or over the
/// matrix `dims` (the incomplete layout has a 2-D time; the orthogonal layout a
/// 2-D temperature).
void define_time_and_data(Cdf& f, const CfSpec& spec, int sample,
                          const std::vector<int>& dims) {
  const bool incomplete = spec.kind == CfKind::incomplete;
  const bool matrix_data = incomplete or spec.kind == CfKind::orthogonal;
  if (incomplete) {
    f.var("time", spec.time_type, {dims[0], dims[1]});
    if (spec.time_fill) {
      f.num("time", "_FillValue", spec.time_type, {*spec.time_fill});
    }
  } else {
    f.var("time", spec.time_type, {sample});
  }
  f.text("time", "units", spec.time_units);
  f.text("time", "standard_name", "time");
  if (matrix_data) {
    f.var("temperature", spec.data_type, {dims[0], dims[1]});
  } else {
    f.var("temperature", spec.data_type, {sample});
  }
  f.text("temperature", "units", "degC");
  f.num("temperature", "_FillValue", spec.data_type, {cf_missing});
}

}  // namespace

void make_cf(const std::filesystem::path& path, const CfSpec& spec) {
  Cdf f{path, spec.cmode};
  f.text("", "Conventions", spec.conventions);
  f.text("", "featureType", "timeSeries");
  const std::size_t stations = spec.ids.size();
  const bool single = spec.kind == CfKind::single_station;
  // The sample dimension and the variables over it.
  std::size_t length = 0;
  switch (spec.kind) {
    case CfKind::orthogonal:
    case CfKind::single_station:
      length = spec.times.at(0).size();
      break;
    case CfKind::incomplete:
      length = longest(spec.times);
      break;
    case CfKind::contiguous_ragged:
    case CfKind::indexed_ragged:
      length = total(spec.times);
      break;
  }
  const bool ragged_kind = spec.kind == CfKind::contiguous_ragged or
                           spec.kind == CfKind::indexed_ragged;
  std::optional<int> early_sample;
  if (spec.sample_first) {
    early_sample =
        f.dim(ragged_kind or spec.kind == CfKind::incomplete ? "obs" : "time",
              length);
  }
  std::optional<int> station;
  int station_id = -1;
  if (not single) {
    station = f.dim("station", stations);
    station_id = *station;
  }
  const int width = f.dim("name_strlen", spec.id_width);
  const nc_type id_type = spec.string_ids    ? NC_STRING
                          : spec.integer_ids ? NC_INT
                                             : NC_CHAR;
  const bool rows = id_type == NC_CHAR;
  if (single) {
    if (rows) {
      f.var("station_name", id_type, {width});
    } else {
      f.var("station_name", id_type);
    }
  } else if (rows and spec.flat_ids) {
    f.var("station_name", id_type, {width});
  } else if (rows) {
    f.var("station_name", id_type, {station_id, width});
  } else {
    f.var("station_name", id_type, {station_id});
  }
  f.text("station_name", "cf_role", "timeseries_id");
  define_positions(f, station);

  const bool ragged = spec.kind == CfKind::contiguous_ragged or
                      spec.kind == CfKind::indexed_ragged;
  const int sample =
      early_sample
          ? *early_sample
          : f.dim(ragged or spec.kind == CfKind::incomplete ? "obs" : "time",
                  length);
  std::vector<int> dims;
  if (single or ragged) {
    dims = {sample};
  } else if (spec.transposed) {
    dims = {sample, station_id};
  } else {
    dims = {station_id, sample};
  }
  define_time_and_data(f, spec, sample, dims);
  if (spec.kind == CfKind::contiguous_ragged) {
    f.var("rowSize", spec.helper_type, {station_id});
    f.text("rowSize", "sample_dimension", "obs");
  }
  if (spec.kind == CfKind::indexed_ragged) {
    f.var("stationIndex", spec.helper_type, {sample});
    f.text("stationIndex", "instance_dimension", "station");
  }
  if (spec.obs_count) {
    f.var("obs_count", spec.helper_type, {station_id});
  }
  if (spec.customize) {
    spec.customize(f, station.value_or(-1), sample);
  }

  // Data.
  if (spec.string_ids) {
    f.put_strings("station_name", spec.ids, spec.null_ids);
  } else if (spec.integer_ids) {
    std::vector<double> numbers;
    numbers.reserve(spec.ids.size());
    for (const std::string& id : spec.ids) {
      numbers.push_back(std::stod(id));
    }
    f.put("station_name", numbers);
  } else if (single) {
    std::string row = spec.ids.at(0);
    row.resize(spec.id_width, '\0');
    f.put_text("station_name", row);
  } else if (spec.flat_ids) {
    std::string row = spec.ids.at(0);
    row.resize(spec.id_width, '\0');
    f.put_text("station_name", row);
  } else {
    f.put_rows("station_name", spec.id_width, spec.ids);
  }
  put_positions(f, stations);
  std::vector<double> time;
  std::vector<double> value;
  const double time_pad = spec.time_fill.value_or(default_fill);
  if (spec.kind == CfKind::orthogonal or single) {
    time = spec.times.at(0);
  }
  if (spec.kind == CfKind::orthogonal or spec.kind == CfKind::incomplete) {
    time.resize(spec.kind == CfKind::incomplete ? stations * length : length);
    value.assign(stations * length, cf_missing);
    if (spec.kind == CfKind::incomplete) {
      std::ranges::fill(time, time_pad);
    }
    for (std::size_t s = 0; s < stations; ++s) {
      for (std::size_t j = 0; j < spec.values.at(s).size(); ++j) {
        const std::size_t at =
            spec.transposed ? (j * stations) + s : (s * length) + j;
        value[at] = spec.values[s][j];
        if (spec.kind == CfKind::incomplete) {
          time[at] = spec.times.at(s).at(j);
        }
      }
    }
  } else if (single) {
    value = spec.values.at(0);
  } else if (spec.kind == CfKind::contiguous_ragged) {
    std::vector<std::int64_t> sizes;
    for (std::size_t s = 0; s < stations; ++s) {
      sizes.push_back(static_cast<std::int64_t>(spec.times.at(s).size()));
      time.insert(time.end(), spec.times[s].begin(), spec.times[s].end());
      value.insert(value.end(), spec.values.at(s).begin(),
                   spec.values.at(s).end());
    }
    f.put_i64("rowSize", sizes);
  } else {
    // Round robin over the stations: s0[0], s1[0], s2[0], s0[1], ...
    std::vector<std::int64_t> index;
    for (std::size_t k = 0; k < longest(spec.times); ++k) {
      for (std::size_t s = 0; s < stations; ++s) {
        if (k < spec.times.at(s).size()) {
          index.push_back(static_cast<std::int64_t>(s));
          time.push_back(spec.times[s][k]);
          value.push_back(spec.values.at(s).at(k));
        }
      }
    }
    f.put_i64("stationIndex", index);
  }
  f.put("time", time);
  f.put("temperature", value);
  if (spec.obs_count) {
    f.put_i64("obs_count", *spec.obs_count);
  }
  f.close();
}

}  // namespace mov::test::ncgen
