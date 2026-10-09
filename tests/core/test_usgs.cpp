// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

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

mov::core::StationId<mov::core::provider::Usgs> site() {
  auto id =
      mov::core::StationId<mov::core::provider::Usgs>::make("USGS-07374000");
  REQUIRE(id.has_value());
  return *std::move(id);
}

usgs::Request continuous_request() {
  const auto range =
      mov::core::TimeRange::make(mov::core::Time{std::chrono::milliseconds{0}},
                                 mov::core::Time{std::chrono::milliseconds{1}});
  REQUIRE(range.has_value());
  return usgs::Continuous{
      .station = site(),
      .series = usgs::ByParameter{.parameter = parameter("00065")},
      .range = *range};
}

usgs::Request daily_request() {
  using std::chrono::days;
  using std::chrono::sys_days;
  const auto range =
      mov::core::DayRange::make(sys_days{days{0}}, sys_days{days{13'150}});
  REQUIRE(range.has_value());
  return usgs::Daily{
      .station = site(),
      .series = usgs::DailyByParameter{.parameter = parameter("00060"),
                                       .statistic = StatisticId::mean()},
      .days = *range};
}

}  // namespace

// At run time: a usgs::Request holds a std::string inside a variant, which
// libc++ 18 cannot constant-evaluate (the toolchain notes, core-design 1.2).
TEST_CASE("USGS maximum windows", "[core][usgs]") {
  CHECK(usgs::max_window(continuous_request()) == std::chrono::days{1000});
  CHECK_FALSE(usgs::max_window(daily_request()).has_value());
  CHECK(continuous_request() != daily_request());
  CHECK(continuous_request() == continuous_request());
}

TEST_CASE("USGS resolution and catalog records are values", "[core][usgs]") {
  STATIC_REQUIRE(std::copyable<usgs::ResolvedSeries>);
  STATIC_REQUIRE(std::equality_comparable<usgs::ResolvedSeries>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<usgs::ResolvedSeries>);
  STATIC_REQUIRE(std::copyable<usgs::SeriesInfo>);
  STATIC_REQUIRE(std::equality_comparable<usgs::SeriesInfo>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<usgs::SeriesInfo>);
  STATIC_REQUIRE(std::copyable<usgs::Request>);
  STATIC_REQUIRE(std::equality_comparable<usgs::Request>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<usgs::Request>);
  // A daily range is days, not instants; a continuous selector has no
  // statistic to name.
  STATIC_REQUIRE_FALSE(std::is_constructible_v<
                       std::variant<usgs::ByParameter, usgs::ByTimeSeries>,
                       usgs::DailyByParameter>);

  const usgs::ResolvedSeries resolved{.id = series_id('a'),
                                      .parameter = parameter("00065"),
                                      .statistic = StatisticId::instantaneous(),
                                      .parameter_name = "Gage height, ft",
                                      .unit = mov::core::parse_unit("ft")};
  usgs::ResolvedSeries other = resolved;
  CHECK(other == resolved);
  other.parameter_name = "Gage height";
  CHECK(other != resolved);

  const usgs::SeriesInfo info{.id = series_id('b'),
                              .parameter = parameter("00060"),
                              .statistic = StatisticId::instantaneous(),
                              .primary = true,
                              .unit = std::nullopt,
                              .validity = mov::core::ValidRange{}};
  usgs::SeriesInfo secondary = info;
  secondary.primary = false;
  CHECK(secondary != info);
}

TEST_CASE("USGS parameters name their quantity", "[core][usgs]") {
  CHECK(usgs::quantity_of(parameter("00060")) ==
        QuantityId{Quantity::discharge});
  CHECK(usgs::quantity_of(parameter("62619")) ==
        QuantityId{Quantity::water_level});
  // Outside the registry: the generic quantity usgs_<code>.
  const QuantityId temperature = usgs::quantity_of(parameter("00010"));
  CHECK(mov::core::token(temperature) == "usgs_00010");
  const auto* generic = std::get_if<GenericQuantity>(&temperature);
  REQUIRE(generic != nullptr);
  CHECK(generic->standard_name().empty());
}

TEST_CASE("every registry datum suits its quantity", "[core][usgs]") {
  for (const std::string_view code : {"00060", "00065", "62620", "62619"}) {
    const auto mapping = usgs::registry_quantity(parameter(code));
    REQUIRE(mapping.has_value());
    if (mapping and mapping->datum) {
      CHECK(mov::core::datum_applicable(QuantityId{mapping->quantity}));
    }
  }
}
