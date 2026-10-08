// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Foreign CF discrete-sampling-geometry `timeSeries` files in the five
// representations of CF 9.3 (H.2.1 to H.2.5 and the single station of 9.2),
// written at test time with the raw netCDF-C API, independent of mov::io.
// One station set and one data variable (`temperature`, degC) are enough to
// show how a layout stores them; the tests that need more define their own
// variables through `customize`.
#pragma once

#include <netcdf.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "nc_build.hpp"

namespace mov::test::ncgen {

enum class CfKind {
  orthogonal,         // CF 9.3.1: time(time), data (station, time)
  incomplete,         // CF 9.3.2: time(station, obs)
  contiguous_ragged,  // CF 9.3.3: rowSize(station), time(obs)
  indexed_ragged,     // CF 9.3.4: stationIndex(obs), time(obs)
  single_station,     // CF 9.2: scalar coordinates, time(time)
};

/// The value of `temperature` that stands for a missing sample.
inline constexpr double cf_missing = -999.0;

struct CfSpec {
  CfKind kind{CfKind::orthogonal};
  /// The matrix is (time, station) / (obs, station) instead.
  bool transposed{false};
  /// NC_NETCDF4, or 0 for a classic file.
  int cmode{NC_NETCDF4};
  std::string conventions{"CF-1.8"};
  /// One id per station; the station dimension is as long as this.
  std::vector<std::string> ids{"A", "B", "C"};
  /// Per station, in `time_units`. An orthogonal file needs equal times.
  std::vector<std::vector<double>> times{
      {0, 1, 2, 3}, {0, 1, 2, 3}, {0, 1, 2, 3}};
  /// Per station; cf_missing is a missing sample.
  std::vector<std::vector<double>> values{
      {20, 21, 22, 23}, {30, 31, cf_missing, 33}, {40, 41, 42, 43}};
  std::string time_units{"hours since 2000-01-01 00:00:00"};
  /// `_FillValue` of the time variable of a 2-D layout; none: the library's
  /// default fill pads it.
  std::optional<double> time_fill{-1.0};
  /// The `obs_count` helper of the incomplete layout.
  std::optional<std::vector<std::int64_t>> obs_count;
  /// Width of the id rows (the char dimension).
  std::size_t id_width{4};
  /// The ids are an NC_STRING variable, or an integer one (the ids must be
  /// decimal numbers then).
  bool string_ids{false};
  bool integer_ids{false};
  /// Elements of an NC_STRING id variable that are NULL, not text.
  std::vector<bool> null_ids;
  /// The char id variable is one row (over the char dimension alone), whatever
  /// the number of stations.
  bool flat_ids{false};
  /// The sample dimension is defined before the station dimension.
  bool sample_first{false};
  /// The external type of `time`, of `temperature`, and of the integer
  /// helpers (`rowSize`, `stationIndex`, `obs_count`).
  nc_type time_type{NC_DOUBLE};
  nc_type data_type{NC_DOUBLE};
  nc_type helper_type{NC_INT};
  /// Called with the file in define mode, after the variables above are
  /// defined and before the data is written, with the station dimension (-1
  /// for a single station) and the sample dimension. It may put data of its
  /// own.
  std::function<void(Cdf&, int station, int sample)> customize;
};

void make_cf(const std::filesystem::path& path, const CfSpec& spec);

}  // namespace mov::test::ncgen
