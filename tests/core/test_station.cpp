// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"

using mov::core::DataSource;
using mov::core::DatumTable;
using mov::core::Epsg;
using mov::core::FileStation;
using mov::core::GaugeStation;
using mov::core::Location;
using mov::core::NativePoint;
using mov::core::StationId;
using mov::core::StationIdError;
using mov::core::StationKey;
using mov::core::StationText;
using mov::core::to_file_station;
using mov::core::ValidRange;
using mov::core::provider::Coops;
using mov::core::provider::Ndbc;
using mov::core::provider::Usgs;

namespace {

Location location(double lat, double lon) {
  const auto loc = Location::make({.lat = lat, .lon = lon});
  REQUIRE(loc.has_value());
  return loc.value_or(Location::make({.lat = 0.0, .lon = 0.0}).value());
}

template <class P>
StationId<P> id(std::string_view raw) {
  auto made = StationId<P>::make(raw);
  REQUIRE(made.has_value());
  return std::move(made).value();
}

StationKey key(std::string text) {
  auto made = StationKey::make(std::move(text));
  REQUIRE(made.has_value());
  return std::move(made).value();
}

StationText text(std::string t) {
  auto made = StationText::make(std::move(t));
  REQUIRE(made.has_value());
  return std::move(made).value_or(StationText{});
}

}  // namespace

TEST_CASE("StationId::make normalizes at run time too", "[core][station]") {
  const StationId<Ndbc> buoy = id<Ndbc>(" 42a01\n");
  CHECK(buoy.value() == "42A01");
  CHECK(StationId<Coops>::make("8761724x") ==
        std::unexpected{StationIdError::invalid});
  CHECK(StationId<Usgs>::make("") == std::unexpected{StationIdError::empty});
  const std::string heap(300, 'x');  // beyond the small-string buffer
  CHECK(StationId<mov::core::provider::Xtide>::make(heap) ==
        std::unexpected{StationIdError::invalid});
  const StationId<Usgs> river = id<Usgs>("usgs-07374000");
  CHECK(river.value() == "USGS-07374000");
  CHECK(river.key() == key("USGS-07374000"));
}

TEST_CASE("station text and keys hold heap-sized text", "[core][station]") {
  const std::string long_name(300, 'n');
  const StationText t = text(long_name);
  const StationKey k = key(long_name);
  CHECK(t.view() == long_name);
  CHECK(k.text() == t);
  CHECK(StationKey::make(long_name + '\0') ==
        std::unexpected{mov::core::StationKeyError::embedded_nul});
  CHECK(key("a") < key("b"));
  CHECK(text("") < text("a"));
}

TEST_CASE("station ids are usable as ordered keys", "[core][station]") {
  std::map<StationId<Coops>, int> by_id;
  by_id.emplace(id<Coops>("8761724"), 2);
  by_id.emplace(id<Coops>("1611400"), 1);
  REQUIRE(by_id.size() == 2);
  CHECK(by_id.begin()->first.value() == "1611400");

  std::vector<StationId<Coops>> ids{id<Coops>("9999999"), id<Coops>("0000001")};
  std::ranges::sort(ids);
  CHECK(ids.front().value() == "0000001");
}

TEST_CASE("to_file_station keeps id, name and location and tags the source",
          "[core][station]") {
  using std::chrono::days;
  using std::chrono::sys_days;
  const GaugeStation<Coops> gauge{
      .id = id<Coops>("8761724"),
      .name = text("Grand Isle, LA"),
      .location = location(29.2633, -89.9567),
      .validity = ValidRange::make(
                      {.first = sys_days{days{10'000}}, .last = std::nullopt})
                      .value_or(ValidRange{}),
      .datums = DatumTable{}};
  const FileStation file = to_file_station(gauge);
  CHECK(file == FileStation{.id = key("8761724"),
                            .name = text("Grand Isle, LA"),
                            .location = location(29.2633, -89.9567),
                            .native = std::nullopt,
                            .source = DataSource::noaa_coops});

  const GaugeStation<Usgs> river{.id = id<Usgs>("USGS-07374000"),
                                 .name = text("Mississippi River at Vicksburg"),
                                 .location = location(30.4458, -91.1917),
                                 .validity = ValidRange{},
                                 .datums = DatumTable{}};
  CHECK(to_file_station(river).source == DataSource::usgs);
  const FileStation file_river = to_file_station(river);
  CHECK(file_river.id.view() == "USGS-07374000");
}

TEST_CASE("FileStation equality covers every field", "[core][station]") {
  const auto native = NativePoint::make(
      {.x = 1.0, .y = 2.0}, Epsg::make(26915).value_or(Epsg::wgs84()));
  REQUIRE(native.has_value());
  const FileStation a{
      .id = key("0"),
      .name = text("Station 0"),
      .location = location(1.0, 2.0),
      .native = native.value_or(
          NativePoint::make({.x = 0.0, .y = 0.0}, Epsg::wgs84()).value()),
      .source = DataSource::adcirc};
  FileStation b = a;
  CHECK(a == b);
  b.native = std::nullopt;
  CHECK(a != b);
  b = a;
  b.source = std::nullopt;
  CHECK(a != b);
  b = a;
  b.name = text("Station 1");
  CHECK(a != b);
}
