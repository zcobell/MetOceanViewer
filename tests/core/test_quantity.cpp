// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/quantity.hpp"
#include "mov/core/units.hpp"

using mov::core::canonical_unit;
using mov::core::datum_applicable;
using mov::core::DischargeUnit;
using mov::core::GenericQuantity;
using mov::core::info;
using mov::core::is_canonical_other;
using mov::core::LengthUnit;
using mov::core::OtherUnit;
using mov::core::parse_unit;
using mov::core::PressureUnit;
using mov::core::Quantity;
using mov::core::QuantityId;
using mov::core::SpeedUnit;
using mov::core::TemperatureUnit;
using mov::core::token;
using mov::core::Unit;

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
                                    Quantity::discharge,
                                    Quantity::difference};

// The registry quantities that have a fixed unit (all but `difference`).
std::vector<Quantity> fixed_unit_quantities() {
  std::vector<Quantity> out;
  std::ranges::copy_if(all_quantities, std::back_inserter(out),
                       [](Quantity q) { return q != Quantity::difference; });
  return out;
}

GenericQuantity generic(std::string_view tok, std::string_view standard = "") {
  const auto parsed =
      GenericQuantity::parse({.token = tok, .standard_name = standard});
  REQUIRE(parsed.has_value());
  return parsed.value_or(GenericQuantity::value());
}

}  // namespace

TEST_CASE("a default GenericQuantity and QuantityId are the unknown quantity",
          "[core][quantity]") {
  CHECK(GenericQuantity{} == GenericQuantity::value());
  const GenericQuantity unknown;
  CHECK(unknown.token() == "value");
  // A default QuantityId is the generic `value`, never a registry entry.
  const QuantityId id{};
  CHECK(id == QuantityId{GenericQuantity::value()});
  CHECK(id.index() == 0);
  CHECK(token(id) == "value");
  CHECK(datum_applicable(id));
}

TEST_CASE("GenericQuantity::value is the quantity of unknown series",
          "[core][quantity]") {
  const GenericQuantity value = GenericQuantity::value();
  CHECK(value.token() == "value");
  CHECK(value.standard_name().empty());
  CHECK(value == GenericQuantity::value());
  // Parsing the same token and no standard name gives the same quantity.
  CHECK(GenericQuantity::parse({.token = "value", .standard_name = ""}) ==
        value);
}

TEST_CASE("GenericQuantity::parse keeps the token and standard name",
          "[core][quantity]") {
  const auto ph = GenericQuantity::parse(
      {.token = "ph", .standard_name = "sea_water_ph_reported_on_total_scale"});
  REQUIRE(ph.has_value());
  CHECK((ph and ph->token() == "ph"));
  CHECK((ph and ph->standard_name() == "sea_water_ph_reported_on_total_scale"));
  CHECK(GenericQuantity::parse({.token = "Chl_a2", .standard_name = ""})
            .has_value());
  CHECK(
      GenericQuantity::parse({.token = "x", .standard_name = ""}).has_value());
  // Identity is the token and the standard name together.
  CHECK(generic("ph", "a") == generic("ph", "a"));
  CHECK(not(generic("ph", "a") == generic("ph", "b")));
  CHECK(not(generic("ph", "a") == generic("pH", "a")));
}

TEST_CASE("GenericQuantity::parse rejects registry tokens",
          "[core][quantity]") {
  // The caller uses the registry entry instead.
  for (const Quantity q : all_quantities) {
    INFO("token: " << info(q).token);
    CHECK(not(
        GenericQuantity::parse({.token = info(q).token, .standard_name = ""})
            .has_value()));
    CHECK(not(GenericQuantity::parse(
                  {.token = info(q).token, .standard_name = "anything"})
                  .has_value()));
  }
}

TEST_CASE("GenericQuantity::parse rejects bad names", "[core][quantity]") {
  for (const std::string_view bad :
       {"", "1abc", "_x", "a-b", "a b", "a.b", "a/b", "a:b", " a", "a ",
        "ph\xC3\xA9", "\xC3\xA9_"}) {
    INFO("token: " << bad);
    CHECK(not(GenericQuantity::parse({.token = bad, .standard_name = ""})
                  .has_value()));
  }
  CHECK(not GenericQuantity::parse(
                {.token = std::string_view{"a\0b", 3}, .standard_name = ""})
                .has_value());
  // Case-sensitive: a registry token in another case is a different name.
  CHECK(GenericQuantity::parse({.token = "Water_Level", .standard_name = ""})
            .has_value());
}

TEST_CASE("QuantityId holds either a registry entry or a generic quantity",
          "[core][quantity]") {
  const QuantityId registry{Quantity::wind_speed};
  const QuantityId other{generic("ph")};
  CHECK(token(registry) == "wind_speed");
  CHECK(token(other) == "ph");
  const QuantityId unknown{GenericQuantity::value()};
  CHECK(token(unknown) == "value");
  CHECK(not(registry == other));
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
  CHECK(not(datum_applicable(QuantityId{Quantity::wave_height})));
  // A difference is not a water level, whatever it was computed from.
  CHECK(not(datum_applicable(QuantityId{Quantity::difference})));
}

TEST_CASE("difference is a registry quantity with no standard name or unit",
          "[core][quantity]") {
  CHECK(mov::core::parse_quantity_token("difference") == Quantity::difference);
  CHECK(token(Quantity::difference) == "difference");
  CHECK(info(Quantity::difference).standard_name.empty());
  CHECK(not info(Quantity::difference).long_name.empty());
  CHECK(info(Quantity::difference).canonical_unit.empty());
  CHECK(canonical_unit(Quantity::difference) == std::nullopt);
  // Not an OtherUnit with an empty symbol either.
  CHECK(not GenericQuantity::parse({.token = "difference", .standard_name = ""})
                .has_value());
}

TEST_CASE("every registry quantity has a parseable canonical unit",
          "[core][quantity]") {
  for (const Quantity q : fixed_unit_quantities()) {
    INFO("quantity: " << info(q).token << ", unit: " << info(q).canonical_unit);
    const auto unit = parse_unit(info(q).canonical_unit);
    REQUIRE(unit.has_value());
    // A unit outside the families must be one the readers recognize.
    if (const auto* other = unit ? std::get_if<OtherUnit>(&*unit) : nullptr) {
      CHECK(is_canonical_other(*other));
    }
    // canonical_unit agrees with parse_unit.
    CHECK(canonical_unit(q) == unit);
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
    CHECK(not(i.token.empty()));
    CHECK((i.standard_name.empty() == (q == Quantity::difference)));
    CHECK(not(i.long_name.empty()));
    // Every token is itself a legal NetCDF-style name: lower case and "_".
    for (const char c : i.token) {
      CHECK(((c >= 'a' and c <= 'z') or c == '_'));
    }
  }
}

TEST_CASE("generic quantities with one token are different values",
          "[core][quantity]") {
  // == compares token and standard name; collision checks key on token().
  const GenericQuantity a =
      generic("ph", "sea_water_ph_reported_on_total_scale");
  const GenericQuantity b = generic("ph", "");
  CHECK_FALSE(a == b);
  CHECK(a.token() == b.token());
}

TEST_CASE("token(Quantity) is the registry token", "[core][quantity]") {
  for (const Quantity q : all_quantities) {
    CHECK(token(q) == info(q).token);
    const QuantityId id{q};
    CHECK(token(id) == token(q));
  }
}

TEST_CASE("canonical_unit parses the registry's unit", "[core][quantity]") {
  for (const Quantity q : fixed_unit_quantities()) {
    INFO("quantity: " << info(q).token);
    CHECK(canonical_unit(q) == parse_unit(info(q).canonical_unit));
  }
  CHECK(canonical_unit(Quantity::water_level) ==
        std::optional<Unit>{LengthUnit::meter});
  CHECK(canonical_unit(Quantity::air_pressure) ==
        std::optional<Unit>{PressureUnit::hectopascal});
  CHECK(canonical_unit(Quantity::air_temperature) ==
        std::optional<Unit>{TemperatureUnit::celsius});
}

TEST_CASE("is_canonical_other is derived from the registry",
          "[core][quantity]") {
  // The registry's non-family units, and their aliases, are canonical.
  for (const char* text : {"percent", "%", "degree", "deg", "degT", "degrees",
                           "degrees_true", "s", "sec", "S m-1"}) {
    INFO("unit: " << text);
    const auto unit = parse_unit(text);
    const auto* other = unit ? std::get_if<OtherUnit>(&*unit) : nullptr;
    REQUIRE(other != nullptr);
    CHECK(is_canonical_other(*other));
  }
  for (const char* text : {"furlong", "Mb", "m s-2", "kelvin"}) {
    INFO("unit: " << text);
    const auto unit = parse_unit(text);
    const auto* other = unit ? std::get_if<OtherUnit>(&*unit) : nullptr;
    REQUIRE(other != nullptr);
    CHECK(not is_canonical_other(*other));
  }
  // Every OtherUnit among the registry's canonical units is canonical.
  for (const Quantity q : fixed_unit_quantities()) {
    const std::optional<Unit> unit = canonical_unit(q);
    if (const auto* other = unit ? std::get_if<OtherUnit>(&*unit) : nullptr) {
      CHECK(is_canonical_other(*other));
    }
  }
}
