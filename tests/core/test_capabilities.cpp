// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// offered() over USGS capabilities (the CO-OPS truth table is in
// test_capabilities_constexpr.cpp).

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/windows.hpp"

namespace usgs = mov::core::usgs;

using mov::core::NotOffered;
using mov::core::offered;
using usgs::ParameterCode;
using usgs::StatisticId;
using usgs::TimeSeriesId;
using UsgsCaps = mov::core::Capabilities<mov::core::provider::Usgs>;

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

usgs::SeriesInfo series(char id, std::string_view code, StatisticId stat) {
  return {.id = series_id(id),
          .parameter = parameter(code),
          .statistic = stat,
          .primary = true,
          .unit = std::nullopt,
          .validity = mov::core::ValidRange{}};
}

/// A river site: instantaneous discharge and gage height, and the daily mean
/// of discharge.
UsgsCaps river() {
  return {.series = {series('a', "00060", StatisticId::instantaneous()),
                     series('b', "00065", StatisticId::instantaneous()),
                     series('c', "00060", StatisticId::mean())}};
}

mov::core::StationId<mov::core::provider::Usgs> site() {
  auto id =
      mov::core::StationId<mov::core::provider::Usgs>::make("USGS-07374000");
  REQUIRE(id.has_value());
  return *std::move(id);
}

usgs::Request continuous(
    std::variant<usgs::ByParameter, usgs::ByTimeSeries> s) {
  const auto range =
      mov::core::TimeRange::make(mov::core::Time{std::chrono::milliseconds{0}},
                                 mov::core::Time{std::chrono::milliseconds{1}});
  REQUIRE(range.has_value());
  return usgs::Continuous{.station = site(), .series = s, .range = *range};
}

usgs::Request daily(
    std::variant<usgs::DailyByParameter, usgs::ByTimeSeries> s) {
  using std::chrono::days;
  using std::chrono::sys_days;
  const auto range =
      mov::core::DayRange::make(sys_days{days{0}}, sys_days{days{7}});
  REQUIRE(range.has_value());
  return usgs::Daily{.station = site(), .series = s, .days = *range};
}

usgs::ByParameter by_parameter(std::string_view code) {
  return {.parameter = parameter(code)};
}

usgs::DailyByParameter daily_by(std::string_view code, StatisticId stat) {
  return {.parameter = parameter(code), .statistic = stat};
}

usgs::ByTimeSeries by_id(char id) { return {.id = series_id(id)}; }

std::expected<void, NotOffered> refused(NotOffered why) {
  return std::unexpected{why};
}

}  // namespace

TEST_CASE("continuous values by parameter need an instantaneous series",
          "[core][capabilities]") {
  CHECK(offered(river(), continuous(by_parameter("00060"))).has_value());
  CHECK(offered(river(), continuous(by_parameter("00065"))).has_value());
  CHECK(offered(river(), continuous(by_parameter("62620"))) ==
        refused(NotOffered::parameter));
  // Only a daily series of the parameter.
  const UsgsCaps daily_only{
      .series = {series('d', "00010", StatisticId::maximum())}};
  CHECK(offered(daily_only, continuous(by_parameter("00010"))) ==
        refused(NotOffered::statistic));
  CHECK(offered(UsgsCaps{}, continuous(by_parameter("00060"))) ==
        refused(NotOffered::parameter));
}

TEST_CASE("daily values by parameter need the statistic",
          "[core][capabilities]") {
  CHECK(offered(river(), daily(daily_by("00060", StatisticId::mean())))
            .has_value());
  CHECK(offered(river(), daily(daily_by("00060", StatisticId::maximum()))) ==
        refused(NotOffered::statistic));
  // Instantaneous is not a daily statistic.
  CHECK(offered(river(), daily(daily_by("00065", StatisticId::mean()))) ==
        refused(NotOffered::statistic));
  CHECK(offered(river(), daily(daily_by("00045", StatisticId::mean()))) ==
        refused(NotOffered::parameter));
}

TEST_CASE("a series by id must exist and belong to the collection",
          "[core][capabilities]") {
  CHECK(offered(river(), continuous(by_id('a'))).has_value());
  CHECK(offered(river(), daily(by_id('c'))).has_value());
  CHECK(offered(river(), continuous(by_id('c'))) ==
        refused(NotOffered::statistic));
  CHECK(offered(river(), daily(by_id('b'))) == refused(NotOffered::statistic));
  CHECK(offered(river(), continuous(by_id('f'))) ==
        refused(NotOffered::series));
  CHECK(offered(river(), daily(by_id('f'))) == refused(NotOffered::series));
}
