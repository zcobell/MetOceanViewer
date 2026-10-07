// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for mov/core/quantity.hpp.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <concepts>
#include <cstddef>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include "mov/core/quantity.hpp"
#include "mov/test/toolchain.hpp"

using mov::core::datum_applicable;
using mov::core::GenericQuantity;
using mov::core::info;
using mov::core::parse_quantity_token;
using mov::core::Quantity;
using mov::core::QuantityId;
using mov::core::QuantityInfo;
using mov::core::token;

namespace {

// One row of the quantity registry table, docs/station-netcdf.md section 6,
// written out independently of the implementation.
struct Row {
  Quantity quantity;
  std::string_view token;
  std::string_view standard_name;
  std::string_view units;
};

constexpr Row row(Quantity quantity, std::string_view token,
                  std::string_view standard_name, std::string_view units) {
  return {.quantity = quantity,
          .token = token,
          .standard_name = standard_name,
          .units = units};
}

constexpr std::array sn_registry{
    row(Quantity::water_level, "water_level",
        "water_surface_height_above_reference_datum", "m"),
    row(Quantity::water_level_prediction, "water_level_prediction",
        "water_surface_height_above_reference_datum", "m"),
    row(Quantity::air_temperature, "air_temperature", "air_temperature",
        "degC"),
    row(Quantity::water_temperature, "water_temperature",
        "sea_water_temperature", "degC"),
    row(Quantity::dew_point, "dew_point", "dew_point_temperature", "degC"),
    row(Quantity::wind_speed, "wind_speed", "wind_speed", "m s-1"),
    row(Quantity::wind_direction, "wind_direction", "wind_from_direction",
        "degree"),
    row(Quantity::wind_gust, "wind_gust", "wind_speed_of_gust", "m s-1"),
    row(Quantity::wind_u, "wind_u", "eastward_wind", "m s-1"),
    row(Quantity::wind_v, "wind_v", "northward_wind", "m s-1"),
    row(Quantity::air_pressure, "air_pressure", "air_pressure", "hPa"),
    row(Quantity::relative_humidity, "relative_humidity", "relative_humidity",
        "percent"),
    row(Quantity::conductivity, "conductivity",
        "sea_water_electrical_conductivity", "S m-1"),
    row(Quantity::visibility, "visibility", "visibility_in_air", "m"),
    row(Quantity::current_u, "current_u", "eastward_sea_water_velocity",
        "m s-1"),
    row(Quantity::current_v, "current_v", "northward_sea_water_velocity",
        "m s-1"),
    row(Quantity::wave_height, "wave_height",
        "sea_surface_wave_significant_height", "m"),
    row(Quantity::wave_period_dominant, "wave_period_dominant",
        "sea_surface_wave_period_at_variance_spectral_density_maximum", "s"),
    row(Quantity::wave_period_average, "wave_period_average",
        "sea_surface_wave_mean_period", "s"),
    row(Quantity::wave_direction, "wave_direction",
        "sea_surface_wave_from_direction", "degree"),
    row(Quantity::discharge, "discharge",
        "water_volume_transport_in_river_channel", "m3 s-1"

        )};

constexpr bool info_matches_the_sn_table() {
  return std::ranges::all_of(sn_registry, [](const Row& r) {
    const QuantityInfo i = info(r.quantity);
    return i.token == r.token and i.standard_name == r.standard_name and
           i.canonical_unit == r.units and not i.long_name.empty();
  });
}

constexpr bool tokens_round_trip() {
  return std::ranges::all_of(sn_registry, [](const Row& r) {
    return parse_quantity_token(info(r.quantity).token) == r.quantity;
  });
}

// Registry order is enumerator order, with no gaps: the table is indexed by
// the enumerator.
constexpr bool registry_is_in_enumerator_order() {
  return sn_registry.size() ==
             static_cast<std::size_t>(Quantity::discharge) + 1 and
         std::ranges::all_of(sn_registry, [](const Row& r) {
           return &r - sn_registry.data() ==
                  static_cast<std::ptrdiff_t>(r.quantity);
         });
}

constexpr bool only_water_levels_take_a_datum() {
  return std::ranges::all_of(sn_registry, [](const Row& r) {
    const bool is_water_level = r.quantity == Quantity::water_level or
                                r.quantity == Quantity::water_level_prediction;
    return datum_applicable(QuantityId{r.quantity}) == is_water_level;
  });
}

template <class T>
concept TokenCallable =
    requires(T&& q) { mov::core::token(std::forward<T>(q)); };

}  // namespace

TEST_CASE("quantity types are value types", "[core][quantity][constexpr]") {
  STATIC_REQUIRE(std::regular<Quantity>);
  STATIC_REQUIRE(std::regular<QuantityId>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<QuantityId>);
  STATIC_REQUIRE(std::regular<GenericQuantity>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<GenericQuantity>);
  STATIC_REQUIRE(std::regular<QuantityInfo>);
}

TEST_CASE("GenericQuantity is the default or comes from parsing",
          "[core][quantity][constexpr]") {
  STATIC_REQUIRE(std::is_default_constructible_v<GenericQuantity>);
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
  MOV_STATIC_REQUIRE_VARIANT(only_water_levels_take_a_datum());
  MOV_STATIC_REQUIRE_VARIANT(
      datum_applicable(QuantityId{Quantity::water_level}));
  MOV_STATIC_REQUIRE_VARIANT(
      datum_applicable(QuantityId{Quantity::water_level_prediction}));
  MOV_STATIC_REQUIRE_FALSE_VARIANT(
      datum_applicable(QuantityId{Quantity::wind_speed}));
  MOV_STATIC_REQUIRE_FALSE_VARIANT(
      datum_applicable(QuantityId{Quantity::air_pressure}));
  MOV_STATIC_REQUIRE_FALSE_VARIANT(
      datum_applicable(QuantityId{Quantity::visibility}));
}

TEST_CASE("token() of a QuantityId rejects temporaries",
          "[core][quantity][constexpr]") {
  STATIC_REQUIRE(TokenCallable<const QuantityId&>);
  STATIC_REQUIRE(TokenCallable<QuantityId&>);
  STATIC_REQUIRE_FALSE(TokenCallable<QuantityId>);
}

TEST_CASE("QuantityId starts with the generic alternative",
          "[core][quantity][constexpr]") {
  STATIC_REQUIRE(
      std::same_as<std::variant_alternative_t<0, QuantityId>, GenericQuantity>);
  STATIC_REQUIRE(
      std::same_as<std::variant_alternative_t<1, QuantityId>, Quantity>);
  STATIC_REQUIRE(std::variant_size_v<QuantityId> == 2);
}

TEST_CASE("token() of a registry quantity is constexpr",
          "[core][quantity][constexpr]") {
  STATIC_REQUIRE(token(Quantity::wind_u) == "wind_u");
  STATIC_REQUIRE(token(Quantity::discharge) == "discharge");
  // A Quantity and a QuantityId in a variable agree.
  MOV_CONSTEXPR_VARIANT QuantityId id{Quantity::wind_v};
  MOV_STATIC_REQUIRE_VARIANT(token(id) == "wind_v");
  MOV_STATIC_REQUIRE_VARIANT(token(id) == token(Quantity::wind_v));
  STATIC_REQUIRE(TokenCallable<Quantity>);
}
