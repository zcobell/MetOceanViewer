// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// STATIC_REQUIRE checks for the compile-time half of mov/core/units.hpp.

#include <algorithm>
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <compare>
#include <concepts>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>

#include "mov/core/units.hpp"
#include "mov/test/toolchain.hpp"
#include "test_helpers.hpp"

using mov::core::Affine;
using mov::core::conversion;
using mov::core::Discharge;
using mov::core::DischargeUnit;
using mov::core::is_temperature;
using mov::core::Length;
using mov::core::LengthUnit;
using mov::core::OtherUnit;
using mov::core::Pressure;
using mov::core::PressureUnit;
using mov::core::Speed;
using mov::core::SpeedUnit;
using mov::core::TemperatureUnit;
using mov::core::Unit;
using mov::core::UnknownUnit;
using mov::test::near;
using mov::test::near_abs;

namespace {

constexpr std::array all_lengths{
    LengthUnit::meter,     LengthUnit::foot,         LengthUnit::inch,
    LengthUnit::kilometer, LengthUnit::statute_mile, LengthUnit::nautical_mile};
constexpr std::array all_speeds{
    SpeedUnit::meter_per_second, SpeedUnit::foot_per_second, SpeedUnit::knot,
    SpeedUnit::mile_per_hour, SpeedUnit::kilometer_per_hour};
constexpr std::array all_pressures{
    PressureUnit::pascal, PressureUnit::hectopascal, PressureUnit::millibar,
    PressureUnit::meter_of_water};
constexpr std::array all_discharges{DischargeUnit::cubic_meter_per_second,
                                    DischargeUnit::cubic_foot_per_second};
constexpr std::array all_temperatures{TemperatureUnit::celsius,
                                      TemperatureUnit::fahrenheit};

constexpr std::array sample_values{1.0, -3.7, 1234.5678, 0.0, 1.0e-9, 2.5e7};
// No tiny values: an affine temperature conversion adds 32, which swamps them.
constexpr std::array conversion_values{1.0, -3.7, 1234.5678, 0.0, 2.5e7};

// in(x, u).as(u) returns x within 1e-12 (relative), for every enumerator.
template <class M, class U, std::size_t N>
constexpr bool round_trips(const std::array<U, N>& units) {
  for (const U unit : units) {
    for (const double x : sample_values) {
      const double back = M::in(x, unit).as(unit);
      if (not(x == 0.0 ? back == 0.0 : near(back, x))) {
        return false;
      }
    }
  }
  return true;
}

// in(1, u).as(base) is the SI factor, bit for bit.
constexpr bool is_factor(LengthUnit u, double si) {
  return Length::in(1.0, u).as(LengthUnit::meter) == si;
}
constexpr bool is_factor(SpeedUnit u, double si) {
  return Speed::in(1.0, u).as(SpeedUnit::meter_per_second) == si;
}
constexpr bool is_factor(PressureUnit u, double si) {
  return Pressure::in(1.0, u).as(PressureUnit::pascal) == si;
}
constexpr bool is_factor(DischargeUnit u, double si) {
  return Discharge::in(1.0, u).as(DischargeUnit::cubic_meter_per_second) == si;
}

constexpr bool conversion_is_identity(const auto& units) {
  return std::ranges::all_of(
      units, [](auto u) { return conversion(u, u) == Affine{}; });
}

// Converting x from -> to -> from returns x.
template <class U, std::size_t N>
constexpr bool conversions_invert(const std::array<U, N>& units) {
  for (const U from : units) {
    for (const U to : units) {
      for (const double x : conversion_values) {
        const double back = conversion(to, from)(conversion(from, to)(x));
        if (not(x == 0.0 ? near_abs(back, 0.0, 1e-9) : near(back, x, 1e-12))) {
          return false;
        }
      }
    }
  }
  return true;
}

template <class U, std::size_t N>
constexpr bool all_names_distinct(const std::array<U, N>& units) {
  for (std::size_t i = 0; i < N; ++i) {
    if (mov::core::symbol(units[i]).empty() or
        mov::core::udunits(units[i]).empty()) {
      return false;
    }
    for (std::size_t j = i + 1; j < N; ++j) {
      if (mov::core::symbol(units[i]) == mov::core::symbol(units[j]) or
          mov::core::udunits(units[i]) == mov::core::udunits(units[j])) {
        return false;
      }
    }
  }
  return true;
}

// Concepts, so that a deleted overload is a substitution failure.
template <class T>
concept SymbolCallable =
    requires(T&& u) { mov::core::symbol(std::forward<T>(u)); };
template <class T>
concept UdunitsCallable =
    requires(T&& u) { mov::core::udunits(std::forward<T>(u)); };

template <class A, class B>
concept ConversionCallable = requires(A a, B b) { conversion(a, b); };

// conversion(a, b) followed by conversion(b, c) is conversion(a, c).
template <class U, std::size_t N>
constexpr bool conversions_compose(const std::array<U, N>& units) {
  for (const U a : units) {
    for (const U b : units) {
      for (const U c : units) {
        for (const double x : conversion_values) {
          const double via_b = conversion(b, c)(conversion(a, b)(x));
          const double direct = conversion(a, c)(x);
          if (not(x == 0.0 ? near_abs(via_b, direct, 1e-9)
                           : near(via_b, direct, 1e-12))) {
            return false;
          }
        }
      }
    }
  }
  return true;
}

}  // namespace

TEST_CASE("unit types are value types", "[core][units][constexpr]") {
  STATIC_REQUIRE(std::regular<Length>);
  STATIC_REQUIRE(std::regular<Speed>);
  STATIC_REQUIRE(std::regular<Pressure>);
  STATIC_REQUIRE(std::regular<Discharge>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Length>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<Length>);
  STATIC_REQUIRE(std::is_trivially_copyable_v<Affine>);
  STATIC_REQUIRE(sizeof(Length) == sizeof(double));
  STATIC_REQUIRE(std::regular<Affine>);
  STATIC_REQUIRE(std::regular<Unit>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<Unit>);
  STATIC_REQUIRE(std::copyable<OtherUnit>);
  STATIC_REQUIRE(std::equality_comparable<OtherUnit>);
  STATIC_REQUIRE(std::is_nothrow_move_constructible_v<OtherUnit>);
  STATIC_REQUIRE(std::regular<UnknownUnit>);
}

TEST_CASE("Measure has no implicit unit", "[core][units][constexpr]") {
  // The raw SI constructor is private: a value always names its unit.
  STATIC_REQUIRE_FALSE(std::is_constructible_v<Length, double>);
  STATIC_REQUIRE_FALSE(std::is_convertible_v<double, Length>);
  STATIC_REQUIRE(Length{}.as(LengthUnit::foot) == 0.0);
  // Different families do not mix.
  STATIC_REQUIRE_FALSE(std::is_convertible_v<Speed, Length>);
  STATIC_REQUIRE_FALSE(std::is_constructible_v<Length, Speed>);
  STATIC_REQUIRE_FALSE(std::equality_comparable_with<Length, Speed>);
}

TEST_CASE("unit factors to SI are exact", "[core][units][constexpr]") {
  STATIC_REQUIRE(is_factor(LengthUnit::meter, 1.0));
  STATIC_REQUIRE(is_factor(LengthUnit::foot, 0.3048));
  STATIC_REQUIRE(is_factor(LengthUnit::inch, 0.0254));
  STATIC_REQUIRE(is_factor(LengthUnit::kilometer, 1000.0));
  STATIC_REQUIRE(is_factor(LengthUnit::statute_mile, 1609.344));
  STATIC_REQUIRE(is_factor(LengthUnit::nautical_mile, 1852.0));

  STATIC_REQUIRE(is_factor(SpeedUnit::meter_per_second, 1.0));
  STATIC_REQUIRE(is_factor(SpeedUnit::foot_per_second, 0.3048));
  STATIC_REQUIRE(is_factor(SpeedUnit::knot, 1852.0 / 3600.0));
  STATIC_REQUIRE(is_factor(SpeedUnit::mile_per_hour, 1609.344 / 3600.0));
  STATIC_REQUIRE(is_factor(SpeedUnit::kilometer_per_hour, 1000.0 / 3600.0));

  STATIC_REQUIRE(is_factor(PressureUnit::pascal, 1.0));
  STATIC_REQUIRE(is_factor(PressureUnit::hectopascal, 100.0));
  STATIC_REQUIRE(is_factor(PressureUnit::millibar, 100.0));
  // v4 used 9806.38; the standard value is documented as a non-goal to match.
  STATIC_REQUIRE(is_factor(PressureUnit::meter_of_water, 9806.65));

  STATIC_REQUIRE(is_factor(DischargeUnit::cubic_meter_per_second, 1.0));
  STATIC_REQUIRE(
      is_factor(DischargeUnit::cubic_foot_per_second, 0.028316846592));
}

TEST_CASE("in and as round-trip every enumerator", "[core][units][constexpr]") {
  STATIC_REQUIRE(round_trips<Length>(all_lengths));
  STATIC_REQUIRE(round_trips<Speed>(all_speeds));
  STATIC_REQUIRE(round_trips<Pressure>(all_pressures));
  STATIC_REQUIRE(round_trips<Discharge>(all_discharges));
}

TEST_CASE("Measure arithmetic", "[core][units][constexpr]") {
  constexpr Length ten_m = Length::in(10.0, LengthUnit::meter);
  constexpr Length one_km = Length::in(1.0, LengthUnit::kilometer);
  STATIC_REQUIRE((one_km + ten_m).as(LengthUnit::meter) == 1010.0);
  STATIC_REQUIRE((one_km - ten_m).as(LengthUnit::meter) == 990.0);
  STATIC_REQUIRE((-ten_m).as(LengthUnit::meter) == -10.0);
  STATIC_REQUIRE((+ten_m).as(LengthUnit::meter) == 10.0);
  STATIC_REQUIRE((ten_m * 3.0).as(LengthUnit::meter) == 30.0);
  STATIC_REQUIRE((3.0 * ten_m).as(LengthUnit::meter) == 30.0);
  STATIC_REQUIRE((ten_m / 4.0).as(LengthUnit::meter) == 2.5);
  STATIC_REQUIRE(one_km / ten_m == 100.0);  // dimensionless
  STATIC_REQUIRE(std::same_as<decltype(one_km / ten_m), double>);
  // A Length is not a Speed, so there is no Length / Length -> Length.
  STATIC_REQUIRE(std::same_as<decltype(ten_m * 2.0), Length>);
}

TEST_CASE("Measure orders by its SI value", "[core][units][constexpr]") {
  constexpr Length foot = Length::in(1.0, LengthUnit::foot);
  constexpr Length meter = Length::in(1.0, LengthUnit::meter);
  STATIC_REQUIRE(foot < meter);
  STATIC_REQUIRE(meter > foot);
  STATIC_REQUIRE(foot <= foot);
  STATIC_REQUIRE(foot == Length::in(0.3048, LengthUnit::meter));
  STATIC_REQUIRE(foot != meter);
  STATIC_REQUIRE((foot <=> meter) == std::partial_ordering::less);
}

TEST_CASE("Affine evaluates scale * x + offset", "[core][units][constexpr]") {
  STATIC_REQUIRE(Affine{}(3.5) == 3.5);
  STATIC_REQUIRE(Affine{.scale = 2.0, .offset = 1.0}(3.0) == 7.0);
  STATIC_REQUIRE(Affine{.scale = 2.0} == Affine{.scale = 2.0, .offset = 0.0});
}

TEST_CASE("conversion within a family", "[core][units][constexpr]") {
  STATIC_REQUIRE(conversion(LengthUnit::foot, LengthUnit::meter) ==
                 Affine{.scale = 0.3048, .offset = 0.0});
  STATIC_REQUIRE(
      near(conversion(LengthUnit::meter, LengthUnit::foot)(0.3048), 1.0));
  STATIC_REQUIRE(
      near(conversion(LengthUnit::foot, LengthUnit::inch)(1.0), 12.0));
  STATIC_REQUIRE(
      near(conversion(SpeedUnit::knot, SpeedUnit::meter_per_second)(10.0),
           18520.0 / 3600.0));
  STATIC_REQUIRE(conversion(PressureUnit::millibar,
                            PressureUnit::hectopascal)(1013.25) == 1013.25);
  STATIC_REQUIRE(near(conversion(DischargeUnit::cubic_foot_per_second,
                                 DischargeUnit::cubic_meter_per_second)(1000.0),
                      28.316846592));
  STATIC_REQUIRE(conversion_is_identity(all_lengths));
  STATIC_REQUIRE(conversion_is_identity(all_speeds));
  STATIC_REQUIRE(conversion_is_identity(all_pressures));
  STATIC_REQUIRE(conversion_is_identity(all_discharges));
  STATIC_REQUIRE(conversion_is_identity(all_temperatures));
  STATIC_REQUIRE(conversions_invert(all_lengths));
  STATIC_REQUIRE(conversions_invert(all_speeds));
  STATIC_REQUIRE(conversions_invert(all_pressures));
  STATIC_REQUIRE(conversions_invert(all_discharges));
  STATIC_REQUIRE(conversions_invert(all_temperatures));
}

TEST_CASE("temperature conversion is affine", "[core][units][constexpr]") {
  constexpr Affine c_to_f =
      conversion(TemperatureUnit::celsius, TemperatureUnit::fahrenheit);
  constexpr Affine f_to_c =
      conversion(TemperatureUnit::fahrenheit, TemperatureUnit::celsius);
  STATIC_REQUIRE(c_to_f(0.0) == 32.0);
  STATIC_REQUIRE(c_to_f(100.0) == 212.0);
  STATIC_REQUIRE(c_to_f(-40.0) == -40.0);
  STATIC_REQUIRE(near(f_to_c(212.0), 100.0));
  STATIC_REQUIRE(near(f_to_c(50.0), 10.0));
  STATIC_REQUIRE(near(f_to_c(-40.0), -40.0));
  // The offset is the whole point: it is not a pure scale.
  STATIC_REQUIRE(c_to_f.offset == 32.0);
}

TEST_CASE("is_temperature classifies the Unit variant",
          "[core][units][constexpr]") {
  MOV_STATIC_REQUIRE_VARIANT(is_temperature(Unit{TemperatureUnit::celsius}));
  MOV_STATIC_REQUIRE_VARIANT(is_temperature(Unit{TemperatureUnit::fahrenheit}));
  MOV_STATIC_REQUIRE_FALSE_VARIANT(is_temperature(Unit{LengthUnit::foot}));
  MOV_STATIC_REQUIRE_FALSE_VARIANT(is_temperature(Unit{SpeedUnit::knot}));
  MOV_STATIC_REQUIRE_FALSE_VARIANT(is_temperature(Unit{PressureUnit::pascal}));
  MOV_STATIC_REQUIRE_FALSE_VARIANT(
      is_temperature(Unit{DischargeUnit::cubic_meter_per_second}));
}

TEST_CASE("Unit views into owned storage reject temporaries",
          "[core][units][constexpr]") {
  STATIC_REQUIRE(SymbolCallable<const Unit&>);
  STATIC_REQUIRE(SymbolCallable<Unit&>);
  STATIC_REQUIRE_FALSE(SymbolCallable<Unit>);
  STATIC_REQUIRE(UdunitsCallable<const Unit&>);
  STATIC_REQUIRE_FALSE(UdunitsCallable<Unit>);
}

TEST_CASE("OtherUnit cannot be built outside parse_unit",
          "[core][units][constexpr]") {
  STATIC_REQUIRE_FALSE(std::is_default_constructible_v<OtherUnit>);
  STATIC_REQUIRE_FALSE(std::is_constructible_v<OtherUnit, std::string>);
  STATIC_REQUIRE_FALSE(std::is_constructible_v<OtherUnit, const char*>);
  // The passkey has no public constructor, so the two-argument form is
  // unreachable too.
  STATIC_REQUIRE_FALSE(
      std::is_default_constructible_v<mov::core::detail::UnitKey>);
  STATIC_REQUIRE_FALSE(
      std::is_constructible_v<mov::core::detail::UnitKey, int>);
  // The variant has no stray string alternative either.
  STATIC_REQUIRE_FALSE(std::is_constructible_v<Unit, std::string>);
  STATIC_REQUIRE_FALSE(std::is_constructible_v<Unit, const char*>);
}

TEST_CASE("conversions compose", "[core][units][constexpr]") {
  STATIC_REQUIRE(conversions_compose(all_lengths));
  STATIC_REQUIRE(conversions_compose(all_speeds));
  STATIC_REQUIRE(conversions_compose(all_pressures));
  STATIC_REQUIRE(conversions_compose(all_discharges));
  STATIC_REQUIRE(conversions_compose(all_temperatures));
}

TEST_CASE("converting between unit families is a compile error",
          "[core][units][constexpr]") {
  STATIC_REQUIRE(ConversionCallable<LengthUnit, LengthUnit>);
  STATIC_REQUIRE(ConversionCallable<TemperatureUnit, TemperatureUnit>);
  STATIC_REQUIRE(ConversionCallable<Unit, Unit>);
  STATIC_REQUIRE(ConversionCallable<LengthUnit, Unit>);  // runtime Unit path
  STATIC_REQUIRE_FALSE(ConversionCallable<LengthUnit, SpeedUnit>);
  STATIC_REQUIRE_FALSE(ConversionCallable<SpeedUnit, PressureUnit>);
  STATIC_REQUIRE_FALSE(ConversionCallable<TemperatureUnit, LengthUnit>);
  STATIC_REQUIRE_FALSE(ConversionCallable<DischargeUnit, TemperatureUnit>);
}

TEST_CASE("symbol and udunits of an enumerator are constexpr",
          "[core][units][constexpr]") {
  STATIC_REQUIRE(mov::core::symbol(LengthUnit::foot) == "ft");
  STATIC_REQUIRE(mov::core::symbol(SpeedUnit::meter_per_second) == "m/s");
  STATIC_REQUIRE(mov::core::symbol(PressureUnit::hectopascal) == "hPa");
  STATIC_REQUIRE(mov::core::symbol(DischargeUnit::cubic_meter_per_second) ==
                 "m3/s");
  STATIC_REQUIRE(mov::core::symbol(TemperatureUnit::fahrenheit) ==
                 "\xC2\xB0"
                 "F");
  STATIC_REQUIRE(mov::core::udunits(SpeedUnit::meter_per_second) == "m s-1");
  STATIC_REQUIRE(mov::core::udunits(PressureUnit::meter_of_water) == "m H2O");
  STATIC_REQUIRE(mov::core::udunits(TemperatureUnit::celsius) == "degC");
  // Every enumerator has a distinct non-empty name.
  STATIC_REQUIRE(all_names_distinct(all_lengths));
  STATIC_REQUIRE(all_names_distinct(all_speeds));
  STATIC_REQUIRE(all_names_distinct(all_pressures));
  STATIC_REQUIRE(all_names_distinct(all_discharges));
  STATIC_REQUIRE(all_names_distinct(all_temperatures));
}

TEST_CASE("symbol and udunits of a Unit variable are constexpr",
          "[core][units][constexpr]") {
  MOV_CONSTEXPR_VARIANT Unit knot{SpeedUnit::knot};
  MOV_STATIC_REQUIRE_VARIANT(mov::core::symbol(knot) == "kt");
  MOV_STATIC_REQUIRE_VARIANT(mov::core::udunits(knot) == "knot");
  // The enumerator and variant paths agree.
  MOV_STATIC_REQUIRE_VARIANT(mov::core::symbol(knot) ==
                             mov::core::symbol(SpeedUnit::knot));
}
