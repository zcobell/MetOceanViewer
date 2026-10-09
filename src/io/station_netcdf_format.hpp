// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// What the v5 station netCDF writer and reader share: the fixed names and
// attribute values of docs/station-netcdf.md (SN), and the parsers of the
// header attributes. Private to src/io/.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "mov/io/netcdf/name.hpp"

namespace mov::io::detail::station_nc {

// ---- names (SN 4)
// ----------------------------------------------------------------

inline constexpr nc::NcNameRef station_dim{"station"};
inline constexpr nc::NcNameRef time_dim{"time"};
inline constexpr nc::NcNameRef obs_dim{"obs"};
inline constexpr nc::NcNameRef id_len_dim{"station_id_len"};
inline constexpr nc::NcNameRef name_len_dim{"station_name_len"};
inline constexpr nc::NcNameRef provider_len_dim{"station_provider_len"};

inline constexpr nc::NcNameRef station_id{"station_id"};
inline constexpr nc::NcNameRef station_name{"station_name"};
inline constexpr nc::NcNameRef station_provider{"station_provider"};
inline constexpr nc::NcNameRef lat{"lat"};
inline constexpr nc::NcNameRef lon{"lon"};
inline constexpr nc::NcNameRef crs{"crs"};
inline constexpr nc::NcNameRef time{"time"};
inline constexpr nc::NcNameRef obs_count{"obs_count"};

inline constexpr nc::NcNameRef elevation{"elevation"};  // reserved (SN 4.2)

/// Every variable and dimension name the format uses for itself (SN 4.1,
/// 4.2), `elevation` included (reserved for a later minor version): a data
/// variable may not take one. Dimensions are listed too, because a variable
/// named like a dimension it is not the coordinate of confuses xarray.
inline constexpr std::array<nc::NcNameRef, 14> reserved_names{
    station_dim, time_dim,     obs_dim,
    id_len_dim,  name_len_dim, provider_len_dim,
    station_id,  station_name, station_provider,
    lat,         lon,          elevation,
    crs,         obs_count};

[[nodiscard]] constexpr bool is_reserved(std::string_view name) noexcept {
  for (const nc::NcNameRef reserved : reserved_names) {
    if (reserved.view() == name) {
      return true;
    }
  }
  return false;
}

/// The suffix of a data variable's wet/dry status variable (SN 8.2).
inline constexpr std::string_view status_suffix = "_status";

/// The tokens of the columns of a table so far.
using TokenSet = std::set<std::string, std::less<>>;

/// Whether `token` can name a column next to the columns whose tokens are
/// `others` (SN 4.2, 6, 8.2): the one rule of the writer and of every reader
/// that makes a schema. It is not a name the format uses itself; it and its
/// status variable `<token>_status` (reserved for every column whether or not
/// it has Dry samples) are netCDF names (at most NC_MAX_NAME bytes, so the
/// token at most 249); no other column has this token; none has this token's
/// status name; and this is not the status name of another column.
[[nodiscard]] inline bool writable_token(std::string_view token,
                                         const TokenSet& others) {
  if (is_reserved(token) or others.contains(token)) {
    return false;
  }
  const std::string status = std::string{token} + std::string{status_suffix};
  if (not nc::NcName::make(token) or not nc::NcName::make(status) or
      others.contains(status)) {
    return false;
  }
  return not(
      token.ends_with(status_suffix) and
      others.contains(token.substr(0, token.size() - status_suffix.size())));
}

// ---- values (SN 3, 5, 7, 8, 10)
// -----------------------------------------------------

/// NC_FILL_DOUBLE, the _FillValue of every double variable that has one
/// (only src/io/netcdf/ may include <netcdf.h>; the tests compare the two).
inline constexpr double fill_double = 9.9692099683868690e+36;
inline constexpr std::int8_t fill_status = -128;
inline constexpr std::int8_t status_dry = 0;
inline constexpr std::int8_t status_wet = 1;

inline constexpr std::string_view conventions = "CF-1.11";
inline constexpr std::string_view feature_type = "timeSeries";
inline constexpr std::string_view time_units =
    "milliseconds since 1970-01-01 00:00:00";
inline constexpr std::string_view calendar = "proleptic_gregorian";
inline constexpr std::string_view coordinates =
    "time lat lon station_id station_name";
inline constexpr std::string_view epsg_4326 = "EPSG:4326";
inline constexpr double wgs84_semi_major_axis = 6378137.0;
inline constexpr double wgs84_inverse_flattening = 298.257223563;
inline constexpr std::string_view wgs84_wkt =
    R"wkt(GEOGCRS["WGS 84",DATUM["World Geodetic System 1984",ELLIPSOID["WGS 84",6378137,298.257223563,LENGTHUNIT["metre",1]]],PRIMEM["Greenwich",0,ANGLEUNIT["degree",0.0174532925199433]],CS[ellipsoidal,2],AXIS["geodetic latitude (Lat)",north,ORDER[1],ANGLEUNIT["degree",0.0174532925199433]],AXIS["geodetic longitude (Lon)",east,ORDER[2],ANGLEUNIT["degree",0.0174532925199433]],ID["EPSG",4326]])wkt";
inline constexpr std::string_view flag_meanings = "dry wet";

/// Elements of one chunk of a sample variable (SN 3: about 512 KiB of
/// doubles).
inline constexpr std::size_t chunk_elements = 65536;
inline constexpr int deflate_level = 2;

}  // namespace mov::io::detail::station_nc
