// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cmath>
#include <expected>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/io/projection.hpp"

namespace {

using mov::core::Epsg;
using mov::core::Location;
using mov::core::LocationError;
using mov::core::NativePoint;
using mov::core::Xy;
using mov::io::ProjectionErrc;
using mov::io::ProjectionError;
using mov::io::Projector;
using mov::io::to_location;

Epsg epsg(int code) {
  const auto made = Epsg::make(code);
  REQUIRE(made.has_value());
  return *made;
}

NativePoint native(double x, double y, int code) {
  const auto made = NativePoint::make(Xy{.x = x, .y = y}, epsg(code));
  REQUIRE(made.has_value());
  return *made;
}

// 1e-7 degrees is about one centimetre.
constexpr double degrees_tolerance = 1e-7;

bool near_location(const Location& got, double lat, double lon) {
  return std::abs(got.lat() - lat) <= degrees_tolerance and
         std::abs(got.lon() - lon) <= degrees_tolerance;
}

// Reference values from an independent implementation (the Krueger series of
// the UTM projection, GRS80, in double precision) of lat/lon -> easting,
// northing in zone 15N (central meridian 93 W); regenerate with
// tests/io/utm_reference.py.
struct UtmCase {
  double lat;
  double lon;
  double easting;
  double northing;
};
constexpr UtmCase utm(double lat, double lon, double easting, double northing) {
  return UtmCase{
      .lat = lat, .lon = lon, .easting = easting, .northing = northing};
}

constexpr std::array utm15n{
    utm(29.98, -90.01, 788502.4135602423, 3320332.9763316396),
    utm(29.0, -94.0, 402597.42743897205, 3208397.729925411),
    utm(45.0, -93.0, 500000.0, 4982950.400106854),
    utm(0.5, -91.5, 666925.3263676233, 55284.10835407458),
    utm(60.0, -95.25, 374516.3371712304, 6653545.372944641),
};

}  // namespace

TEST_CASE("EPSG:4326 passes through without PROJ", "[io][projection]") {
  const auto location = to_location(native(-90.01, 29.98, 4326));
  REQUIRE(location.has_value());
  // Exactly the coordinates, x as longitude and y as latitude.
  CHECK(*location == *Location::make({.lat = 29.98, .lon = -90.01}));
}

TEST_CASE("EPSG:4326 longitudes use the Location normalization",
          "[io][projection]") {
  const auto east = to_location(native(270.0, 10.0, 4326));
  REQUIRE(east.has_value());
  CHECK(east->lon() == -90.0);
  const auto antimeridian = to_location(native(-180.0, 0.0, 4326));
  REQUIRE(antimeridian.has_value());
  CHECK(antimeridian->lon() == 180.0);
}

TEST_CASE("an EPSG:4326 point outside the Location ranges is a LocationError",
          "[io][projection]") {
  const auto high = to_location(native(0.0, 91.0, 4326));
  REQUIRE(not high.has_value());
  CHECK(high.error() == std::variant<ProjectionError, LocationError>{
                            LocationError::latitude_out_of_range});
  const auto wide = to_location(native(361.0, 0.0, 4326));
  REQUIRE(not wide.has_value());
  CHECK(std::holds_alternative<LocationError>(wide.error()));
}

TEST_CASE("UTM zone 15N (EPSG:26915) agrees with the reference series",
          "[io][projection]") {
  for (const UtmCase& c : utm15n) {
    CAPTURE(c.lat, c.lon);
    const auto location = to_location(native(c.easting, c.northing, 26915));
    REQUIRE(location.has_value());
    CHECK(near_location(*location, c.lat, c.lon));
  }
}

TEST_CASE("Web Mercator (EPSG:3857) is inverted exactly", "[io][projection]") {
  // x = R * lon, y = R * ln(tan(pi/4 + lat/2)), R = 6378137.
  const auto new_orleans =
      to_location(native(-10019867.366302555, 3500979.288950576, 3857));
  REQUIRE(new_orleans.has_value());
  CHECK(near_location(*new_orleans, 29.98, -90.01));
  const auto southern =
      to_location(native(18952143.307554826, -5700582.732404124, 3857));
  REQUIRE(southern.has_value());
  CHECK(near_location(*southern, -45.5, 170.25));
}

TEST_CASE("a Projector converts many points of one CRS", "[io][projection]") {
  auto projector = Projector::make(epsg(26915));
  REQUIRE(projector.has_value());
  CHECK(projector->crs() == epsg(26915));
  for (const UtmCase& c : utm15n) {
    const auto location =
        projector->to_location(Xy{.x = c.easting, .y = c.northing});
    REQUIRE(location.has_value());
    CHECK(near_location(*location, c.lat, c.lon));
  }
}

TEST_CASE("a Projector can be moved", "[io][projection]") {
  auto made = Projector::make(epsg(26915));
  REQUIRE(made.has_value());
  Projector moved{std::move(*made)};
  const UtmCase& c = utm15n[0];
  const auto location = moved.to_location(Xy{.x = c.easting, .y = c.northing});
  REQUIRE(location.has_value());
  CHECK(near_location(*location, c.lat, c.lon));

  auto other = Projector::make(epsg(3857));
  REQUIRE(other.has_value());
  other = std::move(moved);
  CHECK(other->crs() == epsg(26915));
  CHECK(other->to_location(Xy{.x = c.easting, .y = c.northing}).has_value());
}

TEST_CASE("the identity Projector for EPSG:4326 needs no PROJ",
          "[io][projection]") {
  auto projector = Projector::make(Epsg::wgs84());
  REQUIRE(projector.has_value());
  const auto location = projector->to_location(Xy{.x = -90.5, .y = 30.5});
  REQUIRE(location.has_value());
  CHECK(*location == *Location::make({.lat = 30.5, .lon = -90.5}));
}

TEST_CASE("an unknown EPSG code is reported with the code",
          "[io][projection]") {
  const auto result = to_location(native(1.0, 2.0, 999999));
  REQUIRE(not result.has_value());
  CHECK(result.error() ==
        std::variant<ProjectionError, LocationError>{ProjectionError{
            .code = ProjectionErrc::unknown_crs, .crs = epsg(999999)}});
  const auto made = Projector::make(epsg(999999));
  REQUIRE(not made.has_value());
  CHECK(made.error().code == ProjectionErrc::unknown_crs);
  CHECK(made.error().crs == epsg(999999));
}

TEST_CASE("the per-thread cache follows the CRS of the latest call",
          "[io][projection]") {
  const UtmCase& c = utm15n[0];
  const auto utm_point = native(c.easting, c.northing, 26915);
  const auto mercator_point =
      native(-10019867.366302555, 3500979.288950576, 3857);
  for (int round = 0; round < 3; ++round) {
    const auto a = to_location(utm_point);
    const auto b = to_location(mercator_point);
    const auto d = to_location(native(-90.01, 29.98, 4326));
    REQUIRE(a.has_value());
    REQUIRE(b.has_value());
    REQUIRE(d.has_value());
    CHECK(near_location(*a, 29.98, -90.01));
    CHECK(near_location(*b, 29.98, -90.01));
    CHECK(near_location(*d, 29.98, -90.01));
  }
}

TEST_CASE("threads convert independently", "[io][projection]") {
  const UtmCase& c = utm15n[1];
  const auto point = native(c.easting, c.northing, 26915);
  std::vector<int> ok(4,
                      0);  // not vector<bool>: threads write adjacent elements
  {
    std::vector<std::jthread> threads;
    threads.reserve(ok.size());
    for (int& slot : ok) {
      threads.emplace_back([&slot, &point] {
        bool all = true;
        for (int n = 0; n < 20; ++n) {
          const auto location = to_location(point);
          all = all and location.has_value() and
                near_location(*location, c.lat, c.lon);
        }
        slot = all ? 1 : 0;
      });
    }
  }
  for (const int thread_ok : ok) {
    CHECK(thread_ok == 1);
  }
}

TEST_CASE("a point the projection cannot invert is transform_failed",
          "[io][projection]") {
  // Far outside the zone: the transverse Mercator inverse does not converge.
  const auto result = to_location(native(1e15, 0.0, 26915));
  REQUIRE(not result.has_value());
  CHECK(result.error() ==
        std::variant<ProjectionError, LocationError>{ProjectionError{
            .code = ProjectionErrc::transform_failed, .crs = epsg(26915)}});

  // The Projector stays usable afterwards.
  auto projector = Projector::make(epsg(26915));
  REQUIRE(projector.has_value());
  CHECK(not projector->to_location(Xy{.x = 1e15, .y = 0.0}).has_value());
  const UtmCase& c = utm15n[0];
  CHECK(
      projector->to_location(Xy{.x = c.easting, .y = c.northing}).has_value());
}

TEST_CASE("a code that is not a horizontal CRS is not usable",
          "[io][projection]") {
  // 5703 is a vertical CRS (NAVD88 height) and 4978 a geocentric one: PROJ
  // would turn either into meaningless angles.
  for (const int code : {5703, 4978}) {
    CAPTURE(code);
    const auto made = Projector::make(epsg(code));
    REQUIRE(not made.has_value());
    CHECK(made.error().code == ProjectionErrc::unknown_crs);
  }
}
