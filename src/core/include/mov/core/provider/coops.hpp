// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>
#include <variant>

#include "mov/core/datum.hpp"
#include "mov/core/enum_set.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/required.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/windows.hpp"

/// The request vocabulary of NOAA CO-OPS (docs/providers-design.md section
/// 3.2): what can be asked of the data API, with every illegal combination
/// left unrepresentable. A datum exists only on water level, an interval only
/// where the API takes one, and the paired product only on its two grids.
namespace mov::core::coops {

/// The datums the data API accepts that the core can name (decision 32: only
/// these are offered, and nothing is shifted client-side). CRD and LWD
/// (rivers) are not offered, as in v4; IGLD is IGLD85.
enum class Datum : std::uint8_t {
  mhhw,
  mhw,
  mtl,
  msl,
  mlw,
  mllw,
  navd88,
  stnd,
  igld85
};
inline constexpr std::array every_datum{
    Datum::mhhw, Datum::mhw,    Datum::mtl,  Datum::msl,   Datum::mlw,
    Datum::mllw, Datum::navd88, Datum::stnd, Datum::igld85};

/// An observed water-level series. The comments name the API products.
enum class Observed : std::uint8_t {
  six_minute,  // water_level
  one_minute,  // one_minute_water_level (takes no interval)
  hourly,      // hourly_height
};
inline constexpr std::array every_observed{
    Observed::six_minute, Observed::one_minute, Observed::hourly};

/// The meteorological and water-quality products. The first five are v4's;
/// the rest are v5-extras #1 and may be dropped before they ship. Air gap is
/// not one: it takes no interval (AirGap).
enum class Met : std::uint8_t {
  air_temperature,
  water_temperature,
  wind,
  air_pressure,
  humidity,
  conductivity,
  salinity,
  visibility
};
inline constexpr std::array every_met{
    Met::air_temperature, Met::water_temperature, Met::wind,
    Met::air_pressure,    Met::humidity,          Met::conductivity,
    Met::salinity,        Met::visibility};

}  // namespace mov::core::coops

namespace mov::core {

// EnumSet checks that each array lists 0, 1, ..., N - 1 in order.
template <>
struct Enumerators<coops::Datum> {
  static constexpr const auto& values = coops::every_datum;
};
template <>
struct Enumerators<coops::Observed> {
  static constexpr const auto& values = coops::every_observed;
};
template <>
struct Enumerators<coops::Met> {
  static constexpr const auto& values = coops::every_met;
};

}  // namespace mov::core

namespace mov::core::coops {

using DatumSet = EnumSet<Datum>;
using ObservedSet = EnumSet<Observed>;
using MetSet = EnumSet<Met>;

/// The interval of tide predictions, in minutes. `hilo` (high and low
/// waters) is not one: it is a v5.x addition (v5-extras #7).
enum class PredictionInterval : std::uint8_t {
  one,
  five,
  six,
  ten,
  fifteen,
  thirty,
  sixty
};

/// The common grid of observed and predicted levels fetched as a pair.
enum class PairedGrid : std::uint8_t { six_minute, hourly };

struct ObservedLevel {
  Observed series;
  friend constexpr bool operator==(ObservedLevel, ObservedLevel) = default;
};
struct PredictedLevel {
  PredictionInterval interval;
  friend constexpr bool operator==(PredictedLevel, PredictedLevel) = default;
};
/// Observed and predicted levels on one grid: one request, two exchanges.
struct ObservedVsPredicted {
  PairedGrid grid;
  friend constexpr bool operator==(ObservedVsPredicted,
                                   ObservedVsPredicted) = default;
};
using LevelKind =
    std::variant<ObservedLevel, PredictedLevel, ObservedVsPredicted>;

/// Water level of any kind. The API requires a datum for every kind and has
/// no neutral default, so the datum is Required: leaving it out does not
/// compile.
struct WaterLevel {
  LevelKind kind;
  Required<Datum> datum;
  friend constexpr bool operator==(const WaterLevel&,
                                   const WaterLevel&) = default;
};

/// The API's 6-minute default or `interval=h`.
enum class MetInterval : std::uint8_t { six_minute, hourly };

/// A met product: no datum, and an interval that must be named.
struct Meteorological {
  Met product;
  Required<MetInterval> interval;
  friend constexpr bool operator==(Meteorological, Meteorological) = default;
};

/// The distance from a bridge to the water (v5-extras #1): 6-minute data
/// only, with neither a datum nor an interval (the API ignores both; P2's
/// recordings, docs/providers-design.md section 1.1).
struct AirGap {
  friend constexpr bool operator==(AirGap, AirGap) = default;
};

/// Not default-constructible: a default would be some water level against
/// some datum.
using Product = std::variant<WaterLevel, Meteorological, AirGap>;
static_assert(not std::is_default_constructible_v<WaterLevel> and
              not std::is_default_constructible_v<Meteorological> and
              not std::is_default_constructible_v<Product>);

/// One station, one time range, one product.
struct Request {
  StationId<provider::Coops> station;
  TimeRange range;
  Product product;
  friend constexpr bool operator==(const Request&, const Request&) = default;
};

namespace detail {

struct DatumRow {
  Datum datum;
  VerticalDatum vertical;
  std::string_view wire;  // the data API's `datum=` value
};

// One row per enumerator, indexed by the enumerator (checked below).
inline constexpr std::array<DatumRow, every_datum.size()> datum_rows{{
    {.datum = Datum::mhhw, .vertical = VerticalDatum::mhhw, .wire = "MHHW"},
    {.datum = Datum::mhw, .vertical = VerticalDatum::mhw, .wire = "MHW"},
    {.datum = Datum::mtl, .vertical = VerticalDatum::mtl, .wire = "MTL"},
    {.datum = Datum::msl, .vertical = VerticalDatum::msl, .wire = "MSL"},
    {.datum = Datum::mlw, .vertical = VerticalDatum::mlw, .wire = "MLW"},
    {.datum = Datum::mllw, .vertical = VerticalDatum::mllw, .wire = "MLLW"},
    {.datum = Datum::navd88, .vertical = VerticalDatum::navd88, .wire = "NAVD"},
    {.datum = Datum::stnd, .vertical = VerticalDatum::stnd, .wire = "STND"},
    {.datum = Datum::igld85, .vertical = VerticalDatum::igld85, .wire = "IGLD"},
}};

[[nodiscard]] constexpr const DatumRow& row_of(Datum d) noexcept {
  return datum_rows[static_cast<std::size_t>(d)];
}

// Every datum finds its own row, and no two rows name one core datum.
static_assert(std::ranges::all_of(every_datum, [](Datum d) {
  return row_of(d).datum == d and
         std::ranges::count(datum_rows, row_of(d).vertical,
                            &DatumRow::vertical) == 1;
}));

}  // namespace detail

/// The core datum a CO-OPS datum is.
[[nodiscard]] constexpr VerticalDatum to_vertical(Datum d) noexcept {
  return detail::row_of(d).vertical;
}

/// The CO-OPS datum of a core datum; nullopt for NGVD29, which the data API
/// does not serve (decision 32).
[[nodiscard]] constexpr std::optional<Datum> from_vertical(
    VerticalDatum v) noexcept {
  const auto row =
      std::ranges::find(detail::datum_rows, v, &detail::DatumRow::vertical);
  if (row == detail::datum_rows.end()) {
    return std::nullopt;
  }
  return row->datum;
}

/// The data API's `datum=` value: "NAVD" for NAVD88, "IGLD" for IGLD85, else
/// the core token.
[[nodiscard]] constexpr std::string_view wire_token(Datum d) noexcept {
  return detail::row_of(d).wire;
}

/// The observed series of a paired grid.
[[nodiscard]] constexpr Observed observed(PairedGrid g) noexcept {
  switch (g) {
    case PairedGrid::six_minute:
      return Observed::six_minute;
    case PairedGrid::hourly:
      break;
  }
  return Observed::hourly;
}

/// The prediction interval of a paired grid.
[[nodiscard]] constexpr PredictionInterval predicted(PairedGrid g) noexcept {
  switch (g) {
    case PairedGrid::six_minute:
      return PredictionInterval::six;
    case PairedGrid::hourly:
      break;
  }
  return PredictionInterval::sixty;
}

/// The longest window the API serves for one observed series: 4 days at one
/// minute, 31 days at six (the live error text, provider-apis.md 2.1; the
/// documentation says "1 month"), a year hourly.
[[nodiscard]] constexpr WindowSpan max_window(Observed s) noexcept {
  switch (s) {
    case Observed::one_minute:
      return WindowSpan::of<4, std::chrono::days>();
    case Observed::six_minute:
      return WindowSpan::of<31, std::chrono::days>();
    case Observed::hourly:
      break;
  }
  return WindowSpan::of<365, std::chrono::days>();
}

/// Predictions at any interval but `hilo`: a year.
[[nodiscard]] constexpr WindowSpan max_window(
    PredictionInterval /*interval*/) noexcept {
  return WindowSpan::of<365, std::chrono::days>();
}

/// Met products follow the observed limits of their interval.
[[nodiscard]] constexpr WindowSpan max_window(MetInterval i) noexcept {
  switch (i) {
    case MetInterval::six_minute:
      return max_window(Observed::six_minute);
    case MetInterval::hourly:
      break;
  }
  return max_window(Observed::hourly);
}

/// The longest window one request of the product may span; a paired product
/// takes the smaller of its two halves.
[[nodiscard]] constexpr WindowSpan max_window(const Product& product) noexcept {
  return std::visit(
      Overloaded{
          [](const WaterLevel& w) {
            return std::visit(
                Overloaded{
                    [](ObservedLevel o) { return max_window(o.series); },
                    [](PredictedLevel p) { return max_window(p.interval); },
                    [](ObservedVsPredicted p) {
                      return std::min(max_window(observed(p.grid)),
                                      max_window(predicted(p.grid)));
                    }},
                w.kind);
          },
          [](Meteorological m) { return max_window(m.interval.get()); },
          [](AirGap) { return max_window(Observed::six_minute); }},
      product);
}

/// The grain windows are aligned to: whole minutes, for every product (the
/// API's begin_date and end_date have minutes).
inline constexpr WindowSpan grain = WindowSpan::of<1, std::chrono::minutes>();

/// What a product's result is made of: one series, an observed and a
/// predicted series, or the wind columns (speed, direction, gust).
enum class Shape : std::uint8_t { series, observed_vs_predicted, wind };

[[nodiscard]] constexpr Shape shape(const Product& product) noexcept {
  return std::visit(
      Overloaded{[](const WaterLevel& w) {
                   return std::holds_alternative<ObservedVsPredicted>(w.kind)
                              ? Shape::observed_vs_predicted
                              : Shape::series;
                 },
                 [](Meteorological m) {
                   return m.product == Met::wind ? Shape::wind : Shape::series;
                 },
                 [](AirGap) { return Shape::series; }},
      product);
}

}  // namespace mov::core::coops
