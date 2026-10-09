// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <variant>

#include "mov/core/datum.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/station.hpp"
#include "mov/core/units.hpp"

namespace mov::core {

namespace coops {

/// The tide predictions a station has, by its entry in the CO-OPS
/// `type=tidepredictions` station list: a harmonic station (type R) has
/// them at every interval, a subordinate one (type S) only as high and low
/// waters (`hilo`, which v5.0 does not offer), and a station that is not in
/// the list has none. The station's `tidal` flag does not say this: on
/// 2026-10-09 56 met stations had `tidal` true and no predictions at all
/// (docs/providers-design.md section 1.1).
enum class Predictions : std::uint8_t { none, hilo_only, any_interval };

/// Why a CO-OPS station does not offer a product.
enum class NotOffered : std::uint8_t {
  product,      // no such product: no water level, or no such met sensor
  interval,     // water level or predictions, but not this series or interval
  predictions,  // no tide predictions
  datum,        // the datum is not served at the station
};

}  // namespace coops

namespace usgs {

/// Why a USGS site does not offer a request.
enum class NotOffered : std::uint8_t {
  parameter,  // no series of the parameter
  statistic,  // the parameter or series, but not with the statistic or in
              // the collection the request needs
  series,     // no series with the id
};

}  // namespace usgs

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
  coops::Predictions predictions = coops::Predictions::none;
  /// Heights from datums.json; nullopt when that failed or had no MSL.
  std::optional<DatumTable> datums;
  /// The datums.json names that are coops::Datum values. STND need not be
  /// listed: every station serves its station datum, and offered() says so.
  coops::DatumSet datums_offered;
  friend constexpr bool operator==(const Capabilities&,
                                   const Capabilities&) = default;
};

template <>
struct Capabilities<provider::Usgs> {
  /// The station catalog holds the map filter's series (decision 33); the
  /// site metadata fetched on selection holds every series of the site.
  usgs::SeriesSet series;
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

/// Whether the station offers the product: one check for the CLI and the
/// app's picker. Water level is checked in this order: the level kind
/// (product when the station has no water level, interval when it lacks the
/// observed series or has only high/low predictions, predictions when it
/// has none), then the datum (STND is always served). A met product needs
/// its sensor.
[[nodiscard]] constexpr std::expected<void, coops::NotOffered> offered(
    const Capabilities<provider::Coops>& caps,
    const coops::Product& product) noexcept {
  using coops::NotOffered;
  using Offer = std::expected<void, NotOffered>;
  const auto series_offered = [&caps](coops::Observed s) -> Offer {
    if (caps.observed.empty()) {
      return std::unexpected{NotOffered::product};
    }
    if (not caps.observed.contains(s)) {
      return std::unexpected{NotOffered::interval};
    }
    return {};
  };
  // Every PredictionInterval is one a subordinate station refuses.
  const auto predictions_offered = [&caps]() -> Offer {
    switch (caps.predictions) {
      case coops::Predictions::none:
        return std::unexpected{NotOffered::predictions};
      case coops::Predictions::hilo_only:
        return std::unexpected{NotOffered::interval};
      case coops::Predictions::any_interval:
        break;
    }
    return {};
  };
  const auto datum_offered = [&caps](coops::Datum d) -> Offer {
    if (d != coops::Datum::stnd and not caps.datums_offered.contains(d)) {
      return std::unexpected{NotOffered::datum};
    }
    return {};
  };
  return std::visit(
      Overloaded{
          [&](const coops::WaterLevel& w) -> Offer {
            const Offer kind = std::visit(
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
            return kind.and_then([&] { return datum_offered(w.datum.get()); });
          },
          [&caps](coops::Meteorological m) -> Offer {
            if (not caps.met.contains(m.product)) {
              return std::unexpected{NotOffered::product};
            }
            return {};
          }},
      product);
}

/// Whether the site's series offer the request. By parameter: parameter when
/// no series has it, then statistic when none has it with the request's
/// statistic (00011 for continuous values; a daily request naming 00011 is
/// refused too). By id: series when no series has the id, then statistic
/// when the series is of the other collection. Whether a series is Primary
/// is resolution's question (resolve), not this one.
[[nodiscard]] std::expected<void, usgs::NotOffered> offered(
    const Capabilities<provider::Usgs>& caps, const usgs::Request& request);

}  // namespace mov::core
