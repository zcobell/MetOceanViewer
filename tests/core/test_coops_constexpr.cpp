// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/provider/coops.hpp.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

#include "mov/core/datum.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/windows.hpp"
#include "test_helpers.hpp"

namespace coops = mov::core::coops;

using coops::Datum;
using coops::Met;
using coops::Meteorological;
using coops::MetInterval;
using coops::Observed;
using coops::ObservedLevel;
using coops::ObservedVsPredicted;
using coops::PairedGrid;
using coops::PredictedLevel;
using coops::PredictionInterval;
using coops::Product;
using coops::Shape;
using coops::WaterLevel;
using mov::core::VerticalDatum;
using std::chrono::days;

namespace {

constexpr Product observed_level(Observed s) {
  return WaterLevel{.kind = ObservedLevel{.series = s}, .datum = Datum::mllw};
}
constexpr Product predicted_level(PredictionInterval i) {
  return WaterLevel{.kind = PredictedLevel{.interval = i},
                    .datum = Datum::navd88};
}
constexpr Product paired(PairedGrid g) {
  return WaterLevel{.kind = ObservedVsPredicted{.grid = g},
                    .datum = Datum::msl};
}
constexpr Product met(Met m, MetInterval i) {
  return Meteorological{.product = m, .interval = i};
}

constexpr bool datums_round_trip() {
  return std::ranges::all_of(coops::every_datum, [](Datum d) {
    return coops::from_vertical(coops::to_vertical(d)) == d;
  });
}

constexpr bool every_prediction_interval_is_a_year() {
  constexpr std::array intervals{
      PredictionInterval::one,     PredictionInterval::five,
      PredictionInterval::six,     PredictionInterval::ten,
      PredictionInterval::fifteen, PredictionInterval::thirty,
      PredictionInterval::sixty};
  return std::ranges::all_of(intervals, [](PredictionInterval i) {
    return coops::max_window(predicted_level(i)).duration() == days{365};
  });
}

constexpr bool met_windows_follow_interval() {
  return std::ranges::all_of(coops::every_met, [](Met m) {
    return coops::max_window(met(m, MetInterval::six_minute)).duration() ==
               days{31} and
           coops::max_window(met(m, MetInterval::hourly)).duration() ==
               days{365};
  });
}

constexpr bool only_wind_is_wind_shaped() {
  return std::ranges::all_of(coops::every_met, [](Met m) {
    const Shape s = coops::shape(met(m, MetInterval::hourly));
    return m == Met::wind ? s == Shape::wind : s == Shape::series;
  });
}

constexpr mov::core::TimeRange range_ms(std::int64_t b, std::int64_t e) {
  return *mov::core::TimeRange::make(mov::test::at_ms(b), mov::test::at_ms(e));
}

}  // namespace

TEST_CASE("CO-OPS vocabulary types are values", "[core][coops][constexpr]") {
  STATIC_REQUIRE(std::regular<coops::DatumSet>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<Product>);
  STATIC_REQUIRE(std::copyable<WaterLevel>);
  STATIC_REQUIRE(std::equality_comparable<Product>);
  STATIC_REQUIRE(std::copyable<coops::Request>);
  STATIC_REQUIRE(std::equality_comparable<coops::Request>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<coops::Request>);
  // No default request: a station id and a range have none.
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<coops::Request>);
  // A met product has no datum, and a datum is not a product.
  STATIC_REQUIRE_FALSE(std::is_constructible_v<Product, Datum>);
  // No default product, water level or met product: each would name some
  // datum or interval nobody chose.
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<Product>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<WaterLevel>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<Meteorological>);
  STATIC_REQUIRE_FALSE(
      std::is_default_constructible_v<mov::core::Required<Datum>>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<mov::core::Required<Datum>>);
  // The enumerator lists are whole and in order.
  STATIC_REQUIRE(mov::core::DenseEnum<Datum>);
  STATIC_REQUIRE(mov::core::DenseEnum<Observed>);
  STATIC_REQUIRE(mov::core::DenseEnum<Met>);
  STATIC_REQUIRE(std::variant_size_v<Product> == 2);
  STATIC_REQUIRE(std::variant_size_v<coops::LevelKind> == 3);
}

TEST_CASE("CO-OPS datums map onto core datums", "[core][coops][constexpr]") {
  STATIC_REQUIRE(datums_round_trip());
  STATIC_REQUIRE(coops::to_vertical(Datum::navd88) == VerticalDatum::navd88);
  STATIC_REQUIRE(coops::to_vertical(Datum::igld85) == VerticalDatum::igld85);
  STATIC_REQUIRE(coops::to_vertical(Datum::stnd) == VerticalDatum::stnd);
  // NGVD29 is not served by the data API (decision 32).
  STATIC_REQUIRE_FALSE(coops::from_vertical(VerticalDatum::ngvd29).has_value());
  STATIC_REQUIRE(coops::from_vertical(VerticalDatum::mhw) == Datum::mhw);
}

TEST_CASE("CO-OPS datum wire tokens are pinned", "[core][coops][constexpr]") {
  STATIC_REQUIRE(coops::wire_token(Datum::mhhw) == "MHHW");
  STATIC_REQUIRE(coops::wire_token(Datum::mhw) == "MHW");
  STATIC_REQUIRE(coops::wire_token(Datum::mtl) == "MTL");
  STATIC_REQUIRE(coops::wire_token(Datum::msl) == "MSL");
  STATIC_REQUIRE(coops::wire_token(Datum::mlw) == "MLW");
  STATIC_REQUIRE(coops::wire_token(Datum::mllw) == "MLLW");
  STATIC_REQUIRE(coops::wire_token(Datum::navd88) == "NAVD");
  STATIC_REQUIRE(coops::wire_token(Datum::stnd) == "STND");
  STATIC_REQUIRE(coops::wire_token(Datum::igld85) == "IGLD");
}

TEST_CASE("CO-OPS paired grids name their halves", "[core][coops][constexpr]") {
  STATIC_REQUIRE(coops::observed(PairedGrid::six_minute) ==
                 Observed::six_minute);
  STATIC_REQUIRE(coops::observed(PairedGrid::hourly) == Observed::hourly);
  STATIC_REQUIRE(coops::predicted(PairedGrid::six_minute) ==
                 PredictionInterval::six);
  STATIC_REQUIRE(coops::predicted(PairedGrid::hourly) ==
                 PredictionInterval::sixty);
}

TEST_CASE("CO-OPS maximum windows per product", "[core][coops][constexpr]") {
  STATIC_REQUIRE(
      coops::max_window(observed_level(Observed::one_minute)).duration() ==
      days{4});
  STATIC_REQUIRE(
      coops::max_window(observed_level(Observed::six_minute)).duration() ==
      days{31});
  STATIC_REQUIRE(
      coops::max_window(observed_level(Observed::hourly)).duration() ==
      days{365});
  STATIC_REQUIRE(every_prediction_interval_is_a_year());
  // A paired product takes the smaller of its halves.
  STATIC_REQUIRE(coops::max_window(paired(PairedGrid::six_minute)).duration() ==
                 days{31});
  STATIC_REQUIRE(coops::max_window(paired(PairedGrid::hourly)).duration() ==
                 days{365});
  STATIC_REQUIRE(met_windows_follow_interval());
  STATIC_REQUIRE(coops::grain.duration() == std::chrono::minutes{1});
}

TEST_CASE("CO-OPS products compare by value", "[core][coops][constexpr]") {
  STATIC_REQUIRE(observed_level(Observed::hourly) ==
                 observed_level(Observed::hourly));
  STATIC_REQUIRE(WaterLevel{.kind = ObservedLevel{.series = Observed::hourly},
                            .datum = Datum::mllw} !=
                 WaterLevel{.kind = ObservedLevel{.series = Observed::hourly},
                            .datum = Datum::msl});
  STATIC_REQUIRE(met(Met::wind, MetInterval::hourly) !=
                 met(Met::wind, MetInterval::six_minute));
  // Required reads as its value.
  STATIC_REQUIRE(WaterLevel{.kind = ObservedLevel{.series = Observed::hourly},
                            .datum = Datum::stnd}
                     .datum.get() == Datum::stnd);
  STATIC_REQUIRE(
      coops::wire_token(WaterLevel{
          .kind = PredictedLevel{.interval = PredictionInterval::one},
          .datum = Datum::navd88}
                            .datum) == "NAVD");
}

TEST_CASE("CO-OPS product shapes", "[core][coops][constexpr]") {
  STATIC_REQUIRE(coops::shape(observed_level(Observed::hourly)) ==
                 Shape::series);
  STATIC_REQUIRE(coops::shape(predicted_level(PredictionInterval::one)) ==
                 Shape::series);
  STATIC_REQUIRE(coops::shape(paired(PairedGrid::hourly)) ==
                 Shape::observed_vs_predicted);
  STATIC_REQUIRE(only_wind_is_wind_shaped());
}

TEST_CASE("CO-OPS requests compare by every field",
          "[core][coops][constexpr]") {
  using mov::core::StationId;
  using mov::core::provider::Coops;
  constexpr auto request = [](std::string_view id, Product p) {
    return coops::Request{.station = *StationId<Coops>::make(id),
                          .range = range_ms(0, 60'000),
                          .product = p};
  };
  STATIC_REQUIRE(request("8761724", observed_level(Observed::hourly)) ==
                 request("8761724", observed_level(Observed::hourly)));
  STATIC_REQUIRE(request("8761724", observed_level(Observed::hourly)) !=
                 request("8761725", observed_level(Observed::hourly)));
  STATIC_REQUIRE(request("8761724", observed_level(Observed::hourly)) !=
                 request("8761724", observed_level(Observed::six_minute)));
  STATIC_REQUIRE(request("8761724", paired(PairedGrid::hourly)) !=
                 request("8761724", met(Met::wind, MetInterval::hourly)));
}

TEST_CASE("CO-OPS windows of a request sit on minutes",
          "[core][coops][constexpr][regression][L1]") {
  // 100 days and a few seconds of 6-minute water level: three 31-day
  // windows and a short one, every bound a whole minute.
  constexpr auto windows_on_minutes = [] {
    const auto r = range_ms(5'000, (100LL * 86'400'000) + 7'000);
    const auto a = mov::core::aligned(r, coops::grain);
    if (not a) {
      return false;
    }
    const auto w = mov::core::split(
        *a, coops::max_window(observed_level(Observed::six_minute)));
    return w.size() == 4 and
           std::ranges::all_of(w, [](const mov::core::TimeRange& x) {
             return x.begin().time_since_epoch().count() % 60'000 == 0 and
                    x.end().time_since_epoch().count() % 60'000 == 0;
           });
  };
  STATIC_REQUIRE(windows_on_minutes());
}
