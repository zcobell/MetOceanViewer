// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// ndbc::file_target: the files of a station, byte for byte as recorded in
// tests/fixtures/io/ndbc/provenance.txt (docs/providers-design.md sections
// 1.3 and 4.1).

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <string>
#include <string_view>

#include "mov/core/provider/ndbc.hpp"
#include "mov/io/wire/ndbc.hpp"
#include "mov/io/wire/origin.hpp"

namespace {

namespace wn = mov::io::wire::ndbc;
using mov::core::ndbc::FileKind;
using mov::io::wire::Origin;
using std::chrono::month;
using std::chrono::year;

wn::Station station(std::string_view id) {
  return wn::Station::make(id).value();
}

std::string path_of(std::string_view id, const wn::File& file) {
  const auto target = wn::file_target(station(id), file);
  REQUIRE(target.has_value());
  CHECK(target->origin == Origin::ndbc);
  CHECK(target->query.empty());
  return target->path;
}

}  // namespace

TEST_CASE("a year's file: lower-case id, four-digit year", "[io][wire][ndbc]") {
  CHECK(path_of("41002", wn::Historical{.year = year{1973}}) ==
        "/data/historical/stdmet/41002h1973.txt.gz");
  CHECK(path_of("32ST0", wn::Historical{.year = year{2019}}) ==
        "/data/historical/stdmet/32st0h2019.txt.gz");
  CHECK(path_of("sgof1", wn::Historical{.year = year{5}}) ==
        "/data/historical/stdmet/sgof1h0005.txt.gz");
}

TEST_CASE("a finalized month: directory, lower-case id, month code, year",
          "[io][wire][ndbc][regression][L16]") {
  CHECK(path_of("41002", wn::Monthly{.month = year{2026} / month{1},
                                     .spelling = wn::MonthSpelling::letter}) ==
        "/data/stdmet/Jan/4100212026.txt.gz");
  CHECK(path_of("41002", wn::Monthly{.month = year{2026} / month{8},
                                     .spelling = wn::MonthSpelling::letter}) ==
        "/data/stdmet/Aug/4100282026.txt.gz");
  CHECK(path_of("32ST0", wn::Monthly{.month = year{2025} / month{10},
                                     .spelling = wn::MonthSpelling::letter}) ==
        "/data/stdmet/Oct/32st0a2025.txt.gz");
  CHECK(path_of("32ST0", wn::Monthly{.month = year{2025} / month{12},
                                     .spelling = wn::MonthSpelling::decimal}) ==
        "/data/stdmet/Dec/32st0122025.txt.gz");
}

TEST_CASE("the month codes and directories, all twelve", "[io][wire][ndbc]") {
  constexpr std::array<std::string_view, 12> directories{
      "Jan", "Feb", "Mar", "Apr", "May", "Jun",
      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  constexpr std::array<std::string_view, 12> letters{
      "1", "2", "3", "4", "5", "6", "7", "8", "9", "a", "b", "c"};
  for (unsigned m = 1; m <= 12; ++m) {
    CHECK(wn::month_directory(month{m}) == directories[m - 1]);
    CHECK(wn::month_code(month{m}, wn::MonthSpelling::letter) ==
          letters[m - 1]);
    CHECK(wn::month_code(month{m}, wn::MonthSpelling::decimal) ==
          std::to_string(m));
  }
  CHECK_FALSE(wn::month_directory(month{0}).has_value());
  CHECK_FALSE(wn::month_code(month{13}, wn::MonthSpelling::letter).has_value());
}

TEST_CASE("the latest month: no year, uncompressed", "[io][wire][ndbc]") {
  CHECK(path_of("41002", wn::LatestMonth{.month = month{9}}) ==
        "/data/stdmet/Sep/41002.txt");
  CHECK(path_of("32ST0", wn::LatestMonth{.month = month{12}}) ==
        "/data/stdmet/Dec/32st0.txt");
}

TEST_CASE("realtime2: the only upper-case id", "[io][wire][ndbc]") {
  CHECK(path_of("41002", wn::Realtime{}) == "/data/realtime2/41002.txt");
  CHECK(path_of("32st0", wn::Realtime{}) == "/data/realtime2/32ST0.txt");
}

TEST_CASE("years and months a file name cannot spell", "[io][wire][ndbc]") {
  using wn::TargetErrc;
  const auto s = station("41002");
  CHECK(
      wn::file_target(s, wn::Historical{.year = year{10000}}).error().reason ==
      TargetErrc::year_out_of_range);
  CHECK(wn::file_target(s, wn::Historical{.year = year{-1}}).error().reason ==
        TargetErrc::year_out_of_range);
  CHECK(wn::file_target(s, wn::Monthly{.month = year{2026} / month{13},
                                       .spelling = wn::MonthSpelling::letter})
            .error()
            .reason == TargetErrc::month_out_of_range);
  CHECK(wn::file_target(s, wn::Monthly{.month = year{10000} / month{1},
                                       .spelling = wn::MonthSpelling::letter})
            .error()
            .reason == TargetErrc::year_out_of_range);
  CHECK(wn::file_target(s, wn::LatestMonth{.month = month{0}}).error().reason ==
        TargetErrc::month_out_of_range);
}

TEST_CASE("the station list", "[io][wire][ndbc]") {
  const auto t = wn::active_stations_target();
  CHECK(t.origin == Origin::ndbc);
  CHECK(t.path == "/activestations.xml");
  CHECK(t.query.empty());
}

TEST_CASE("a file's kind", "[io][wire][ndbc]") {
  STATIC_REQUIRE(wn::kind(wn::Historical{.year = year{2000}}) ==
                 FileKind::historical);
  STATIC_REQUIRE(wn::kind(wn::Monthly{.month = year{2026} / month{1},
                                      .spelling = wn::MonthSpelling::letter}) ==
                 FileKind::monthly);
  STATIC_REQUIRE(wn::kind(wn::LatestMonth{.month = month{9}}) ==
                 FileKind::monthly_latest);
  STATIC_REQUIRE(wn::kind(wn::Realtime{}) == FileKind::realtime);
}
