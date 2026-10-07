// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <bit>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <numeric>
#include <optional>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/series_ops.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "series_ops_helpers.hpp"
#include "test_helpers.hpp"

using mov::core::Affine;
using mov::core::Bucket;
using mov::core::ColumnIndex;
using mov::core::combine;
using mov::core::convert;
using mov::core::DataSource;
using mov::core::Dry;
using mov::core::Extent;
using mov::core::extent;
using mov::core::Extreme;
using mov::core::FileStation;
using mov::core::GenericQuantity;
using mov::core::IncompatibleUnits;
using mov::core::LengthUnit;
using mov::core::Location;
using mov::core::max_abs_time_ms;
using mov::core::Missing;
using mov::core::ObsVsPred;
using mov::core::Point;
using mov::core::Quantity;
using mov::core::quick_stats;
using mov::core::QuickStats;
using mov::core::residual;
using mov::core::ResidualErrc;
using mov::core::Sample;
using mov::core::scale_offset;
using mov::core::SeriesMeta;
using mov::core::shift_time;
using mov::core::slice;
using mov::core::SpeedUnit;
using mov::core::StationIndex;
using mov::core::StationRow;
using mov::core::StationTable;
using mov::core::summarize;
using mov::core::TemperatureUnit;
using mov::core::Time;
using mov::core::TimeAxis;
using mov::core::TimeOverflow;
using mov::core::TimeRange;
using mov::core::TimeSeries;
using mov::core::Unit;
using mov::core::UnitError;
using mov::core::UnknownUnit;
using mov::core::ValueRange;
using mov::core::Variable;
using mov::core::VerticalDatum;
using mov::test::at_ms;
using mov::test::axis_of;
using mov::test::level_meta;
using mov::test::make_series;
using mov::test::near;
using mov::test::near_abs;
using mov::test::random_rows;
using mov::test::series_of_rows;
using mov::test::val;

namespace {

constexpr std::uint32_t seed_value = 20261007;  // fixed: a failure reproduces

Bucket bucket_of(std::span<const Point> rows) {
  return std::accumulate(rows.begin(), rows.end(), Bucket{},
                         [](const Bucket& acc, const Point& p) {
                           return acc + Bucket::of(p.time, p.sample);
                         });
}

std::vector<Sample> samples_of(const TimeSeries& s) {
  return {s.samples().begin(), s.samples().end()};
}

std::vector<Time> times_of(const TimeSeries& s) {
  return {s.times().begin(), s.times().end()};
}

}  // namespace

// ---- Bucket
// -------------------------------------------------------------------

TEST_CASE("Bucket::of classifies one sample", "[core][series_ops][bucket]") {
  const Bucket v = Bucket::of(at_ms(7), val(2.5));
  CHECK(v.values() == 1);
  CHECK(v.missing() == 0);
  CHECK(v.dry() == 0);
  CHECK(v.min() == Extreme{.value = 2.5, .time = at_ms(7)});
  CHECK(v.max() == v.min());
  CHECK(v.sum() == 2.5);
  CHECK(not v.has_gap());

  const Bucket m = Bucket::of(at_ms(1), Sample{Missing{}});
  CHECK(m.missing() == 1);
  CHECK(m.values() == 0);
  CHECK(m.dry() == 0);
  CHECK(not m.min());
  CHECK(not m.max());
  CHECK(m.has_gap());

  const Bucket d = Bucket::of(at_ms(1), Sample{Dry{}});
  CHECK(d.dry() == 1);
  CHECK(d.missing() == 0);
  CHECK(d.has_gap());
}

TEST_CASE("Bucket: the default is an exact two-sided identity",
          "[core][series_ops][bucket]") {
  std::mt19937 rng{seed_value};
  CHECK(Bucket{} == Bucket{});
  CHECK(Bucket{}.values() == 0);
  CHECK(not Bucket{}.min());
  for (int trial = 0; trial < 100; ++trial) {
    const auto rows =
        random_rows(rng, 1 + (static_cast<std::size_t>(trial) % 40));
    const Bucket b = bucket_of(rows);
    CHECK(Bucket{} + b == b);
    CHECK(b + Bucket{} == b);
  }
}

TEST_CASE("Bucket: + is associative, exactly on dyadic data",
          "[core][series_ops][bucket]") {
  std::mt19937 rng{seed_value};
  for (int trial = 0; trial < 200; ++trial) {
    const auto rows =
        random_rows(rng, 3 + (static_cast<std::size_t>(trial) % 60));
    std::uniform_int_distribution<std::size_t> cut{0, rows.size()};
    std::size_t i = cut(rng);
    std::size_t j = cut(rng);
    if (i > j) {
      std::swap(i, j);
    }
    const std::span<const Point> all{rows};
    const Bucket a = bucket_of(all.first(i));
    const Bucket b = bucket_of(all.subspan(i, j - i));
    const Bucket c = bucket_of(all.subspan(j));
    INFO("cut points " << i << ", " << j << " of " << rows.size());
    CHECK((a + b) + c == a + (b + c));
    // Any grouping equals the whole fold, because the sums are exact.
    CHECK((a + b) + c == bucket_of(all));
  }
}

TEST_CASE("Bucket: + is associative within rounding on arbitrary doubles",
          "[core][series_ops][bucket]") {
  std::mt19937 rng{seed_value};
  std::uniform_real_distribution<double> value{-1000.0, 1000.0};
  for (int trial = 0; trial < 200; ++trial) {
    std::vector<Point> rows;
    const std::size_t n = 3 + (static_cast<std::size_t>(trial) % 60);
    for (std::size_t k = 0; k < n; ++k) {
      rows.push_back({.time = at_ms(static_cast<std::int64_t>(k)),
                      .sample = val(value(rng))});
    }
    std::uniform_int_distribution<std::size_t> cut{0, n};
    std::size_t i = cut(rng);
    std::size_t j = cut(rng);
    if (i > j) {
      std::swap(i, j);
    }
    const std::span<const Point> all{rows};
    const Bucket a = bucket_of(all.first(i));
    const Bucket b = bucket_of(all.subspan(i, j - i));
    const Bucket c = bucket_of(all.subspan(j));
    const Bucket left = (a + b) + c;
    const Bucket right = a + (b + c);
    CHECK(left.values() == right.values());
    CHECK(left.min() == right.min());
    CHECK(left.max() == right.max());
    CHECK(near_abs(left.sum(), right.sum(), 1e-9));
    CHECK(near_abs(left.sum(), bucket_of(all).sum(), 1e-9));
  }
}

TEST_CASE("Bucket: the first of equal extremes wins and + is not commutative",
          "[core][series_ops][bucket]") {
  const Bucket early = Bucket::of(at_ms(1), val(5.0));
  const Bucket late = Bucket::of(at_ms(2), val(5.0));
  CHECK((early + late).max() == Extreme{.value = 5.0, .time = at_ms(1)});
  CHECK((early + late).min() == Extreme{.value = 5.0, .time = at_ms(1)});
  CHECK((late + early).max() == Extreme{.value = 5.0, .time = at_ms(2)});
  CHECK(early + late != late + early);
}

TEST_CASE("Bucket: counts, extremes and the gap flag of a mixed run",
          "[core][series_ops][bucket]") {
  const std::vector<Point> rows{{.time = at_ms(0), .sample = val(3.0)},
                                {.time = at_ms(1), .sample = Sample{Missing{}}},
                                {.time = at_ms(2), .sample = val(-1.0)},
                                {.time = at_ms(3), .sample = Sample{Dry{}}},
                                {.time = at_ms(4), .sample = val(3.0)}};
  const Bucket b = bucket_of(rows);
  CHECK(b.values() == 3);
  CHECK(b.missing() == 1);
  CHECK(b.dry() == 1);
  CHECK(b.min() == Extreme{.value = -1.0, .time = at_ms(2)});
  CHECK(b.max() == Extreme{.value = 3.0, .time = at_ms(0)});
  CHECK(b.sum() == 5.0);
  CHECK(b.has_gap());
}

TEST_CASE("summarize is the ordered left fold of the points",
          "[core][series_ops][bucket]") {
  CHECK(summarize(TimeSeries{}) == Bucket{});
  std::mt19937 rng{seed_value};
  for (int trial = 0; trial < 50; ++trial) {
    const auto rows =
        random_rows(rng, 1 + (static_cast<std::size_t>(trial) % 50));
    CHECK(summarize(series_of_rows(rows)) == bucket_of(rows));
  }
}

// ---- extent
// ------------------------------------------------------------------------

TEST_CASE("extent of an empty series is nullopt (B14)",
          "[core][series_ops][extent][regression][B14]") {
  CHECK(extent(TimeSeries{}) == std::nullopt);
  CHECK(extent(make_series({}, {}, level_meta())) == std::nullopt);
}

TEST_CASE("extent of a series has its times and value range",
          "[core][series_ops][extent]") {
  const TimeSeries s = make_series(
      axis_of({10, 20, 30, 40}),
      {val(2.0), Sample{Missing{}}, val(-4.5), Sample{Dry{}}}, level_meta());
  CHECK(extent(s) == Extent{.first = at_ms(10),
                            .last = at_ms(40),
                            .values = ValueRange{.min = -4.5, .max = 2.0}});
}

TEST_CASE("extent of a series without a value has times only",
          "[core][series_ops][extent]") {
  const TimeSeries s = make_series(
      axis_of({5, 6}), {Sample{Missing{}}, Sample{Dry{}}}, level_meta());
  CHECK(extent(s) ==
        Extent{.first = at_ms(5), .last = at_ms(6), .values = std::nullopt});
}

TEST_CASE("extent of several series is the true min and max (B13)",
          "[core][series_ops][extent][regression][B13]") {
  const std::vector<TimeSeries> all{
      make_series(axis_of({10, 20}), {val(1.0), val(5.0)}), TimeSeries{},
      make_series(axis_of({0, 15}), {val(-3.0), val(2.0)}),
      make_series(axis_of({30}), {Sample{Missing{}}})};
  CHECK(extent(all) == Extent{.first = at_ms(0),
                              .last = at_ms(30),
                              .values = ValueRange{.min = -3.0, .max = 5.0}});
  CHECK(extent(std::span<const TimeSeries>{}) == std::nullopt);
  const std::vector<TimeSeries> empties(3);
  CHECK(extent(empties) == std::nullopt);
}

TEST_CASE("combine of extents is a semilattice with the empty extent as unit",
          "[core][series_ops][extent]") {
  std::mt19937 rng{seed_value};
  std::vector<std::optional<Extent>> pool{std::nullopt};
  for (int k = 0; k < 12; ++k) {
    pool.push_back(extent(
        series_of_rows(random_rows(rng, static_cast<std::size_t>(k) % 5))));
  }
  for (const auto& a : pool) {
    CHECK(combine(a, std::nullopt) == a);
    CHECK(combine(std::nullopt, a) == a);
    CHECK(combine(a, a) == a);
    for (const auto& b : pool) {
      CHECK(combine(a, b) == combine(b, a));
      for (const auto& c : pool) {
        CHECK(combine(combine(a, b), c) == combine(a, combine(b, c)));
      }
    }
  }
}

// ---- quick_stats
// ----------------------------------------------------------------

TEST_CASE("quick_stats is total", "[core][series_ops][quick_stats]") {
  CHECK(quick_stats(TimeSeries{}) ==
        QuickStats{.values = 0, .missing = 0, .dry = 0, .stats = std::nullopt});
  const TimeSeries gaps = make_series(
      axis_of({1, 2, 3}), {Sample{Missing{}}, Sample{Dry{}}, Sample{Missing{}}},
      level_meta());
  CHECK(quick_stats(gaps) ==
        QuickStats{.values = 0, .missing = 2, .dry = 1, .stats = std::nullopt});
}

TEST_CASE("quick_stats counts and summarizes",
          "[core][series_ops][quick_stats]") {
  const TimeSeries s = make_series(
      axis_of({0, 10, 20, 30, 40}),
      {val(1.0), Sample{Missing{}}, val(4.0), Sample{Dry{}}, val(1.0)},
      level_meta());
  const QuickStats q = quick_stats(s);
  CHECK(q.values == 3);
  CHECK(q.missing == 1);
  CHECK(q.dry == 1);
  REQUIRE(q.stats.has_value());
  CHECK(q.stats->min == Extreme{.value = 1.0, .time = at_ms(0)});  // first
  CHECK(q.stats->max == Extreme{.value = 4.0, .time = at_ms(20)});
  CHECK(q.stats->mean == 2.0);
}

TEST_CASE("quick_stats peak is the first maximum, as max_element finds it",
          "[core][series_ops][quick_stats]") {
  std::mt19937 rng{seed_value};
  for (int trial = 0; trial < 200; ++trial) {
    const auto rows =
        random_rows(rng, 2 + (static_cast<std::size_t>(trial) % 50));
    std::vector<Point> values;
    std::ranges::copy_if(rows, std::back_inserter(values),
                         [](const Point& p) { return p.sample.is_value(); });
    const QuickStats q = quick_stats(series_of_rows(rows));
    if (values.empty()) {
      CHECK(not q.stats);
      continue;
    }
    const auto by_value = [](const Point& a, const Point& b) {
      return a.sample.value() < b.sample.value();
    };
    const auto peak = std::ranges::max_element(values, by_value);
    const auto lowest = std::ranges::min_element(values, by_value);
    REQUIRE(q.stats.has_value());
    CHECK(q.stats->max.time == peak->time);
    CHECK(q.stats->max.value == peak->sample.value());
    CHECK(q.stats->min.time == lowest->time);
    CHECK(q.stats->min.value == lowest->sample.value());
    CHECK(q.values == values.size());
  }
}

TEST_CASE("quick_stats mean stays finite when the sum overflows",
          "[core][series_ops][quick_stats]") {
  constexpr double huge = 1.7e308;
  const TimeSeries same =
      make_series(axis_of({0, 1, 2}), {val(huge), val(huge), val(huge)});
  const QuickStats q = quick_stats(same);
  REQUIRE(q.stats.has_value());
  CHECK(near(q.stats->mean, huge));

  // inf + (-inf) would be NaN.
  const TimeSeries opposite = make_series(
      axis_of({0, 1, 2, 3}), {val(huge), val(huge), val(-huge), val(-huge)});
  const QuickStats r = quick_stats(opposite);
  REQUIRE(r.stats.has_value());
  CHECK(near_abs(r.stats->mean, 0.0, 1e300));
  CHECK(std::isfinite(r.stats->mean));
}

// ---- residual
// -----------------------------------------------------------------------

namespace {

const Unit metre = LengthUnit::meter;

SeriesMeta obs_meta(std::optional<VerticalDatum> d = VerticalDatum::msl) {
  return level_meta(metre, d, "obs", Quantity::water_level);
}
SeriesMeta pred_meta(std::optional<VerticalDatum> d = VerticalDatum::msl) {
  return level_meta(metre, d, "pred", Quantity::water_level_prediction);
}

ObsVsPred pair_of(const SeriesMeta& o, const SeriesMeta& p) {
  return {.observed = make_series(axis_of({0, 1}), {val(1.0), val(2.0)}, o),
          .predicted = make_series(axis_of({0, 1}), {val(0.5), val(1.0)}, p)};
}

}  // namespace

TEST_CASE("residual is observed minus predicted on equal times only",
          "[core][series_ops][residual]") {
  const ObsVsPred pair{.observed = make_series(axis_of({0, 1, 2, 3, 5}),
                                               {val(1.0), val(2.0), val(3.0),
                                                Sample{Dry{}}, Sample{Dry{}}},
                                               obs_meta()),
                       .predicted = make_series(axis_of({1, 3, 4, 5, 6}),
                                                {val(0.5), val(1.0), val(9.0),
                                                 Sample{Missing{}}, val(7.0)},
                                                pred_meta())};
  const auto r = residual(pair);
  REQUIRE(r.has_value());
  CHECK(times_of(*r) == std::vector<Time>{at_ms(1), at_ms(3), at_ms(5)});
  // 2.0 - 0.5; Dry - 1.0 is Dry; Dry - Missing is Missing (the combine rule).
  CHECK(samples_of(*r) ==
        std::vector<Sample>{val(1.5), Sample{Dry{}}, Sample{Missing{}}});
}

TEST_CASE("residual merge-join edge cases", "[core][series_ops][residual]") {
  const auto join = [](TimeAxis a, TimeAxis b) {
    const std::size_t n = a.size();
    const std::size_t m = b.size();
    const ObsVsPred pair{
        .observed = make_series(std::move(a), std::vector<Sample>(n, val(3.0)),
                                obs_meta()),
        .predicted = make_series(std::move(b), std::vector<Sample>(m, val(1.0)),
                                 pred_meta())};
    const auto r = residual(pair);
    REQUIRE(r.has_value());
    return times_of(r.value_or(TimeSeries{}));
  };
  const auto at = [](std::initializer_list<std::int64_t> ms) {
    std::vector<Time> out;
    for (const std::int64_t m : ms) {
      out.push_back(at_ms(m));
    }
    return out;
  };
  CHECK(join({}, {}).empty());
  CHECK(join(axis_of({1, 2}), {}).empty());
  CHECK(join({}, axis_of({1, 2})).empty());
  CHECK(join(axis_of({1, 2, 3}), axis_of({4, 5})).empty());  // disjoint
  CHECK(join(axis_of({4, 5}), axis_of({1, 2, 3})).empty());
  CHECK(join(axis_of({1, 3, 5}), axis_of({2, 4, 6})).empty());  // interleaved
  CHECK(join(axis_of({1, 2, 3}), axis_of({1, 2, 3})) == at({1, 2, 3}));
  CHECK(join(axis_of({1, 2, 3, 4, 5}), axis_of({3})) == at({3}));
  CHECK(join(axis_of({3}), axis_of({1, 2, 3, 4, 5})) == at({3}));
  // Only the first and the last match.
  CHECK(join(axis_of({1, 2, 9}), axis_of({1, 5, 9})) == at({1, 9}));
  // Neighbouring milliseconds are not equal times.
  CHECK(join(axis_of({10}), axis_of({11})).empty());
}

TEST_CASE("residual meta: generic value, common unit, no datum, minus label",
          "[core][series_ops][residual]") {
  const auto r = residual(pair_of(obs_meta(), pred_meta()));
  REQUIRE(r.has_value());
  CHECK(r->meta().quantity() ==
        mov::core::QuantityId{GenericQuantity::value()});
  CHECK(r->meta().unit() == std::optional<Unit>{metre});
  CHECK(r->meta().datum() == std::nullopt);
  // "obs" U+2212 "pred", with the sign as its UTF-8 bytes.
  CHECK(r->meta().label() == "obs \xE2\x88\x92 pred");
}

TEST_CASE("residual needs known, equal units", "[core][series_ops][residual]") {
  const auto no_unit = level_meta(std::nullopt, VerticalDatum::msl);
  CHECK(residual(pair_of(no_unit, pred_meta())) ==
        std::unexpected{ResidualErrc::unit_unknown});
  CHECK(residual(pair_of(obs_meta(), no_unit)) ==
        std::unexpected{ResidualErrc::unit_unknown});
  CHECK(residual(pair_of(no_unit, no_unit)) ==
        std::unexpected{ResidualErrc::unit_unknown});
  // Equal after conversion is still unequal: no silent conversion.
  const auto feet = level_meta(Unit{LengthUnit::foot}, VerticalDatum::msl,
                               "pred", Quantity::water_level_prediction);
  CHECK(residual(pair_of(obs_meta(), feet)) ==
        std::unexpected{ResidualErrc::units_differ});
  const auto in_feet = residual(pair_of(
      level_meta(Unit{LengthUnit::foot}, VerticalDatum::msl, "obs"), feet));
  REQUIRE(in_feet.has_value());
  CHECK(in_feet->meta().unit() == std::optional<Unit>{LengthUnit::foot});
}

TEST_CASE("residual refuses temperatures", "[core][series_ops][residual]") {
  const auto air = [](const char* label) {
    return level_meta(Unit{TemperatureUnit::celsius}, std::nullopt, label,
                      Quantity::air_temperature);
  };
  CHECK(residual(pair_of(air("a"), air("b"))) ==
        std::unexpected{ResidualErrc::temperature_difference});
  // Fahrenheit too, and a generic series (which could carry a datum): the
  // temperature check comes before the datum check.
  const auto generic_f =
      level_meta(Unit{TemperatureUnit::fahrenheit}, std::nullopt, "g",
                 GenericQuantity::value());
  CHECK(residual(pair_of(generic_f, generic_f)) ==
        std::unexpected{ResidualErrc::temperature_difference});
  // Unequal temperature units are units_differ, which comes first.
  const auto air_f = level_meta(Unit{TemperatureUnit::fahrenheit}, std::nullopt,
                                "f", Quantity::air_temperature);
  CHECK(residual(pair_of(air("a"), air_f)) ==
        std::unexpected{ResidualErrc::units_differ});
}

TEST_CASE("residual needs known, equal datums where a datum applies",
          "[core][series_ops][residual]") {
  CHECK(residual(pair_of(obs_meta(std::nullopt), pred_meta())) ==
        std::unexpected{ResidualErrc::datum_unknown});
  CHECK(residual(pair_of(obs_meta(), pred_meta(std::nullopt))) ==
        std::unexpected{ResidualErrc::datum_unknown});
  CHECK(residual(pair_of(obs_meta(std::nullopt), pred_meta(std::nullopt))) ==
        std::unexpected{ResidualErrc::datum_unknown});
  CHECK(residual(pair_of(obs_meta(VerticalDatum::msl),
                         pred_meta(VerticalDatum::mllw))) ==
        std::unexpected{ResidualErrc::datums_differ});
  CHECK(residual(pair_of(obs_meta(VerticalDatum::navd88),
                         pred_meta(VerticalDatum::navd88)))
            .has_value());
  // Units are checked before datums.
  const auto feet = level_meta(Unit{LengthUnit::foot}, std::nullopt, "pred",
                               Quantity::water_level_prediction);
  CHECK(residual(pair_of(obs_meta(std::nullopt), feet)) ==
        std::unexpected{ResidualErrc::units_differ});
}

TEST_CASE("residual of quantities that carry no datum needs none",
          "[core][series_ops][residual]") {
  const auto wind = [](const char* label) {
    return level_meta(Unit{SpeedUnit::meter_per_second}, std::nullopt, label,
                      Quantity::wind_speed);
  };
  const auto r = residual(pair_of(wind("a"), wind("b")));
  REQUIRE(r.has_value());
  CHECK(r->meta().unit() == std::optional<Unit>{SpeedUnit::meter_per_second});
  // One side that can carry a datum is enough to require both.
  CHECK(residual(pair_of(obs_meta(), wind("b"))) ==
        std::unexpected{ResidualErrc::units_differ});
  const auto ms = level_meta(Unit{SpeedUnit::meter_per_second},
                             VerticalDatum::msl, "g", GenericQuantity::value());
  CHECK(residual(pair_of(ms, wind("b"))) ==
        std::unexpected{ResidualErrc::datum_unknown});
}

TEST_CASE("residual does not modify its inputs",
          "[core][series_ops][residual]") {
  const ObsVsPred pair = pair_of(obs_meta(), pred_meta());
  const ObsVsPred copy = pair;
  static_cast<void>(residual(pair));
  CHECK(pair == copy);
}

// ---- slice
// ------------------------------------------------------------------------

namespace {

TimeRange range(std::int64_t begin, std::int64_t end) {
  const auto r = TimeRange::make(at_ms(begin), at_ms(end));
  REQUIRE(r.has_value());
  return *r;
}

}  // namespace

TEST_CASE("slice is half-open", "[core][series_ops][slice]") {
  const TimeSeries s =
      make_series(axis_of({10, 20, 30, 40}),
                  {val(1.0), val(2.0), val(3.0), val(4.0)}, level_meta());
  const auto check = [&s](std::int64_t b, std::int64_t e,
                          std::vector<Time> expect_times,
                          std::vector<Sample> expect_samples) {
    INFO("[" << b << ", " << e << ")");
    for (const TimeSeries& got :
         {slice(s, range(b, e)), slice(TimeSeries{s}, range(b, e))}) {
      CHECK(times_of(got) == expect_times);
      CHECK(samples_of(got) == expect_samples);
      CHECK(got.meta() == s.meta());
    }
  };
  check(20, 40, {at_ms(20), at_ms(30)}, {val(2.0), val(3.0)});  // end excluded
  check(21, 40, {at_ms(30)}, {val(3.0)});
  check(20, 21, {at_ms(20)}, {val(2.0)});
  check(0, 1000, {at_ms(10), at_ms(20), at_ms(30), at_ms(40)},
        {val(1.0), val(2.0), val(3.0), val(4.0)});
  check(0, 10, {}, {});  // ends where the series starts
  check(41, 50, {}, {});
  check(11, 20, {}, {});  // between two samples
  check(40, 41, {at_ms(40)}, {val(4.0)});
  CHECK(slice(TimeSeries{}, range(0, 5)).empty());
}

TEST_CASE("slice agrees with filtering by contains, for both overloads",
          "[core][series_ops][slice]") {
  std::mt19937 rng{seed_value};
  std::uniform_int_distribution<int> any{-5, 200};
  for (int trial = 0; trial < 200; ++trial) {
    const auto rows = random_rows(rng, static_cast<std::size_t>(trial) % 60);
    const TimeSeries s = series_of_rows(rows, level_meta());
    int b = any(rng);
    int e = any(rng);
    if (b == e) {
      ++e;
    }
    if (b > e) {
      std::swap(b, e);
    }
    const TimeRange r = range(b, e);
    std::vector<Point> kept;
    std::ranges::copy_if(rows, std::back_inserter(kept),
                         [&r](const Point& p) { return r.contains(p.time); });
    const TimeSeries expected = series_of_rows(kept, level_meta());
    CHECK(slice(s, r) == expected);
    CHECK(slice(TimeSeries{s}, r) == expected);
  }
}

// ---- shift_time
// -----------------------------------------------------------------

TEST_CASE("shift_time moves every time and nothing else",
          "[core][series_ops][shift_time]") {
  using std::chrono::milliseconds;
  const TimeSeries s =
      make_series(axis_of({0, 1000, 5000}), {val(1.0), Sample{Dry{}}, Sample{}},
                  level_meta());
  const auto later = shift_time(s, milliseconds{250});
  REQUIRE(later.has_value());
  CHECK(times_of(*later) ==
        std::vector<Time>{at_ms(250), at_ms(1250), at_ms(5250)});
  CHECK(samples_of(*later) == samples_of(s));
  CHECK(later->meta() == s.meta());

  const auto earlier = shift_time(s, milliseconds{-3'600'000});
  REQUIRE(earlier.has_value());
  CHECK(times_of(*earlier).front() == at_ms(-3'600'000));

  CHECK(shift_time(s, milliseconds{0}) == s);
  CHECK(shift_time(TimeSeries{}, milliseconds{5}) == TimeSeries{});
}

TEST_CASE("shift_time is bounded by +-2^53 ms and names the first offender",
          "[core][series_ops][shift_time]") {
  using std::chrono::milliseconds;
  const std::int64_t top = max_abs_time_ms;
  const TimeSeries high =
      make_series(axis_of({0, top - 1, top}), {val(1.0), val(2.0), val(3.0)});
  CHECK(shift_time(high, milliseconds{0}) == high);
  CHECK(shift_time(high, milliseconds{1}) ==
        std::unexpected{TimeOverflow{.index = 2}});
  CHECK(shift_time(high, milliseconds{top}) ==
        std::unexpected{TimeOverflow{.index = 1}});

  const TimeSeries low =
      make_series(axis_of({-top, -top + 5, 0}), {val(1.0), val(2.0), val(3.0)});
  CHECK(shift_time(low, milliseconds{-1}) ==
        std::unexpected{TimeOverflow{.index = 0}});
  CHECK(shift_time(low, milliseconds{1}).has_value());

  // The extreme steps overflow 64 bits, not only the bound.
  constexpr auto biggest = milliseconds::max();
  constexpr auto smallest = milliseconds::min();
  CHECK(shift_time(high, biggest) == std::unexpected{TimeOverflow{.index = 0}});
  CHECK(shift_time(high, smallest) ==
        std::unexpected{TimeOverflow{.index = 0}});
}

TEST_CASE("shift_time can bring a series that is out of bounds back in",
          "[core][series_ops][shift_time]") {
  using std::chrono::milliseconds;
  const std::int64_t beyond = max_abs_time_ms + 10;
  const TimeSeries s =
      make_series(axis_of({beyond - 5, beyond}), {val(1.0), val(2.0)});
  // Not shifted at all: the unshifted times already leave the range.
  CHECK(shift_time(s, milliseconds{0}) ==
        std::unexpected{TimeOverflow{.index = 0}});
  // Only the later sample is out of range once the series moves back by 7.
  CHECK(shift_time(s, milliseconds{-7}) ==
        std::unexpected{TimeOverflow{.index = 1}});
  const auto back = shift_time(s, milliseconds{-20});
  REQUIRE(back.has_value());
  CHECK(times_of(*back) ==
        std::vector<Time>{at_ms(beyond - 25), at_ms(beyond - 20)});
}

// ---- scale_offset
// ----------------------------------------------------------------

TEST_CASE("scale_offset is y = scale * x + offset on values only",
          "[core][series_ops][scale_offset]") {
  const TimeSeries s = make_series(
      axis_of({0, 1, 2, 3}),
      {val(1.0), Sample{Dry{}}, Sample{Missing{}}, val(-0.5)}, level_meta());
  const TimeSeries r = scale_offset(s, Affine{.scale = 2.0, .offset = 1.0});
  CHECK(samples_of(r) == std::vector<Sample>{val(3.0), Sample{Dry{}},
                                             Sample{Missing{}}, val(0.0)});
  CHECK(times_of(r) == times_of(s));
  CHECK(r.meta() == s.meta());  // a calibration: unit and datum stay
}

TEST_CASE("scale_offset turns non-finite results into Missing",
          "[core][series_ops][scale_offset]") {
  const TimeSeries s = make_series(axis_of({0, 1}), {val(1e300), val(1.0)});
  const TimeSeries r = scale_offset(s, Affine{.scale = 1e300, .offset = 0.0});
  CHECK(samples_of(r) == std::vector<Sample>{Sample{Missing{}}, val(1e300)});
  const double nan = std::numeric_limits<double>::quiet_NaN();
  CHECK(samples_of(scale_offset(s, Affine{.scale = nan, .offset = 0.0})) ==
        std::vector<Sample>(2, Sample{Missing{}}));
}

TEST_CASE("scale_offset by the identity changes nothing, not even -0.0",
          "[core][series_ops][scale_offset]") {
  const TimeSeries s = make_series(axis_of({0}), {val(-0.0)});
  const TimeSeries r = scale_offset(s, Affine{});
  REQUIRE(r.samples().front().value().has_value());
  CHECK(std::signbit(r.samples().front().value().value_or(1.0)));
  CHECK(r == s);
}

// ---- convert
// -----------------------------------------------------------------------

TEST_CASE("convert changes the values and the unit together",
          "[core][series_ops][convert]") {
  const TimeSeries s = make_series(
      axis_of({0, 1, 2}), {val(1.0), Sample{Dry{}}, Sample{Missing{}}},
      level_meta(metre, VerticalDatum::navd88, "level"));
  const auto feet = convert(s, Unit{LengthUnit::foot});
  REQUIRE(feet.has_value());
  CHECK(feet->meta().unit() == std::optional<Unit>{LengthUnit::foot});
  CHECK(feet->meta().datum() == VerticalDatum::navd88);  // untouched
  CHECK(feet->meta().label() == "level");
  CHECK(feet->meta().quantity() == s.meta().quantity());
  CHECK(times_of(*feet) == times_of(s));
  REQUIRE(feet->samples()[0].value().has_value());
  CHECK(near(feet->samples()[0].value().value_or(0.0), 1.0 / 0.3048));
  CHECK(feet->samples()[1] == Sample{Dry{}});
  CHECK(feet->samples()[2] == Sample{Missing{}});

  // And back.
  const auto metres_again = convert(*feet, metre);
  REQUIRE(metres_again.has_value());
  CHECK(near(metres_again->samples()[0].value().value_or(0.0), 1.0));
  CHECK(metres_again->meta() == s.meta());
}

TEST_CASE("convert temperatures is affine", "[core][series_ops][convert]") {
  const SeriesMeta air =
      level_meta(Unit{TemperatureUnit::celsius}, std::nullopt, "air",
                 Quantity::air_temperature);
  const TimeSeries s =
      make_series(axis_of({0, 1}), {val(100.0), val(-40.0)}, air);
  const auto f = convert(s, Unit{TemperatureUnit::fahrenheit});
  REQUIRE(f.has_value());
  CHECK(near(f->samples()[0].value().value_or(0.0), 212.0));
  CHECK(near(f->samples()[1].value().value_or(0.0), -40.0));
  CHECK(f->meta().unit() == std::optional<Unit>{TemperatureUnit::fahrenheit});
}

TEST_CASE("convert to the same unit changes nothing",
          "[core][series_ops][convert]") {
  const TimeSeries s = make_series(axis_of({0, 1}), {val(-0.0), val(2.0)},
                                   level_meta(metre, VerticalDatum::msl));
  const auto same = convert(s, metre);
  REQUIRE(same.has_value());
  CHECK(*same == s);
  CHECK(std::signbit(same->samples()[0].value().value_or(1.0)));

  // Equal OtherUnits convert as the identity.
  const std::optional<Unit> percent = mov::core::parse_unit("percent");
  REQUIRE(percent.has_value());
  const TimeSeries humidity = make_series(
      axis_of({0}), {val(55.0)},
      level_meta(percent, std::nullopt, "rh", Quantity::relative_humidity));
  CHECK(convert(humidity, *percent) == humidity);
}

TEST_CASE("convert reports why it cannot", "[core][series_ops][convert]") {
  const TimeSeries unitless =
      make_series(axis_of({0}), {val(1.0)}, level_meta(std::nullopt));
  CHECK(convert(unitless, metre) == std::unexpected{UnitError{UnknownUnit{}}});

  const TimeSeries length = make_series(axis_of({0}), {val(1.0)}, level_meta());
  const Unit knots = SpeedUnit::knot;
  CHECK(convert(length, knots) == std::unexpected{UnitError{IncompatibleUnits{
                                      .from = metre, .to = knots}}});
  const std::optional<Unit> percent = mov::core::parse_unit("percent");
  REQUIRE(percent.has_value());
  CHECK(convert(length, *percent) ==
        std::unexpected{
            UnitError{IncompatibleUnits{.from = metre, .to = *percent}}});
}

TEST_CASE("convert turns a value that overflows into Missing",
          "[core][series_ops][convert]") {
  const TimeSeries s =
      make_series(axis_of({0, 1}), {val(1e308), val(1.0)},
                  level_meta(Unit{LengthUnit::kilometer}, std::nullopt));
  const auto m = convert(s, metre);
  REQUIRE(m.has_value());
  CHECK(m->samples()[0] == Sample{Missing{}});
  CHECK(m->samples()[1] == val(1000.0));
}

namespace {

FileStation station(const char* id) {
  return {.id = mov::core::StationKey::make(id).value(),
          .name = mov::core::StationText::make("name").value(),
          .location = Location::make({.lat = 29.0, .lon = -90.0}).value(),
          .native = std::nullopt,
          .source = DataSource::adcirc};
}

SeriesMeta wind_meta(std::optional<Unit> unit) {
  return SeriesMeta::make({.quantity = Quantity::wind_speed, .unit = unit});
}

// Two stations on different axes; column 0 water level (m), column 1 wind
// speed (m/s), column 2 wind gust with no unit.
StationTable two_station_table() {
  auto t = StationTable::make(
      {{.meta = level_meta(metre, std::nullopt, "level"),
        .per_station = {{val(1.0), Sample{Dry{}}},
                        {val(2.0), val(3.0), Sample{}}}},
       {.meta = wind_meta(SpeedUnit::meter_per_second),
        .per_station = {{val(10.0), val(20.0)},
                        {val(5.0), Sample{}, val(7.0)}}},
       {.meta = SeriesMeta::make({.quantity = Quantity::wind_gust}),
        .per_station = {{val(1.0), val(2.0)}, {val(3.0), val(4.0), val(5.0)}}}},
      {axis_of({0, 10}), axis_of({0, 10, 20})},
      {StationRow{.station = station("A"), .axis = 0},
       StationRow{.station = station("B"), .axis = 1}});
  REQUIRE(t.has_value());
  return t.value_or(StationTable{});
}

}  // namespace

TEST_CASE("convert converts one column of a table at every station",
          "[core][series_ops][convert][station_table]") {
  const StationTable t = two_station_table();
  const auto r = convert(t, ColumnIndex{0}, Unit{LengthUnit::foot});
  REQUIRE(r.has_value());
  CHECK(r->schema()[0].unit() == std::optional<Unit>{LengthUnit::foot});
  CHECK(r->schema()[0].label() == "level");
  CHECK(r->schema()[1] == t.schema()[1]);  // other columns untouched
  CHECK(r->size() == t.size());
  for (const StationIndex i : t.stations()) {
    CHECK(r->station(i) == t.station(i));
    CHECK(std::ranges::equal(r->times(i), t.times(i)));
    CHECK(std::ranges::equal(r->column(i, ColumnIndex{1}),
                             t.column(i, ColumnIndex{1})));
  }
  const auto a = r->column(StationIndex{0}, ColumnIndex{0});
  CHECK(near(a[0].value().value_or(0.0), 1.0 / 0.3048));
  CHECK(a[1] == Sample{Dry{}});
  const auto b = r->column(StationIndex{1}, ColumnIndex{0});
  CHECK(near(b[0].value().value_or(0.0), 2.0 / 0.3048));
  CHECK(near(b[1].value().value_or(0.0), 3.0 / 0.3048));
  CHECK(b[2] == Sample{Missing{}});

  // The series view agrees with converting the series directly.
  const auto direct = convert(t.series(StationIndex{1}, ColumnIndex{0}),
                              Unit{LengthUnit::foot});
  REQUIRE(direct.has_value());
  CHECK(r->series(StationIndex{1}, ColumnIndex{0}) == *direct);
}

TEST_CASE("convert of a table column reports the same errors as a series",
          "[core][series_ops][convert][station_table]") {
  const StationTable t = two_station_table();
  CHECK(convert(t, ColumnIndex{2}, metre) ==
        std::unexpected{UnitError{UnknownUnit{}}});
  const Unit knots = SpeedUnit::knot;
  CHECK(convert(t, ColumnIndex{0}, knots) ==
        std::unexpected{
            UnitError{IncompatibleUnits{.from = metre, .to = knots}}});
  const auto wind = convert(t, ColumnIndex{1}, knots);
  REQUIRE(wind.has_value());
  CHECK(wind->schema()[1].unit() == std::optional<Unit>{SpeedUnit::knot});
  CHECK(near(
      wind->column(StationIndex{0}, ColumnIndex{1})[0].value().value_or(0.0),
      10.0 / (1852.0 / 3600.0)));
  // Converting to the unit it already has gives back an equal table.
  CHECK(convert(t, ColumnIndex{0}, metre) == t);
}
