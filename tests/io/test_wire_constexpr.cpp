// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/io/wire: the origins and the CO-OPS series of
// every product with their wire tokens.

#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

#include "mov/core/provider/coops.hpp"
#include "mov/io/detail/json.hpp"
#include "mov/io/detail/text_position.hpp"
#include "mov/io/wire/coops.hpp"
#include "mov/io/wire/origin.hpp"

namespace {

namespace cc = mov::core::coops;
using mov::io::wire::base;
using mov::io::wire::Origin;
using mov::io::wire::coops::Api;
using mov::io::wire::coops::datum_of;
using mov::io::wire::coops::interval_token;
using mov::io::wire::coops::Observation;
using mov::io::wire::coops::origin_of;
using mov::io::wire::coops::Pair;
using mov::io::wire::coops::Prediction;
using mov::io::wire::coops::product_name;
using mov::io::wire::coops::ProductSeries;
using mov::io::wire::coops::Series;
using mov::io::wire::coops::series_of;
using mov::io::wire::coops::Single;

constexpr cc::Product observed(cc::Observed s, cc::Datum d) {
  return cc::WaterLevel{.kind = cc::ObservedLevel{.series = s}, .datum = d};
}
constexpr cc::Product predicted(cc::PredictionInterval i, cc::Datum d) {
  return cc::WaterLevel{.kind = cc::PredictedLevel{.interval = i}, .datum = d};
}
constexpr cc::Product paired(cc::PairedGrid g, cc::Datum d) {
  return cc::WaterLevel{.kind = cc::ObservedVsPredicted{.grid = g}, .datum = d};
}
constexpr cc::Product met(cc::Met m, cc::MetInterval i) {
  return cc::Meteorological{.product = m, .interval = i};
}
constexpr cc::Product air_gap() { return cc::AirGap{}; }

// The series of a single product (a pair has none).
constexpr std::optional<Series> single(const cc::Product& p) {
  const ProductSeries s = series_of(p);
  if (const auto* one = std::get_if<Single>(&s)) {
    return one->series;
  }
  return std::nullopt;
}

constexpr bool has_interval(const cc::Product& p, std::string_view token) {
  const auto s = single(p);
  if (not s) {
    return false;
  }
  const auto t = interval_token(*s);
  return t.has_value() and *t == token;
}

constexpr bool no_interval(const cc::Product& p) {
  const auto s = single(p);
  return s.has_value() and not interval_token(*s).has_value();
}

constexpr std::string_view name_of(const cc::Product& p) {
  const auto s = single(p);
  return s ? product_name(*s) : std::string_view{};
}

}  // namespace

TEST_CASE("origins are https bases without a trailing slash",
          "[io][wire][constexpr]") {
  STATIC_REQUIRE(base(Origin::coops_data) ==
                 "https://api.tidesandcurrents.noaa.gov");
  STATIC_REQUIRE(base(Origin::coops_metadata) == base(Origin::coops_data));
  STATIC_REQUIRE(base(Origin::usgs) == "https://api.waterdata.usgs.gov");
  STATIC_REQUIRE(base(Origin::ndbc) == "https://www.ndbc.noaa.gov");
  STATIC_REQUIRE(base(Origin::github) == "https://api.github.com");
  STATIC_REQUIRE(origin_of(Api::data) == Origin::coops_data);
  STATIC_REQUIRE(origin_of(Api::metadata) == Origin::coops_metadata);
}

TEST_CASE("a product is one series, a pair is its grid and datum",
          "[io][wire][constexpr]") {
  STATIC_REQUIRE(single(observed(cc::Observed::hourly, cc::Datum::mllw)) ==
                 Series{Observation{.series = cc::Observed::hourly,
                                    .datum = cc::Datum::mllw}});
  STATIC_REQUIRE(
      single(predicted(cc::PredictionInterval::ten, cc::Datum::navd88)) ==
      Series{Prediction{.interval = cc::PredictionInterval::ten,
                        .datum = cc::Datum::navd88}});
  STATIC_REQUIRE(single(air_gap()) == Series{cc::AirGap{}});
  // L10: both halves of a pair carry its datum, each in its own request.
  STATIC_REQUIRE(series_of(paired(cc::PairedGrid::hourly, cc::Datum::msl)) ==
                 ProductSeries{Pair{.grid = cc::PairedGrid::hourly,
                                    .datum = cc::Datum::msl}});
  constexpr Pair hourly{.grid = cc::PairedGrid::hourly,
                        .datum = cc::Datum::msl};
  STATIC_REQUIRE(
      hourly.observed() ==
      Observation{.series = cc::Observed::hourly, .datum = cc::Datum::msl});
  STATIC_REQUIRE(hourly.predicted() ==
                 Prediction{.interval = cc::PredictionInterval::sixty,
                            .datum = cc::Datum::msl});
  constexpr Pair six{.grid = cc::PairedGrid::six_minute,
                     .datum = cc::Datum::stnd};
  STATIC_REQUIRE(six.predicted() ==
                 Prediction{.interval = cc::PredictionInterval::six,
                            .datum = cc::Datum::stnd});
  STATIC_REQUIRE(not single(paired(cc::PairedGrid::hourly, cc::Datum::msl)));
}

TEST_CASE("each series names its datagetter product",
          "[io][wire][constexpr][regression][L5]") {
  STATIC_REQUIRE(name_of(observed(cc::Observed::six_minute, cc::Datum::mllw)) ==
                 "water_level");
  STATIC_REQUIRE(name_of(observed(cc::Observed::one_minute, cc::Datum::mllw)) ==
                 "one_minute_water_level");
  STATIC_REQUIRE(name_of(observed(cc::Observed::hourly, cc::Datum::mllw)) ==
                 "hourly_height");
  STATIC_REQUIRE(name_of(predicted(cc::PredictionInterval::one,
                                   cc::Datum::mllw)) == "predictions");
  constexpr auto met_name = [](cc::Met m) {
    return name_of(met(m, cc::MetInterval::six_minute));
  };
  STATIC_REQUIRE(met_name(cc::Met::air_temperature) == "air_temperature");
  STATIC_REQUIRE(met_name(cc::Met::water_temperature) == "water_temperature");
  STATIC_REQUIRE(met_name(cc::Met::wind) == "wind");
  STATIC_REQUIRE(met_name(cc::Met::air_pressure) == "air_pressure");
  STATIC_REQUIRE(met_name(cc::Met::humidity) == "humidity");
  STATIC_REQUIRE(met_name(cc::Met::conductivity) == "conductivity");
  STATIC_REQUIRE(met_name(cc::Met::salinity) == "salinity");
  STATIC_REQUIRE(met_name(cc::Met::visibility) == "visibility");
  STATIC_REQUIRE(name_of(air_gap()) == "air_gap");
}

TEST_CASE("intervals are sent only when they are not the default",
          "[io][wire][constexpr]") {
  constexpr auto pred = [](cc::PredictionInterval i) {
    return predicted(i, cc::Datum::mllw);
  };
  STATIC_REQUIRE(has_interval(pred(cc::PredictionInterval::one), "1"));
  STATIC_REQUIRE(has_interval(pred(cc::PredictionInterval::five), "5"));
  STATIC_REQUIRE(no_interval(pred(cc::PredictionInterval::six)));
  STATIC_REQUIRE(has_interval(pred(cc::PredictionInterval::ten), "10"));
  STATIC_REQUIRE(has_interval(pred(cc::PredictionInterval::fifteen), "15"));
  STATIC_REQUIRE(has_interval(pred(cc::PredictionInterval::thirty), "30"));
  STATIC_REQUIRE(has_interval(pred(cc::PredictionInterval::sixty), "h"));
  STATIC_REQUIRE(no_interval(observed(cc::Observed::hourly, cc::Datum::mllw)));
  STATIC_REQUIRE(no_interval(met(cc::Met::wind, cc::MetInterval::six_minute)));
  STATIC_REQUIRE(
      has_interval(met(cc::Met::wind, cc::MetInterval::hourly), "h"));
  // Air gap takes no interval at all.
  STATIC_REQUIRE(no_interval(air_gap()));
}

TEST_CASE("only water levels carry a datum on the wire",
          "[io][wire][constexpr]") {
  STATIC_REQUIRE(datum_of(Series{Observation{.series = cc::Observed::six_minute,
                                             .datum = cc::Datum::navd88}}) ==
                 cc::Datum::navd88);
  STATIC_REQUIRE(datum_of(Series{Prediction{
                     .interval = cc::PredictionInterval::six,
                     .datum = cc::Datum::igld85}}) == cc::Datum::igld85);
  STATIC_REQUIRE(not datum_of(Series{cc::AirGap{}}));
  STATIC_REQUIRE(not datum_of(Series{cc::Meteorological{
      .product = cc::Met::wind, .interval = cc::MetInterval::hourly}}));
}

TEST_CASE("an offset's line and column", "[io][wire][constexpr]") {
  using mov::io::detail::where_of;
  STATIC_REQUIRE(where_of("abc", 0).line == 1);
  STATIC_REQUIRE(where_of("abc", 2).column == 2);
  STATIC_REQUIRE(where_of("a\nbc", 3).line == 2);
  STATIC_REQUIRE(where_of("a\nbc", 3).column == 1);
  STATIC_REQUIRE(where_of("a\n", 2).column == 0);
  STATIC_REQUIRE(where_of("ab", 99).column == 2);
  STATIC_REQUIRE(mov::io::detail::json::bom_length("\xEF\xBB\xBF{}") == 3);
  STATIC_REQUIRE(mov::io::detail::json::bom_length("{}") == 0);
}

TEST_CASE("the wire values are regular", "[io][wire][constexpr]") {
  // No series has a default: its datum is asked for (as core's WaterLevel).
  STATIC_REQUIRE(not std::is_default_constructible_v<Observation>);
  STATIC_REQUIRE(not std::is_default_constructible_v<Prediction>);
  STATIC_REQUIRE(not std::is_default_constructible_v<Series>);
  STATIC_REQUIRE(not std::is_default_constructible_v<ProductSeries>);
  STATIC_REQUIRE(not std::is_default_constructible_v<Pair>);
  STATIC_REQUIRE(std::copyable<Observation>);
  STATIC_REQUIRE(std::equality_comparable<Prediction>);
  STATIC_REQUIRE(std::copyable<Series>);
  STATIC_REQUIRE(std::equality_comparable<Series>);
  STATIC_REQUIRE(std::copyable<ProductSeries>);
  STATIC_REQUIRE(std::equality_comparable<ProductSeries>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<ProductSeries>);
}
