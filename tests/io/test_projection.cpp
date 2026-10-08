// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/io/projection.hpp"
#include "mov/io/warning.hpp"
#include "mov/test/scratch_dir.hpp"

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

// The directory of this build's proj.db (see tests/io/CMakeLists.txt); an
// application would pass the one it ships.
void configure_test_database() {
#if defined(MOV_TEST_PROJ_DATA_DIR)
  mov::io::set_projection_data_dir(MOV_TEST_PROJ_DATA_DIR);
#endif
}

const bool database_configured = (configure_test_database(), true);

// Sets (or, with nullopt, removes) an environment variable for the lifetime of
// the object, then restores the old value.
class ScopedEnv {
 public:
  ScopedEnv(const char* name, const std::optional<std::string>& value)
      : name_{name} {
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    if (const char* old = std::getenv(name)) {
      previous_ = old;
    }
    set(value);
  }
  ScopedEnv(const ScopedEnv&) = delete;
  ScopedEnv& operator=(const ScopedEnv&) = delete;
  ScopedEnv(ScopedEnv&&) = delete;
  ScopedEnv& operator=(ScopedEnv&&) = delete;
  ~ScopedEnv() { set(previous_); }

 private:
  void set(const std::optional<std::string>& value) const {
#ifdef _WIN32
    _putenv_s(name_, value ? value->c_str() : "");
#else
    if (value) {
      ::setenv(name_, value->c_str(), 1);
    } else {
      ::unsetenv(name_);
    }
#endif
  }

  const char* name_;
  std::optional<std::string> previous_;
};

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

TEST_CASE("the kind of a CRS: geographic is any geographic 2D or 3D CRS",
          "[io][projection]") {
  configure_test_database();
  using mov::io::CrsKind;
  const auto kind = [](int code) { return mov::io::crs_kind(epsg(code)); };
  CHECK(kind(4326) == CrsKind::geographic);
  CHECK(kind(4269) == CrsKind::geographic);  // NAD83, not EPSG:4326
  CHECK(kind(4979) == CrsKind::geographic);  // WGS 84, 3D
  CHECK(kind(26915) == CrsKind::projected);
  CHECK(kind(32615) == CrsKind::projected);
  CHECK(kind(3857) == CrsKind::projected);
  // What is neither is an error, as for a Projector.
  CHECK(kind(5703).error().code == ProjectionErrc::unknown_crs);  // vertical
  CHECK(Projector::make(epsg(4269))->kind() == CrsKind::geographic);
}

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

TEST_CASE("to_location is right for CRSs in any order", "[io][projection]") {
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
    std::vector<std::thread> threads;
    threads.reserve(ok.size());
    for (int& slot : ok) {
      // A non-const copy: MSVC will not capture `c` implicitly, while Clang
      // calls an explicit capture of a constant unnecessary.
      threads.emplace_back([&slot, &point, expected = UtmCase{c}] {
        bool all = true;
        for (int n = 0; n < 20; ++n) {
          const auto location = to_location(point);
          all = all and location.has_value() and
                near_location(*location, expected.lat, expected.lon);
        }
        slot = all ? 1 : 0;
      });
    }
    for (std::thread& thread : threads) {
      thread.join();
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

// ---- how good the transformation was ---------------------------------------

namespace {

// A placeholder for value_or: a test that finds no warning has already failed.
mov::io::Warning no_warning() {
  return mov::io::Warning{.code = mov::io::WarningCode::times_reordered};
}

// Converts `count` points around the centre of a CRS's useful area.
void convert_points(Projector& projector, std::size_t count) {
  for (std::size_t i = 0; i < count; ++i) {
    const auto location = projector.to_location(
        Xy{.x = 500000.0 + static_cast<double>(i), .y = 3300000.0});
    REQUIRE(location.has_value());
  }
}

}  // namespace

TEST_CASE("an exact transformation reports no approximation",
          "[io][projection][accuracy]") {
  // WGS 84 / UTM zone 16N and Web Mercator are defined on WGS 84 itself.
  for (const int code : {32616, 3857}) {
    CAPTURE(code);
    auto projector = Projector::make(epsg(code));
    REQUIRE(projector.has_value());
    convert_points(*projector, 5);
    CHECK(projector->accuracy().points == 5);
    CHECK(projector->accuracy().sampled_points == 5);
    CHECK(projector->accuracy().ballpark_points == 0);
    CHECK(not projector->accuracy().worst_stated_m.has_value());
    CHECK(not projector->approximation_warning().has_value());
  }
}

TEST_CASE("EPSG:4326 has nothing to report", "[io][projection][accuracy]") {
  auto projector = Projector::make(Epsg::wgs84());
  REQUIRE(projector.has_value());
  CHECK(projector->to_location(Xy{.x = -90.5, .y = 30.5}).has_value());
  CHECK(projector->accuracy().points == 0);
  CHECK(not projector->approximation_warning().has_value());
}

// NAD83 to WGS 84 is a datum shift of a metre or two that PROJ states with an
// accuracy; a reader must say so.
TEST_CASE("a transformation with a stated accuracy worse than a metre warns",
          "[io][projection][accuracy]") {
  auto projector = Projector::make(epsg(26915));
  REQUIRE(projector.has_value());
  convert_points(*projector, 7);
  CHECK(projector->accuracy().worst_stated_m.value_or(0.0) > 1.0);
  const auto found = projector->approximation_warning();
  REQUIRE(found.has_value());
  const mov::io::Warning warning = found.value_or(no_warning());
  CHECK(warning.code == mov::io::WarningCode::crs_approximate);
  CHECK(warning.subject == "EPSG:26915");
  CHECK(warning.count == 7);
}

// Where PROJ knows no operation between two datums for the point (NAD83 is
// defined for North America, not China) it falls back to a ballpark
// transformation, which can be metres off.
TEST_CASE("a ballpark transformation warns", "[io][projection][accuracy]") {
  for (const int code : {4269}) {
    CAPTURE(code);
    auto projector = Projector::make(epsg(code));
    REQUIRE(projector.has_value());
    const auto location = projector->to_location(Xy{.x = 100.0, .y = 40.0});
    static_cast<void>(location);
    CHECK(projector->accuracy().ballpark_points == 1);
    const auto found = projector->approximation_warning();
    REQUIRE(found.has_value());
    const mov::io::Warning warning = found.value_or(no_warning());
    CHECK(warning.code == mov::io::WarningCode::crs_approximate);
    CHECK(warning.subject == "EPSG:" + std::to_string(code));
  }
}

// Asking PROJ for the operation of every point costs 200 times the point; only
// a sample is asked.
TEST_CASE("the operation is asked for a sample of the points",
          "[io][projection][accuracy]") {
  auto projector = Projector::make(epsg(26915));
  REQUIRE(projector.has_value());
  convert_points(*projector, 1000);
  CHECK(projector->accuracy().points == 1000);
  // The first 256, then 320, 384, ..., 960.
  CHECK(projector->accuracy().sampled_points == 256 + 11);
}

// ---- where proj.db is ------------------------------------------------------

TEST_CASE("a missing database is database_unavailable, not unknown_crs",
          "[io][projection][database]") {
  const mov::test::ScratchDir broken;
  mov::test::write_bytes(broken / "proj.db", "this is not a database");
  const ScopedEnv env{"MOV_PROJ_DATA", broken.path().string()};
  const auto made = Projector::make(epsg(26915));
  REQUIRE(not made.has_value());
  CHECK(made.error().code == ProjectionErrc::database_unavailable);
  CHECK(made.error().crs == epsg(26915));
  const auto located = to_location(native(500000.0, 3300000.0, 26915));
  REQUIRE(not located.has_value());
  CHECK(std::holds_alternative<ProjectionError>(located.error()));
  // EPSG:4326 never needs the database.
  CHECK(to_location(native(-90.0, 30.0, 4326)).has_value());
}

TEST_CASE("MOV_PROJ_DATA overrides the directory the application set",
          "[io][projection][database]") {
  REQUIRE(Projector::make(epsg(26915)).has_value());
  {
    const mov::test::ScratchDir broken;
    mov::test::write_bytes(broken / "proj.db", "this is not a database");
    const ScopedEnv env{"MOV_PROJ_DATA", broken.path().string()};
    CHECK(not Projector::make(epsg(26915)).has_value());
  }
  // With the variable gone the application's setting works again.
  CHECK(Projector::make(epsg(26915)).has_value());
  // An empty variable is no variable.
  const ScopedEnv blank{"MOV_PROJ_DATA", std::string{}};
  CHECK(Projector::make(epsg(26915)).has_value());
}
