// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Parts and their merge (docs/providers-design.md §5.4): the empty part as
// identity, the left-biased union, unit conversion, UnitMismatch and the
// metas-agree law, associativity over generated parts.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
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
#include "mov/fetch/part.hpp"

namespace {

namespace core = mov::core;
using mov::fetch::Column;
using mov::fetch::combine;
using mov::fetch::fold;
using mov::fetch::MergeError;
using mov::fetch::MetaMismatch;
using mov::fetch::Part;
using mov::fetch::UnitMismatch;
using mov::io::WarningCode;

core::Time at(std::int64_t s) { return core::Time{std::chrono::seconds{s}}; }

core::Point pt(std::int64_t s, double v) {
  return {.time = at(s), .sample = core::finite_or_missing(v)};
}

core::SeriesMeta meta(core::Unit unit, std::string label = "wl",
                      core::Quantity q = core::Quantity::water_level) {
  return core::SeriesMeta::make(
      {.quantity = q, .label = std::move(label), .unit = std::move(unit)});
}

const core::Unit meter{core::LengthUnit::meter};
const core::Unit foot{core::LengthUnit::foot};
const core::Unit knot{core::SpeedUnit::knot};

Column column(core::SeriesMeta m, std::vector<core::Point> rows) {
  return *Column::make(std::move(m), std::move(rows));
}

Part part(std::string key, Column c) {
  Part p;
  p.columns.emplace(std::move(key), std::move(c));
  return p;
}

std::vector<core::Point> rows_of(const Part& p, const std::string& key) {
  const auto rows = p.columns.at(key).rows();
  return {rows.begin(), rows.end()};
}

}  // namespace

TEST_CASE("a column's rows strictly increase", "[fetch][part]") {
  CHECK(Column::make(meta(meter), {pt(0, 1), pt(1, 2)}).has_value());
  CHECK(Column::make(meta(meter), {}).has_value());
  CHECK(Column::make(meta(meter), {pt(0, 1), pt(0, 2)}).error() ==
        core::TimeNotIncreasing{.index = 1});
  CHECK(Column::make(meta(meter), {pt(0, 1), pt(5, 2), pt(4, 3)}).error() ==
        core::TimeNotIncreasing{.index = 2});
  auto c = column(meta(meter), {pt(0, 1)});
  const std::vector<core::Point> rows = std::move(c).into_rows();
  CHECK(rows == std::vector<core::Point>{pt(0, 1)});
}

TEST_CASE("the empty part is the identity of combine", "[fetch][part]") {
  const Part p = part("wl", column(meta(meter), {pt(0, 1), pt(60, 2)}));
  const auto left = combine(Part{}, p);
  const auto right = combine(p, Part{});
  REQUIRE(left);
  REQUIRE(right);
  CHECK(left->value == p);
  CHECK(right->value == p);
  CHECK(left->warnings.empty());
  CHECK(right->warnings.empty());
}

TEST_CASE("combine is the union by time, the first part winning ties",
          "[fetch][part]") {
  const Part a = part("wl", column(meta(meter), {pt(0, 1), pt(120, 3)}));
  const Part b =
      part("wl", column(meta(meter), {pt(60, 20), pt(120, 30), pt(180, 40)}));
  const auto merged = combine(a, b);
  REQUIRE(merged);
  CHECK(
      rows_of(merged->value, "wl") ==
      std::vector<core::Point>{pt(0, 1), pt(60, 20), pt(120, 3), pt(180, 40)});
  REQUIRE(merged->warnings.size() == 1);
  CHECK(merged->warnings[0] ==
        mov::io::Warning{.code = WarningCode::duplicate_times_dropped,
                         .subject = "wl",
                         .count = 1});
}

TEST_CASE("disjoint windows concatenate with no dropped rows",
          "[fetch][part][regression][L1]") {
  const Part a = part("wl", column(meta(meter), {pt(0, 1), pt(60, 2)}));
  const Part b = part("wl", column(meta(meter), {pt(120, 3), pt(180, 4)}));
  const auto merged = combine(a, b);
  REQUIRE(merged);
  CHECK(rows_of(merged->value, "wl").size() == 4);
  CHECK(merged->warnings.empty());
}

TEST_CASE("columns of one part only are kept as they are", "[fetch][part]") {
  const Part a = part("wl", column(meta(meter), {pt(0, 1)}));
  const Part b =
      part("wspd",
           column(meta(knot, "wind", core::Quantity::wind_speed), {pt(0, 5)}));
  const auto merged = combine(a, b);
  REQUIRE(merged);
  CHECK(merged->value.columns.size() == 2);
  CHECK(merged->value.columns.at("wspd") == b.columns.at("wspd"));
}

TEST_CASE("a later part's unit is converted to the first's", "[fetch][part]") {
  const Part a = part("wl", column(meta(meter), {pt(0, 1)}));
  const Part b = part(
      "wl", column(meta(foot), {pt(60, 10),
                                {.time = at(120), .sample = core::Missing{}},
                                {.time = at(180), .sample = core::Dry{}}}));
  const auto merged = combine(a, b);
  REQUIRE(merged);
  const auto rows = rows_of(merged->value, "wl");
  REQUIRE(rows.size() == 4);
  CHECK(rows[1].sample.value() == 10 * 0.3048);
  CHECK(rows[2].sample.is_missing());
  CHECK(rows[3].sample.is_dry());
  CHECK(merged->value.columns.at("wl").meta() == meta(meter));
}

TEST_CASE("units that do not convert are UnitMismatch", "[fetch][part]") {
  const Part a = part("x", column(meta(meter), {pt(0, 1)}));
  const Part b = part("x", column(meta(knot), {pt(60, 1)}));
  CHECK(combine(a, b).error() ==
        MergeError{UnitMismatch{.column = "x", .first = meter, .other = knot}});
  // A unit on one side only does not convert either.
  const Part none = part(
      "x", column(core::SeriesMeta::make(
                      {.quantity = core::Quantity::water_level, .label = "wl"}),
                  {pt(60, 1)}));
  CHECK(combine(a, none).error() ==
        MergeError{UnitMismatch{
            .column = "x", .first = meter, .other = std::nullopt}});
  // Neither side with a unit: nothing to convert.
  CHECK(combine(none, none).has_value());
}

TEST_CASE("metadata that disagrees is MetaMismatch", "[fetch][part]") {
  const Part a = part("x", column(meta(meter), {pt(0, 1)}));
  const Part label = part("x", column(meta(meter, "other"), {pt(60, 1)}));
  const Part quantity = part(
      "x", column(meta(meter, "wl", core::Quantity::water_level_prediction),
                  {pt(60, 1)}));
  const Part datum =
      part("x", column(*meta(meter).assume_datum(core::VerticalDatum::mllw),
                       {pt(60, 1)}));
  for (const Part* other : {&label, &quantity, &datum}) {
    CHECK(combine(a, *other).error() ==
          MergeError{MetaMismatch{.column = "x"}});
  }
}

TEST_CASE("fold is combine from the left, from the empty part",
          "[fetch][part]") {
  CHECK(fold({})->value == Part{});
  const Part a = part("wl", column(meta(meter), {pt(0, 1)}));
  const Part b = part("wl", column(meta(meter), {pt(0, 2), pt(60, 3)}));
  const Part c = part("wl", column(meta(foot), {pt(60, 9)}));
  const auto folded = fold({a, b, c});
  REQUIRE(folded);
  CHECK(rows_of(folded->value, "wl") ==
        std::vector<core::Point>{pt(0, 1), pt(60, 3)});
  CHECK(folded->warnings.size() == 2);
  const Part bad = part("wl", column(meta(knot), {pt(120, 1)}));
  CHECK_FALSE(fold({a, bad}).has_value());
}

namespace {

/// A generator with a fixed seed: a failure must reproduce.
std::mt19937_64 fixed_rng(std::uint32_t stream) {
  std::seed_seq seed{2026U, 10U, 15U, stream};
  return std::mt19937_64{seed};
}

/// Up to three columns of up to 12 rows on a coarse grid, so times
/// collide across parts. Metadata drawn from a small pool.
Part random_part(std::mt19937_64& rng, bool uniform_meta) {
  std::uniform_int_distribution<int> count{0, 12};
  std::uniform_int_distribution<int> slot{0, 30};
  std::uniform_int_distribution<int> value{-100, 100};
  std::uniform_int_distribution<int> pick{0, 3};
  const std::vector<std::string> keys{"a", "b", "c"};
  Part p;
  for (const auto& key : keys) {
    if (pick(rng) == 0) {
      continue;
    }
    std::vector<std::int64_t> times;
    for (int i = count(rng); i > 0; --i) {
      times.push_back(slot(rng) * 60);
    }
    std::ranges::sort(times);
    const auto [first, last] = std::ranges::unique(times);
    times.erase(first, last);
    std::vector<core::Point> rows;
    rows.reserve(times.size());
    for (const std::int64_t t : times) {
      rows.push_back(pt(t, value(rng)));
    }
    const int m = uniform_meta ? 0 : pick(rng);
    core::SeriesMeta md = m == 1   ? meta(foot)
                          : m == 2 ? meta(meter, "other")
                          : m == 3 ? meta(knot)
                                   : meta(meter);
    p.columns.emplace(key, column(std::move(md), std::move(rows)));
  }
  return p;
}

std::size_t dropped(const std::vector<mov::io::Warning>& warnings) {
  std::size_t n = 0;
  for (const auto& w : warnings) {
    n += w.count;
  }
  return n;
}

}  // namespace

TEST_CASE("combine is associative over generated parts of one unit",
          "[fetch][part][property]") {
  std::mt19937_64 rng = fixed_rng(1);
  for (int i = 0; i < 500; ++i) {
    const Part a = random_part(rng, true);
    const Part b = random_part(rng, true);
    const Part c = random_part(rng, true);
    const auto ab = combine(a, b);
    const auto bc = combine(b, c);
    REQUIRE(ab);
    REQUIRE(bc);
    const auto left = combine(ab->value, c);
    const auto right = combine(a, bc->value);
    REQUIRE(left);
    REQUIRE(right);
    CHECK(left->value == right->value);
    // Every row of b and c that lost to an earlier one is counted once.
    CHECK(dropped(ab->warnings) + dropped(left->warnings) ==
          dropped(bc->warnings) + dropped(right->warnings));
  }
}

TEST_CASE("metas agree after any successful combine",
          "[fetch][part][property]") {
  std::mt19937_64 rng = fixed_rng(2);
  int merged = 0;
  int refused = 0;
  for (int i = 0; i < 500; ++i) {
    const Part a = random_part(rng, false);
    const Part b = random_part(rng, false);
    const auto ab = combine(a, b);
    // A shared column merges exactly when quantity, label and datum agree
    // and the units convert.
    bool compatible = true;
    for (const auto& [key, col] : b.columns) {
      const auto found = a.columns.find(key);
      if (found == a.columns.end()) {
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
    for (const auto& [key, col] : ab->value.columns) {
      const auto& first =
          a.columns.contains(key) ? a.columns.at(key) : b.columns.at(key);
      CHECK(col.meta() == first.meta());
    }
  }
  CHECK(merged > 0);
  CHECK(refused > 0);
}
