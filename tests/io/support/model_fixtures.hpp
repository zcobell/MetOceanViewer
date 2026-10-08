// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// ADCIRC and D-Flow FM netCDF output written at test time with the raw
// netCDF-C API, independent of mov::io (see nc_fixtures.hpp). The ADCIRC
// layout is that of the legacy fixtures (MetOceanViewer/function_tests, LF
// section 12); the D-Flow layout is built from the D-Flow FM manual's
// description of a history file, because no real file is available (plan
// decision 26).

#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace mov::test::ncgen {

/// The external type of the data variables.
enum class DataType { float64, float32, int8, int16, int32, int64, uint8 };
/// The file format.
enum class FileFormat { netcdf4, classic, offset64 };
/// The external type of the time variable.
enum class TimeType { float64, int64 };

/// An ADCIRC station output. Dimensions `time` (unlimited unless
/// `fixed_time`) and `station` (and `namelen` with names); variables
/// `time(time)`, `x(station)`, `y(station)` (double), `station_name(station,
/// namelen)`, and one data variable per entry of `variables`, over (time,
/// station), in `type`. Global attributes `model = "ADCIRC"`, `dt`, and the
/// junk `base_date` the legacy files carry.
struct AdcircNc {
  std::vector<std::string> variables{"zeta"};
  std::size_t stations{3};
  std::size_t steps{5};
  DataType type{DataType::float64};

  /// The value of component `c` (0 for the first variable) at time index `t`
  /// and station `s`: the physical value, or for int16 the stored value. NaN
  /// is allowed. Default: 100 c + 10 s + t / 4 (exact in float).
  std::function<double(std::size_t t, std::size_t s, std::size_t c)> value;

  /// `_FillValue` of the data variables (in their type); none if empty. The
  /// library's default fill applies then.
  std::optional<double> fill{-99999.0};
  std::optional<double> missing_value;
  std::optional<double> scale_factor;
  std::optional<double> add_offset;

  /// x and y per station; default x = -90 - s / 2, y = 29 - s.
  std::vector<double> x;
  std::vector<double> y;
  bool write_coordinates{true};

  /// Seconds of each time step; default 600 (t + 1).
  std::vector<double> times;
  TimeType time_type{TimeType::float64};
  /// `_FillValue` of `time`; none if empty.
  std::optional<double> time_fill;
  bool write_time{true};
  /// `time:units`; no attribute if empty.
  std::optional<std::string> time_units{"seconds since 2010-01-01 00:00:00"};
  /// `time:calendar`; no attribute if empty.
  std::optional<std::string> time_calendar;
  std::optional<std::string> model{"ADCIRC"};
  /// `model` is written as an integer, not text.
  bool model_as_number{false};
  /// The global `ics` (1 Cartesian, 2 spherical); none if empty.
  std::optional<int> ics;

  /// Rows of `station_name` (NUL-padded to `name_len`; a row may hold junk
  /// after a NUL). No variable if empty.
  std::vector<std::string> station_names;
  std::size_t name_len{50};

  /// The `station` dimension is defined before `time` (so `time` is not
  /// dimension 0).
  bool station_dim_first{false};
  bool fixed_time{false};
  /// netcdf4 (HDF5), or a classic file: CDF-1, or CDF-2 (64-bit offsets). A
  /// classic file has no chunks or compression (`chunks` and `deflate` are
  /// ignored) and its variables are contiguous.
  FileFormat format{FileFormat::netcdf4};
  /// Deflate level of the data variables (shuffle on); 0 is none.
  int deflate{0};
  /// The data variables are over (station, time), not (time, station).
  bool transposed_data{false};
  /// Chunk sizes (time, station) of the data variables; netCDF-C's default
  /// if empty.
  std::vector<std::size_t> chunks;
  /// Written time steps per call (memory of the generator).
  std::size_t write_rows{256};
};

void make_adcirc_nc(const std::filesystem::path& path, const AdcircNc& spec);

/// A data variable of a D-Flow history file.
struct DflowVar {
  std::string name;
  /// 0: (time, stations); 1: (time, stations, laydim); 2: (time, stations,
  /// laydimw).
  int shape{0};
  /// The attributes; none if empty.
  std::string units;
  std::string standard_name;
  std::string long_name;
  /// `_FillValue` (in the variable's type); none if empty.
  std::optional<double> fill{-999.0};
  DataType type{DataType::float64};
  /// The three attributes above are written as integers, not text.
  bool numeric_attributes{false};
  /// The value at time index t, station s, layer l (0 for a flat variable);
  /// NaN allowed. Default: 1000 (variable index) + 100 l + 10 s + t / 4.
  std::function<double(std::size_t t, std::size_t s, std::size_t l)> value;
};

/// A D-Flow FM history file. Dimensions `time`, `stations`, `name_len`, and
/// `laydim` / `laydimw` when `layers` / `interfaces` are not 0; variables
/// `time(time)`, `station_name(stations, name_len)`, `station_x_coordinate`,
/// `station_y_coordinate`, and `vars`.
struct DflowNc {
  std::size_t stations{3};
  std::size_t steps{4};
  std::size_t layers{0};
  std::size_t interfaces{0};
  std::size_t name_len{64};
  /// Rows of `station_name`, padded with `pad` (D-Flow pads with blanks; the
  /// legacy fixtures with NULs). Left unwritten (all NUL) if empty.
  std::vector<std::string> station_names;
  char pad{' '};
  /// Default x = 10 + s, y = 40 + s / 2 (degrees).
  std::vector<double> x;
  std::vector<double> y;
  DataType coordinate_type{DataType::float64};
  std::vector<double> times;  // default 600 t, in the units of `time`
  std::string time_units{"seconds since 2001-01-01 00:00:00"};
  std::optional<std::string> calendar;
  std::vector<DflowVar> vars;

  /// `stations` is defined before `time`.
  bool stations_dim_first{false};
  /// Leave out what the names say (the file is then not a history file).
  bool omit_time_dim{false};
  bool omit_stations_dim{false};
  bool omit_time_var{false};
  bool omit_coordinates{false};
  bool omit_names{false};
  /// The coordinates are over (time, stations); step t is the position plus
  /// 0.5 t (a model with moving stations).
  bool coordinates_over_time{false};
};

void make_dflow_nc(const std::filesystem::path& path, const DflowNc& spec);

}  // namespace mov::test::ncgen
