// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/units.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "mov/core/detail/overloaded.hpp"

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

template <class U>
struct Spelling {
  std::string_view text;
  U unit;
  bool any_case;  // text is lower case and matches in any case
};

template <class U>
constexpr Spelling<U> symbol_spelling(std::string_view text, U unit) {
  return {.text = text, .unit = unit, .any_case = false};
}
template <class U>
constexpr Spelling<U> word_spelling(std::string_view lower_text, U unit) {
  return {.text = lower_text, .unit = unit, .any_case = true};
}

constexpr char lower_ascii(char c) noexcept {
  return (c >= 'A' and c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

constexpr bool equal_any_case(std::string_view text,
                              std::string_view lower_text) noexcept {
  return std::ranges::equal(text, lower_text, [](char a, char b) noexcept {
    return lower_ascii(a) == b;
  });
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
    symbol_spelling("\xC2\xB0"
                    "C",
                    TemperatureUnit::celsius),
    symbol_spelling("degF", TemperatureUnit::fahrenheit),
    symbol_spelling("deg F", TemperatureUnit::fahrenheit),
    symbol_spelling("F", TemperatureUnit::fahrenheit),
    word_spelling("fahrenheit", TemperatureUnit::fahrenheit),
    symbol_spelling("\xC2\xB0"
                    "F",
                    TemperatureUnit::fahrenheit),
};

template <class U, std::size_t N>
std::optional<Unit> lookup(const std::array<Spelling<U>, N>& table,
                           std::string_view text) {
  const auto it = std::ranges::find_if(table, [text](const Spelling<U>& s) {
    return s.any_case ? equal_any_case(text, s.text) : text == s.text;
  });
  if (it == table.end()) {
    return std::nullopt;
  }
  return Unit{it->unit};
}

std::optional<Unit> find_family_unit(std::string_view text) {
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

constexpr bool is_space(char c) noexcept {
  return c == ' ' or c == '\t' or c == '\n' or c == '\r' or c == '\v' or
         c == '\f';
}

/// Trims, and collapses each inner run of whitespace to one space.
std::string normalized(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  bool pending_space = false;
  for (const char c : text) {
    if (is_space(c)) {
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

std::string_view canonical_alias(std::string_view text) noexcept {
  if (text == "%") {
    return "percent";
  }
  if (text == "deg" or text == "degT") {
    return "degree";
  }
  if (text == "sec") {
    return "s";
  }
  return text;
}

constexpr std::array<std::string_view, 4> canonical_others{"percent", "degree",
                                                           "s", "S m-1"};

// ---- symbol and udunits
// ------------------------------------------------------

template <std::size_t N>
using Names = std::array<std::string_view, N>;

constexpr Names<6> length_symbols{"m", "ft", "in", "km", "mi", "nmi"};
constexpr Names<5> speed_symbols{"m/s", "ft/s", "kt", "mph", "km/h"};
constexpr Names<4> pressure_symbols{"Pa", "hPa", "mb", "mH2O"};
constexpr Names<2> discharge_symbols{"m3/s", "ft3/s"};
constexpr Names<2> temperature_symbols{
    "\xC2\xB0"
    "C",
    "\xC2\xB0"
    "F"};

constexpr Names<6> length_udunits{"m",  "ft",   "in",
                                  "km", "mile", "nautical_mile"};
constexpr Names<5> speed_udunits{"m s-1", "ft s-1", "knot", "mile hour-1",
                                 "km hour-1"};
constexpr Names<4> pressure_udunits{"Pa", "hPa", "millibar", "mH2O"};
constexpr Names<2> discharge_udunits{"m3 s-1", "ft3 s-1"};
constexpr Names<2> temperature_udunits{"degC", "degF"};

template <class E, std::size_t N>
constexpr std::string_view name_of(const Names<N>& names, E e) noexcept {
  return names[static_cast<std::size_t>(e)];
}

}  // namespace

std::optional<Unit> parse_unit(std::string_view text) {
  const std::string trimmed = normalized(text);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  if (auto family = find_family_unit(trimmed)) {
    return family;
  }
  return Unit{detail::UnitFactory::make(std::string{canonical_alias(trimmed)})};
}

bool is_canonical_other(const OtherUnit& unit) noexcept {
  return std::ranges::find(canonical_others, unit.symbol()) !=
         canonical_others.end();
}

std::string_view symbol(const Unit& u) noexcept {
  return std::visit(
      detail::Overloaded{
          [](LengthUnit v) { return name_of(length_symbols, v); },
          [](SpeedUnit v) { return name_of(speed_symbols, v); },
          [](PressureUnit v) { return name_of(pressure_symbols, v); },
          [](DischargeUnit v) { return name_of(discharge_symbols, v); },
          [](TemperatureUnit v) { return name_of(temperature_symbols, v); },
          [](const OtherUnit& v) { return v.symbol(); }},
      u);
}

std::string_view udunits(const Unit& u) noexcept {
  return std::visit(
      detail::Overloaded{
          [](LengthUnit v) { return name_of(length_udunits, v); },
          [](SpeedUnit v) { return name_of(speed_udunits, v); },
          [](PressureUnit v) { return name_of(pressure_udunits, v); },
          [](DischargeUnit v) { return name_of(discharge_udunits, v); },
          [](TemperatureUnit v) { return name_of(temperature_udunits, v); },
          [](const OtherUnit& v) { return v.symbol(); }},
      u);
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
