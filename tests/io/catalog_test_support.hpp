// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Helpers of the station catalog tests: building entries and catalogs, and
// the golden-file check.

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/station_catalog.hpp"
#include "mov/test/fixture.hpp"
#include "mov/test/scratch_dir.hpp"
#include "wire_test_support.hpp"

namespace mov::test::catalog {

using core::provider::Coops;
using core::provider::Ndbc;
using core::provider::Usgs;

template <class P>
[[nodiscard]] core::StationId<P> id_of(std::string_view raw) {
  auto id = core::StationId<P>::make(raw);
  REQUIRE(id.has_value());
  return *std::move(id);
}

[[nodiscard]] inline core::StationText text(std::string_view s) {
  auto t = core::StationText::make(std::string{s});
  REQUIRE(t.has_value());
  return *std::move(t);
}

[[nodiscard]] inline core::Location at(double lat, double lon) {
  const auto l = core::Location::make({.lat = lat, .lon = lon});
  REQUIRE(l.has_value());
  return *l;
}

[[nodiscard]] inline std::chrono::sys_days day(int y, unsigned m, unsigned d) {
  using namespace std::chrono;
  return sys_days{year{y} / month{m} / std::chrono::day{d}};
}

[[nodiscard]] inline core::ValidRange valid(
    std::optional<std::chrono::sys_days> first,
    std::optional<std::chrono::sys_days> last = std::nullopt) {
  const auto r = core::ValidRange::make({.first = first, .last = last});
  REQUIRE(r.has_value());
  return *r;
}

/// A station of P at (lat, lon) with no validity and empty capabilities.
template <class P>
[[nodiscard]] io::CatalogEntry<P> entry(std::string_view id,
                                        std::string_view name, double lat,
                                        double lon) {
  return io::CatalogEntry<P>{.station = {.id = id_of<P>(id),
                                         .name = text(name),
                                         .location = at(lat, lon),
                                         .validity = {}},
                             .caps = {}};
}

/// The header every test catalog has unless it says otherwise.
[[nodiscard]] inline io::CatalogHeader header(
    core::Time generated = utc(2026, 10, 15, 17, 0, 0)) {
  return {.generated = generated,
          .sources = {{.url = text("https://example.org/list.json"),
                       .retrieved = utc(2026, 10, 15, 16, 59, 30)}}};
}

template <class P>
[[nodiscard]] io::Catalog<P> catalog(std::vector<io::CatalogEntry<P>> entries,
                                     io::CatalogHeader h = header()) {
  auto c = io::Catalog<P>::make(std::move(h), std::move(entries));
  REQUIRE(c.has_value());
  return *std::move(c);
}

/// A clock well after every test catalog's `generated`.
[[nodiscard]] inline core::Time later() { return utc(2026, 10, 20); }

/// The text as a catalog, read with ctx and the clock `later()`.
[[nodiscard]] inline auto parse(std::string_view text,
                                const io::ReadContext& ctx = {}) {
  return io::parse_station_catalog(text, later(), ctx);
}

/// The canonical text of a catalog (REQUIREs success).
template <class P>
[[nodiscard]] std::string format(const io::Catalog<P>& c) {
  auto text = io::format_station_catalog(io::StationCatalog{c});
  REQUIRE(text.has_value());
  return *std::move(text);
}

/// Compares `actual` with the committed fixture `io/station_catalog/<name>`
/// byte for byte. On a difference the actual text is written to
/// `<name>.actual` in the working directory (the test's build directory), to
/// diff against the fixture.
inline void check_golden(const std::string& actual, std::string_view name) {
  const std::string expected =
      read_bytes(fixture("io/station_catalog/" + std::string{name}));
  if (actual != expected) {
    std::ofstream out{std::string{name} + ".actual", std::ios::binary};
    out << actual;
  }
  CHECK(actual == expected);
}

}  // namespace mov::test::catalog
