// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/quantity.hpp.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "mov/core/quantity.hpp"

using mov::core::datum_applicable;
using mov::core::GenericQuantity;
using mov::core::info;
using mov::core::parse_quantity_token;
using mov::core::Quantity;
using mov::core::QuantityId;
using mov::core::QuantityInfo;

namespace {

// One row of the quantity registry table, docs/station-netcdf.md section 6,
// written out independently of the implementation.
struct Row {
  Quantity quantity;
  std::string_view token;
  std::string_view standard_name;
  std::string_view units;
};

constexpr std::array sn_registry{
    Row{Quantity::water_level, "water_level",
        "water_surface_height_above_reference_datum", "m"},
    Row{Quantity::water_level_prediction, "water_level_prediction",
        "water_surface_height_above_reference_datum", "m"},
    Row{Quantity::air_temperature, "air_temperature", "air_temperature",
        "degC"},
    Row{Quantity::water_temperature, "water_temperature",
        "sea_water_temperature", "degC"},
    Row{Quantity::dew_point, "dew_point", "dew_point_temperature", "degC"},
    Row{Quantity::wind_speed, "wind_speed", "wind_speed", "m s-1"},
    Row{Quantity::wind_direction, "wind_direction", "wind_from_direction",
        "degree"},
    Row{Quantity::wind_gust, "wind_gust", "wind_speed_of_gust", "m s-1"},
    Row{Quantity::wind_u, "wind_u", "eastward_wind", "m s-1"},
    Row{Quantity::wind_v, "wind_v", "northward_wind", "m s-1"},
    Row{Quantity::air_pressure, "air_pressure", "air_pressure", "hPa"},
    Row{Quantity::relative_humidity, "relative_humidity", "relative_humidity",
        "percent"},
    Row{Quantity::conductivity, "conductivity",
        "sea_water_electrical_conductivity", "S m-1"},
    Row{Quantity::visibility, "visibility", "visibility_in_air", "m"},
    Row{Quantity::current_u, "current_u", "eastward_sea_water_velocity",
        "m s-1"},
    Row{Quantity::current_v, "current_v", "northward_sea_water_velocity",
        "m s-1"},
    Row{Quantity::wave_height, "wave_height",
        "sea_surface_wave_significant_height", "m"},
    Row{Quantity::wave_period_dominant, "wave_period_dominant",
        "sea_surface_wave_period_at_variance_spectral_density_maximum", "s"},
    Row{Quantity::wave_period_average, "wave_period_average",
        "sea_surface_wave_mean_period", "s"},
    Row{Quantity::wave_direction, "wave_direction",
        "sea_surface_wave_from_direction", "degree"},
    Row{Quantity::discharge, "discharge",
        "water_volume_transport_in_river_channel", "m3 s-1"},
};

constexpr bool info_matches_the_sn_table() {
  for (const Row& row : sn_registry) {
    const QuantityInfo i = info(row.quantity);
    if (i.token != row.token or i.standard_name != row.standard_name or
        i.canonical_unit != row.units or i.long_name.empty()) {
      return false;
    }
  }
  return true;
}

constexpr bool tokens_round_trip() {
  for (const Row& row : sn_registry) {
    if (parse_quantity_token(info(row.quantity).token) != row.quantity) {
      return false;
    }
  }
  return true;
}

// Registry order is enumerator order, with no gaps: the table is indexed by
// the enumerator.
constexpr bool registry_is_in_enumerator_order() {
  for (std::size_t i = 0; i < sn_registry.size(); ++i) {
    if (static_cast<std::size_t>(sn_registry[i].quantity) != i) {
      return false;
    }
  }
  return sn_registry.size() ==
         static_cast<std::size_t>(Quantity::discharge) + 1;
}

constexpr bool only_water_levels_take_a_datum() {
  for (const Row& row : sn_registry) {
    const bool is_water_level =
        row.quantity == Quantity::water_level or
        row.quantity == Quantity::water_level_prediction;
    if (datum_applicable(QuantityId{row.quantity}) != is_water_level) {
      return false;
    }
  }
  return true;
}

template <class T>
concept TokenCallable =
    requires(T&& q) { mov::core::token(std::forward<T>(q)); };

}  // namespace

TEST_CASE("quantity types are value types", "[core][quantity][constexpr]") {
  STATIC_REQUIRE(std::regular<Quantity>);
  STATIC_REQUIRE(std::regular<QuantityId>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<QuantityId>);
  STATIC_REQUIRE(std::copyable<GenericQuantity>);
  STATIC_REQUIRE(std::equality_comparable<GenericQuantity>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<GenericQuantity>);
  STATIC_REQUIRE(std::regular<QuantityInfo>);
}

TEST_CASE("GenericQuantity can only be built by parsing",
          "[core][quantity][constexpr]") {
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<GenericQuantity>);
  STATIC_REQUIRE_FALSE(
      std::is_constructible_v<GenericQuantity, std::string_view,
                              std::string_view>);
  STATIC_REQUIRE_FALSE(std::is_constructible_v<GenericQuantity, const char*>);
  STATIC_REQUIRE_FALSE(std::is_convertible_v<std::string_view, QuantityId>);
}

TEST_CASE("the registry matches SN section 6", "[core][quantity][constexpr]") {
  STATIC_REQUIRE(registry_is_in_enumerator_order());
  STATIC_REQUIRE(info_matches_the_sn_table());
}

TEST_CASE("quantity tokens round-trip", "[core][quantity][constexpr]") {
  STATIC_REQUIRE(tokens_round_trip());
  STATIC_REQUIRE(parse_quantity_token("water_level") == Quantity::water_level);
  STATIC_REQUIRE(parse_quantity_token("wave_period_dominant") ==
                 Quantity::wave_period_dominant);
  STATIC_REQUIRE(parse_quantity_token("discharge") == Quantity::discharge);
}

TEST_CASE("parse_quantity_token rejects anything but a registry token",
          "[core][quantity][constexpr]") {
  STATIC_REQUIRE_FALSE(parse_quantity_token("").has_value());
  // `value` is the generic quantity, not a registry entry.
  STATIC_REQUIRE_FALSE(parse_quantity_token("value").has_value());
  STATIC_REQUIRE_FALSE(parse_quantity_token("Water_Level").has_value());
  STATIC_REQUIRE_FALSE(parse_quantity_token("water_level ").has_value());
  STATIC_REQUIRE_FALSE(parse_quantity_token("water_leve").has_value());
  STATIC_REQUIRE_FALSE(parse_quantity_token("wind").has_value());
  STATIC_REQUIRE_FALSE(
      parse_quantity_token(std::string_view{"wind_u\0", 7}).has_value());
}

TEST_CASE("datum_applicable truth table", "[core][quantity][constexpr]") {
  STATIC_REQUIRE(only_water_levels_take_a_datum());
  STATIC_REQUIRE(datum_applicable(QuantityId{Quantity::water_level}));
  STATIC_REQUIRE(
      datum_applicable(QuantityId{Quantity::water_level_prediction}));
  STATIC_REQUIRE_FALSE(datum_applicable(QuantityId{Quantity::wind_speed}));
  STATIC_REQUIRE_FALSE(datum_applicable(QuantityId{Quantity::air_pressure}));
  STATIC_REQUIRE_FALSE(datum_applicable(QuantityId{Quantity::visibility}));
}

TEST_CASE("token() of a QuantityId rejects temporaries",
          "[core][quantity][constexpr]") {
  STATIC_REQUIRE(TokenCallable<const QuantityId&>);
  STATIC_REQUIRE(TokenCallable<QuantityId&>);
  STATIC_REQUIRE_FALSE(TokenCallable<QuantityId>);
}
