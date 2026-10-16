// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <concepts>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/provider/usgs.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/windows.hpp"

namespace usgs = mov::core::usgs;

using mov::core::GenericQuantity;
using mov::core::Quantity;
using mov::core::QuantityId;
using usgs::ParameterCode;
using usgs::ResolveError;
using usgs::SeriesInfo;
using usgs::SeriesSet;
using usgs::StatisticId;
using usgs::TimeSeriesId;

namespace {

ParameterCode parameter(std::string_view text) {
  const auto code = ParameterCode::make(text);
  REQUIRE(code.has_value());
  return *code;
}

TimeSeriesId series_id(char fill) {
  const auto id = TimeSeriesId::make(std::string(32, fill));
  REQUIRE(id.has_value());
  return *id;
}

SeriesInfo row(char id, std::string_view code, StatisticId stat,
               bool primary = true) {
  return {.id = series_id(id),
          .parameter = parameter(code),
          .statistic = stat,
          .parameter_name = "name of " + std::string{code},
          .primary = primary,
          .unit = std::nullopt,
          .validity = mov::core::ValidRange{}};
}

SeriesSet set_of(std::vector<SeriesInfo> rows) {
  auto set = SeriesSet::make(std::move(rows));
  REQUIRE(set.has_value());
  return std::move(set).value_or(SeriesSet{});
}

/// A river site: Primary instantaneous discharge and gage height, a second
/// (not Primary) discharge series, and the daily mean of discharge.
SeriesSet river() {
  return set_of({row('b', "00065", StatisticId::instantaneous()),
                 row('a', "00060", StatisticId::instantaneous()),
                 row('d', "00060", StatisticId::instantaneous(), false),
                 row('c', "00060", StatisticId::mean())});
}

mov::core::StationId<mov::core::provider::Usgs> site_id() {
  auto id =
      mov::core::StationId<mov::core::provider::Usgs>::make("USGS-07374000");
  REQUIRE(id.has_value());
  return *std::move(id);
}

usgs::Continuous continuous(
    std::variant<usgs::ByParameter, usgs::ByTimeSeries> selector) {
  const auto range =
      mov::core::TimeRange::make(mov::core::Time{std::chrono::milliseconds{0}},
                                 mov::core::Time{std::chrono::milliseconds{1}});
  REQUIRE(range.has_value());
  return {.station = site_id(), .series = selector, .range = *range};
}

usgs::Daily daily(
    std::variant<usgs::DailyByParameter, usgs::ByTimeSeries> selector) {
  using std::chrono::days;
  using std::chrono::local_days;
  const auto range =
      mov::core::DayRange::make(local_days{days{0}}, local_days{days{13'150}});
  REQUIRE(range.has_value());
  return {.station = site_id(), .series = selector, .days = *range};
}

usgs::ByParameter by_parameter(std::string_view code) {
  return {.parameter = parameter(code)};
}

usgs::DailyByParameter daily_by(std::string_view code, StatisticId stat) {
  return {.parameter = parameter(code), .statistic = stat};
}

usgs::ByTimeSeries by_id(char id) { return {.id = series_id(id)}; }

template <class T>
concept HasSeries = requires(T&& t) { std::forward<T>(t).series(); };

}  // namespace

TEST_CASE("USGS records are values", "[core][usgs]") {
  STATIC_REQUIRE(std::copyable<SeriesInfo>);
  STATIC_REQUIRE(std::equality_comparable<SeriesInfo>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<SeriesInfo>);
  STATIC_REQUIRE(std::regular<SeriesSet>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<SeriesSet>);
  STATIC_REQUIRE(std::copyable<usgs::Request>);
  STATIC_REQUIRE(std::equality_comparable<usgs::Request>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<usgs::Request>);
  STATIC_REQUIRE(std::copyable<usgs::Resolved<usgs::Continuous>>);
  STATIC_REQUIRE(std::equality_comparable<usgs::Resolved<usgs::Daily>>);
  // A resolved series comes only from resolve(), and the collections do not
  // mix.
  STATIC_REQUIRE_FALSE(
      std::is_constructible_v<usgs::Resolved<usgs::Continuous>, SeriesInfo>);
  STATIC_REQUIRE_FALSE(std::is_convertible_v<usgs::Resolved<usgs::Daily>,
                                             usgs::Resolved<usgs::Continuous>>);
  STATIC_REQUIRE(HasSeries<const usgs::Resolved<usgs::Daily>&>);
  STATIC_REQUIRE_FALSE(HasSeries<usgs::Resolved<usgs::Daily>>);
  // A continuous selector has no statistic to name.
  STATIC_REQUIRE_FALSE(std::is_constructible_v<
                       std::variant<usgs::ByParameter, usgs::ByTimeSeries>,
                       usgs::DailyByParameter>);
}

// At run time: a usgs::Request holds a std::string inside a variant, which
// libc++ 18 cannot constant-evaluate (core-design 1.2).
TEST_CASE("USGS continuous requests span at most 1000 days", "[core][usgs]") {
  CHECK(usgs::max_window(continuous(by_parameter("00065"))).duration() ==
        std::chrono::days{1000});
  const usgs::Request c = continuous(by_parameter("00065"));
  const usgs::Request d = daily(daily_by("00060", StatisticId::mean()));
  CHECK(c != d);
  CHECK(c == usgs::Request{continuous(by_parameter("00065"))});
}

TEST_CASE("a series set is sorted and unique by id", "[core][usgs]") {
  const SeriesSet site = river();
  REQUIRE(site.size() == 4);
  CHECK(site.items()[0].id == series_id('a'));
  CHECK(site.items()[3].id == series_id('d'));
  const SeriesInfo* c = site.find(series_id('c'));
  REQUIRE(c != nullptr);
  CHECK(c->statistic == StatisticId::mean());
  CHECK(site.find(series_id('e')) == nullptr);
  CHECK(site.find(series_id('0')) == nullptr);
  CHECK(site.find(series_id('f')) == nullptr);
  CHECK(SeriesSet{}.empty());
  const SeriesSet none;
  CHECK(none.find(series_id('a')) == nullptr);
  // Built in another order, the same set.
  CHECK(set_of({row('c', "00060", StatisticId::mean()),
                row('d', "00060", StatisticId::instantaneous(), false),
                row('a', "00060", StatisticId::instantaneous()),
                row('b', "00065", StatisticId::instantaneous())}) == site);
}

TEST_CASE("a series set refuses repeated ids and several Primary series",
          "[core][usgs]") {
  CHECK(SeriesSet::make({row('b', "00065", StatisticId::instantaneous()),
                         row('a', "00060", StatisticId::mean()),
                         row('b', "00060", StatisticId::mean(), false)}) ==
        std::unexpected{usgs::SeriesSetError{
            usgs::DuplicateSeriesId{.id = series_id('b')}}});
  CHECK(SeriesSet::make({row('a', "00060", StatisticId::instantaneous()),
                         row('b', "00060", StatisticId::mean()),
                         row('c', "00060", StatisticId::instantaneous())}) ==
        std::unexpected{usgs::SeriesSetError{
            usgs::SeveralPrimary{.parameter = parameter("00060"),
                                 .statistic = StatisticId::instantaneous()}}});
  // Repeated ids are reported before several Primary series.
  CHECK(SeriesSet::make({row('a', "00060", StatisticId::instantaneous()),
                         row('a', "00060", StatisticId::instantaneous())}) ==
        std::unexpected{usgs::SeriesSetError{
            usgs::DuplicateSeriesId{.id = series_id('a')}}});
  // Primary series of different statistics or parameters are fine.
  CHECK(SeriesSet::make({row('a', "00060", StatisticId::instantaneous()),
                         row('b', "00060", StatisticId::mean()),
                         row('c', "00065", StatisticId::mean())})
            .has_value());
}

TEST_CASE("resolution by parameter takes the Primary series", "[core][usgs]") {
  const auto c = usgs::resolve(continuous(by_parameter("00060")), river());
  REQUIRE(c.has_value());
  CHECK(c.transform([](const auto& r) { return r.series().id; }) ==
        series_id('a'));
  const auto d =
      usgs::resolve(daily(daily_by("00060", StatisticId::mean())), river());
  REQUIRE(d.has_value());
  CHECK(d.transform([](const auto& r) { return r.series().parameter_name; }) ==
        std::string{"name of 00060"});
  // No Primary series of the parameter and statistic.
  CHECK(usgs::resolve(continuous(by_parameter("62620")), river()) ==
        std::unexpected{ResolveError::no_series});
  CHECK(usgs::resolve(daily(daily_by("00065", StatisticId::mean())), river()) ==
        std::unexpected{ResolveError::no_series});
  const SeriesSet only_secondary =
      set_of({row('d', "00060", StatisticId::instantaneous(), false)});
  CHECK(usgs::resolve(continuous(by_parameter("00060")), only_secondary) ==
        std::unexpected{ResolveError::no_series});
  // A daily request naming the instantaneous statistic.
  CHECK(usgs::resolve(daily(daily_by("00060", StatisticId::instantaneous())),
                      river()) ==
        std::unexpected{ResolveError::wrong_collection});
}

TEST_CASE("resolution by id checks the collection", "[core][usgs]") {
  // Not Primary, but named: resolution takes it.
  const auto secondary = usgs::resolve(continuous(by_id('d')), river());
  REQUIRE(secondary.has_value());
  CHECK(secondary.transform([](const auto& r) { return r.series().primary; }) ==
        false);
  CHECK(usgs::resolve(daily(by_id('c')), river()).has_value());
  CHECK(usgs::resolve(continuous(by_id('c')), river()) ==
        std::unexpected{ResolveError::wrong_collection});
  CHECK(usgs::resolve(daily(by_id('a')), river()) ==
        std::unexpected{ResolveError::wrong_collection});
  CHECK(usgs::resolve(continuous(by_id('f')), river()) ==
        std::unexpected{ResolveError::no_series});
  CHECK(usgs::resolve(daily(by_id('f')), river()) ==
        std::unexpected{ResolveError::no_series});
  // Two resolutions of one request are equal.
  CHECK(usgs::resolve(continuous(by_id('a')), river()) ==
        usgs::resolve(continuous(by_parameter("00060")), river()));
}

TEST_CASE("USGS parameters name their quantity", "[core][usgs]") {
  CHECK(usgs::quantity_of(parameter("00060")) ==
        QuantityId{Quantity::discharge});
  CHECK(usgs::quantity_of(parameter("62619")) ==
        QuantityId{Quantity::water_level});
  CHECK(usgs::quantity_of(parameter("00010")) ==
        QuantityId{Quantity::water_temperature});
  // Outside the registry: the generic quantity usgs_<code>, the same value
  // GenericQuantity::parse makes of that token.
  const QuantityId precipitation = usgs::quantity_of(parameter("00045"));
  CHECK(mov::core::token(precipitation) == "usgs_00045");
  const auto parsed =
      GenericQuantity::parse({.token = "usgs_00045", .standard_name = ""});
  REQUIRE(parsed.has_value());
  CHECK(precipitation == QuantityId{parsed.value_or(GenericQuantity{})});
}

TEST_CASE("every registry datum suits its quantity", "[core][usgs]") {
  for (const std::string_view code : {"00010", "00020", "00035", "00036",
                                      "00060", "00065", "62620", "62619"}) {
    const auto mapping = usgs::registry_quantity(parameter(code));
    REQUIRE(mapping.has_value());
    if (mapping and mapping->datum) {
      CHECK(mov::core::datum_applicable(QuantityId{mapping->quantity}));
    }
  }
}
