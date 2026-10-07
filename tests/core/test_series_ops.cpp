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
#include "mov/core/datum_shift.hpp"
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

using mov::core::Bucket;
using mov::core::Calibration;
using mov::core::ColumnIndex;
using mov::core::convert;
using mov::core::DataSource;
using mov::core::Dry;
using mov::core::Extent;
using mov::core::extent;
using mov::core::Extreme;
using mov::core::FileStation;
using mov::core::GenericQuantity;
using mov::core::IncompatibleUnits;
using mov::core::index_window;
using mov::core::join;
using mov::core::LengthUnit;
using mov::core::Location;
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
using mov::core::shift;
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
using mov::core::VerticalDatum;
using mov::core::Window;
using mov::test::at_ms;
using mov::test::axis_of;
using mov::test::bucket_of;
using mov::test::calibration;
using mov::test::first_of;
using mov::test::last_of;
using mov::test::level_meta;
using mov::test::make_series;
using mov::test::max_of;
using mov::test::mean_of;
using mov::test::min_of;
using mov::test::near;
using mov::test::near_abs;
using mov::test::random_rows;
using mov::test::series_of_rows;
using mov::test::stats_of;
using mov::test::sum_of;
using mov::test::unit_of;
using mov::test::val;

namespace {

std::vector<Sample> samples_of(const TimeSeries& s) {
  return {s.samples().begin(), s.samples().end()};
}

std::vector<Time> times_of(const TimeSeries& s) {
  return {s.times().begin(), s.times().end()};
}

std::uint64_t bits(double v) { return std::bit_cast<std::uint64_t>(v); }

// Three buckets from a random cut of rows: the pieces of a left-to-right run.
struct Pieces {
  Bucket a;
  Bucket b;
  Bucket c;
  Bucket whole;
};

Pieces cut_randomly(std::mt19937& rng, const std::vector<Point>& rows) {
  std::uniform_int_distribution<std::size_t> cut{0, rows.size()};
  std::size_t i = cut(rng);
  std::size_t j = cut(rng);
  if (i > j) {
    std::swap(i, j);
  }
  const std::span<const Point> all{rows};
  return {.a = bucket_of(all.first(i)),
          .b = bucket_of(all.subspan(i, j - i)),
          .c = bucket_of(all.subspan(j)),
          .whole = bucket_of(all)};
}

}  // namespace

// ---- Bucket ----

TEST_CASE("Bucket::of classifies one sample", "[core][series_ops][bucket]") {
  const Bucket v = Bucket::of(at_ms(7), val(2.5));
  const Extreme only{.value = 2.5, .time = at_ms(7)};
  CHECK(v.values() == 1);
  CHECK(v.missing() == 0);
  CHECK(v.dry() == 0);
  CHECK(min_of(v) == only);
  CHECK(max_of(v) == only);
  CHECK(first_of(v) == only);
  CHECK(last_of(v) == only);
  CHECK(sum_of(v) == 2.5);
  CHECK(mean_of(v) == 2.5);
  CHECK(not v.has_gap());

  const Bucket m = Bucket::of(at_ms(1), Sample{Missing{}});
  CHECK(m.missing() == 1);
  CHECK(m.values() == 0);
  CHECK(m.dry() == 0);
  CHECK(not m.summary());
  CHECK(m.has_gap());

  const Bucket d = Bucket::of(at_ms(1), Sample{Dry{}});
  CHECK(d.dry() == 1);
  CHECK(d.missing() == 0);
  CHECK(not d.summary());
  CHECK(d.has_gap());
}

TEST_CASE("Bucket: the default is an exact two-sided identity",
          "[core][series_ops][bucket]") {
  std::mt19937 rng = mov::test::fixed_rng();
  CHECK(Bucket{} == Bucket{});
  CHECK(Bucket{}.values() == 0);
  CHECK(not Bucket{}.summary());
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
  std::mt19937 rng = mov::test::fixed_rng();
  for (int trial = 0; trial < 200; ++trial) {
    const auto rows =
        random_rows(rng, 3 + (static_cast<std::size_t>(trial) % 60));
    const auto [a, b, c, whole] = cut_randomly(rng, rows);
    CHECK((a + b) + c == a + (b + c));
    // Any grouping equals the whole fold, because the sums are exact. That
    // includes first, last, min and max.
    CHECK((a + b) + c == whole);
  }
}

TEST_CASE("Bucket: + is associative within rounding on arbitrary doubles",
          "[core][series_ops][bucket]") {
  std::mt19937 rng = mov::test::fixed_rng();
  std::uniform_real_distribution<double> value{-1000.0, 1000.0};
  for (int trial = 0; trial < 200; ++trial) {
    const std::size_t n = 3 + (static_cast<std::size_t>(trial) % 60);
    std::vector<Point> rows(n);
    std::ranges::generate(rows, [&, k = std::int64_t{0}]() mutable {
      return Point{.time = at_ms(k++), .sample = val(value(rng))};
    });
    const auto [a, b, c, whole] = cut_randomly(rng, rows);
    const Bucket left = (a + b) + c;
    const Bucket right = a + (b + c);
    CHECK(left.values() == right.values());
    CHECK(min_of(left) == min_of(right));
    CHECK(max_of(left) == max_of(right));
    CHECK(first_of(left) == first_of(right));
    CHECK(last_of(left) == last_of(right));
    CHECK(near_abs(sum_of(left).value_or(0.0), sum_of(right).value_or(0.0),
                   1e-9));
    CHECK(near_abs(sum_of(left).value_or(0.0), sum_of(whole).value_or(0.0),
                   1e-9));
  }
}

TEST_CASE("Bucket: the first of equal extremes wins and + is not commutative",
          "[core][series_ops][bucket]") {
  const Bucket early = Bucket::of(at_ms(1), val(5.0));
  const Bucket late = Bucket::of(at_ms(2), val(5.0));
  CHECK(max_of(early + late) == Extreme{.value = 5.0, .time = at_ms(1)});
  CHECK(min_of(early + late) == Extreme{.value = 5.0, .time = at_ms(1)});
  CHECK(max_of(late + early) == Extreme{.value = 5.0, .time = at_ms(2)});
  CHECK(early + late != late + early);
}

TEST_CASE("Bucket: first and last are the earliest and latest value",
          "[core][series_ops][bucket]") {
  const Bucket gap = Bucket::of(at_ms(0), Sample{Missing{}});
  const Bucket dry = Bucket::of(at_ms(1), Sample{Dry{}});
  const Bucket p = Bucket::of(at_ms(2), val(1.0));
  const Bucket q = Bucket::of(at_ms(3), val(9.0));
  const Extreme at_p{.value = 1.0, .time = at_ms(2)};
  const Extreme at_q{.value = 9.0, .time = at_ms(3)};
  CHECK(first_of(gap + dry + p + q) == at_p);
  CHECK(last_of(gap + dry + p + q) == at_q);
  CHECK(first_of(p + q + gap) == at_p);
  CHECK(last_of(p + q + gap + dry) == at_q);
  // Left-biased first, right-biased last.
  CHECK(first_of(q + p) == at_q);
  CHECK(last_of(q + p) == at_p);

  std::mt19937 rng = mov::test::fixed_rng();
  for (int trial = 0; trial < 100; ++trial) {
    const auto rows =
        random_rows(rng, 1 + (static_cast<std::size_t>(trial) % 40));
    const auto is_value = [](const Point& r) { return r.sample.is_value(); };
    const Bucket b = bucket_of(rows);
    const auto first_row = std::ranges::find_if(rows, is_value);
    const auto last_row =
        std::ranges::find_if(rows.rbegin(), rows.rend(), is_value);
    if (first_row == rows.end()) {
      CHECK(not b.summary());
      continue;
    }
    CHECK(first_of(b) ==
          Extreme{.value = first_row->sample.value().value_or(0.0),
                  .time = first_row->time});
    CHECK(last_of(b) == Extreme{.value = last_row->sample.value().value_or(0.0),
                                .time = last_row->time});
  }
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
  CHECK(min_of(b) == Extreme{.value = -1.0, .time = at_ms(2)});
  CHECK(max_of(b) == Extreme{.value = 3.0, .time = at_ms(0)});
  CHECK(first_of(b) == Extreme{.value = 3.0, .time = at_ms(0)});
  CHECK(last_of(b) == Extreme{.value = 3.0, .time = at_ms(4)});
  CHECK(sum_of(b) == 5.0);
  CHECK(b.has_gap());
}

TEST_CASE("Bucket: the mean is bit-identical to the plain sum over n",
          "[core][series_ops][bucket]") {
  std::mt19937 rng = mov::test::fixed_rng();
  std::uniform_real_distribution<double> value{-1e6, 1e6};
  std::uniform_real_distribution<double> tiny{-1e-20, 1e-20};
  for (int trial = 0; trial < 200; ++trial) {
    const std::size_t n = 1 + (static_cast<std::size_t>(trial) % 80);
    std::vector<double> v(n);
    std::ranges::generate(v, [&] { return value(rng) + tiny(rng); });
    std::vector<Point> rows(n);
    std::ranges::transform(v, rows.begin(),
                           [k = std::int64_t{0}](double x) mutable {
                             return Point{.time = at_ms(k++), .sample = val(x)};
                           });
    // The ordered left fold of the plain doubles.
    const double plain = std::accumulate(v.begin(), v.end(), 0.0);
    const Bucket b = bucket_of(rows);
    REQUIRE(sum_of(b).has_value());
    CHECK(bits(sum_of(b).value_or(0.0)) == bits(plain));
    CHECK(bits(mean_of(b).value_or(0.0)) ==
          bits(plain / static_cast<double>(n)));
  }
}

TEST_CASE("Bucket: huge values neither overflow nor make a NaN",
          "[core][series_ops][bucket]") {
  constexpr double huge = 1.7e308;
  const auto at = [](std::int64_t t, double x) {
    return Bucket::of(at_ms(t), val(x));
  };
  // The sum of three of them is out of range, but the mean is not.
  const Bucket triple = at(0, huge) + at(1, huge) + at(2, huge);
  CHECK(not std::isfinite(sum_of(triple).value_or(0.0)));
  CHECK(near(mean_of(triple).value_or(0.0), huge));

  // inf + -inf would be NaN; here every grouping gives the same exact answer.
  const Bucket a = at(0, huge);
  const Bucket b = at(1, huge);
  const Bucket c = at(2, -huge);
  const Bucket d = at(3, -huge);
  const Bucket left = ((a + b) + c) + d;
  CHECK(left == a + (b + (c + d)));
  CHECK(left == (a + b) + (c + d));
  CHECK(left == (a + (b + c)) + d);
  CHECK(sum_of(left) == 0.0);
  CHECK(mean_of(left) == 0.0);
  CHECK(std::isfinite(mean_of(a + b + c).value_or(0.0)));
  CHECK(near(mean_of(a + b + c).value_or(0.0), huge / 3.0));
}

TEST_CASE("summarize is the ordered left fold, over a series or spans",
          "[core][series_ops][bucket]") {
  CHECK(summarize(TimeSeries{}) == Bucket{});
  std::mt19937 rng = mov::test::fixed_rng();
  for (int trial = 0; trial < 50; ++trial) {
    const auto rows =
        random_rows(rng, 1 + (static_cast<std::size_t>(trial) % 50));
    const TimeSeries s = series_of_rows(rows);
    CHECK(summarize(s) == bucket_of(rows));
    CHECK(summarize(s.times(), s.samples()) == summarize(s));
    // A sub-span summarizes that part of the series.
    const std::size_t from = rows.size() / 3;
    const std::size_t to = rows.size() - (rows.size() / 4);
    CHECK(summarize(s.times().subspan(from, to - from),
                    s.samples().subspan(from, to - from)) ==
          bucket_of(std::span<const Point>{rows}.subspan(from, to - from)));
  }
}

// ---- extent ----

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

TEST_CASE("join of extents is a semilattice with the empty extent as unit",
          "[core][series_ops][extent]") {
  std::mt19937 rng = mov::test::fixed_rng();
  std::vector<std::optional<Extent>> pool{std::nullopt};
  for (std::size_t k = 0; k < 12; ++k) {
    pool.push_back(extent(series_of_rows(random_rows(rng, k % 5))));
  }
  for (const auto& a : pool) {
    CHECK(join(a, std::nullopt) == a);
    CHECK(join(std::nullopt, a) == a);
    CHECK(join(a, a) == a);
    for (const auto& b : pool) {
      CHECK(join(a, b) == join(b, a));
      for (const auto& c : pool) {
        CHECK(join(join(a, b), c) == join(a, join(b, c)));
      }
    }
  }
}

// ---- quick_stats ----

TEST_CASE("quick_stats is total", "[core][series_ops][quick_stats]") {
  const QuickStats none = quick_stats(TimeSeries{});
  CHECK(none == QuickStats{.missing = 0, .dry = 0, .stats = std::nullopt});
  CHECK(none.values() == 0);
  const TimeSeries gaps = make_series(
      axis_of({1, 2, 3}), {Sample{Missing{}}, Sample{Dry{}}, Sample{Missing{}}},
      level_meta());
  const QuickStats g = quick_stats(gaps);
  CHECK(g == QuickStats{.missing = 2, .dry = 1, .stats = std::nullopt});
  CHECK(g.values() == 0);
}

TEST_CASE("quick_stats counts and summarizes",
          "[core][series_ops][quick_stats]") {
  const TimeSeries s = make_series(
      axis_of({0, 10, 20, 30, 40}),
      {val(1.0), Sample{Missing{}}, val(4.0), Sample{Dry{}}, val(1.0)},
      level_meta());
  const QuickStats q = quick_stats(s);
  CHECK(q.values() == 3);
  CHECK(q.missing == 1);
  CHECK(q.dry == 1);
  const mov::core::ValueStats stats = stats_of(q);
  CHECK(stats.count == 3);
  CHECK(stats.min == Extreme{.value = 1.0, .time = at_ms(0)});  // first
  CHECK(stats.max == Extreme{.value = 4.0, .time = at_ms(20)});
  CHECK(stats.mean == 2.0);
  CHECK(q == quick_stats(s));
}

TEST_CASE("quick_stats peak is the first maximum, as max_element finds it",
          "[core][series_ops][quick_stats]") {
  std::mt19937 rng = mov::test::fixed_rng();
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
    const mov::core::ValueStats stats = stats_of(q);
    CHECK(stats.max.time == peak->time);
    CHECK(stats.max.value == peak->sample.value());
    CHECK(stats.min.time == lowest->time);
    CHECK(stats.min.value == lowest->sample.value());
    CHECK(q.values() == values.size());
  }
}

TEST_CASE("quick_stats mean stays finite when the sum overflows",
          "[core][series_ops][quick_stats]") {
  constexpr double huge = 1.7e308;
  const TimeSeries same = make_series(
      axis_of({0, 1, 2, 3, 4}),
      {val(huge), Sample{Missing{}}, val(huge), Sample{Dry{}}, val(huge)});
  CHECK(near(stats_of(quick_stats(same)).mean, huge));

  // inf + (-inf) would be NaN.
  const TimeSeries opposite = make_series(
      axis_of({0, 1, 2, 3}), {val(huge), val(huge), val(-huge), val(-huge)});
  CHECK(stats_of(quick_stats(opposite)).mean == 0.0);
}

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

// ---- residual ----
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
  const auto joined_times = [](TimeAxis a, TimeAxis b) {
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
  CHECK(joined_times({}, {}).empty());
  CHECK(joined_times(axis_of({1, 2}), {}).empty());
  CHECK(joined_times({}, axis_of({1, 2})).empty());
  CHECK(joined_times(axis_of({1, 2, 3}), axis_of({4, 5})).empty());  // disjoint
  CHECK(joined_times(axis_of({4, 5}), axis_of({1, 2, 3})).empty());
  CHECK(joined_times(axis_of({1, 3, 5}), axis_of({2, 4, 6}))
            .empty());  // interleaved
  CHECK(joined_times(axis_of({1, 2, 3}), axis_of({1, 2, 3})) == at({1, 2, 3}));
  CHECK(joined_times(axis_of({1, 2, 3, 4, 5}), axis_of({3})) == at({3}));
  CHECK(joined_times(axis_of({3}), axis_of({1, 2, 3, 4, 5})) == at({3}));
  // Only the first and the last match.
  CHECK(joined_times(axis_of({1, 2, 9}), axis_of({1, 5, 9})) == at({1, 9}));
  // Neighbouring milliseconds are not equal times.
  CHECK(joined_times(axis_of({10}), axis_of({11})).empty());
}

TEST_CASE("residual output: the difference quantity, common unit, no datum",
          "[core][series_ops][residual]") {
  const auto r = residual(pair_of(obs_meta(), pred_meta()));
  REQUIRE(r.has_value());
  CHECK(r->meta().quantity() == mov::core::QuantityId{Quantity::difference});
  CHECK(r->meta().unit() == std::optional<Unit>{metre});
  CHECK(r->meta().datum() == std::nullopt);
  // "obs" U+2212 "pred", with the sign as its UTF-8 bytes.
  CHECK(r->meta().label() == "obs \xE2\x88\x92 pred");
  // Not a water level: a datum cannot be added, and it cannot be shifted.
  CHECK(r->meta().assume_datum(VerticalDatum::msl) ==
        std::unexpected{mov::core::AssumeDatumError::not_applicable});
  CHECK(
      shift(*r, VerticalDatum::mllw, mov::core::DatumTable{}) ==
      std::unexpected{mov::core::ShiftError{mov::core::UnknownSourceDatum{}}});
}

TEST_CASE("a residual can feed a residual", "[core][series_ops][residual]") {
  const auto first = residual(pair_of(obs_meta(), pred_meta()));
  REQUIRE(first.has_value());
  const auto second =
      residual(ObsVsPred{.observed = *first, .predicted = *first});
  REQUIRE(second.has_value());
  CHECK(second->meta().quantity() ==
        mov::core::QuantityId{Quantity::difference});
  CHECK(samples_of(*second) == std::vector<Sample>(2, val(0.0)));
  // But a difference is not a water level.
  const auto level =
      make_series(axis_of({0, 1}), {val(1.0), val(1.0)}, pred_meta());
  CHECK(residual(ObsVsPred{.observed = *first, .predicted = level}) ==
        std::unexpected{ResidualErrc::quantities_differ});
}

TEST_CASE("residual needs the same quantity or an observed and its prediction",
          "[core][series_ops][residual]") {
  const auto meta_for = [](Quantity q, const char* label) {
    const bool takes_datum = mov::core::datum_applicable(q);
    return level_meta(
        metre, takes_datum ? std::optional{VerticalDatum::msl} : std::nullopt,
        label, q);
  };
  const auto with = [&meta_for](Quantity o, Quantity p) {
    return pair_of(meta_for(o, "o"), meta_for(p, "p"));
  };
  CHECK(residual(with(Quantity::water_level, Quantity::water_level_prediction))
            .has_value());
  CHECK(
      residual(with(Quantity::water_level, Quantity::water_level)).has_value());
  CHECK(residual(with(Quantity::water_level_prediction,
                      Quantity::water_level_prediction))
            .has_value());
  // The pair is ordered: observed first.
  CHECK(
      residual(with(Quantity::water_level_prediction, Quantity::water_level)) ==
      std::unexpected{ResidualErrc::quantities_differ});
  CHECK(residual(with(Quantity::water_level, Quantity::wave_height)) ==
        std::unexpected{ResidualErrc::quantities_differ});
  CHECK(residual(with(Quantity::wind_speed, Quantity::wind_gust)) ==
        std::unexpected{ResidualErrc::quantities_differ});

  // Generic quantities match by token.
  const auto generic = [](const char* token, const char* standard) {
    const auto g =
        GenericQuantity::parse({.token = token, .standard_name = standard});
    REQUIRE(g.has_value());
    return mov::core::QuantityId{g.value_or(GenericQuantity::value())};
  };
  const auto meta_of = [](mov::core::QuantityId q) {
    return level_meta(metre, VerticalDatum::msl, "g", std::move(q));
  };
  CHECK(residual(
            pair_of(meta_of(generic("ph", "a")), meta_of(generic("ph", "b"))))
            .has_value());
  CHECK(residual(
            pair_of(meta_of(generic("ph", "")), meta_of(generic("sal", "")))) ==
        std::unexpected{ResidualErrc::quantities_differ});
  CHECK(residual(pair_of(meta_of(generic("ph", "")),
                         level_meta(metre, VerticalDatum::msl, "w",
                                    Quantity::water_level))) ==
        std::unexpected{ResidualErrc::quantities_differ});
}

TEST_CASE("residual checks quantities, then units, temperatures, datums",
          "[core][series_ops][residual]") {
  const auto feet = [](Quantity q, const char* label) {
    return level_meta(Unit{LengthUnit::foot}, std::nullopt, label, q);
  };
  // Quantities come first: the units and datums are also wrong here.
  CHECK(residual(pair_of(
            level_meta(std::nullopt, std::nullopt, "o", Quantity::wave_height),
            feet(Quantity::water_level, "p"))) ==
        std::unexpected{ResidualErrc::quantities_differ});
  // Then units.
  CHECK(residual(pair_of(obs_meta(std::nullopt),
                         feet(Quantity::water_level_prediction, "p"))) ==
        std::unexpected{ResidualErrc::units_differ});
  // Then temperatures, then datums.
  const auto air = [](TemperatureUnit u) {
    return level_meta(Unit{u}, std::nullopt, "t", Quantity::air_temperature);
  };
  CHECK(residual(pair_of(air(TemperatureUnit::celsius),
                         air(TemperatureUnit::celsius))) ==
        std::unexpected{ResidualErrc::temperature_difference});
  CHECK(residual(pair_of(obs_meta(std::nullopt), pred_meta(std::nullopt))) ==
        std::unexpected{ResidualErrc::datum_unknown});
}

TEST_CASE("residual needs known, equal units", "[core][series_ops][residual]") {
  const auto no_unit = level_meta(std::nullopt, VerticalDatum::msl, "p",
                                  Quantity::water_level_prediction);
  CHECK(residual(pair_of(obs_meta(), no_unit)) ==
        std::unexpected{ResidualErrc::unit_unknown});
  const auto obs_no_unit =
      level_meta(std::nullopt, VerticalDatum::msl, "o", Quantity::water_level);
  CHECK(residual(pair_of(obs_no_unit, pred_meta())) ==
        std::unexpected{ResidualErrc::unit_unknown});
  CHECK(residual(pair_of(obs_no_unit, no_unit)) ==
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
  CHECK(r->meta().quantity() == mov::core::QuantityId{Quantity::difference});
}

TEST_CASE("residual does not modify its inputs",
          "[core][series_ops][residual]") {
  ObsVsPred pair = pair_of(obs_meta(), pred_meta());
  const ObsVsPred copy = pair;
  const auto r = residual(pair);
  CHECK(r.has_value());
  CHECK(pair == copy);
}

// ---- slice ----

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
  std::mt19937 rng = mov::test::fixed_rng();
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

TEST_CASE("index_window gives the half-open index range of a time range",
          "[core][series_ops][slice]") {
  const TimeSeries s =
      make_series(axis_of({10, 20, 30, 40}), std::vector<Sample>(4, val(1.0)));
  CHECK(index_window(s.times(), range(20, 40)) == Window{.lo = 1, .hi = 3});
  CHECK(index_window(s.times(), range(0, 100)) == Window{.lo = 0, .hi = 4});
  CHECK(index_window(s.times(), range(21, 30)) == Window{.lo = 2, .hi = 2});
  CHECK(index_window(s.times(), range(41, 50)) == Window{.lo = 4, .hi = 4});
  CHECK(index_window(s.times(), range(0, 10)) == Window{.lo = 0, .hi = 0});
  CHECK(index_window({}, range(0, 10)) == Window{.lo = 0, .hi = 0});
}

TEST_CASE("slice is index_window and a copy; summarize needs no copy",
          "[core][series_ops][slice]") {
  std::mt19937 rng = mov::test::fixed_rng();
  for (int trial = 0; trial < 100; ++trial) {
    const auto rows = random_rows(rng, static_cast<std::size_t>(trial) % 60);
    const TimeSeries s = series_of_rows(rows, level_meta());
    const TimeRange r = range(10 + (trial % 7), 80 + (trial % 11));
    const Window w = index_window(s.times(), r);
    const TimeSeries cut = slice(s, r);
    CHECK(cut.size() == w.hi - w.lo);
    CHECK(summarize(cut) == summarize(s.times().subspan(w.lo, w.hi - w.lo),
                                      s.samples().subspan(w.lo, w.hi - w.lo)));
  }
}

// ---- shift_time ----

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

TEST_CASE("shift_time is limited only by 64-bit overflow",
          "[core][series_ops][shift_time]") {
  using std::chrono::milliseconds;
  constexpr std::int64_t top = std::numeric_limits<std::int64_t>::max();
  constexpr std::int64_t bottom = std::numeric_limits<std::int64_t>::min();
  const TimeSeries high =
      make_series(axis_of({0, top - 1, top}), {val(1.0), val(2.0), val(3.0)});
  CHECK(shift_time(high, milliseconds{0}) == high);
  CHECK(shift_time(high, milliseconds{1}) ==
        std::unexpected{TimeOverflow{.index = 2}});
  CHECK(shift_time(high, milliseconds{top}) ==
        std::unexpected{TimeOverflow{.index = 1}});
  CHECK(shift_time(high, milliseconds{-1}).has_value());

  const TimeSeries low = make_series(axis_of({bottom, bottom + 5, 0}),
                                     {val(1.0), val(2.0), val(3.0)});
  CHECK(shift_time(low, milliseconds{-1}) ==
        std::unexpected{TimeOverflow{.index = 0}});
  CHECK(shift_time(low, milliseconds{1}).has_value());
  CHECK(shift_time(low, milliseconds{5}).has_value());
  const TimeSeries near_bottom =
      make_series(axis_of({-1, 0}), {val(1.0), val(2.0)});
  CHECK(shift_time(near_bottom, milliseconds::min()) ==
        std::unexpected{TimeOverflow{.index = 0}});
  const TimeSeries from_zero =
      make_series(axis_of({0, 1}), {val(1.0), val(2.0)});
  CHECK(shift_time(from_zero, milliseconds::max()) ==
        std::unexpected{TimeOverflow{.index = 1}});
  CHECK(shift_time(from_zero, milliseconds::min()).has_value());
}

TEST_CASE("shift_time(s, 0) is s for any legal series",
          "[core][series_ops][shift_time]") {
  using std::chrono::milliseconds;
  // Beyond the +-2^53 ms file bound, which StationTable enforces, not this.
  const std::int64_t beyond = mov::core::max_abs_time_ms + 10;
  const TimeSeries s = make_series(axis_of({-beyond, 0, beyond}),
                                   {val(1.0), val(2.0), val(3.0)});
  CHECK(shift_time(s, milliseconds{0}) == s);
  const auto moved = shift_time(s, milliseconds{20});
  REQUIRE(moved.has_value());
  CHECK(times_of(*moved) ==
        std::vector<Time>{at_ms(-beyond + 20), at_ms(20), at_ms(beyond + 20)});
}

// ---- scale_offset ----

TEST_CASE("a Calibration has finite coefficients",
          "[core][series_ops][scale_offset]") {
  const double nan = std::numeric_limits<double>::quiet_NaN();
  const double inf = std::numeric_limits<double>::infinity();
  CHECK(not Calibration::make({.scale = nan, .offset = 0.0}));
  CHECK(not Calibration::make({.scale = 1.0, .offset = nan}));
  CHECK(not Calibration::make({.scale = inf, .offset = 0.0}));
  CHECK(not Calibration::make({.scale = 1.0, .offset = -inf}));
  const Calibration c = calibration(0.0, 3.0);  // zero is allowed
  CHECK(c.scale() == 0.0);
  CHECK(c.offset() == 3.0);
  CHECK(Calibration{} == Calibration::make({.scale = 1.0, .offset = 0.0}));
  CHECK(Calibration{}.scale() == 1.0);
  CHECK(Calibration{}.offset() == 0.0);
}

TEST_CASE("scale_offset is y = scale * x + offset on values only",
          "[core][series_ops][scale_offset]") {
  const TimeSeries s = make_series(
      axis_of({0, 1, 2, 3}),
      {val(1.0), Sample{Dry{}}, Sample{Missing{}}, val(-0.5)}, level_meta());
  const TimeSeries r = scale_offset(s, calibration(2.0, 1.0));
  CHECK(samples_of(r) == std::vector<Sample>{val(3.0), Sample{Dry{}},
                                             Sample{Missing{}}, val(0.0)});
  CHECK(times_of(r) == times_of(s));
  CHECK(r.meta() == s.meta());  // a calibration: unit and datum stay
}

TEST_CASE("scale_offset turns an overflowing result into Missing",
          "[core][series_ops][scale_offset]") {
  const TimeSeries s = make_series(axis_of({0, 1}), {val(1e300), val(1.0)});
  CHECK(samples_of(scale_offset(s, calibration(1e300, 0.0))) ==
        std::vector<Sample>{Sample{Missing{}}, val(1e300)});
}

TEST_CASE("scale_offset by the identity changes nothing, not even -0.0",
          "[core][series_ops][scale_offset]") {
  const TimeSeries s = make_series(axis_of({0}), {val(-0.0)});
  const TimeSeries r = scale_offset(s, Calibration{});
  CHECK(std::signbit(r.samples().front().value().value_or(1.0)));
  CHECK(r == s);
}

// ---- convert ----

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
  const Unit percent = unit_of("percent");
  const TimeSeries humidity = make_series(
      axis_of({0}), {val(55.0)},
      level_meta(percent, std::nullopt, "rh", Quantity::relative_humidity));
  CHECK(convert(humidity, percent) == humidity);
}

TEST_CASE("convert reports why it cannot", "[core][series_ops][convert]") {
  const TimeSeries unitless =
      make_series(axis_of({0}), {val(1.0)}, level_meta(std::nullopt));
  CHECK(convert(unitless, metre) == std::unexpected{UnitError{UnknownUnit{}}});

  const TimeSeries length = make_series(axis_of({0}), {val(1.0)}, level_meta());
  const Unit knots = SpeedUnit::knot;
  CHECK(convert(length, knots) == std::unexpected{UnitError{IncompatibleUnits{
                                      .from = metre, .to = knots}}});
  const Unit percent = unit_of("percent");
  CHECK(convert(length, percent) == std::unexpected{UnitError{IncompatibleUnits{
                                        .from = metre, .to = percent}}});
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
  return SeriesMeta::make(
      {.quantity = Quantity::wind_speed, .unit = std::move(unit)});
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
