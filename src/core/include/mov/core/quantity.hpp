// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace mov::core {

/// The quantity registry (station-netcdf.md section 6): the variables the
/// providers and model readers produce. A series whose quantity is not here
/// uses GenericQuantity.
enum class Quantity : std::uint8_t {
  water_level,
  water_level_prediction,
  air_temperature,
  water_temperature,
  dew_point,
  wind_speed,
  wind_direction,
  wind_gust,
  wind_u,
  wind_v,
  air_pressure,
  relative_humidity,
  conductivity,
  visibility,
  current_u,
  current_v,
  wave_height,
  wave_period_dominant,
  wave_period_average,
  wave_direction,
  discharge,
};

/// A quantity outside the registry: SN's `value` (unknown quantity) and the
/// tokens of foreign or future files. Identity is the token (and standard
/// name). Only `value()` and `parse` build one.
class GenericQuantity {
 public:
  /// Token "value", no standard name.
  [[nodiscard]] static GenericQuantity value();

  /// `token` must match [A-Za-z][A-Za-z0-9_]* and must not be a registry
  /// token (nullopt otherwise; the caller then uses the registry entry).
  /// `standard_name` is kept as given and may be empty.
  [[nodiscard]] static std::optional<GenericQuantity> parse(
      std::string_view token, std::string_view standard_name);

  [[nodiscard]] std::string_view token() const& noexcept { return token_; }
  std::string_view token() && = delete;
  [[nodiscard]] std::string_view standard_name() const& noexcept {
    return standard_name_;
  }
  std::string_view standard_name() && = delete;

  friend bool operator==(const GenericQuantity&,
                         const GenericQuantity&) = default;

 private:
  GenericQuantity(std::string token, std::string standard_name)
      : token_{std::move(token)}, standard_name_{std::move(standard_name)} {}

  std::string token_;
  std::string standard_name_;
};

/// Default-constructed it is Quantity::water_level; build a generic one
/// explicitly.
using QuantityId = std::variant<Quantity, GenericQuantity>;

/// The registry row of a quantity.
struct QuantityInfo {
  std::string_view token;
  std::string_view standard_name;
  std::string_view long_name;
  /// Units the writers store, as netCDF text; parse_unit turns it into a Unit.
  std::string_view canonical_unit;
  friend constexpr bool operator==(const QuantityInfo&,
                                   const QuantityInfo&) = default;
};

namespace detail {

inline constexpr std::array<QuantityInfo, 21> quantity_registry{{
    {.token = "water_level",
     .standard_name = "water_surface_height_above_reference_datum",
     .long_name = "Water level",
     .canonical_unit = "m"},
    {.token = "water_level_prediction",
     .standard_name = "water_surface_height_above_reference_datum",
     .long_name = "Predicted water level",
     .canonical_unit = "m"},
    {.token = "air_temperature",
     .standard_name = "air_temperature",
     .long_name = "Air temperature",
     .canonical_unit = "degC"},
    {.token = "water_temperature",
     .standard_name = "sea_water_temperature",
     .long_name = "Water temperature",
     .canonical_unit = "degC"},
    {.token = "dew_point",
     .standard_name = "dew_point_temperature",
     .long_name = "Dew point temperature",
     .canonical_unit = "degC"},
    {.token = "wind_speed",
     .standard_name = "wind_speed",
     .long_name = "Wind speed",
     .canonical_unit = "m s-1"},
    {.token = "wind_direction",
     .standard_name = "wind_from_direction",
     .long_name = "Wind direction (from)",
     .canonical_unit = "degree"},
    {.token = "wind_gust",
     .standard_name = "wind_speed_of_gust",
     .long_name = "Wind gust speed",
     .canonical_unit = "m s-1"},
    {.token = "wind_u",
     .standard_name = "eastward_wind",
     .long_name = "Eastward wind",
     .canonical_unit = "m s-1"},
    {.token = "wind_v",
     .standard_name = "northward_wind",
     .long_name = "Northward wind",
     .canonical_unit = "m s-1"},
    {.token = "air_pressure",
     .standard_name = "air_pressure",
     .long_name = "Air pressure",
     .canonical_unit = "hPa"},
    {.token = "relative_humidity",
     .standard_name = "relative_humidity",
     .long_name = "Relative humidity",
     .canonical_unit = "percent"},
    {.token = "conductivity",
     .standard_name = "sea_water_electrical_conductivity",
     .long_name = "Conductivity",
     .canonical_unit = "S m-1"},
    {.token = "visibility",
     .standard_name = "visibility_in_air",
     .long_name = "Visibility",
     .canonical_unit = "m"},
    {.token = "current_u",
     .standard_name = "eastward_sea_water_velocity",
     .long_name = "Eastward current",
     .canonical_unit = "m s-1"},
    {.token = "current_v",
     .standard_name = "northward_sea_water_velocity",
     .long_name = "Northward current",
     .canonical_unit = "m s-1"},
    {.token = "wave_height",
     .standard_name = "sea_surface_wave_significant_height",
     .long_name = "Significant wave height",
     .canonical_unit = "m"},
    {.token = "wave_period_dominant",
     .standard_name =
         "sea_surface_wave_period_at_variance_spectral_density_maximum",
     .long_name = "Dominant wave period",
     .canonical_unit = "s"},
    {.token = "wave_period_average",
     .standard_name = "sea_surface_wave_mean_period",
     .long_name = "Average wave period",
     .canonical_unit = "s"},
    {.token = "wave_direction",
     .standard_name = "sea_surface_wave_from_direction",
     .long_name = "Wave direction (from)",
     .canonical_unit = "degree"},
    {.token = "discharge",
     .standard_name = "water_volume_transport_in_river_channel",
     .long_name = "River discharge",
     .canonical_unit = "m3 s-1"},
}};
static_assert(quantity_registry.size() ==
                  static_cast<std::size_t>(Quantity::discharge) + 1,
              "one registry row per Quantity, in enumerator order");

}  // namespace detail

/// The registry row, indexed by the enumerator.
[[nodiscard]] constexpr QuantityInfo info(Quantity q) noexcept {
  return detail::quantity_registry[static_cast<std::size_t>(q)];
}

/// The quantity with exactly this token (case-sensitive), or nullopt.
[[nodiscard]] constexpr std::optional<Quantity> parse_quantity_token(
    std::string_view token) noexcept {
  for (std::size_t i = 0; i < detail::quantity_registry.size(); ++i) {
    if (detail::quantity_registry[i].token == token) {
      return static_cast<Quantity>(i);
    }
  }
  return std::nullopt;
}

/// The token of either alternative.
[[nodiscard]] std::string_view token(const QuantityId& q) noexcept;
std::string_view token(QuantityId&&) = delete;

/// THE datum predicate (C3): only water_level, water_level_prediction and
/// generic quantities (legacy files put a datum on `value`) can carry a
/// vertical datum.
[[nodiscard]] constexpr bool datum_applicable(const QuantityId& q) noexcept {
  const Quantity* registry = std::get_if<Quantity>(&q);
  return registry == nullptr or *registry == Quantity::water_level or
         *registry == Quantity::water_level_prediction;
}

}  // namespace mov::core
