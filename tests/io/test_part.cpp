// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Parts and their merge (docs/providers-design.md §5.4): the empty part as
// identity, the left-biased union, unit conversion, UnitMismatch and the
// metas-agree law, the drop tally, associativity over generated parts.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "mov/io/part.hpp"

namespace {

namespace core = mov::core;
using mov::io::ColumnKey;
using mov::io::combine;
using mov::io::DropTally;
using mov::io::DuplicateColumn;
using mov::io::fold;
using mov::io::MergeError;
using mov::io::MetaMismatch;
using mov::io::Part;
using mov::io::UnitMismatch;
using mov::io::WarningCode;

core::Time at(std::int64_t s) { return core::Time{std::chrono::seconds{s}}; }

const core::Unit meter{core::LengthUnit::meter};
const core::Unit foot{core::LengthUnit::foot};
const core::Unit knot{core::SpeedUnit::knot};

core::SeriesMeta meta(std::optional<core::Unit> unit, std::string label = "wl",
                      core::Quantity q = core::Quantity::water_level) {
  return core::SeriesMeta::make(
      {.quantity = q, .label = std::move(label), .unit = std::move(unit)});
}

struct Row {
  std::int64_t s;
  core::Sample sample;
};

core::TimeSeries series(core::SeriesMeta m, const std::vector<Row>& rows) {
  core::TimeAxis times;
  std::vector<core::Sample> samples;
  for (const Row& r : rows) {
    times.push_back(at(r.s));
    samples.push_back(r.sample);
  }
  return *core::TimeSeries::make(std::move(times), std::move(samples),
                                 std::move(m));
}

core::Sample v(double x) { return core::finite_or_missing(x); }

Part part_of(core::TimeSeries s) {
  Part p;
  const auto inserted = p.insert(std::move(s));
  REQUIRE(inserted);
  return p;
}

const ColumnKey wl_key{.quantity = core::Quantity::water_level};

std::vector<std::int64_t> seconds_of(const Part& p) {
  std::vector<std::int64_t> out;
  for (const core::Time t : p.columns().at(wl_key).times()) {
    out.push_back(
        std::chrono::duration_cast<std::chrono::seconds>(t.time_since_epoch())
            .count());
  }
  return out;
}

}  // namespace

TEST_CASE("a part is keyed by each series' own quantity", "[io][part]") {
  Part p;
  REQUIRE(p.insert(series(meta(meter), {{.s = 0, .sample = v(1)}})));
  REQUIRE(p.insert(
      series(meta(meter, "predicted", core::Quantity::water_level_prediction),
             {{.s = 0, .sample = v(2)}})));
  CHECK(p.columns().size() == 2);
  CHECK(p.insert(series(meta(meter), {})).error() ==
        DuplicateColumn{.column = wl_key});
  CHECK(mov::io::format(wl_key) == "water_level");
  CHECK(
      ColumnKey::of(series(meta(knot, "w", core::Quantity::wind_speed), {})) ==
      ColumnKey{.quantity = core::Quantity::wind_speed});
}

TEST_CASE("the empty part is the identity of combine", "[io][part]") {
  const Part p = part_of(series(
      meta(meter), {{.s = 0, .sample = v(1)}, {.s = 60, .sample = v(2)}}));
  const auto left = combine(Part{}, p);
  const auto right = combine(p, Part{});
  REQUIRE(left);
  REQUIRE(right);
  CHECK(left->part == p);
  CHECK(right->part == p);
  CHECK(left->dropped == DropTally{});
  CHECK(right->dropped == DropTally{});
}

TEST_CASE("combine is the union by time, the first part winning ties",
          "[io][part]") {
  const Part a = part_of(series(
      meta(meter), {{.s = 0, .sample = v(1)}, {.s = 120, .sample = v(3)}}));
  const Part b = part_of(series(meta(meter), {{.s = 60, .sample = v(20)},
                                              {.s = 120, .sample = v(30)},
                                              {.s = 180, .sample = v(40)}}));
  const auto merged = combine(a, b);
  REQUIRE(merged);
  CHECK(seconds_of(merged->part) == std::vector<std::int64_t>{0, 60, 120, 180});
  CHECK(merged->part.columns().at(wl_key).samples()[2] == v(3));
  CHECK(merged->dropped == DropTally{.dropped = {{wl_key, 1}}});
}

TEST_CASE("disjoint windows in order append with no dropped rows",
          "[io][part][regression][L1]") {
  const Part a = part_of(series(
      meta(meter), {{.s = 0, .sample = v(1)}, {.s = 60, .sample = v(2)}}));
  const Part b = part_of(series(
      meta(meter), {{.s = 120, .sample = v(3)}, {.s = 180, .sample = v(4)}}));
  const auto merged = combine(a, b);
  REQUIRE(merged);
  CHECK(seconds_of(merged->part).size() == 4);
  CHECK(merged->dropped == DropTally{});
  // Out of order: still the union.
  const auto reversed = combine(b, a);
  REQUIRE(reversed);
  CHECK(seconds_of(reversed->part) ==
        std::vector<std::int64_t>{0, 60, 120, 180});
}

TEST_CASE("a later part's unit is converted to the first's", "[io][part]") {
  const Part a = part_of(series(meta(meter), {{.s = 0, .sample = v(1)}}));
  const Part b =
      part_of(series(meta(foot), {{.s = 60, .sample = v(10)},
                                  {.s = 120, .sample = core::Missing{}},
                                  {.s = 180, .sample = core::Dry{}}}));
  const auto merged = combine(a, b);
  REQUIRE(merged);
  const auto& s = merged->part.columns().at(wl_key);
  REQUIRE(s.size() == 4);
  CHECK(s.samples()[1].value() == 10 * 0.3048);
  CHECK(s.samples()[2].is_missing());
  CHECK(s.samples()[3].is_dry());
  CHECK(s.meta() == meta(meter));
}

TEST_CASE("units that do not convert are UnitMismatch", "[io][part]") {
  const Part a = part_of(series(meta(meter), {{.s = 0, .sample = v(1)}}));
  const Part b = part_of(series(meta(knot), {{.s = 60, .sample = v(1)}}));
  CHECK(combine(a, b).error() ==
        MergeError{
            UnitMismatch{.column = wl_key, .first = meter, .other = knot}});
  const Part none =
      part_of(series(meta(std::nullopt), {{.s = 60, .sample = v(1)}}));
  CHECK(combine(a, none).error() ==
        MergeError{UnitMismatch{
            .column = wl_key, .first = meter, .other = std::nullopt}});
  CHECK(combine(none, none).has_value());
}

TEST_CASE("metadata that disagrees is MetaMismatch, checked before units",
          "[io][part]") {
  const Part a = part_of(series(meta(meter), {{.s = 0, .sample = v(1)}}));
  const Part label =
      part_of(series(meta(knot, "other"), {{.s = 60, .sample = v(1)}}));
  const Part datum =
      part_of(series(*meta(meter).assume_datum(core::VerticalDatum::mllw),
                     {{.s = 60, .sample = v(1)}}));
  CHECK(combine(a, label).error() ==
        MergeError{MetaMismatch{.column = wl_key}});
  CHECK(combine(a, datum).error() ==
        MergeError{MetaMismatch{.column = wl_key}});
}

TEST_CASE("fold is combine from the left, one warning per column",
          "[io][part]") {
  CHECK(fold({})->value == Part{});
  const Part a = part_of(series(meta(meter), {{.s = 0, .sample = v(1)}}));
  const Part b = part_of(series(
      meta(meter), {{.s = 0, .sample = v(2)}, {.s = 60, .sample = v(3)}}));
  const Part c = part_of(series(meta(foot), {{.s = 60, .sample = v(9)}}));
  const auto folded = fold({a, b, c});
  REQUIRE(folded);
  CHECK(seconds_of(folded->value) == std::vector<std::int64_t>{0, 60});
  REQUIRE(folded->warnings.size() == 1);
  CHECK(folded->warnings[0] ==
        mov::io::Warning{.code = WarningCode::duplicate_times_dropped,
                         .subject = "water_level",
                         .count = 2});
  const Part bad = part_of(series(meta(knot), {{.s = 120, .sample = v(1)}}));
  CHECK_FALSE(fold({a, bad}).has_value());
}

TEST_CASE("clip keeps the rows in the range", "[io][part]") {
  const Part p = part_of(series(meta(meter), {{.s = 0, .sample = v(1)},
                                              {.s = 60, .sample = v(2)},
                                              {.s = 120, .sample = v(3)}}));
  const Part clipped =
      mov::io::clip(p, *core::TimeRange::make(at(30), at(120)));
  CHECK(seconds_of(clipped) == std::vector<std::int64_t>{60});
}

TEST_CASE("the drop tally is a monoid", "[io][part]") {
  const DropTally x{.dropped = {{wl_key, 2}}};
  const DropTally y{
      .dropped = {{wl_key, 1},
                  {ColumnKey{.quantity = core::Quantity::wind_speed}, 4}}};
  CHECK(x + DropTally{} == x);
  CHECK(DropTally{} + x == x);
  CHECK((x + y) + x == x + (y + x));
  CHECK((x + y).dropped.at(wl_key) == 3);
}

namespace {

/// A generator with a fixed seed: a failure must reproduce.
std::mt19937_64 fixed_rng(std::uint32_t stream) {
  std::seed_seq seed{2026U, 10U, 15U, stream};
  return std::mt19937_64{seed};
}

/// Up to three columns of up to 12 rows on a coarse grid, so times collide
/// across parts. Metadata drawn from a small pool.
Part random_part(std::mt19937_64& rng, bool uniform_meta) {
  std::uniform_int_distribution<int> count{0, 12};
  std::uniform_int_distribution<int> slot{0, 30};
  std::uniform_int_distribution<int> value{-100, 100};
  std::uniform_int_distribution<int> pick{0, 3};
  const std::array<core::Quantity, 3> quantities{
      core::Quantity::water_level, core::Quantity::water_level_prediction,
      core::Quantity::air_pressure};
  Part p;
  for (const core::Quantity q : quantities) {
    if (pick(rng) == 0) {
      continue;
    }
    std::vector<std::int64_t> times;
    for (int i = count(rng); i > 0; --i) {
      times.push_back(std::int64_t{slot(rng)} * 60);
    }
    std::ranges::sort(times);
    const auto [first, last] = std::ranges::unique(times);
    times.erase(first, last);
    std::vector<Row> rows;
    rows.reserve(times.size());
    for (const std::int64_t t : times) {
      rows.push_back({.s = t, .sample = v(value(rng))});
    }
    const int m = uniform_meta ? 0 : pick(rng);
    core::SeriesMeta md = m == 1   ? meta(foot, "x", q)
                          : m == 2 ? meta(meter, "other", q)
                          : m == 3 ? meta(knot, "x", q)
                                   : meta(meter, "x", q);
    const auto inserted = p.insert(series(std::move(md), rows));
    REQUIRE(inserted);
  }
  return p;
}

}  // namespace

TEST_CASE("combine is associative over generated parts, tallies included",
          "[io][part][property]") {
  std::mt19937_64 rng = fixed_rng(1);
  for (int i = 0; i < 500; ++i) {
    const Part a = random_part(rng, true);
    const Part b = random_part(rng, true);
    const Part c = random_part(rng, true);
    const auto ab = combine(a, b);
    const auto bc = combine(b, c);
    REQUIRE(ab);
    REQUIRE(bc);
    const auto left = combine(ab->part, c);
    const auto right = combine(a, bc->part);
    REQUIRE(left);
    REQUIRE(right);
    CHECK(left->part == right->part);
    // The drop tally is exact on either bracketing.
    CHECK(ab->dropped + left->dropped == bc->dropped + right->dropped);
    // fold gives the left bracketing.
    const auto folded = fold({a, b, c});
    REQUIRE(folded);
    CHECK(folded->value == left->part);
  }
}

TEST_CASE("metas agree after any successful combine", "[io][part][property]") {
  std::mt19937_64 rng = fixed_rng(2);
  int merged = 0;
  int refused = 0;
  for (int i = 0; i < 500; ++i) {
    const Part a = random_part(rng, false);
    const Part b = random_part(rng, false);
    const auto ab = combine(a, b);
    bool compatible = true;
    for (const auto& [key, col] : b.columns()) {
      const auto found = a.columns().find(key);
      if (found == a.columns().end()) {
        continue;
      }
      const auto& x = found->second.meta();
      const auto& y = col.meta();
      const bool metas = x.quantity() == y.quantity() and
                         x.label() == y.label() and x.datum() == y.datum();
      const bool units =
          x.unit().has_value() and y.unit().has_value() and
          core::conversion(x.unit().value_or(meter), y.unit().value_or(meter))
              .has_value();
      compatible = compatible and metas and units;
    }
    CHECK(ab.has_value() == compatible);
    if (not ab) {
      ++refused;
      continue;
    }
    ++merged;
    for (const auto& [key, col] : ab->part.columns()) {
      const auto& first =
          a.columns().contains(key) ? a.columns().at(key) : b.columns().at(key);
      CHECK(col.meta() == first.meta());
      CHECK(ColumnKey::of(col) == key);
    }
  }
  CHECK(merged > 0);
  CHECK(refused > 0);
}
