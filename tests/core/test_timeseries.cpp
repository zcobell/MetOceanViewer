// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "test_helpers.hpp"

using mov::core::ConstructionErrc;
using mov::core::ConstructionError;
using mov::core::Dry;
using mov::core::LengthUnit;
using mov::core::MetaError;
using mov::core::Missing;
using mov::core::normalize;
using mov::core::Normalized;
using mov::core::NormalizeReport;
using mov::core::Point;
using mov::core::Quantity;
using mov::core::Sample;
using mov::core::SeriesMeta;
using mov::core::Time;
using mov::core::TimeSeries;
using mov::core::Unit;
using mov::core::VerticalDatum;
using mov::test::at_ms;

namespace {

Sample val(double v) { return Sample::of(v).value_or(Sample{}); }

std::vector<Time> times_of(std::initializer_list<std::int64_t> ms) {
  std::vector<Time> out;
  for (const std::int64_t m : ms) {
    out.push_back(at_ms(m));
  }
  return out;
}

SeriesMeta water_level() {
  return SeriesMeta::make({.quantity = Quantity::water_level,
                           .label = "level",
                           .unit = LengthUnit::meter})
      .value_or(SeriesMeta{});
}

TimeSeries series(std::initializer_list<std::int64_t> ms,
                  std::vector<Sample> samples) {
  auto made = TimeSeries::make(times_of(ms), std::move(samples), water_level());
  REQUIRE(made.has_value());
  return made.value_or(TimeSeries{});
}

std::vector<Point> rows(
    std::initializer_list<std::pair<std::int64_t, Sample>> r) {
  std::vector<Point> out;
  for (const auto& [ms, sample] : r) {
    out.push_back({.time = at_ms(ms), .sample = sample});
  }
  return out;
}

std::vector<Time> times_vector(const TimeSeries& s) {
  return {s.times().begin(), s.times().end()};
}

std::vector<Sample> samples_vector(const TimeSeries& s) {
  return {s.samples().begin(), s.samples().end()};
}

}  // namespace

TEST_CASE("a default TimeSeries is empty", "[core][timeseries]") {
  const TimeSeries s;
  CHECK(s.empty());
  CHECK(s.times().empty());
  CHECK(s.samples().empty());
  CHECK(s.meta() == SeriesMeta{});
}

TEST_CASE("TimeSeries::make keeps times, samples and metadata",
          "[core][timeseries]") {
  const auto s =
      TimeSeries::make(times_of({0, 10, 20}),
                       {val(1.0), Sample{Dry{}}, Sample{}}, water_level());
  REQUIRE(s.has_value());
  CHECK(s->size() == 3);
  CHECK(not s->empty());
  CHECK(times_vector(*s) == times_of({0, 10, 20}));
  CHECK(samples_vector(*s) ==
        std::vector<Sample>{val(1.0), Sample{Dry{}}, Sample{Missing{}}});
  CHECK(s->meta() == water_level());

  // An empty series is valid.
  CHECK(TimeSeries::make({}, {}, water_level()).has_value());
}

TEST_CASE("TimeSeries::make reports a length mismatch at the shorter length",
          "[core][timeseries]") {
  CHECK(TimeSeries::make(times_of({0, 1, 2}), {val(1.0)}, SeriesMeta{}) ==
        std::unexpected{ConstructionError{
            .code = ConstructionErrc::length_mismatch, .index = 1}});
  CHECK(TimeSeries::make(times_of({0}), {val(1.0), val(2.0)}, SeriesMeta{}) ==
        std::unexpected{ConstructionError{
            .code = ConstructionErrc::length_mismatch, .index = 1}});
  CHECK(TimeSeries::make({}, {val(1.0)}, SeriesMeta{}) ==
        std::unexpected{ConstructionError{
            .code = ConstructionErrc::length_mismatch, .index = 0}});
}

TEST_CASE("TimeSeries::make rejects disorder at the first offending index",
          "[core][timeseries]") {
  const std::vector<Sample> four(4, Sample{});
  // Equal times are not strictly increasing (C2).
  CHECK(TimeSeries::make(times_of({0, 1, 1, 2}), four, SeriesMeta{}) ==
        std::unexpected{ConstructionError{
            .code = ConstructionErrc::time_not_increasing, .index = 2}});
  CHECK(TimeSeries::make(times_of({5, 1, 2, 3}), four, SeriesMeta{}) ==
        std::unexpected{ConstructionError{
            .code = ConstructionErrc::time_not_increasing, .index = 1}});
  CHECK(TimeSeries::make(times_of({0, 1, 2, -7}), four, SeriesMeta{}) ==
        std::unexpected{ConstructionError{
            .code = ConstructionErrc::time_not_increasing, .index = 3}});
  // The length is checked first.
  CHECK(TimeSeries::make(times_of({1, 0}), {Sample{}}, SeriesMeta{})
            .error_or(ConstructionError{})
            .code == ConstructionErrc::length_mismatch);
}

TEST_CASE("points() zips times and samples", "[core][timeseries]") {
  const TimeSeries s = series({1, 2}, {val(3.0), Sample{Dry{}}});
  std::vector<Point> seen;
  for (const auto& [time, sample] : s.points()) {
    seen.push_back({.time = time, .sample = sample});
  }
  CHECK(seen == rows({{1, val(3.0)}, {2, Sample{Dry{}}}}));
}

TEST_CASE("with_label changes only the label, from lvalues and rvalues",
          "[core][timeseries]") {
  const TimeSeries s = series({1, 2}, {val(3.0), val(4.0)});
  const TimeSeries copy = s.with_label("renamed");
  CHECK(copy.meta().label() == "renamed");
  CHECK(copy.meta() == s.meta().with_label("renamed"));
  CHECK(times_vector(copy) == times_vector(s));
  CHECK(samples_vector(copy) == samples_vector(s));
  CHECK(s.meta().label() == "level");

  TimeSeries source = s;
  const TimeSeries moved = std::move(source).with_label("moved");
  CHECK(moved.meta().label() == "moved");
  CHECK(samples_vector(moved) == samples_vector(s));
}

TEST_CASE("assume_unit and assume_datum forward the meta rules",
          "[core][timeseries]") {
  const TimeSeries s = series({1}, {val(3.0)});
  CHECK(s.assume_unit(LengthUnit::foot)
            .error_or(MetaError::datum_not_applicable) ==
        MetaError::already_set);
  const auto with_datum = s.assume_datum(VerticalDatum::navd88);
  REQUIRE(with_datum.has_value());
  CHECK(with_datum->meta().datum() == VerticalDatum::navd88);
  CHECK(samples_vector(*with_datum) == samples_vector(s));

  const auto unknown =
      TimeSeries::make(times_of({1}), {val(1.0)}, SeriesMeta{});
  REQUIRE(unknown.has_value());
  const auto feet =
      unknown.value_or(TimeSeries{}).assume_unit(LengthUnit::foot);
  REQUIRE(feet.has_value());
  CHECK(feet->meta().unit() == std::optional<Unit>{LengthUnit::foot});

  const auto wind = TimeSeries::make(
      times_of({1}), {val(1.0)},
      SeriesMeta::make({.quantity = Quantity::wind_u}).value_or(SeriesMeta{}));
  REQUIRE(wind.has_value());
  CHECK(wind.value_or(TimeSeries{})
            .assume_datum(VerticalDatum::msl)
            .error_or(MetaError::already_set) ==
        MetaError::datum_not_applicable);

  // The rvalue overloads give the same results.
  TimeSeries source = s;
  const auto datum_from_rvalue =
      std::move(source).assume_datum(VerticalDatum::navd88);
  CHECK(datum_from_rvalue == with_datum);
  TimeSeries unknown_source = unknown.value_or(TimeSeries{});
  const auto unit_from_rvalue =
      std::move(unknown_source).assume_unit(LengthUnit::foot);
  CHECK(unit_from_rvalue == feet);
}

TEST_CASE("transform_samples keeps times and metadata", "[core][timeseries]") {
  const TimeSeries s = series({1, 2, 3}, {val(1.0), Sample{Dry{}}, Sample{}});
  const auto doubled = [](Sample x) {
    const std::optional<double> v = x.value();
    return v ? val(*v * 2.0) : x;
  };
  const TimeSeries t = s.transform_samples(doubled);
  CHECK(times_vector(t) == times_vector(s));
  CHECK(t.meta() == s.meta());
  CHECK(samples_vector(t) ==
        std::vector<Sample>{val(2.0), Sample{Dry{}}, Sample{}});

  TimeSeries source = s;
  const TimeSeries from_rvalue = std::move(source).transform_samples(doubled);
  CHECK(from_rvalue == t);
}

TEST_CASE("TimeSeries equality compares times, samples and meta",
          "[core][timeseries]") {
  const TimeSeries s = series({1, 2}, {val(1.0), val(2.0)});
  CHECK(s == series({1, 2}, {val(1.0), val(2.0)}));
  CHECK(s != series({1, 3}, {val(1.0), val(2.0)}));
  CHECK(s != series({1, 2}, {val(1.0), Sample{Dry{}}}));
  CHECK(s != s.with_label("other"));
}

TEST_CASE("normalize leaves a clean input untouched", "[core][timeseries]") {
  const auto input =
      rows({{0, val(1.0)}, {5, Sample{Dry{}}}, {9, Sample{}}, {12, val(-2.5)}});
  const Normalized n = normalize(input, water_level());
  CHECK(n.report == NormalizeReport{});
  CHECK(n.report.clean());
  CHECK(n.series ==
        series({0, 5, 9, 12}, {val(1.0), Sample{Dry{}}, Sample{}, val(-2.5)}));

  const Normalized empty = normalize({}, water_level());
  CHECK(empty.report.clean());
  CHECK(empty.series.empty());
  CHECK(empty.series.meta() == water_level());
}

TEST_CASE("normalize sorts and counts descents", "[core][timeseries]") {
  // Descents: 3 -> 1 and 4 -> 2.
  const Normalized n = normalize(
      rows({{3, val(3.0)}, {1, val(1.0)}, {4, val(4.0)}, {2, val(2.0)}}),
      SeriesMeta{});
  CHECK(n.report == NormalizeReport{.descents = 2,
                                    .duplicates_dropped = 0,
                                    .conflicting_duplicates = 0});
  CHECK(not n.report.clean());
  CHECK(times_vector(n.series) == times_of({1, 2, 3, 4}));
  CHECK(samples_vector(n.series) ==
        std::vector<Sample>{val(1.0), val(2.0), val(3.0), val(4.0)});
}

TEST_CASE("normalize keeps the first of equal times and counts conflicts",
          "[core][timeseries]") {
  // No descent, two repeats of t=1: one equal, one different.
  const Normalized n = normalize(
      rows({{0, val(0.0)}, {1, val(1.0)}, {1, val(1.0)}, {1, Sample{}}}),
      SeriesMeta{});
  CHECK(n.report == NormalizeReport{.descents = 0,
                                    .duplicates_dropped = 2,
                                    .conflicting_duplicates = 1});
  CHECK(not n.report.clean());
  CHECK(times_vector(n.series) == times_of({0, 1}));
  CHECK(samples_vector(n.series) == std::vector<Sample>{val(0.0), val(1.0)});
}

TEST_CASE("normalize keeps the first in input order among equal times",
          "[core][timeseries]") {
  // After the (stable) sort the two t=1 rows keep their input order, so the
  // Dry one, which came first, survives.
  const Normalized n = normalize(rows({{2, val(2.0)},
                                       {1, Sample{Dry{}}},
                                       {0, val(0.0)},
                                       {1, val(9.0)},
                                       {2, val(2.0)}}),
                                 SeriesMeta{});
  CHECK(n.report == NormalizeReport{.descents = 2,
                                    .duplicates_dropped = 2,
                                    .conflicting_duplicates = 1});
  CHECK(times_vector(n.series) == times_of({0, 1, 2}));
  CHECK(samples_vector(n.series) ==
        std::vector<Sample>{val(0.0), Sample{Dry{}}, val(2.0)});
}

TEST_CASE("normalize is idempotent", "[core][timeseries]") {
  const Normalized once = normalize(
      rows({{4, val(1.0)}, {4, val(2.0)}, {-3, Sample{}}, {0, val(5.0)}}),
      water_level());
  std::vector<Point> again;
  for (const auto& [time, sample] : once.series.points()) {
    again.push_back({.time = time, .sample = sample});
  }
  const Normalized twice = normalize(std::move(again), water_level());
  CHECK(twice.report.clean());
  CHECK(twice.series == once.series);
}
