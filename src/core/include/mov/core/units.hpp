// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <array>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace mov::core {

enum class LengthUnit : std::uint8_t {
  meter,
  foot,
  inch,
  kilometer,
  statute_mile,
  nautical_mile
};
enum class SpeedUnit : std::uint8_t {
  meter_per_second,
  foot_per_second,
  knot,
  mile_per_hour,
  kilometer_per_hour
};
enum class PressureUnit : std::uint8_t {
  pascal,
  hectopascal,
  millibar,
  meter_of_water
};
enum class DischargeUnit : std::uint8_t {
  cubic_meter_per_second,
  cubic_foot_per_second
};
/// A unit option only: the affine Temperature value type is deferred to
/// Phase 3, so there is no Measure over it.
enum class TemperatureUnit : std::uint8_t { celsius, fahrenheit, kelvin };

namespace detail {

// Factor to the SI unit of each family: m, m/s, Pa, m3/s. Indexed by the
// enumerator, so the tables follow the enum declaration order above.
inline constexpr std::array<double, 6> length_factors{1.0,    0.3048,   0.0254,
                                                      1000.0, 1609.344, 1852.0};
inline constexpr std::array<double, 5> speed_factors{
    1.0, 0.3048, 1852.0 / 3600.0, 1609.344 / 3600.0, 1000.0 / 3600.0};
// 9806.65 Pa per metre of water; v4 used 9806.38, a documented non-goal.
inline constexpr std::array<double, 4> pressure_factors{1.0, 100.0, 100.0,
                                                        9806.65};
inline constexpr std::array<double, 2> discharge_factors{1.0, 0.028316846592};

static_assert(length_factors.size() ==
              static_cast<std::size_t>(LengthUnit::nautical_mile) + 1);
static_assert(speed_factors.size() ==
              static_cast<std::size_t>(SpeedUnit::kilometer_per_hour) + 1);
static_assert(pressure_factors.size() ==
              static_cast<std::size_t>(PressureUnit::meter_of_water) + 1);
static_assert(discharge_factors.size() ==
              static_cast<std::size_t>(DischargeUnit::cubic_foot_per_second) +
                  1);

[[nodiscard]] constexpr double si_factor(LengthUnit u) noexcept {
  return length_factors[static_cast<std::size_t>(u)];
}
[[nodiscard]] constexpr double si_factor(SpeedUnit u) noexcept {
  return speed_factors[static_cast<std::size_t>(u)];
}
[[nodiscard]] constexpr double si_factor(PressureUnit u) noexcept {
  return pressure_factors[static_cast<std::size_t>(u)];
}
[[nodiscard]] constexpr double si_factor(DischargeUnit u) noexcept {
  return discharge_factors[static_cast<std::size_t>(u)];
}

}  // namespace detail

/// The unit enums that scale linearly to an SI base.
template <class U>
concept MeasureUnit = requires(U u) {
  { detail::si_factor(u) } -> std::same_as<double>;
};

/// Every unit enum: the linear ones plus TemperatureUnit.
template <class U>
concept UnitEnum = MeasureUnit<U> or std::same_as<U, TemperatureUnit>;

/// A physical quantity stored in SI. There is no raw-number constructor, so a
/// value always names its unit, and different families do not mix.
template <MeasureUnit U>
class Measure {
 public:
  constexpr Measure() noexcept = default;  // zero

  [[nodiscard]] static constexpr Measure in(double value, U unit) noexcept {
    return Measure{value * detail::si_factor(unit)};
  }
  [[nodiscard]] constexpr double as(U unit) const noexcept {
    return si_ / detail::si_factor(unit);
  }

  [[nodiscard]] friend constexpr Measure operator+(Measure a,
                                                   Measure b) noexcept {
    return Measure{a.si_ + b.si_};
  }
  [[nodiscard]] friend constexpr Measure operator-(Measure a,
                                                   Measure b) noexcept {
    return Measure{a.si_ - b.si_};
  }
  [[nodiscard]] friend constexpr Measure operator-(Measure a) noexcept {
    return Measure{-a.si_};
  }
  [[nodiscard]] friend constexpr Measure operator+(Measure a) noexcept {
    return a;
  }
  [[nodiscard]] friend constexpr Measure operator*(Measure a,
                                                   double k) noexcept {
    return Measure{a.si_ * k};
  }
  [[nodiscard]] friend constexpr Measure operator*(double k,
                                                   Measure a) noexcept {
    return Measure{k * a.si_};
  }
  [[nodiscard]] friend constexpr Measure operator/(Measure a,
                                                   double k) noexcept {
    return Measure{a.si_ / k};
  }
  /// The ratio of two like quantities is dimensionless.
  [[nodiscard]] friend constexpr double operator/(Measure a,
                                                  Measure b) noexcept {
    return a.si_ / b.si_;
  }

  /// Partial order by SI value (NaN is unordered, like double).
  friend constexpr std::partial_ordering operator<=>(
      const Measure&, const Measure&) noexcept = default;
  friend constexpr bool operator==(const Measure&,
                                   const Measure&) noexcept = default;

 private:
  explicit constexpr Measure(double si) noexcept : si_{si} {}

  double si_{};
};

using Length = Measure<LengthUnit>;
using Speed = Measure<SpeedUnit>;
using Pressure = Measure<PressureUnit>;
using Discharge = Measure<DischargeUnit>;

namespace detail {

class UnitKey;
struct UnitFactory;

}  // namespace detail

/// A unit that is none of the families above ("percent", "degree", "S m-1",
/// or whatever a foreign file says). Only parse_unit builds one, from text
/// it has normalized, so equal spellings are equal units.
class OtherUnit {
 public:
  constexpr OtherUnit(const detail::UnitKey&, std::string canonical)
      : symbol_{std::move(canonical)} {}

  [[nodiscard]] constexpr std::string_view symbol() const& noexcept {
    return symbol_;
  }
  std::string_view symbol() const&& = delete;

  friend constexpr bool operator==(const OtherUnit&,
                                   const OtherUnit&) = default;

 private:
  std::string symbol_;
};

/// A unit of a series. Default-constructed it is LengthUnit::meter; text
/// becomes a Unit only through parse_unit.
using Unit = std::variant<LengthUnit, SpeedUnit, PressureUnit, DischargeUnit,
                          TemperatureUnit, OtherUnit>;

namespace detail {

/// Passkey for OtherUnit: only UnitFactory (units.cpp) can construct one.
class UnitKey {
 private:
  friend struct UnitFactory;
  UnitKey() noexcept = default;
};

}  // namespace detail

/// "" and blank text are nullopt: the unit is unset, not unknown. A spelling
/// from the table in docs/core-design.md section 2.2 (plus udunits and
/// symbol() output) gives the family enumerator; anything else becomes an
/// OtherUnit with surrounding whitespace removed, inner runs collapsed and the
/// aliases `%`, `deg`, `degT`, `degrees`, `degrees_true`, `sec`
/// canonicalized. Whether an OtherUnit is one the registry itself uses is
/// is_canonical_other (quantity.hpp).
[[nodiscard]] std::optional<Unit> parse_unit(std::string_view text);

/// The `degree` OtherUnit (plane angle; what parse_unit makes of "degree",
/// "deg", "degT", "degrees" and "degrees_true").
[[nodiscard]] Unit degree();

namespace detail {

/// parse_unit for text that is not blank, such as a registry spelling: total,
/// with no nullopt to unwrap. Blank text gives an OtherUnit with an empty
/// symbol (callers prove non-blankness instead, e.g. by static_assert).
[[nodiscard]] Unit unit_of_nonblank(std::string_view text);

}  // namespace detail

namespace detail {

template <std::size_t N>
using Names = std::array<std::string_view, N>;

// One name per enumerator of each family, in enumerator order.
struct NameTable {
  Names<6> length;
  Names<5> speed;
  Names<4> pressure;
  Names<2> discharge;
  Names<3> temperature;
};

// Each table has one entry per enumerator: a new enumerator breaks the build
// here, not at run time.
static_assert(std::tuple_size_v<decltype(NameTable::length)> ==
              static_cast<std::size_t>(LengthUnit::nautical_mile) + 1);
static_assert(std::tuple_size_v<decltype(NameTable::speed)> ==
              static_cast<std::size_t>(SpeedUnit::kilometer_per_hour) + 1);
static_assert(std::tuple_size_v<decltype(NameTable::pressure)> ==
              static_cast<std::size_t>(PressureUnit::meter_of_water) + 1);
static_assert(std::tuple_size_v<decltype(NameTable::discharge)> ==
              static_cast<std::size_t>(DischargeUnit::cubic_foot_per_second) +
                  1);
static_assert(std::tuple_size_v<decltype(NameTable::temperature)> ==
              static_cast<std::size_t>(TemperatureUnit::kelvin) + 1);

// The degree sign is spelled as its UTF-8 bytes: the source encoding is not
// assumed. The literals are split so the hex escape ends before the letter.
inline constexpr NameTable symbol_names{
    .length = {"m", "ft", "in", "km", "mi", "nmi"},
    .speed = {"m/s", "ft/s", "kt", "mph", "km/h"},
    .pressure = {"Pa", "hPa", "mb", "mH2O"},
    .discharge = {"m3/s", "ft3/s"},
    .temperature = {"\xC2\xB0"
                    "C",
                    "\xC2\xB0"
                    "F",
                    "K"}};

inline constexpr NameTable udunits_names{
    .length = {"m", "ft", "in", "km", "mile", "nautical_mile"},
    .speed = {"m s-1", "ft s-1", "knot", "mile hour-1", "km hour-1"},
    .pressure = {"Pa", "hPa", "millibar", "m H2O"},
    .discharge = {"m3 s-1", "ft3 s-1"},
    .temperature = {"degC", "degF", "K"}};

[[nodiscard]] constexpr std::string_view name_of(const NameTable& t,
                                                 LengthUnit u) noexcept {
  return t.length[static_cast<std::size_t>(u)];
}
[[nodiscard]] constexpr std::string_view name_of(const NameTable& t,
                                                 SpeedUnit u) noexcept {
  return t.speed[static_cast<std::size_t>(u)];
}
[[nodiscard]] constexpr std::string_view name_of(const NameTable& t,
                                                 PressureUnit u) noexcept {
  return t.pressure[static_cast<std::size_t>(u)];
}
[[nodiscard]] constexpr std::string_view name_of(const NameTable& t,
                                                 DischargeUnit u) noexcept {
  return t.discharge[static_cast<std::size_t>(u)];
}
[[nodiscard]] constexpr std::string_view name_of(const NameTable& t,
                                                 TemperatureUnit u) noexcept {
  return t.temperature[static_cast<std::size_t>(u)];
}

// The name of a runtime Unit. get_if cannot throw, which the noexcept callers
// need (std::visit can, on a valueless variant).
static_assert(std::variant_size_v<Unit> == 6,
              "name_in must handle every alternative of Unit");

template <UnitEnum E>
[[nodiscard]] constexpr std::optional<std::string_view> family_name(
    const NameTable& t, const Unit& u) noexcept {
  const E* unit = std::get_if<E>(&u);
  if (unit == nullptr) {
    return std::nullopt;
  }
  return name_of(t, *unit);
}

[[nodiscard]] constexpr std::string_view name_in(const NameTable& t,
                                                 const Unit& u) noexcept {
  if (const auto name = family_name<LengthUnit>(t, u)) {
    return *name;
  }
  if (const auto name = family_name<SpeedUnit>(t, u)) {
    return *name;
  }
  if (const auto name = family_name<PressureUnit>(t, u)) {
    return *name;
  }
  if (const auto name = family_name<DischargeUnit>(t, u)) {
    return *name;
  }
  if (const auto name = family_name<TemperatureUnit>(t, u)) {
    return *name;
  }
  const OtherUnit* other = std::get_if<OtherUnit>(&u);
  return other != nullptr ? other->symbol() : std::string_view{};
}

}  // namespace detail

/// Display text, "m/s": for an enumerator, or for a Unit held in a variable.
/// The view of an OtherUnit points into the Unit, so a temporary Unit is
/// rejected.
template <UnitEnum E>
[[nodiscard]] constexpr std::string_view symbol(E u) noexcept {
  return detail::name_of(detail::symbol_names, u);
}
[[nodiscard]] constexpr std::string_view symbol(const Unit& u) noexcept {
  return detail::name_in(detail::symbol_names, u);
}
std::string_view symbol(Unit&&) = delete;

/// netCDF `units` text, "m s-1".
template <UnitEnum E>
[[nodiscard]] constexpr std::string_view udunits(E u) noexcept {
  return detail::name_of(detail::udunits_names, u);
}
[[nodiscard]] constexpr std::string_view udunits(const Unit& u) noexcept {
  return detail::name_in(detail::udunits_names, u);
}
std::string_view udunits(Unit&&) = delete;

[[nodiscard]] constexpr bool is_temperature(const Unit& u) noexcept {
  return std::holds_alternative<TemperatureUnit>(u);
}

/// y = scale * x + offset: how a value converts between units. Evaluated as a
/// multiply and an add that each round (the build disables FMA contraction),
/// so a result is the same bit for bit at compile time and at run time.
struct Affine {
  double scale{1.0};
  double offset{0.0};

  [[nodiscard]] constexpr double operator()(double x) const noexcept {
    return (scale * x) + offset;
  }
  friend constexpr bool operator==(Affine, Affine) = default;
};

struct IncompatibleUnits {
  Unit from;
  Unit to;
  friend bool operator==(const IncompatibleUnits&,
                         const IncompatibleUnits&) = default;
};
/// The series has no unit to convert from.
struct UnknownUnit {
  friend constexpr bool operator==(UnknownUnit, UnknownUnit) = default;
};
using UnitError = std::variant<IncompatibleUnits, UnknownUnit>;

namespace detail {

/// y = second(first(x)).
[[nodiscard]] constexpr Affine then(Affine first, Affine second) noexcept {
  return Affine{.scale = second.scale * first.scale,
                .offset = (second.scale * first.offset) + second.offset};
}

/// The affine map from a temperature in `u` to degrees Celsius.
[[nodiscard]] constexpr Affine to_celsius(TemperatureUnit u) noexcept {
  switch (u) {
    case TemperatureUnit::celsius:
      break;
    case TemperatureUnit::fahrenheit:
      return Affine{.scale = 5.0 / 9.0, .offset = -160.0 / 9.0};
    case TemperatureUnit::kelvin:
      return Affine{.scale = 1.0, .offset = -273.15};
  }
  return Affine{};
}

/// The affine map from degrees Celsius to a temperature in `u`.
[[nodiscard]] constexpr Affine from_celsius(TemperatureUnit u) noexcept {
  switch (u) {
    case TemperatureUnit::celsius:
      break;
    case TemperatureUnit::fahrenheit:
      return Affine{.scale = 1.8, .offset = 32.0};
    case TemperatureUnit::kelvin:
      return Affine{.scale = 1.0, .offset = 273.15};
  }
  return Affine{};
}

[[nodiscard]] constexpr Affine temperature_conversion(
    TemperatureUnit from, TemperatureUnit to) noexcept {
  if (from == to) {
    return Affine{};
  }
  // Celsius is the hub: its two direct maps are the exact constants, and no
  // composition is applied to them.
  if (from == TemperatureUnit::celsius) {
    return from_celsius(to);
  }
  if (to == TemperatureUnit::celsius) {
    return to_celsius(from);
  }
  return then(to_celsius(from), from_celsius(to));
}

}  // namespace detail

/// Conversion within one family; identity for equal units. Temperatures are
/// affine, everything else is a pure scale.
template <UnitEnum U>
[[nodiscard]] constexpr Affine conversion(U from, U to) noexcept {
  if constexpr (std::same_as<U, TemperatureUnit>) {
    return detail::temperature_conversion(from, to);
  } else {
    if (from == to) {
      return Affine{};
    }
    return Affine{.scale = detail::si_factor(from) / detail::si_factor(to)};
  }
}

/// Two enumerators of different families (a length and a speed) never
/// convert: a compile error, not a runtime IncompatibleUnits.
template <UnitEnum A, UnitEnum B>
  requires(not std::same_as<A, B>)
Affine conversion(A, B) = delete;

/// Conversion between runtime units: the same family converts, an OtherUnit
/// converts only to an equal one (identity), anything else is incompatible.
[[nodiscard]] std::expected<Affine, IncompatibleUnits> conversion(
    const Unit& from, const Unit& to);

}  // namespace mov::core
