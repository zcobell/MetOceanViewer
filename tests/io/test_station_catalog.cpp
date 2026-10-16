// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The station catalog model: Catalog<P> and the rules for loading and
// refreshing it (docs/providers-design.md section 7).

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <format>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "catalog_test_support.hpp"
#include "mov/core/station.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/station_catalog.hpp"
#include "mov/io/warning.hpp"
#include "wire_test_support.hpp"

namespace {

namespace cat = mov::test::catalog;
namespace io = mov::io;
using cat::Coops;
using cat::Ndbc;
using cat::Usgs;
using mov::test::utc;

// n NDBC stations with ids 00000, 00001, ...
io::NdbcCatalog stations(std::size_t n,
                         mov::core::Time generated = utc(2026, 10, 15)) {
  std::vector<io::CatalogEntry<Ndbc>> entries;
  entries.reserve(n);
  for (std::size_t i = 0; i < n; ++i) {
    entries.push_back(
        cat::entry<Ndbc>(std::format("{:05}", i), "buoy", 30.0, -80.0));
  }
  return cat::catalog<Ndbc>(std::move(entries), cat::header(generated));
}

}  // namespace

TEST_CASE("a catalog is sorted by id and finds its stations", "[io][catalog]") {
  const auto c = cat::catalog<Coops>(
      {cat::entry<Coops>("8729840", "Pensacola", 30.4, -87.2),
       cat::entry<Coops>("1611400", "Nawiliwili", 21.95, -159.36),
       cat::entry<Coops>("8771972", "San Luis Pass", 29.08, -95.13)});
  REQUIRE(c.size() == 3);
  CHECK(c.entries()[0].station.id.value() == "1611400");
  CHECK(c.entries()[1].station.id.value() == "8729840");
  CHECK(c.entries()[2].station.id.value() == "8771972");
  const auto* found = c.find(cat::id_of<Coops>("8729840"));
  REQUIRE(found != nullptr);
  CHECK(found->station.name.view() == "Pensacola");
  CHECK(c.find(cat::id_of<Coops>("8729841")) == nullptr);
  CHECK(c.find(cat::id_of<Coops>("0000000")) == nullptr);
  CHECK(c.find(cat::id_of<Coops>("9999999")) == nullptr);
  CHECK(not c.empty());
}

TEST_CASE("a catalog refuses a repeated station", "[io][catalog]") {
  const auto made = io::UsgsCatalog::make(
      cat::header(), {cat::entry<Usgs>("USGS-02", "b", 30, -90),
                      cat::entry<Usgs>("USGS-01", "a", 30, -90),
                      cat::entry<Usgs>("USGS-02", "b again", 31, -90),
                      cat::entry<Usgs>("USGS-01", "a again", 31, -90)});
  REQUIRE(not made.has_value());
  // The smallest repeated id.
  CHECK(made.error().id.value() == "USGS-01");
}

TEST_CASE("an empty catalog is a catalog", "[io][catalog]") {
  const auto c = cat::catalog<Ndbc>({});
  CHECK(c.empty());
  CHECK(c.find(cat::id_of<Ndbc>("41001")) == nullptr);
}

TEST_CASE("source_of and catalog_of name the provider", "[io][catalog]") {
  const io::StationCatalog any{stations(1)};
  CHECK(io::source_of(any) == mov::core::DataSource::ndbc);
  CHECK(io::source_of(io::StationCatalog{cat::catalog<Coops>({})}) ==
        mov::core::DataSource::noaa_coops);
  CHECK(io::source_of(io::StationCatalog{cat::catalog<Usgs>({})}) ==
        mov::core::DataSource::usgs);

  const std::vector<io::Warning> warnings{
      {.code = io::WarningCode::minor_newer, .subject = "1.1"}};
  const auto ndbc = io::catalog_of<Ndbc>(
      io::Read<io::StationCatalog>{.value = any, .warnings = warnings});
  REQUIRE(ndbc.has_value());
  CHECK(ndbc->value.size() == 1);
  CHECK(ndbc->warnings == warnings);

  const auto coops = io::catalog_of<Coops>(
      io::Read<io::StationCatalog>{.value = any, .warnings = {}});
  const auto e = mov::test::format_error_in(coops);
  CHECK(e.code == io::FormatErrc::wrong_provider);
  CHECK(e.subject == "ndbc");
}

TEST_CASE("a catalog is stale after 30 days", "[io][catalog]") {
  using std::chrono::milliseconds;
  const auto h = cat::header(utc(2026, 9, 1));
  CHECK(not io::is_stale(h, utc(2026, 9, 1)));
  CHECK(not io::is_stale(h, utc(2026, 10, 1)));  // exactly 30 days
  CHECK(io::is_stale(h, utc(2026, 10, 1) + milliseconds{1}));
  CHECK(not io::is_stale(h, utc(2026, 8, 1)));  // a clock behind
}

TEST_CASE("the cache is chosen only when it is newer than the snapshot",
          "[io][catalog]") {
  const auto snapshot = stations(3, utc(2026, 10, 1));
  SECTION("no valid cache") {
    const auto chosen = io::choose_catalog(snapshot, std::nullopt);
    CHECK(chosen.origin == io::CatalogOrigin::snapshot);
    CHECK(chosen.catalog == snapshot);
  }
  SECTION("an older or equally old cache") {
    for (const auto when : {utc(2026, 9, 1), utc(2026, 10, 1)}) {
      const auto chosen = io::choose_catalog(snapshot, stations(5, when));
      CHECK(chosen.origin == io::CatalogOrigin::snapshot);
      CHECK(chosen.catalog.size() == 3);
    }
  }
  SECTION("a newer cache") {
    const auto cache = stations(5, utc(2026, 10, 2));
    const auto chosen = io::choose_catalog(snapshot, cache);
    CHECK(chosen.origin == io::CatalogOrigin::cache);
    CHECK(chosen.catalog == cache);
  }
}

TEST_CASE("a refresh with fewer than half the stations is refused",
          "[io][catalog]") {
  struct Row {
    std::size_t current;
    std::size_t refreshed;
    bool accepted;
  };
  for (const Row row : {Row{.current = 10, .refreshed = 5, .accepted = true},
                        Row{.current = 10, .refreshed = 4, .accepted = false},
                        Row{.current = 9, .refreshed = 5, .accepted = true},
                        Row{.current = 9, .refreshed = 4, .accepted = false},
                        Row{.current = 1, .refreshed = 1, .accepted = true},
                        Row{.current = 1, .refreshed = 0, .accepted = false},
                        Row{.current = 0, .refreshed = 0, .accepted = true},
                        Row{.current = 3, .refreshed = 30, .accepted = true}}) {
    CAPTURE(row.current, row.refreshed);
    const auto refreshed = stations(row.refreshed, utc(2026, 10, 16));
    const auto verdict = io::accept_refresh(stations(row.current), refreshed);
    CHECK(verdict.has_value() == row.accepted);
    if (verdict) {
      CHECK(*verdict == refreshed);
    } else {
      CHECK(verdict.error() == io::TooFewStations{.current = row.current,
                                                  .refreshed = row.refreshed});
    }
  }
}
