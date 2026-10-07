// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <compare>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/units.hpp"

using mov::core::Affine;
using mov::core::conversion;
using mov::core::DischargeUnit;
using mov::core::IncompatibleUnits;
using mov::core::is_canonical_other;
using mov::core::Length;
using mov::core::LengthUnit;
using mov::core::OtherUnit;
using mov::core::parse_unit;
using mov::core::PressureUnit;
using mov::core::SpeedUnit;
using mov::core::symbol;
using mov::core::TemperatureUnit;
using mov::core::udunits;
using mov::core::Unit;

namespace {

struct Spelling {
  std::string_view text;
  Unit expected;
};

// The spelling table of docs/core-design.md section 2.2, pinned.
const std::vector<Spelling>& design_table() {
  static const std::vector<Spelling> table{
      {"m", LengthUnit::meter},
      {"meter", LengthUnit::meter},
      {"meters", LengthUnit::meter},
      {"metre", LengthUnit::meter},
      {"metres", LengthUnit::meter},
      {"ft", LengthUnit::foot},
      {"feet", LengthUnit::foot},
      {"foot", LengthUnit::foot},
      {"m/s", SpeedUnit::meter_per_second},
      {"m s-1", SpeedUnit::meter_per_second},
      {"knots", SpeedUnit::knot},
      {"kn", SpeedUnit::knot},
      {"kt", SpeedUnit::knot},
      {"mph", SpeedUnit::mile_per_hour},
      {"ft/s", SpeedUnit::foot_per_second},
      {"km/h", SpeedUnit::kilometer_per_hour},
      {"degC", TemperatureUnit::celsius},
      {"deg C", TemperatureUnit::celsius},
      {"\xC2\xB0"
       "C",
       TemperatureUnit::celsius},
      {"C", TemperatureUnit::celsius},
      {"degF", TemperatureUnit::fahrenheit},
      {"\xC2\xB0"
       "F",
       TemperatureUnit::fahrenheit},
      {"F", TemperatureUnit::fahrenheit},
      {"Pa", PressureUnit::pascal},
      {"hPa", PressureUnit::hectopascal},
      {"mb", PressureUnit::millibar},
      {"mbar", PressureUnit::millibar},
      {"mH2O", PressureUnit::meter_of_water},
      {"km", LengthUnit::kilometer},
      {"mi", LengthUnit::statute_mile},
      {"nmi", LengthUnit::nautical_mile},
      {"m3/s", DischargeUnit::cubic_meter_per_second},
      {"m3 s-1", DischargeUnit::cubic_meter_per_second},
      {"ft3/s", DischargeUnit::cubic_foot_per_second},
      {"ft3 s-1", DischargeUnit::cubic_foot_per_second},
      {"cfs", DischargeUnit::cubic_foot_per_second},
  };
  return table;
}

// One value of every enumerator.
std::vector<Unit> all_units() {
  return {
      LengthUnit::meter,
      LengthUnit::foot,
      LengthUnit::inch,
      LengthUnit::kilometer,
      LengthUnit::statute_mile,
      LengthUnit::nautical_mile,
      SpeedUnit::meter_per_second,
      SpeedUnit::foot_per_second,
      SpeedUnit::knot,
      SpeedUnit::mile_per_hour,
      SpeedUnit::kilometer_per_hour,
      PressureUnit::pascal,
      PressureUnit::hectopascal,
      PressureUnit::millibar,
      PressureUnit::meter_of_water,
      DischargeUnit::cubic_meter_per_second,
      DischargeUnit::cubic_foot_per_second,
      TemperatureUnit::celsius,
      TemperatureUnit::fahrenheit,
  };
}

// symbol() and udunits() refuse temporaries (their result may view the
// argument), so the tests go through a named parameter.
std::string sym(const Unit& u) { return std::string{symbol(u)}; }
std::string ud(const Unit& u) { return std::string{udunits(u)}; }

std::string other_symbol(std::string_view text) {
  const auto unit = parse_unit(text);
  REQUIRE(unit.has_value());
  const auto* other = std::get_if<OtherUnit>(&*unit);
  REQUIRE(other != nullptr);
  return std::string{other->symbol()};
}

}  // namespace

// Not constexpr: Clang rejects an operation that produces NaN in a constant
// expression.
TEST_CASE("Measure order is partial: NaN is unordered, like double",
          "[core][units]") {
  const Length nan =
      Length::in(std::numeric_limits<double>::quiet_NaN(), LengthUnit::meter);
  const Length meter = Length::in(1.0, LengthUnit::meter);
  CHECK((nan <=> meter) == std::partial_ordering::unordered);
  CHECK_FALSE(nan < meter);
  CHECK_FALSE(nan >= meter);
  CHECK_FALSE(nan == nan);
}

TEST_CASE("parse_unit accepts every documented spelling",
          "[core][units][parse]") {
  for (const auto& [text, expected] : design_table()) {
    INFO("spelling: " << text);
    CHECK(parse_unit(text) == std::optional<Unit>{expected});
  }
}

TEST_CASE("parse_unit of nothing is nullopt, not an unknown unit",
          "[core][units][parse]") {
  CHECK_FALSE(parse_unit("").has_value());
  CHECK_FALSE(parse_unit(" ").has_value());
  CHECK_FALSE(parse_unit("   \t\r\n").has_value());
}

TEST_CASE("parse_unit ignores surrounding and repeated whitespace",
          "[core][units][parse]") {
  CHECK(parse_unit("  m/s\t") ==
        std::optional<Unit>{SpeedUnit::meter_per_second});
  CHECK(parse_unit("m   s-1") ==
        std::optional<Unit>{SpeedUnit::meter_per_second});
  CHECK(parse_unit("\tdeg   C\n") ==
        std::optional<Unit>{TemperatureUnit::celsius});
  CHECK(other_symbol("  nautical   furlongs ") == "nautical furlongs");
}

TEST_CASE("parse_unit is case-sensitive for symbols, not for words",
          "[core][units][parse]") {
  CHECK(parse_unit("Meters") == std::optional<Unit>{LengthUnit::meter});
  CHECK(parse_unit("FEET") == std::optional<Unit>{LengthUnit::foot});
  CHECK(parse_unit("Knots") == std::optional<Unit>{SpeedUnit::knot});
  // mb is millibar; Mb is not, and PA is not a pascal.
  CHECK(other_symbol("Mb") == "Mb");
  CHECK(other_symbol("PA") == "PA");
  CHECK(other_symbol("M") == "M");
  CHECK(other_symbol("c") == "c");
}

TEST_CASE("parse_unit canonicalizes OtherUnit aliases",
          "[core][units][parse]") {
  CHECK(other_symbol("%") == "percent");
  CHECK(other_symbol("percent") == "percent");
  CHECK(other_symbol("deg") == "degree");
  CHECK(other_symbol("degT") == "degree");
  CHECK(other_symbol("degree") == "degree");
  CHECK(other_symbol("sec") == "s");
  CHECK(other_symbol("s") == "s");
  // Aliases produce equal units.
  CHECK(parse_unit("deg") == parse_unit("degT"));
  CHECK(parse_unit("%") == parse_unit("percent"));
}

TEST_CASE("parse_unit keeps an unknown unit as an OtherUnit",
          "[core][units][parse]") {
  CHECK(other_symbol("furlong") == "furlong");
  CHECK(other_symbol("S m-1") == "S m-1");
  CHECK(other_symbol("m s-2") == "m s-2");
  // Not a name collision with the families: "mi" is a mile, "mi/h" is not.
  CHECK(other_symbol("mi/h") == "mi/h");
  CHECK(parse_unit("furlong") == parse_unit(" furlong "));
  CHECK_FALSE(parse_unit("furlong") == parse_unit("rod"));
}

TEST_CASE("is_canonical_other names the registry's non-family units",
          "[core][units][parse]") {
  for (const char* text :
       {"percent", "%", "degree", "deg", "degT", "s", "sec", "S m-1"}) {
    INFO("unit: " << text);
    const auto unit = parse_unit(text);
    REQUIRE(unit.has_value());
    CHECK(is_canonical_other(std::get<OtherUnit>(*unit)));
  }
  for (const char* text : {"furlong", "Mb", "m s-2"}) {
    INFO("unit: " << text);
    const auto unit = parse_unit(text);
    REQUIRE(unit.has_value());
    CHECK_FALSE(is_canonical_other(std::get<OtherUnit>(*unit)));
  }
}

TEST_CASE("symbol and udunits name every unit", "[core][units][symbol]") {
  CHECK(sym(Unit{LengthUnit::meter}) == "m");
  CHECK(sym(Unit{LengthUnit::foot}) == "ft");
  CHECK(sym(Unit{SpeedUnit::meter_per_second}) == "m/s");
  CHECK(sym(Unit{SpeedUnit::knot}) == "kt");
  CHECK(sym(Unit{PressureUnit::hectopascal}) == "hPa");
  CHECK(sym(Unit{DischargeUnit::cubic_meter_per_second}) == "m3/s");
  CHECK(sym(Unit{TemperatureUnit::celsius}) ==
        "\xC2\xB0"
        "C");
  CHECK(ud(Unit{LengthUnit::meter}) == "m");
  CHECK(ud(Unit{SpeedUnit::meter_per_second}) == "m s-1");
  CHECK(ud(Unit{PressureUnit::hectopascal}) == "hPa");
  CHECK(ud(Unit{DischargeUnit::cubic_meter_per_second}) == "m3 s-1");
  CHECK(ud(Unit{TemperatureUnit::celsius}) == "degC");

  const Unit percent = *parse_unit("%");
  CHECK(sym(percent) == "percent");
  CHECK(ud(percent) == "percent");
}

TEST_CASE("symbol and udunits are parse_unit's inverse",
          "[core][units][symbol]") {
  for (const Unit& unit : all_units()) {
    INFO("symbol: " << sym(unit) << ", udunits: " << ud(unit));
    CHECK(parse_unit(sym(unit)) == std::optional<Unit>{unit});
    CHECK(parse_unit(ud(unit)) == std::optional<Unit>{unit});
  }
  for (const char* text : {"percent", "degree", "s", "S m-1", "furlong"}) {
    const Unit unit = *parse_unit(text);
    CHECK(parse_unit(sym(unit)) == std::optional<Unit>{unit});
  }
}

TEST_CASE("symbols are distinct", "[core][units][symbol]") {
  const std::vector<Unit> units = all_units();
  for (std::size_t i = 0; i < units.size(); ++i) {
    for (std::size_t j = i + 1; j < units.size(); ++j) {
      INFO(sym(units[i]) << " vs " << sym(units[j]));
      CHECK(sym(units[i]) != sym(units[j]));
      CHECK(ud(units[i]) != ud(units[j]));
    }
  }
}

TEST_CASE("conversion over the Unit variant", "[core][units][conversion]") {
  const auto foot_to_meter =
      conversion(Unit{LengthUnit::foot}, Unit{LengthUnit::meter});
  REQUIRE(foot_to_meter.has_value());
  CHECK(*foot_to_meter == Affine{.scale = 0.3048, .offset = 0.0});

  const auto c_to_f = conversion(Unit{TemperatureUnit::celsius},
                                 Unit{TemperatureUnit::fahrenheit});
  REQUIRE(c_to_f.has_value());
  CHECK((*c_to_f)(100.0) == 212.0);
}

TEST_CASE("conversion between families is an error",
          "[core][units][conversion]") {
  const Unit length{LengthUnit::meter};
  const Unit speed{SpeedUnit::meter_per_second};
  const auto bad = conversion(length, speed);
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error() == IncompatibleUnits{.from = length, .to = speed});

  // A temperature is not convertible to a length, nor to an unknown unit.
  CHECK_FALSE(conversion(Unit{TemperatureUnit::celsius}, length).has_value());
  CHECK_FALSE(conversion(length, Unit{TemperatureUnit::celsius}).has_value());
  CHECK_FALSE(conversion(Unit{PressureUnit::pascal}, Unit{LengthUnit::meter})
                  .has_value());
}

TEST_CASE("conversion of an OtherUnit is the identity iff the units are equal",
          "[core][units][conversion]") {
  const Unit furlong = *parse_unit("furlong");
  const Unit rod = *parse_unit("rod");
  const auto same = conversion(furlong, *parse_unit(" furlong"));
  REQUIRE(same.has_value());
  CHECK(*same == Affine{});

  const auto different = conversion(furlong, rod);
  REQUIRE_FALSE(different.has_value());
  CHECK(different.error() == IncompatibleUnits{.from = furlong, .to = rod});

  CHECK_FALSE(conversion(furlong, Unit{LengthUnit::meter}).has_value());
  CHECK_FALSE(conversion(Unit{LengthUnit::meter}, furlong).has_value());
  // percent and degree do not convert into each other.
  CHECK_FALSE(conversion(*parse_unit("%"), *parse_unit("deg")).has_value());
}

TEST_CASE("conversion over the Unit variant is the identity for equal units",
          "[core][units][conversion]") {
  for (const Unit& unit : all_units()) {
    const auto same = conversion(unit, unit);
    REQUIRE(same.has_value());
    CHECK(*same == Affine{});
  }
}
