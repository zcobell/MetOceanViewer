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
enum class TemperatureUnit : std::uint8_t { celsius, fahrenheit };

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
  OtherUnit(const detail::UnitKey&, std::string canonical)
      : symbol_{std::move(canonical)} {}

  [[nodiscard]] std::string_view symbol() const& noexcept { return symbol_; }
  std::string_view symbol() && = delete;

  friend bool operator==(const OtherUnit&, const OtherUnit&) = default;

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
/// aliases `%`, `deg`, `degT`, `sec` canonicalized.
[[nodiscard]] std::optional<Unit> parse_unit(std::string_view text);

/// Whether an OtherUnit is one of the registry's own non-family units
/// (percent, degree, s, S m-1). Readers warn `unrecognized_unit` otherwise.
[[nodiscard]] bool is_canonical_other(const OtherUnit& unit) noexcept;

/// Display text, "m/s". The views point into static storage or into `u`.
[[nodiscard]] std::string_view symbol(const Unit& u) noexcept;
std::string_view symbol(Unit&&) = delete;
/// netCDF `units` text, "m s-1".
[[nodiscard]] std::string_view udunits(const Unit& u) noexcept;
std::string_view udunits(Unit&&) = delete;

[[nodiscard]] constexpr bool is_temperature(const Unit& u) noexcept {
  return std::holds_alternative<TemperatureUnit>(u);
}

/// y = scale * x + offset: how a value converts between units.
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

[[nodiscard]] constexpr Affine temperature_conversion(
    TemperatureUnit from, TemperatureUnit to) noexcept {
  if (from == to) {
    return Affine{};
  }
  return from == TemperatureUnit::celsius
             ? Affine{.scale = 1.8, .offset = 32.0}
             : Affine{.scale = 5.0 / 9.0, .offset = -160.0 / 9.0};
}

}  // namespace detail

/// Conversion within one family; identity for equal units. Temperatures are
/// affine, everything else is a pure scale.
template <class U>
  requires(MeasureUnit<U> or std::same_as<U, TemperatureUnit>)
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

/// Conversion between runtime units: the same family converts, an OtherUnit
/// converts only to an equal one (identity), anything else is incompatible.
[[nodiscard]] std::expected<Affine, IncompatibleUnits> conversion(
    const Unit& from, const Unit& to);

}  // namespace mov::core
