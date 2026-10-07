// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/units.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "mov/core/detail/ascii.hpp"

namespace mov::core {

namespace detail {

/// The only code that can mint a UnitKey.
struct UnitFactory {
  static OtherUnit make(std::string canonical) {
    return OtherUnit{UnitKey{}, std::move(canonical)};
  }
};

}  // namespace detail

namespace {

// ---- parse_unit: the spelling tables ---------------------------------------

// exact: the text must match byte for byte (symbols are case-sensitive: Mb is
// not mb). any_case: the table text is lower case and the input may use any.
enum class Match : std::uint8_t { exact, any_case };

template <class U>
struct Spelling {
  std::string_view text{};
  U unit{};
  Match match{Match::exact};
};

template <class U>
[[nodiscard]] constexpr Spelling<U> symbol_spelling(std::string_view text,
                                                    U unit) {
  return {.text = text, .unit = unit, .match = Match::exact};
}
template <class U>
[[nodiscard]] constexpr Spelling<U> word_spelling(std::string_view lower_text,
                                                  U unit) {
  return {.text = lower_text, .unit = unit, .match = Match::any_case};
}

constexpr std::array length_spellings{
    symbol_spelling("m", LengthUnit::meter),
    word_spelling("meter", LengthUnit::meter),
    word_spelling("meters", LengthUnit::meter),
    word_spelling("metre", LengthUnit::meter),
    word_spelling("metres", LengthUnit::meter),
    symbol_spelling("ft", LengthUnit::foot),
    word_spelling("foot", LengthUnit::foot),
    word_spelling("feet", LengthUnit::foot),
    symbol_spelling("in", LengthUnit::inch),
    word_spelling("inch", LengthUnit::inch),
    word_spelling("inches", LengthUnit::inch),
    symbol_spelling("km", LengthUnit::kilometer),
    word_spelling("kilometer", LengthUnit::kilometer),
    word_spelling("kilometers", LengthUnit::kilometer),
    word_spelling("kilometre", LengthUnit::kilometer),
    word_spelling("kilometres", LengthUnit::kilometer),
    symbol_spelling("mi", LengthUnit::statute_mile),
    word_spelling("mile", LengthUnit::statute_mile),
    word_spelling("miles", LengthUnit::statute_mile),
    symbol_spelling("nmi", LengthUnit::nautical_mile),
    word_spelling("nautical_mile", LengthUnit::nautical_mile),
    word_spelling("nautical_miles", LengthUnit::nautical_mile),
};

constexpr std::array speed_spellings{
    symbol_spelling("m/s", SpeedUnit::meter_per_second),
    symbol_spelling("m s-1", SpeedUnit::meter_per_second),
    symbol_spelling("ft/s", SpeedUnit::foot_per_second),
    symbol_spelling("ft s-1", SpeedUnit::foot_per_second),
    symbol_spelling("kn", SpeedUnit::knot),
    symbol_spelling("kt", SpeedUnit::knot),
    word_spelling("knot", SpeedUnit::knot),
    word_spelling("knots", SpeedUnit::knot),
    symbol_spelling("mph", SpeedUnit::mile_per_hour),
    symbol_spelling("mile hour-1", SpeedUnit::mile_per_hour),
    symbol_spelling("km/h", SpeedUnit::kilometer_per_hour),
    symbol_spelling("kph", SpeedUnit::kilometer_per_hour),
    symbol_spelling("km h-1", SpeedUnit::kilometer_per_hour),
    symbol_spelling("km hour-1", SpeedUnit::kilometer_per_hour),
};

constexpr std::array pressure_spellings{
    symbol_spelling("Pa", PressureUnit::pascal),
    symbol_spelling("hPa", PressureUnit::hectopascal),
    symbol_spelling("mb", PressureUnit::millibar),
    symbol_spelling("mbar", PressureUnit::millibar),
    word_spelling("millibar", PressureUnit::millibar),
    word_spelling("millibars", PressureUnit::millibar),
    symbol_spelling("mH2O", PressureUnit::meter_of_water),
    symbol_spelling("m H2O", PressureUnit::meter_of_water),
};

constexpr std::array discharge_spellings{
    symbol_spelling("m3/s", DischargeUnit::cubic_meter_per_second),
    symbol_spelling("m3 s-1", DischargeUnit::cubic_meter_per_second),
    symbol_spelling("ft3/s", DischargeUnit::cubic_foot_per_second),
    symbol_spelling("ft3 s-1", DischargeUnit::cubic_foot_per_second),
    symbol_spelling("cfs", DischargeUnit::cubic_foot_per_second),
};

// The degree sign is spelled as its UTF-8 bytes: the source encoding is not
// assumed. The literals are split so that the hex escape ends before the "C".
constexpr std::array temperature_spellings{
    symbol_spelling("degC", TemperatureUnit::celsius),
    symbol_spelling("deg C", TemperatureUnit::celsius),
    symbol_spelling("C", TemperatureUnit::celsius),
    word_spelling("celsius", TemperatureUnit::celsius),
    symbol_spelling("degree_C", TemperatureUnit::celsius),
    word_spelling("degree_celsius", TemperatureUnit::celsius),
    symbol_spelling("\xC2\xB0"
                    "C",
                    TemperatureUnit::celsius),
    symbol_spelling("degF", TemperatureUnit::fahrenheit),
    symbol_spelling("deg F", TemperatureUnit::fahrenheit),
    symbol_spelling("F", TemperatureUnit::fahrenheit),
    word_spelling("fahrenheit", TemperatureUnit::fahrenheit),
    symbol_spelling("degree_F", TemperatureUnit::fahrenheit),
    word_spelling("degree_fahrenheit", TemperatureUnit::fahrenheit),
    symbol_spelling("\xC2\xB0"
                    "F",
                    TemperatureUnit::fahrenheit),
};

template <class U, std::size_t N>
[[nodiscard]] std::optional<Unit> lookup(
    const std::array<Spelling<U>, N>& table, std::string_view text) {
  const auto it = std::ranges::find_if(table, [text](const Spelling<U>& s) {
    return s.match == Match::any_case ? detail::equal_ignore_case(text, s.text)
                                      : text == s.text;
  });
  if (it == table.end()) {
    return std::nullopt;
  }
  return Unit{it->unit};
}

[[nodiscard]] std::optional<Unit> find_family_unit(std::string_view text) {
  if (auto u = lookup(length_spellings, text)) {
    return u;
  }
  if (auto u = lookup(speed_spellings, text)) {
    return u;
  }
  if (auto u = lookup(pressure_spellings, text)) {
    return u;
  }
  if (auto u = lookup(discharge_spellings, text)) {
    return u;
  }
  return lookup(temperature_spellings, text);
}

/// Trims, and collapses each inner run of whitespace to one space.
[[nodiscard]] std::string normalized(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool pending_space = false;
  for (const char c : text) {
    if (detail::is_space(c)) {
      pending_space = not out.empty();
      continue;
    }
    if (pending_space) {
      out.push_back(' ');
      pending_space = false;
    }
    out.push_back(c);
  }
  return out;
}

// The registry's canonical spelling of an alias, or nullopt if `text` is not
// one. `degrees` and `degrees_true` are the CF/udunits spellings.
[[nodiscard]] std::optional<std::string_view> canonical_alias(
    std::string_view text) noexcept {
  if (text == "%") {
    return "percent";
  }
  if (text == "deg" or text == "degT" or text == "degrees" or
      text == "degrees_true") {
    return "degree";
  }
  if (text == "sec") {
    return "s";
  }
  return std::nullopt;
}

}  // namespace

std::optional<Unit> parse_unit(std::string_view text) {
  std::string trimmed = normalized(text);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  if (auto family = find_family_unit(trimmed)) {
    return family;
  }
  if (const auto alias = canonical_alias(trimmed)) {
    return Unit{detail::UnitFactory::make(std::string{*alias})};
  }
  return Unit{detail::UnitFactory::make(std::move(trimmed))};
}

std::expected<Affine, IncompatibleUnits> conversion(const Unit& from,
                                                    const Unit& to) {
  if (from.index() != to.index()) {
    return std::unexpected{IncompatibleUnits{.from = from, .to = to}};
  }
  return std::visit(
      [&](const auto& f) -> std::expected<Affine, IncompatibleUnits> {
        using Family = std::decay_t<decltype(f)>;
        const auto& t = std::get<Family>(to);
        if constexpr (std::same_as<Family, OtherUnit>) {
          if (f == t) {
            return Affine{};
          }
          return std::unexpected{IncompatibleUnits{.from = from, .to = to}};
        } else {
          return conversion(f, t);
        }
      },
      from);
}

}  // namespace mov::core
