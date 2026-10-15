// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks of the plans' constexpr parts (windows, page caps,
// NDBC month spellings): compile-time in mov_fetch_tests_constexpr,
// run-time in mov_fetch_tests_relaxed_constexpr.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <limits>
#include <variant>

#include "mov/core/provider/coops.hpp"
#include "mov/core/time.hpp"
#include "mov/fetch/paging.hpp"
#include "mov/fetch/plan/coops.hpp"
#include "mov/fetch/plan/ndbc.hpp"

namespace {

namespace cc = mov::core::coops;
using mov::core::Time;
using mov::core::TimeRange;
using std::chrono::days;
using std::chrono::milliseconds;
using std::chrono::minutes;

constexpr Time at(std::int64_t ms) { return Time{milliseconds{ms}}; }

constexpr cc::Product six_minute_level{cc::WaterLevel{
    .kind = cc::ObservedLevel{.series = cc::Observed::six_minute},
    .datum = cc::Datum::mllw}};
constexpr cc::Product paired_hourly{cc::WaterLevel{
    .kind = cc::ObservedVsPredicted{.grid = cc::PairedGrid::hourly},
    .datum = cc::Datum::navd88}};

}  // namespace

TEST_CASE("max_pages is ceil(span / cadence / rows) + 2", "[fetch][paging]") {
  using mov::fetch::max_pages;
  STATIC_REQUIRE(max_pages(days{1000}, minutes{1}, 50000) == 31);
  STATIC_REQUIRE(max_pages(minutes{50000}, minutes{1}, 50000) == 3);
  STATIC_REQUIRE(max_pages(minutes{50001}, minutes{1}, 50000) == 4);
  STATIC_REQUIRE(max_pages(milliseconds{1}, minutes{1}, 50000) == 3);
  // Degenerate inputs count as one.
  STATIC_REQUIRE(max_pages(milliseconds{0}, milliseconds{0}, 0) == 3);
  STATIC_REQUIRE(
      max_pages(milliseconds{std::numeric_limits<std::int64_t>::max()},
                milliseconds{1},
                1) == std::numeric_limits<std::uint32_t>::max());
}

TEST_CASE("CO-OPS windows are minute-aligned and cut at the product's limit",
          "[fetch][plan][coops]") {
  constexpr auto request = [](std::int64_t b, std::int64_t e,
                              const cc::Product& p) {
    return cc::Request{
        .station =
            *mov::core::StationId<mov::core::provider::Coops>::make("8729840"),
        .range = *TimeRange::make(at(b), at(e)),
        .product = p};
  };
  constexpr std::int64_t minute = 60'000;
  constexpr std::int64_t day = 86'400'000;
  // 62 days and a half minute of 6-minute data: three windows of <= 31 days,
  // widened to whole minutes.
  STATIC_REQUIRE(mov::fetch::coops::windows(
                     request(30'000, (62 * day) + 30'000, six_minute_level))
                     ->size() == 3);
  STATIC_REQUIRE(mov::fetch::coops::windows(
                     request(30'000, (62 * day) + 30'000, six_minute_level))
                     ->front()
                     .begin() == at(0));
  STATIC_REQUIRE(mov::fetch::coops::windows(
                     request(30'000, (62 * day) + 30'000, six_minute_level))
                     ->back()
                     .end() == at((62 * day) + minute));
  // A paired product takes the smaller limit (hourly: 365 days).
  STATIC_REQUIRE(
      mov::fetch::coops::windows(request(0, 366 * day, paired_hourly))
          ->size() == 2);
  STATIC_REQUIRE(mov::fetch::coops::exchanges(paired_hourly) == 2);
  STATIC_REQUIRE(mov::fetch::coops::exchanges(six_minute_level) == 1);
}

TEST_CASE("the CO-OPS range at the end of the time line is Unalignable",
          "[fetch][plan][coops]") {
  constexpr std::int64_t last = std::numeric_limits<std::int64_t>::max();
  constexpr auto r = *TimeRange::make(at(last - 10), at(last));
  // A lambda: a constexpr variable cannot hold the id's string on libc++.
  constexpr auto unaligned = [](TimeRange range) {
    return mov::fetch::coops::windows(cc::Request{
        .station =
            *mov::core::StationId<mov::core::provider::Coops>::make("8729840"),
        .range = range,
        .product = six_minute_level});
  };
  STATIC_REQUIRE(unaligned(r).error() == mov::fetch::Unalignable{.range = r});
}

TEST_CASE("NDBC month spellings", "[fetch][plan][ndbc]") {
  using mov::fetch::ndbc::month_token;
  using mov::fetch::ndbc::Monthly;
  using mov::fetch::ndbc::MonthSpelling;
  using std::chrono::year;
  constexpr auto m = [](unsigned month, MonthSpelling s) {
    return Monthly{
        .year = year{2026}, .month = std::chrono::month{month}, .spelling = s};
  };
  STATIC_REQUIRE(month_token(m(1, MonthSpelling::primary)) == "1");
  STATIC_REQUIRE(month_token(m(9, MonthSpelling::primary)) == "9");
  STATIC_REQUIRE(month_token(m(10, MonthSpelling::primary)) == "a");
  STATIC_REQUIRE(month_token(m(12, MonthSpelling::primary)) == "c");
  STATIC_REQUIRE(month_token(m(10, MonthSpelling::decimal)) == "10");
  STATIC_REQUIRE(month_token(m(12, MonthSpelling::decimal)) == "12");
}

TEST_CASE("NDBC source kinds", "[fetch][plan][ndbc]") {
  namespace nd = mov::fetch::ndbc;
  using mov::core::ndbc::FileKind;
  using std::chrono::year;
  STATIC_REQUIRE(nd::kind(nd::Historical{.year = year{2024},
                                         .months_when_absent = false}) ==
                 FileKind::historical);
  STATIC_REQUIRE(
      nd::kind(nd::Monthly{.year = year{2026},
                           .month = std::chrono::March,
                           .spelling = nd::MonthSpelling::primary}) ==
      FileKind::monthly);
  STATIC_REQUIRE(nd::kind(nd::MonthlyLatest{.year = year{2026},
                                            .month = std::chrono::March}) ==
                 FileKind::monthly_latest);
  STATIC_REQUIRE(nd::kind(nd::Realtime{.year = year{2026}}) ==
                 FileKind::realtime);
}
