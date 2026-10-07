// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// detail/civil_time.hpp: calendar fields of a core::Time at any year, against
// std::chrono where chrono can represent the year.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstdint>
#include <random>
#include <utility>

#include "mov/core/time.hpp"
#include "mov/io/detail/civil_time.hpp"

namespace {

namespace detail = mov::io::detail;
namespace chrono = std::chrono;

constexpr mov::core::Time at_ms(std::int64_t ms) {
  return mov::core::Time{chrono::milliseconds{ms}};
}

}  // namespace

static_assert(detail::days_from_civil(1970, 1, 1) == 0);
static_assert(detail::days_from_civil(2000, 3, 1) == 11017);
static_assert(detail::days_from_civil(0, 1, 1) == -719528);
static_assert(detail::civil_from_days(-719528) ==
              detail::CivilDate{.year = 0, .month = 1, .day = 1});
static_assert(detail::imeds_first.time_since_epoch().count() ==
              -719528LL * 86'400'000);
static_assert(detail::imeds_end.time_since_epoch().count() ==
              2932897LL * 86'400'000);
static_assert(detail::in_imeds_range(detail::imeds_first));
static_assert(not detail::in_imeds_range(detail::imeds_end));
static_assert(not detail::in_imeds_range(detail::imeds_first -
                                         chrono::milliseconds{1}));
static_assert(detail::in_imeds_range(detail::imeds_end -
                                     chrono::milliseconds{1}));

TEST_CASE("civil_fields agrees with std::chrono wherever chrono has the year",
          "[io][civil_time]") {
  std::seed_seq seed{12345};
  std::mt19937_64 gen{seed};
  // +-30,000 years in milliseconds.
  std::uniform_int_distribution<std::int64_t> any{-946'000'000'000'000LL,
                                                  946'000'000'000'000LL};
  for (int i = 0; i < 20000; ++i) {
    const std::int64_t ms = any(gen);
    const mov::core::Time t = at_ms(ms);
    const auto day = chrono::floor<chrono::days>(t);
    const chrono::year_month_day date{day};
    const chrono::hh_mm_ss<chrono::milliseconds> clock{t - day};
    const detail::CivilTime c = detail::civil_fields(t);
    REQUIRE(c.year == static_cast<int>(date.year()));
    REQUIRE(c.month == static_cast<unsigned>(date.month()));
    REQUIRE(c.day == static_cast<unsigned>(date.day()));
    REQUIRE(std::cmp_equal(c.hour, clock.hours().count()));
    REQUIRE(std::cmp_equal(c.minute, clock.minutes().count()));
    REQUIRE(std::cmp_equal(c.second, clock.seconds().count()));
    REQUIRE(std::cmp_equal(c.millisecond, clock.subseconds().count()));
  }
}

TEST_CASE("time_of inverts civil_fields over the whole file range",
          "[io][civil_time]") {
  std::seed_seq seed{777};
  std::mt19937_64 gen{seed};
  std::uniform_int_distribution<std::int64_t> any{-mov::core::max_abs_time_ms,
                                                  mov::core::max_abs_time_ms};
  for (int i = 0; i < 50000; ++i) {
    const mov::core::Time t = at_ms(any(gen));
    REQUIRE(detail::time_of(detail::civil_fields(t)) == t);
  }
  for (const std::int64_t ms :
       {-mov::core::max_abs_time_ms, mov::core::max_abs_time_ms,
        std::int64_t{0}, std::int64_t{-1}, std::int64_t{1},
        std::int64_t{-86'400'000}, std::int64_t{-86'400'001}}) {
    CAPTURE(ms);
    CHECK(detail::time_of(detail::civil_fields(at_ms(ms))) == at_ms(ms));
  }
}

TEST_CASE("fields before the epoch are floored", "[io][civil_time]") {
  const detail::CivilTime c = detail::civil_fields(at_ms(-1));
  CHECK(c == detail::CivilTime{.year = 1969,
                               .month = 12,
                               .day = 31,
                               .hour = 23,
                               .minute = 59,
                               .second = 59,
                               .millisecond = 999});
}

TEST_CASE("days_in_month and leap years", "[io][civil_time]") {
  CHECK(detail::days_in_month(2024, 2) == 29);
  CHECK(detail::days_in_month(2023, 2) == 28);
  CHECK(detail::days_in_month(1900, 2) == 28);
  CHECK(detail::days_in_month(2000, 2) == 29);
  CHECK(detail::days_in_month(0, 2) == 29);
  CHECK(detail::days_in_month(-4, 2) == 29);
  CHECK(detail::days_in_month(2023, 4) == 30);
  CHECK(detail::days_in_month(2023, 12) == 31);
}
