// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/provider/usgs.hpp.

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <compare>
#include <concepts>
#include <expected>
#include <optional>
#include <string_view>
#include <type_traits>
#include <utility>

#include "mov/core/datum.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/quantity.hpp"

namespace usgs = mov::core::usgs;

using mov::core::Quantity;
using mov::core::VerticalDatum;
using usgs::CodeError;
using usgs::ParameterCode;
using usgs::QuantityMapping;
using usgs::StatisticId;
using usgs::TimeSeriesId;

namespace {

template <class T>
concept HasView = requires(T&& t) { std::forward<T>(t).view(); };

template <class Code>
constexpr bool makes(std::string_view text) {
  const auto code = Code::make(text);
  return code.has_value() and code->view() == text;
}

template <class Code>
constexpr bool fails(std::string_view text, CodeError e) {
  return Code::make(text) == std::unexpected{e};
}

/// view() of a named code (a temporary cannot be viewed).
constexpr bool spelled(const StatisticId& s, std::string_view text) {
  return s.view() == text;
}

constexpr ParameterCode parameter(std::string_view text) {
  return *ParameterCode::make(text);
}

constexpr std::optional<QuantityMapping> mapping(std::string_view text) {
  return usgs::registry_quantity(parameter(text));
}

constexpr std::string_view hex32 = "0123456789abcdef0123456789abcdef";

}  // namespace

TEST_CASE("USGS codes are values", "[core][usgs][constexpr]") {
  STATIC_REQUIRE(std::copyable<ParameterCode>);
  STATIC_REQUIRE(std::totally_ordered<ParameterCode>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<ParameterCode>);
  STATIC_REQUIRE(std::totally_ordered<StatisticId>);
  STATIC_REQUIRE(std::totally_ordered<TimeSeriesId>);
  // Parse, don't validate: no default and no way around make.
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<ParameterCode>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<StatisticId>);
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<TimeSeriesId>);
  STATIC_REQUIRE_FALSE(
      std::is_constructible_v<ParameterCode, std::string_view>);
  // A parameter code is not a statistic, though both are five digits.
  STATIC_REQUIRE_FALSE(std::is_constructible_v<StatisticId, ParameterCode>);
  STATIC_REQUIRE_FALSE(std::is_convertible_v<ParameterCode, StatisticId>);
  // view() points into the code, so a temporary cannot hand it out.
  STATIC_REQUIRE(HasView<const ParameterCode&>);
  STATIC_REQUIRE_FALSE(HasView<ParameterCode>);
  STATIC_REQUIRE_FALSE(HasView<StatisticId>);
  STATIC_REQUIRE_FALSE(HasView<TimeSeriesId>);
}

TEST_CASE("USGS parameter and statistic codes are five digits",
          "[core][usgs][constexpr]") {
  STATIC_REQUIRE(makes<ParameterCode>("00060"));
  STATIC_REQUIRE(makes<ParameterCode>("62620"));
  STATIC_REQUIRE(fails<ParameterCode>("0006", CodeError::wrong_length));
  STATIC_REQUIRE(fails<ParameterCode>("000600", CodeError::wrong_length));
  STATIC_REQUIRE(fails<ParameterCode>("", CodeError::wrong_length));
  // No trimming: the server's text is taken exactly.
  STATIC_REQUIRE(fails<ParameterCode>(" 0060", CodeError::invalid_character));
  STATIC_REQUIRE(fails<ParameterCode>("0006a", CodeError::invalid_character));
  STATIC_REQUIRE(fails<ParameterCode>("-0060", CodeError::invalid_character));
  STATIC_REQUIRE(makes<StatisticId>("00011"));
  STATIC_REQUIRE(fails<StatisticId>("11", CodeError::wrong_length));
  STATIC_REQUIRE(fails<StatisticId>("0001x", CodeError::invalid_character));
  STATIC_REQUIRE(parameter("00060") < parameter("00065"));
  STATIC_REQUIRE(parameter("00065") == parameter("00065"));
}

TEST_CASE("USGS statistic constants", "[core][usgs][constexpr]") {
  STATIC_REQUIRE(spelled(StatisticId::instantaneous(), "00011"));
  STATIC_REQUIRE(spelled(StatisticId::mean(), "00003"));
  STATIC_REQUIRE(spelled(StatisticId::minimum(), "00002"));
  STATIC_REQUIRE(spelled(StatisticId::maximum(), "00001"));
  STATIC_REQUIRE(StatisticId::make("00003") == StatisticId::mean());
}

TEST_CASE("USGS time series ids are 32 lower-case hex digits",
          "[core][usgs][constexpr]") {
  STATIC_REQUIRE(makes<TimeSeriesId>(hex32));
  STATIC_REQUIRE(makes<TimeSeriesId>("ffffffffffffffffffffffffffffffff"));
  STATIC_REQUIRE(fails<TimeSeriesId>(hex32.substr(1), CodeError::wrong_length));
  STATIC_REQUIRE(fails<TimeSeriesId>("0123456789ABCDEF0123456789abcdef",
                                     CodeError::invalid_character));
  STATIC_REQUIRE(fails<TimeSeriesId>("0123456789abcdeg0123456789abcdef",
                                     CodeError::invalid_character));
  STATIC_REQUIRE(fails<TimeSeriesId>("0123456789abcdef-123456789abcdef",
                                     CodeError::invalid_character));
}

TEST_CASE("USGS parameters in the quantity registry",
          "[core][usgs][constexpr]") {
  STATIC_REQUIRE(
      mapping("00060") ==
      QuantityMapping{.quantity = Quantity::discharge, .datum = std::nullopt});
  STATIC_REQUIRE(mapping("00065") ==
                 QuantityMapping{.quantity = Quantity::water_level,
                                 .datum = VerticalDatum::stnd});
  STATIC_REQUIRE(mapping("62620") ==
                 QuantityMapping{.quantity = Quantity::water_level,
                                 .datum = VerticalDatum::navd88});
  STATIC_REQUIRE(mapping("62619") ==
                 QuantityMapping{.quantity = Quantity::water_level,
                                 .datum = VerticalDatum::ngvd29});
  // P3 checked these against the parameter-codes collection (2026-10-15).
  STATIC_REQUIRE(mapping("00010") ==
                 QuantityMapping{.quantity = Quantity::water_temperature,
                                 .datum = std::nullopt});
  STATIC_REQUIRE(mapping("00020") ==
                 QuantityMapping{.quantity = Quantity::air_temperature,
                                 .datum = std::nullopt});
  STATIC_REQUIRE(
      mapping("00035") ==
      QuantityMapping{.quantity = Quantity::wind_speed, .datum = std::nullopt});
  STATIC_REQUIRE(mapping("00036") ==
                 QuantityMapping{.quantity = Quantity::wind_direction,
                                 .datum = std::nullopt});
  // Precipitation (00045) has no registry quantity.
  STATIC_REQUIRE_FALSE(mapping("00045").has_value());
  STATIC_REQUIRE_FALSE(mapping("63680").has_value());
  STATIC_REQUIRE(usgs::grain.duration() == std::chrono::seconds{1});
}

TEST_CASE("USGS statistics name their collection", "[core][usgs][constexpr]") {
  using usgs::Collection;
  STATIC_REQUIRE(usgs::collection_of(StatisticId::instantaneous()) ==
                 Collection::continuous);
  STATIC_REQUIRE(usgs::collection_of(StatisticId::mean()) == Collection::daily);
  STATIC_REQUIRE(usgs::collection_of(StatisticId::maximum()) ==
                 Collection::daily);
  STATIC_REQUIRE(usgs::collection_of(*StatisticId::make("00021")) ==
                 Collection::daily);
}
