// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "mov/core/quantity.hpp"
#include "mov/core/units.hpp"

using mov::core::datum_applicable;
using mov::core::DischargeUnit;
using mov::core::GenericQuantity;
using mov::core::info;
using mov::core::is_canonical_other;
using mov::core::LengthUnit;
using mov::core::OtherUnit;
using mov::core::parse_quantity_token;
using mov::core::parse_unit;
using mov::core::PressureUnit;
using mov::core::Quantity;
using mov::core::QuantityId;
using mov::core::SpeedUnit;
using mov::core::TemperatureUnit;
using mov::core::token;

namespace {

constexpr std::array all_quantities{Quantity::water_level,
                                    Quantity::water_level_prediction,
                                    Quantity::air_temperature,
                                    Quantity::water_temperature,
                                    Quantity::dew_point,
                                    Quantity::wind_speed,
                                    Quantity::wind_direction,
                                    Quantity::wind_gust,
                                    Quantity::wind_u,
                                    Quantity::wind_v,
                                    Quantity::air_pressure,
                                    Quantity::relative_humidity,
                                    Quantity::conductivity,
                                    Quantity::visibility,
                                    Quantity::current_u,
                                    Quantity::current_v,
                                    Quantity::wave_height,
                                    Quantity::wave_period_dominant,
                                    Quantity::wave_period_average,
                                    Quantity::wave_direction,
                                    Quantity::discharge};

GenericQuantity generic(std::string_view tok, std::string_view standard = "") {
  const auto parsed = GenericQuantity::parse(tok, standard);
  REQUIRE(parsed.has_value());
  return *parsed;
}

}  // namespace

TEST_CASE("GenericQuantity::value is the quantity of unknown series",
          "[core][quantity]") {
  const GenericQuantity value = GenericQuantity::value();
  CHECK(value.token() == "value");
  CHECK(value.standard_name().empty());
  CHECK(value == GenericQuantity::value());
  // Parsing the same token and no standard name gives the same quantity.
  CHECK(GenericQuantity::parse("value", "") == value);
}

TEST_CASE("GenericQuantity::parse keeps the token and standard name",
          "[core][quantity]") {
  const auto ph =
      GenericQuantity::parse("ph", "sea_water_ph_reported_on_total_scale");
  REQUIRE(ph.has_value());
  CHECK(ph->token() == "ph");
  CHECK(ph->standard_name() == "sea_water_ph_reported_on_total_scale");
  CHECK(GenericQuantity::parse("Chl_a2", "").has_value());
  CHECK(GenericQuantity::parse("x", "").has_value());
  // Identity is the token and the standard name together.
  CHECK(generic("ph", "a") == generic("ph", "a"));
  CHECK_FALSE(generic("ph", "a") == generic("ph", "b"));
  CHECK_FALSE(generic("ph", "a") == generic("pH", "a"));
}

TEST_CASE("GenericQuantity::parse rejects registry tokens",
          "[core][quantity]") {
  // The caller uses the registry entry instead.
  for (const Quantity q : all_quantities) {
    INFO("token: " << info(q).token);
    CHECK_FALSE(GenericQuantity::parse(info(q).token, "").has_value());
    CHECK_FALSE(GenericQuantity::parse(info(q).token, "anything").has_value());
  }
}

TEST_CASE("GenericQuantity::parse rejects bad names", "[core][quantity]") {
  for (const std::string_view bad : {"", "1abc", "_x", "a-b", "a b", "a.b",
                                     "a/b", "a:b", " a", "a ", "caf\xC3\xA9",
                                     "\xC3\xA9"
                                     "a"}) {
    INFO("token: " << bad);
    CHECK_FALSE(GenericQuantity::parse(bad, "").has_value());
  }
  CHECK_FALSE(
      GenericQuantity::parse(std::string_view{"a\0b", 3}, "").has_value());
  // Case-sensitive: a registry token in another case is a different name.
  CHECK(GenericQuantity::parse("Water_Level", "").has_value());
}

TEST_CASE("QuantityId holds either a registry entry or a generic quantity",
          "[core][quantity]") {
  const QuantityId registry{Quantity::wind_speed};
  const QuantityId other{generic("ph")};
  CHECK(token(registry) == "wind_speed");
  CHECK(token(other) == "ph");
  const QuantityId unknown{GenericQuantity::value()};
  CHECK(token(unknown) == "value");
  CHECK_FALSE(registry == other);
  CHECK(registry == QuantityId{Quantity::wind_speed});
  CHECK(other == QuantityId{generic("ph")});
  for (const Quantity q : all_quantities) {
    const QuantityId id{q};
    CHECK(token(id) == info(q).token);
  }
}

TEST_CASE("datum_applicable: water levels and anything generic",
          "[core][quantity]") {
  CHECK(datum_applicable(QuantityId{GenericQuantity::value()}));
  CHECK(datum_applicable(QuantityId{generic("ph")}));
  CHECK(datum_applicable(QuantityId{Quantity::water_level}));
  CHECK_FALSE(datum_applicable(QuantityId{Quantity::wave_height}));
}

TEST_CASE("every registry quantity has a parseable canonical unit",
          "[core][quantity]") {
  for (const Quantity q : all_quantities) {
    INFO("quantity: " << info(q).token << ", unit: " << info(q).canonical_unit);
    const auto unit = parse_unit(info(q).canonical_unit);
    REQUIRE(unit.has_value());
    // A unit outside the families must be one the readers recognize.
    if (const auto* other = std::get_if<OtherUnit>(&*unit)) {
      CHECK(is_canonical_other(*other));
    }
  }
}

TEST_CASE("canonical units belong to the right family", "[core][quantity]") {
  using U = std::optional<mov::core::Unit>;
  CHECK(parse_unit(info(Quantity::water_level).canonical_unit) ==
        U{LengthUnit::meter});
  CHECK(parse_unit(info(Quantity::visibility).canonical_unit) ==
        U{LengthUnit::meter});
  CHECK(parse_unit(info(Quantity::wind_speed).canonical_unit) ==
        U{SpeedUnit::meter_per_second});
  CHECK(parse_unit(info(Quantity::current_u).canonical_unit) ==
        U{SpeedUnit::meter_per_second});
  CHECK(parse_unit(info(Quantity::air_pressure).canonical_unit) ==
        U{PressureUnit::hectopascal});
  CHECK(parse_unit(info(Quantity::air_temperature).canonical_unit) ==
        U{TemperatureUnit::celsius});
  CHECK(parse_unit(info(Quantity::discharge).canonical_unit) ==
        U{DischargeUnit::cubic_meter_per_second});
  CHECK(parse_unit(info(Quantity::relative_humidity).canonical_unit) ==
        parse_unit("%"));
  CHECK(parse_unit(info(Quantity::wind_direction).canonical_unit) ==
        parse_unit("deg"));
  CHECK(parse_unit(info(Quantity::wave_period_average).canonical_unit) ==
        parse_unit("sec"));
}

TEST_CASE("registry tokens and standard names are well formed",
          "[core][quantity]") {
  for (const Quantity q : all_quantities) {
    const auto i = info(q);
    INFO("token: " << i.token);
    CHECK_FALSE(i.token.empty());
    CHECK_FALSE(i.standard_name.empty());
    CHECK_FALSE(i.long_name.empty());
    // Every token is itself a legal NetCDF-style name: lower case and "_".
    for (const char c : i.token) {
      CHECK(((c >= 'a' and c <= 'z') or c == '_'));
    }
  }
}
