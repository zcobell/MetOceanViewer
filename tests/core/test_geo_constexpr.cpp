// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/geo.hpp.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <type_traits>

#include "mov/core/geo.hpp"
#include "test_helpers.hpp"

using mov::core::Epsg;
using mov::core::EpsgError;
using mov::core::LatLon;
using mov::core::Location;
using mov::core::LocationError;
using mov::core::NativePoint;
using mov::core::NativePointError;
using mov::core::Xy;
using mov::test::infinity;
using mov::test::quiet_nan;

namespace {

constexpr auto make_location(double lat, double lon) {
  return Location::make({.lat = lat, .lon = lon});
}

// Normalizing an already-normalized longitude changes nothing, and the result
// is always in (-180, 180].
constexpr bool normalization_is_idempotent() {
  constexpr std::array lons{-180.0, -179.75, -90.0,  -0.5,  0.0,   0.5,  90.0,
                            179.75, 180.0,   180.25, 270.0, 359.5, 360.0};
  return std::ranges::all_of(lons, [](double lon) {
    const auto once = Location::make({.lat = 10.0, .lon = lon});
    if (not once) {
      return false;
    }
    const auto twice = Location::make({.lat = 10.0, .lon = once->lon()});
    return once->lon() > -180.0 and once->lon() <= 180.0 and twice and
           *twice == *once;
  });
}

}  // namespace

TEST_CASE("geo types are value types", "[core][geo][constexpr]") {
  STATIC_REQUIRE(std::copyable<Location>);
  STATIC_REQUIRE(std::equality_comparable<Location>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Location>);
  STATIC_REQUIRE(std::copyable<Epsg>);
  STATIC_REQUIRE(std::equality_comparable<Epsg>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Epsg>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<Location>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<Epsg>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<NativePoint>);
  STATIC_REQUIRE(std::copyable<NativePoint>);
  STATIC_REQUIRE(std::equality_comparable<NativePoint>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<NativePoint>);
  STATIC_REQUIRE(std::regular<LatLon>);
}

TEST_CASE("Location and Epsg have no default state",
          "[core][geo][constexpr][regression][B10]") {
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<Location>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<Epsg>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<NativePoint>);
  STATIC_REQUIRE_FALSE(std::is_constructible_v<NativePoint, Xy, Epsg>);
  // Only make() builds them: no public constructor from raw values.
  STATIC_REQUIRE_FALSE(std::is_constructible_v<Location, double, double>);
  STATIC_REQUIRE_FALSE(std::is_constructible_v<Epsg, int>);
}

TEST_CASE("Location::make takes designated lat and lon",
          "[core][geo][constexpr]") {
  constexpr auto here = Location::make({.lat = 29.98, .lon = -90.01});
  STATIC_REQUIRE(here.has_value());
  STATIC_REQUIRE(here->lat() == 29.98);
  STATIC_REQUIRE(here->lon() == -90.01);
  STATIC_REQUIRE(*here == *make_location(29.98, -90.01));
  STATIC_REQUIRE_FALSE(*here == *make_location(29.98, 90.01));
}

TEST_CASE("Location latitude bounds", "[core][geo][constexpr]") {
  STATIC_REQUIRE(make_location(90.0, 0.0).has_value());
  STATIC_REQUIRE(make_location(-90.0, 0.0).has_value());
  STATIC_REQUIRE(make_location(90.000001, 0.0).error() ==
                 LocationError::latitude_out_of_range);
  STATIC_REQUIRE(make_location(-90.000001, 0.0).error() ==
                 LocationError::latitude_out_of_range);
  STATIC_REQUIRE(make_location(1.0e300, 0.0).error() ==
                 LocationError::latitude_out_of_range);
}

TEST_CASE("Location longitude is normalized to (-180, 180]",
          "[core][geo][constexpr]") {
  // Inside (-180, 180]: unchanged.
  STATIC_REQUIRE(make_location(0.0, 0.0)->lon() == 0.0);
  STATIC_REQUIRE(make_location(0.0, 179.5)->lon() == 179.5);
  STATIC_REQUIRE(make_location(0.0, -179.5)->lon() == -179.5);
  STATIC_REQUIRE(make_location(0.0, 180.0)->lon() == 180.0);
  // -180 and 180 are the same meridian: the half-open interval picks 180.
  STATIC_REQUIRE(make_location(0.0, -180.0)->lon() == 180.0);
  STATIC_REQUIRE(*make_location(0.0, -180.0) == *make_location(0.0, 180.0));
  // 0..360 convention (ADCIRC, NDBC Pacific stations).
  STATIC_REQUIRE(make_location(0.0, 180.5)->lon() == -179.5);
  STATIC_REQUIRE(make_location(0.0, 270.0)->lon() == -90.0);
  STATIC_REQUIRE(make_location(0.0, 359.5)->lon() == -0.5);
  STATIC_REQUIRE(make_location(0.0, 360.0)->lon() == 0.0);
}

TEST_CASE("Location longitude bounds", "[core][geo][constexpr]") {
  STATIC_REQUIRE(make_location(0.0, 360.000001).error() ==
                 LocationError::longitude_out_of_range);
  STATIC_REQUIRE(make_location(0.0, -180.000001).error() ==
                 LocationError::longitude_out_of_range);
  STATIC_REQUIRE(make_location(0.0, 1.0e300).error() ==
                 LocationError::longitude_out_of_range);
}

TEST_CASE("Location rejects non-finite coordinates", "[core][geo][constexpr]") {
  STATIC_REQUIRE(make_location(quiet_nan, 0.0).error() ==
                 LocationError::not_finite);
  STATIC_REQUIRE(make_location(0.0, quiet_nan).error() ==
                 LocationError::not_finite);
  STATIC_REQUIRE(make_location(infinity, 0.0).error() ==
                 LocationError::not_finite);
  STATIC_REQUIRE(make_location(0.0, -infinity).error() ==
                 LocationError::not_finite);
  // Non-finite is reported before range, whichever field it is in.
  STATIC_REQUIRE(make_location(quiet_nan, 1000.0).error() ==
                 LocationError::not_finite);
  STATIC_REQUIRE(make_location(1000.0, quiet_nan).error() ==
                 LocationError::not_finite);
  // Latitude is reported before longitude.
  STATIC_REQUIRE(make_location(1000.0, 1000.0).error() ==
                 LocationError::latitude_out_of_range);
}

TEST_CASE("Epsg::make accepts positive codes only",
          "[core][geo][constexpr][regression][B10]") {
  STATIC_REQUIRE(Epsg::make(4326)->code() == 4326);
  STATIC_REQUIRE(Epsg::make(26915)->code() == 26915);
  STATIC_REQUIRE(Epsg::make(1).has_value());
  STATIC_REQUIRE(Epsg::make(0).error() == EpsgError::not_positive);
  STATIC_REQUIRE(Epsg::make(-4326).error() == EpsgError::not_positive);
  STATIC_REQUIRE(Epsg::make(-2147483647 - 1).error() ==
                 EpsgError::not_positive);
  STATIC_REQUIRE(Epsg::wgs84().code() == 4326);
  STATIC_REQUIRE(Epsg::wgs84() == *Epsg::make(4326));
  STATIC_REQUIRE_FALSE(Epsg::wgs84() == *Epsg::make(26915));
}

TEST_CASE("NativePoint carries its CRS", "[core][geo][constexpr]") {
  constexpr auto point =
      NativePoint::make({.x = 500000.0, .y = 3300000.0}, *Epsg::make(26915));
  STATIC_REQUIRE(point.has_value());
  STATIC_REQUIRE(point->x() == 500000.0);
  STATIC_REQUIRE(point->y() == 3300000.0);
  STATIC_REQUIRE(point->crs().code() == 26915);
  STATIC_REQUIRE(*point == *NativePoint::make({.x = 500000.0, .y = 3300000.0},
                                              *Epsg::make(26915)));
  STATIC_REQUIRE_FALSE(
      *point ==
      *NativePoint::make({.x = 500000.0, .y = 3300000.0}, Epsg::wgs84()));
  STATIC_REQUIRE_FALSE(
      *point ==
      *NativePoint::make({.x = 3300000.0, .y = 500000.0}, *Epsg::make(26915)));
}

TEST_CASE("NativePoint rejects non-finite coordinates",
          "[core][geo][constexpr]") {
  constexpr Epsg crs = Epsg::wgs84();
  STATIC_REQUIRE(NativePoint::make({.x = quiet_nan, .y = 0.0}, crs).error() ==
                 NativePointError::not_finite);
  STATIC_REQUIRE(NativePoint::make({.x = 0.0, .y = quiet_nan}, crs).error() ==
                 NativePointError::not_finite);
  STATIC_REQUIRE(NativePoint::make({.x = infinity, .y = 0.0}, crs).error() ==
                 NativePointError::not_finite);
  STATIC_REQUIRE(NativePoint::make({.x = 0.0, .y = -infinity}, crs).error() ==
                 NativePointError::not_finite);
  // Any finite value is a legal planar coordinate (projections are huge).
  STATIC_REQUIRE(NativePoint::make({.x = -1.0e7, .y = 1.0e7}, crs).has_value());
}

TEST_CASE("Location longitude normalization is idempotent",
          "[core][geo][constexpr]") {
  STATIC_REQUIRE(normalization_is_idempotent());
}

// A designated initializer that leaves out lat or lon is a compile error (GCC
// -Wmissing-field-initializers, Clang -Wmissing-designated-field-initializers,
// under the project's -Wextra -Werror). That cannot be asserted from inside
// this file; tests/cmake/compile_fail/reject_latlon_missing_field.cpp does it.
TEST_CASE("LatLon has no default values", "[core][geo][constexpr]") {
  STATIC_REQUIRE(std::regular<LatLon>);
  STATIC_REQUIRE(std::is_aggregate_v<LatLon>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<LatLon>);
}
