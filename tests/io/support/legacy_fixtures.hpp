// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Legacy v4 station netCDF files (docs/legacy-formats.md section 5, dialects
// A and B), written at test time with the raw netCDF-C API. The layout
// reproduces `Hmdf::writeNetcdf` (hmdf.cpp:261-432): dimensions
// `numStations` and `stationNameLen`, `char stationName/stationId(numStations,
// stationNameLen)`, `stationXCoordinate`/`stationYCoordinate(numStations)`
// with `HorizontalProjectionEPSG` on X, and per station N (1-based, zero
// padded) `stationLength_N`, `int64 time_station_N` (seconds since
// `referenceDate`) and `data_station_N`. The writer-side defects of v4 that
// reach a file are reproduced on request: junk after the NUL of a name row
// (v4 wrote names with a fixed count of 200, over-reading shorter strings)
// and a 20-byte `referenceDate`.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace mov::test::ncgen {

/// One station of a legacy file.
struct LegacyStation {
  /// The bytes of the `stationName` row (may hold NULs and junk).
  std::string name;
  /// The bytes of the `stationId` row; not written when the file has no
  /// `stationId` variable.
  std::string id;
  double x{-90.0};
  double y{29.0};
  /// Seconds since the file's `referenceDate`.
  std::vector<std::int64_t> seconds;
  std::vector<double> values;
  /// Only the first `written` values are written (all if empty); the rest read
  /// as the default fill.
  std::optional<std::size_t> written;
  /// The attributes of the data variable; none if empty.
  std::string units{"m"};
  std::string datum{"MLLW"};
  /// `_FillValue` of the data variable; none if empty.
  std::optional<double> fill;
};

enum class LegacyData { float64, float32 };
enum class LegacyTime { int64, int32, float64 };

struct LegacyNc {
  std::vector<LegacyStation> stations;
  /// `stationNameLen`: the row length of the name variables.
  std::size_t name_len{200};
  /// Digits of the station number in the variable names (4:
  /// `time_station_0001`).
  int width{4};
  /// The marks of dialect A.
  bool write_station_id{true};
  bool write_fileformat{true};
  /// `referenceDate` on every time variable; none if empty. Written with the
  /// bytes given (v4's `"1970-01-01 00:00:00\0"` is 20).
  std::optional<std::string> reference_date{
      std::string{"1970-01-01 00:00:00\0", 20}};
  /// `timezone` on every time variable; none if empty.
  std::optional<std::string> timezone{"utc"};
  /// `HorizontalProjectionEPSG` on the X variable (an int); none if empty.
  std::optional<int> epsg{4326};
  /// ... as text instead of an int (v4's getter misread it).
  bool epsg_as_text{false};
  /// The external type of the integer EPSG code.
  int epsg_type{4};  // NC_INT
  LegacyData data_type{LegacyData::float64};
  LegacyTime time_type{LegacyTime::int64};
  /// Leave out what the names say.
  bool omit_num_stations{false};
  bool omit_x{false};
  bool omit_y{false};
  bool omit_names{false};
  /// Leave out the time / data variable of station N (1-based).
  std::optional<std::size_t> omit_time_of;
  std::optional<std::size_t> omit_data_of;
  /// Write `time_station_N` over the dimension of station N + 1.
  std::optional<std::size_t> wrong_dim_for;
};

void make_legacy_nc(const std::filesystem::path& path, const LegacyNc& spec);

/// A CRMS file (dialect C, dropped): `nstation`, `time_station_000001`,
/// `data_station_000001(numParam, stationLength_000001)`, no coordinates.
void make_crms_nc(const std::filesystem::path& path);

/// The name `<prefix><n>` with n zero-padded to `width` digits (wider when n
/// needs more): the same rule v4's `%04i` has.
[[nodiscard]] std::string legacy_name(const std::string& prefix, std::size_t n,
                                      int width);

}  // namespace mov::test::ncgen
