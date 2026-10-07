// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// What the ADCIRC readers allocate when a header lies. This executable
// replaces the global operator new to count the bytes asked for, so it has
// no other tests in it.

#include <atomic>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdlib>
#include <new>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/io/adcirc_ascii.hpp"
#include "mov/io/read_limits.hpp"

namespace {

std::atomic<bool> counting{false};
std::atomic<std::size_t> requested{0};

}  // namespace

// NOLINTBEGIN(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)
void* operator new(std::size_t size) {
  if (counting.load(std::memory_order_relaxed)) {
    requested.fetch_add(size, std::memory_order_relaxed);
  }
  if (void* p = std::malloc(size == 0 ? 1 : size)) {
    return p;
  }
  throw std::bad_alloc{};
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t /*size*/) noexcept { std::free(p); }
// NOLINTEND(cppcoreguidelines-no-malloc,cppcoreguidelines-owning-memory)

namespace {

using mov::core::StationSelection;
using mov::io::AdcircKind;
using mov::io::ReadContext;

// The bytes operator new is asked for while `body` runs.
template <class Body>
std::size_t bytes_requested_by(Body&& body) {
  requested.store(0);
  counting.store(true);
  std::forward<Body>(body)();
  counting.store(false);
  return requested.load();
}

std::vector<mov::core::FileStation> stations(std::size_t n) {
  std::vector<mov::core::FileStation> out;
  out.reserve(n);
  const auto where = *mov::core::Location::make({.lat = 29.0, .lon = -90.0});
  for (std::size_t i = 0; i < n; ++i) {
    out.push_back({.id = *mov::core::StationKey::make(std::to_string(i)),
                   .name = {},
                   .location = where,
                   .native = std::nullopt,
                   .source = std::nullopt});
  }
  return out;
}

// A one-record file for three stations whose header claims `snapshots`
// records, followed by `padding` bytes of blank lines.
std::string lying_file(std::string_view snapshots, std::size_t padding) {
  std::string text = "run\n " + std::string{snapshots} +
                     " 3 0.6E+03 1 1\n 6.0E+02 1\n 1 0.5\n 2 0.5\n 3 0.5\n";
  text += std::string(padding, '\n');
  return text;
}

std::size_t parse_cost(const std::string& text, const ReadContext& ctx) {
  const auto all = stations(3);
  std::size_t ok = 0;
  const std::size_t bytes = bytes_requested_by([&] {
    const auto result =
        mov::io::parse_adcirc_ascii(text, all,
                                    {.kind = AdcircKind::elevation,
                                     .cold_start = mov::core::Time{},
                                     .stations = StationSelection::all(3)},
                                    ctx);
    ok = result.has_value() ? 1 : 0;
  });
  REQUIRE(ok == 1);
  return bytes;
}

}  // namespace

TEST_CASE("a header that claims 2^60 records costs what the text costs",
          "[io][adcirc][alloc]") {
  const ReadContext ctx{};
  const std::string tiny = lying_file("1152921504606846976", 0);
  CHECK(parse_cost(tiny, ctx) < 16 * 1024);

  // With a long text after the first record, the estimate (the text left
  // divided by the first record's size) is what is reserved: a little more
  // than the text, whatever the header says.
  const std::string padded = lying_file("1152921504606846976", 100'000);
  const std::size_t lying = parse_cost(padded, ctx);
  CHECK(lying < 2 * padded.size() + 16 * 1024);
  const std::size_t honest = parse_cost(lying_file("1", 100'000), ctx);
  CHECK(honest < 2 * padded.size() + 16 * 1024);
}

TEST_CASE("max_elements bounds the reservation too", "[io][adcirc][alloc]") {
  ReadContext ctx;
  ctx.limits.max_elements = 30;  // ten records of three stations
  const std::string text = lying_file("1152921504606846976", 100'000);
  CHECK(parse_cost(text, ctx) < 16 * 1024);
}

TEST_CASE("a huge station count allocates for the text, not the count",
          "[io][adcirc][alloc]") {
  std::size_t failed = 0;
  const std::size_t bytes = bytes_requested_by([&] {
    const auto result = mov::io::parse_adcirc_station_file(
        "100000000\n-90.0,29.0\n", mov::core::Epsg::wgs84(), ReadContext{});
    failed = result.has_value() ? 0 : 1;
  });
  CHECK(failed == 1);
  CHECK(bytes < 16 * 1024);
}
