// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for the USGS wire layer: parse_rfc3339, the
// collection paths and the map parameters.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>

#include "mov/core/provider/usgs.hpp"
#include "mov/core/time.hpp"
#include "mov/io/rfc3339.hpp"
#include "mov/io/wire/usgs.hpp"

namespace {

using mov::core::DateTimeError;
using mov::core::Time;
using mov::io::parse_rfc3339;
using mov::io::wire::usgs::Collection;
using mov::io::wire::usgs::items_path;

constexpr Time at(int y, unsigned mo, unsigned d, std::int64_t ms_of_day = 0) {
  using namespace std::chrono;
  return time_point_cast<milliseconds>(sys_days{year{y} / month{mo} / day{d}}) +
         milliseconds{ms_of_day};
}

constexpr std::int64_t hms(std::int64_t h, std::int64_t m, std::int64_t s) {
  return ((h * 60 + m) * 60 + s) * 1000;
}

constexpr bool parses_to(std::string_view text, Time expected) {
  const auto t = parse_rfc3339(text);
  return t.has_value() and *t == expected;
}

constexpr bool fails_at(std::string_view text, std::size_t column) {
  const auto t = parse_rfc3339(text);
  return not t.has_value() and t.error() == DateTimeError{.column = column};
}

}  // namespace

TEST_CASE("RFC 3339 instants: offsets, Z and fractions",
          "[io][usgs][rfc3339][constexpr]") {
  // The USGS form.
  STATIC_REQUIRE(
      parses_to("2026-10-01T00:05:00+00:00", at(2026, 10, 1, hms(0, 5, 0))));
  STATIC_REQUIRE(
      parses_to("2026-10-01T00:05:00Z", at(2026, 10, 1, hms(0, 5, 0))));
  STATIC_REQUIRE(
      parses_to("2026-10-01t00:05:00z", at(2026, 10, 1, hms(0, 5, 0))));
  // An offset is subtracted: 05:00 EST is 10:00 UTC; -00:00 is UTC.
  STATIC_REQUIRE(
      parses_to("2026-01-15T05:00:00-05:00", at(2026, 1, 15, hms(10, 0, 0))));
  STATIC_REQUIRE(
      parses_to("2026-01-15T05:30:00+05:30", at(2026, 1, 15, hms(0, 0, 0))));
  STATIC_REQUIRE(parses_to("2026-01-15T00:00:00-00:00", at(2026, 1, 15)));
  // Across a day and a year.
  STATIC_REQUIRE(
      parses_to("2025-12-31T23:00:00-02:00", at(2026, 1, 1, hms(1, 0, 0))));
  STATIC_REQUIRE(
      parses_to("2026-01-01T01:00:00+02:00", at(2025, 12, 31, hms(23, 0, 0))));
  // Fractions: milliseconds, and digits past them truncated.
  STATIC_REQUIRE(parses_to("2026-10-01T00:58:19.7Z",
                           at(2026, 10, 1, hms(0, 58, 19) + 700)));
  STATIC_REQUIRE(parses_to("2026-10-01T00:58:19.731858+00:00",
                           at(2026, 10, 1, hms(0, 58, 19) + 731)));
  STATIC_REQUIRE(parses_to("2026-10-01T00:58:19.999999999Z",
                           at(2026, 10, 1, hms(0, 58, 19) + 999)));
  STATIC_REQUIRE(parses_to("0000-01-01T00:00:00Z", at(0, 1, 1)));
  STATIC_REQUIRE(parses_to("9999-12-31T23:59:59.999Z",
                           at(9999, 12, 31, hms(23, 59, 59) + 999)));
}

TEST_CASE("RFC 3339: what it does not allow, and where",
          "[io][usgs][rfc3339][constexpr]") {
  STATIC_REQUIRE(fails_at("", 0));
  STATIC_REQUIRE(fails_at("2026-10-01", 10));            // no time
  STATIC_REQUIRE(fails_at("2026-10-01 00:00:00Z", 10));  // a space for T
  STATIC_REQUIRE(fails_at("2026-10-01T00:00Z", 16));     // no seconds
  STATIC_REQUIRE(fails_at("2026-10-01T00:00:00", 19));   // no offset
  STATIC_REQUIRE(fails_at("2026-10-01T00:00:00+0000", 22));
  STATIC_REQUIRE(fails_at("2026-10-01T00:00:60Z", 17));  // no leap second
  STATIC_REQUIRE(fails_at("2026-10-01T24:00:00Z", 11));
  STATIC_REQUIRE(fails_at("2026-02-30T00:00:00Z", 8));    // no such day
  STATIC_REQUIRE(fails_at("2026-10-01T00:00:00.Z", 20));  // a dot, no digit
  STATIC_REQUIRE(fails_at("2026-10-01T00:00:00.1234567890Z", 29));
  STATIC_REQUIRE(fails_at("2026-10-01T00:00:00+24:00", 20));
  STATIC_REQUIRE(fails_at("2026-10-01T00:00:00+05:60", 23));
  STATIC_REQUIRE(fails_at("2026-10-01T00:00:00Z ", 20));  // trailing text
  STATIC_REQUIRE(fails_at(" 2026-10-01T00:00:00Z", 0));
  STATIC_REQUIRE(fails_at("26-10-01T00:00:00Z", 2));
}

TEST_CASE("USGS collections and the map parameters", "[io][usgs][constexpr]") {
  STATIC_REQUIRE(items_path(Collection::continuous) ==
                 "/ogcapi/v1/collections/continuous/items");
  STATIC_REQUIRE(items_path(Collection::daily) ==
                 "/ogcapi/v1/collections/daily/items");
  STATIC_REQUIRE(items_path(Collection::combined_metadata) ==
                 "/ogcapi/v1/collections/combined-metadata/items");
  STATIC_REQUIRE(mov::io::wire::usgs::page_rows == 50000);
  // Decision 33: every map parameter is a parameter code.
  STATIC_REQUIRE(mov::io::wire::usgs::map_parameters.size() == 4);
  STATIC_REQUIRE(mov::core::usgs::ParameterCode::make(
                     mov::io::wire::usgs::map_parameters[2])
                     .has_value());
}
