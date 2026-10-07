// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstdint>
#include <expected>

#include "mov/core/detail/numeric.hpp"

namespace mov::core {

/// Raw coordinates in degrees, as read from a source. Build a Location from
/// it with designated initializers: Location::make({.lat = 29.98, .lon =
/// -90.01}).
struct LatLon {
  double lat{};
  double lon{};
  friend constexpr bool operator==(const LatLon&, const LatLon&) = default;
};

enum class LocationError : std::uint8_t {
  not_finite,
  latitude_out_of_range,
  longitude_out_of_range,
};

/// A WGS84 position. lat is in [-90, 90]. Input longitude may be in [-180,
/// 360] (the 0..360 convention of some models and buoys) and is normalized to
/// (-180, 180]. There is no default Location.
class Location {
 public:
  [[nodiscard]] static constexpr std::expected<Location, LocationError> make(
      LatLon p) noexcept {
    if (not detail::is_finite(p.lat) or not detail::is_finite(p.lon)) {
      return std::unexpected{LocationError::not_finite};
    }
    if (p.lat < -90.0 or p.lat > 90.0) {
      return std::unexpected{LocationError::latitude_out_of_range};
    }
    if (p.lon < -180.0 or p.lon > 360.0) {
      return std::unexpected{LocationError::longitude_out_of_range};
    }
    return Location{p.lat, normalized_longitude(p.lon)};
  }

  [[nodiscard]] constexpr double lat() const noexcept { return lat_; }
  [[nodiscard]] constexpr double lon() const noexcept { return lon_; }

  friend constexpr bool operator==(const Location&, const Location&) = default;

 private:
  constexpr Location(double lat, double lon) noexcept : lat_{lat}, lon_{lon} {}

  // [-180, 360] -> (-180, 180]
  static constexpr double normalized_longitude(double lon) noexcept {
    if (lon == -180.0) {
      return 180.0;
    }
    return lon > 180.0 ? lon - 360.0 : lon;
  }

  double lat_;
  double lon_;
};

enum class EpsgError : std::uint8_t { not_positive };

/// An EPSG coordinate reference system code. There is no default: a CRS is
/// always stated (B10: v4 read the code with the wrong netCDF type).
class Epsg {
 public:
  [[nodiscard]] static constexpr std::expected<Epsg, EpsgError> make(
      int code) noexcept {
    if (code <= 0) {
      return std::unexpected{EpsgError::not_positive};
    }
    return Epsg{code};
  }
  [[nodiscard]] static constexpr Epsg wgs84() noexcept { return Epsg{4326}; }

  [[nodiscard]] constexpr int code() const noexcept { return code_; }

  friend constexpr bool operator==(Epsg, Epsg) = default;

 private:
  explicit constexpr Epsg(int code) noexcept : code_{code} {}

  int code_;
};

/// A position in a file's own CRS, kept as metadata next to the WGS84
/// Location.
struct NativePoint {
  double x;
  double y;
  Epsg crs;
  friend constexpr bool operator==(const NativePoint&,
                                   const NativePoint&) = default;
};

}  // namespace mov::core
