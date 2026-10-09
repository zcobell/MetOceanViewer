// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <concepts>
#include <expected>
#include <type_traits>
#include <utility>
#include <vector>

#include "mov/core/daily_series.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"

using mov::core::ConstructionError;
using mov::core::DailySeries;
using mov::core::LengthMismatch;
using mov::core::Missing;
using mov::core::Quantity;
using mov::core::Sample;
using mov::core::SeriesMeta;
using mov::core::TimeNotIncreasing;
using mov::core::usgs::StatisticId;

namespace {

using std::chrono::local_days;

constexpr local_days on(int y, unsigned m, unsigned d) {
  return local_days{std::chrono::year{y} / std::chrono::month{m} /
                    std::chrono::day{d}};
}

Sample value(double v) { return Sample::of(v).value_or(Sample{Missing{}}); }

SeriesMeta discharge_meta() {
  return SeriesMeta::make(
      {.quantity = Quantity::discharge,
       .label = "Discharge, daily mean",
       .unit = mov::core::DischargeUnit::cubic_foot_per_second});
}

template <class T>
concept HasDays = requires(T&& t) { std::forward<T>(t).days(); };
template <class T>
concept HasMeta = requires(T&& t) { std::forward<T>(t).meta(); };

}  // namespace

TEST_CASE("DailySeries is a value", "[core][daily_series]") {
  STATIC_REQUIRE(std::copyable<DailySeries>);
  STATIC_REQUIRE(std::equality_comparable<DailySeries>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<DailySeries>);
  // A statistic has no default, so neither does a daily series.
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<DailySeries>);
  // The views point into the series, so a temporary cannot hand them out.
  STATIC_REQUIRE(HasDays<const DailySeries&>);
  STATIC_REQUIRE_FALSE(HasDays<DailySeries>);
  STATIC_REQUIRE_FALSE(HasMeta<DailySeries>);
}

TEST_CASE("DailySeries::make keeps days, samples, metadata and statistic",
          "[core][daily_series]") {
  const std::vector<local_days> days{on(2026, 1, 1), on(2026, 1, 2),
                                     on(2026, 1, 5)};
  const std::vector<Sample> samples{value(1.5), Sample{Missing{}}, value(-2.0)};
  const auto series =
      DailySeries::make(discharge_meta(), days, samples, StatisticId::mean());
  REQUIRE(series.has_value());
  CHECK(series->size() == 3);
  CHECK_FALSE(series->empty());
  CHECK(std::ranges::equal(series->days(), days));
  CHECK(std::ranges::equal(series->samples(), samples));
  CHECK(series->meta() == discharge_meta());
  CHECK(series->statistic() == StatisticId::mean());

  const auto other_statistic = DailySeries::make(
      discharge_meta(), days, samples, StatisticId::maximum());
  CHECK(other_statistic != series);
  const auto empty =
      DailySeries::make(SeriesMeta{}, {}, {}, StatisticId::minimum());
  REQUIRE(empty.has_value());
  CHECK(empty->empty());
}

TEST_CASE("DailySeries::make refuses unequal lengths and unordered days",
          "[core][daily_series]") {
  CHECK(DailySeries::make(discharge_meta(), {on(2026, 1, 1)}, {},
                          StatisticId::mean()) ==
        std::unexpected{
            ConstructionError{LengthMismatch{.times = 1, .samples = 0}}});
  // A repeated day is not after its predecessor.
  CHECK(DailySeries::make(
            discharge_meta(), {on(2026, 1, 1), on(2026, 1, 2), on(2026, 1, 2)},
            {value(1.0), value(2.0), value(3.0)}, StatisticId::mean()) ==
        std::unexpected{ConstructionError{TimeNotIncreasing{.index = 2}}});
  CHECK(DailySeries::make(discharge_meta(), {on(2026, 1, 2), on(2026, 1, 1)},
                          {value(1.0), value(2.0)}, StatisticId::mean()) ==
        std::unexpected{ConstructionError{TimeNotIncreasing{.index = 1}}});
  // Lengths are checked before the order.
  CHECK(DailySeries::make(discharge_meta(), {on(2026, 1, 2), on(2026, 1, 1)},
                          {value(1.0)}, StatisticId::mean()) ==
        std::unexpected{
            ConstructionError{LengthMismatch{.times = 2, .samples = 1}}});
}
