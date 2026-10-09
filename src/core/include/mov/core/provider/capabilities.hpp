// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <variant>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/station.hpp"
#include "mov/core/units.hpp"

namespace mov::core {

/// What a station's catalog entry says it can serve, per provider
/// (docs/providers-design.md section 3.3). The station list fills it; the CLI
/// and the app's product picker ask offered().
template <Provider P>
struct Capabilities;

template <>
struct Capabilities<provider::Coops> {
  /// The observed water-level series, from the water-level sensor.
  coops::ObservedSet observed;
  /// The met products of the sensors with status 1.
  coops::MetSet met;
  /// Tide predictions exist (the station's `tidal` member).
  bool predictions = false;
  /// Heights from datums.json; nullopt when that failed or had no MSL.
  std::optional<DatumTable> datums;
  /// The datums the data API serves at the station: the datums.json names
  /// that are coops::Datum values, plus STND, which every station has.
  coops::DatumSet datums_offered;
  friend constexpr bool operator==(const Capabilities&,
                                   const Capabilities&) = default;
};

template <>
struct Capabilities<provider::Usgs> {
  /// The station catalog holds the map filter's series (decision 33); the
  /// site metadata fetched on selection holds every series of the site.
  std::vector<usgs::SeriesInfo> series;
  friend bool operator==(const Capabilities&, const Capabilities&) = default;
};

template <>
struct Capabilities<provider::Ndbc> {
  StationText owner;
  StationText program;
  StationText platform_type;
  std::optional<Length> elevation;
  friend bool operator==(const Capabilities&, const Capabilities&) = default;
};

/// Why a station does not offer a request.
enum class NotOffered : std::uint8_t {
  product,      // CO-OPS: no such product (no water level, no such sensor)
  interval,     // CO-OPS: water level, but not this observed series
  predictions,  // CO-OPS: no tide predictions
  datum,        // CO-OPS: the datum is not served at the station
  parameter,    // USGS: no series of the parameter
  statistic,    // USGS: the parameter or series, but not with the statistic
                // the request needs
  series,       // USGS: no series with the id
};

/// Whether the station offers the product: one check for the CLI and the
/// app's picker. Checked in this order: the level kind (product, interval,
/// predictions), then the datum.
[[nodiscard]] constexpr std::expected<void, NotOffered> offered(
    const Capabilities<provider::Coops>& caps,
    const coops::Product& product) noexcept {
  const auto series_offered =
      [&caps](coops::Observed s) -> std::expected<void, NotOffered> {
    if (caps.observed.empty()) {
      return std::unexpected{NotOffered::product};
    }
    if (not caps.observed.contains(s)) {
      return std::unexpected{NotOffered::interval};
    }
    return {};
  };
  const auto predictions_offered =
      [&caps]() -> std::expected<void, NotOffered> {
    if (not caps.predictions) {
      return std::unexpected{NotOffered::predictions};
    }
    return {};
  };
  return std::visit(
      Overloaded{
          [&](const coops::WaterLevel& w) -> std::expected<void, NotOffered> {
            const auto kind = std::visit(
                Overloaded{[&](coops::ObservedLevel o) {
                             return series_offered(o.series);
                           },
                           [&](coops::PredictedLevel) {
                             return predictions_offered();
                           },
                           [&](coops::ObservedVsPredicted p) {
                             return series_offered(coops::observed(p.grid))
                                 .and_then(predictions_offered);
                           }},
                w.kind);
            if (not kind) {
              return kind;
            }
            if (not caps.datums_offered.contains(w.datum)) {
              return std::unexpected{NotOffered::datum};
            }
            return {};
          },
          [&caps](coops::Meteorological m) -> std::expected<void, NotOffered> {
            if (not caps.met.contains(m.product)) {
              return std::unexpected{NotOffered::product};
            }
            return {};
          }},
      product);
}

/// Whether the station's series list offers the request. By parameter:
/// parameter when no series has it, then statistic when none has it with the
/// request's statistic (00011 for continuous values). By id: series when no
/// series has the id, then statistic when the series is of the other
/// collection (00011 is continuous, anything else daily). Whether a series
/// is Primary is resolution's question, not this one.
[[nodiscard]] std::expected<void, NotOffered> offered(
    const Capabilities<provider::Usgs>& caps, const usgs::Request& request);

}  // namespace mov::core
