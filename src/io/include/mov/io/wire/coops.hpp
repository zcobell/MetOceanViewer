// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <variant>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/required.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"

/// NOAA CO-OPS on the wire (docs/providers-design.md section 4): the requests
/// of a core::coops::Request and the parsers of the responses. Every request
/// asks for UTC (`time_zone=gmt`), metric units and JSON; a water level is
/// requested in its datum (decision 13: nothing is converted or shifted
/// here). The bodies' facts are pinned by the recordings in
/// tests/fixtures/io/coops/ (provenance.txt).
namespace mov::io::wire::coops {

// ---- what one exchange asks for
// ----------------------------------------------

/// An observed water-level series in a datum.
struct Observation {
  core::coops::Observed series;
  core::Required<core::coops::Datum> datum;
  friend constexpr bool operator==(Observation, Observation) = default;
};

/// Tide predictions at an interval, in a datum.
struct Prediction {
  core::coops::PredictionInterval interval;
  core::Required<core::coops::Datum> datum;
  friend constexpr bool operator==(Prediction, Prediction) = default;
};

/// What one datagetter exchange asks for and its body holds: a Product that
/// is not a pair. Observed-versus-predicted is two of these. There is no
/// default series (the datum is Required, as in core::coops::WaterLevel).
using Series =
    std::variant<Observation, Prediction, core::coops::Meteorological>;
static_assert(not std::is_default_constructible_v<Series>);

/// The series of a product in exchange order: one, or the observed series
/// and then the predictions of a pair on its grid.
struct ProductSeries {
  Series first;
  std::optional<Series> second;
  friend constexpr bool operator==(const ProductSeries&,
                                   const ProductSeries&) = default;
};

[[nodiscard]] constexpr ProductSeries series_of(
    const core::coops::Product& product) noexcept {
  using namespace core::coops;
  return std::visit(
      core::Overloaded{
          [](const WaterLevel& w) {
            const Datum datum = w.datum.get();
            return std::visit(
                core::Overloaded{
                    [datum](ObservedLevel o) {
                      return ProductSeries{
                          .first =
                              Observation{.series = o.series, .datum = datum},
                          .second = std::nullopt};
                    },
                    [datum](PredictedLevel p) {
                      return ProductSeries{
                          .first = Prediction{.interval = p.interval,
                                              .datum = datum},
                          .second = std::nullopt};
                    },
                    [datum](ObservedVsPredicted p) {
                      return ProductSeries{
                          .first = Observation{.series = observed(p.grid),
                                               .datum = datum},
                          .second = Series{Prediction{
                              .interval = predicted(p.grid), .datum = datum}}};
                    }},
                w.kind);
          },
          [](Meteorological m) {
            return ProductSeries{.first = m, .second = std::nullopt};
          }},
      product);
}

/// The datagetter `product=` value of a series.
[[nodiscard]] constexpr std::string_view product_name(
    const Series& series) noexcept {
  using namespace core::coops;
  return std::visit(core::Overloaded{[](Observation o) -> std::string_view {
                                       switch (o.series) {
                                         case Observed::six_minute:
                                           return "water_level";
                                         case Observed::one_minute:
                                           return "one_minute_water_level";
                                         case Observed::hourly:
                                           break;
                                       }
                                       return "hourly_height";
                                     },
                                     [](Prediction) -> std::string_view {
                                       return "predictions";
                                     },
                                     [](Meteorological m) -> std::string_view {
                                       switch (m.product) {
                                         case Met::air_temperature:
                                           return "air_temperature";
                                         case Met::water_temperature:
                                           return "water_temperature";
                                         case Met::wind:
                                           return "wind";
                                         case Met::air_pressure:
                                           return "air_pressure";
                                         case Met::humidity:
                                           return "humidity";
                                         case Met::conductivity:
                                           return "conductivity";
                                         case Met::salinity:
                                           return "salinity";
                                         case Met::visibility:
                                           return "visibility";
                                         case Met::air_gap:
                                           break;
                                       }
                                       return "air_gap";
                                     }},
                    series);
}

/// The datagetter `interval=` value of a series; nullopt where the API's
/// default is the one meant (6-minute predictions and met data) or the
/// product takes none (observed water level, whose interval is its product).
[[nodiscard]] constexpr std::optional<std::string_view> interval_token(
    const Series& series) noexcept {
  using namespace core::coops;
  using Token = std::optional<std::string_view>;
  return std::visit(
      core::Overloaded{[](Observation) -> Token { return std::nullopt; },
                       [](Prediction p) -> Token {
                         switch (p.interval) {
                           case PredictionInterval::one:
                             return "1";
                           case PredictionInterval::five:
                             return "5";
                           case PredictionInterval::six:
                             return std::nullopt;
                           case PredictionInterval::ten:
                             return "10";
                           case PredictionInterval::fifteen:
                             return "15";
                           case PredictionInterval::thirty:
                             return "30";
                           case PredictionInterval::sixty:
                             break;
                         }
                         return "h";
                       },
                       [](Meteorological m) -> Token {
                         if (m.interval.get() == MetInterval::hourly) {
                           return "h";
                         }
                         return std::nullopt;
                       }},
      series);
}

/// The datagetter `datum=` value of a series: water levels only (the API
/// ignores a datum on met products, air gap included).
[[nodiscard]] constexpr std::optional<core::coops::Datum> datum_of(
    const Series& series) noexcept {
  using Datum = std::optional<core::coops::Datum>;
  return std::visit(
      core::Overloaded{
          [](Observation o) -> Datum { return o.datum.get(); },
          [](Prediction p) -> Datum { return p.datum.get(); },
          [](core::coops::Meteorological) -> Datum { return std::nullopt; }},
      series);
}

// ---- targets
// ------------------------------------------------------------------

/// One exchange of a window: what it asks for and where.
struct DataTarget {
  Series series;
  Composed target;
  friend bool operator==(const DataTarget&, const DataTarget&) = default;
};

/// The exchanges of one window: one, or two for observed-versus-predicted
/// (the observed series first).
struct WindowTargets {
  DataTarget first;
  std::optional<DataTarget> second;
  friend bool operator==(const WindowTargets&, const WindowTargets&) = default;
};

/// The datagetter requests of `request` over `window` (one unit of the plan,
/// section 5.2). Parameters in this order: product, application
/// (MetOceanViewer), station, begin_date, end_date, datum (water levels
/// only), interval (when not the default), time_zone (gmt), units (metric),
/// format (json). The API's range is inclusive and its times are whole
/// minutes, so the half-open window [b, e) is sent as the minutes it holds:
/// begin_date the first whole minute at or after b, end_date the last whole
/// minute before e, as `yyyyMMdd HH:mm`. Adjacent windows therefore never
/// share a sample (v4 bug L1). FormatError `unrepresentable_time` when the
/// window holds no whole minute or a minute outside the years 0000-9999.
[[nodiscard]] std::expected<WindowTargets, FormatError> targets(
    const core::coops::Request& request, core::TimeRange window);

/// The metadata API's station lists. The catalog reads the water-level and
/// met lists (and the air-gap one, whose bridges are in neither) for the
/// stations and their sensors, and the tide-prediction list for each
/// station's prediction type (section 3.3).
enum class StationList : std::uint8_t {
  water_levels,
  met,
  air_gap,
  tide_predictions
};
inline constexpr std::array every_station_list{
    StationList::water_levels, StationList::met, StationList::air_gap,
    StationList::tide_predictions};

/// `stations.json?type=<list>`, with `expand=details,sensors` for all but the
/// tide-prediction list (whose stations have no sensors).
[[nodiscard]] Composed station_list_target(StationList list);

/// `stations/<id>/datums.json?units=metric`.
[[nodiscard]] Composed datums_target(
    const core::StationId<core::provider::Coops>& station);

// ---- bodies
// ---------------------------------------------------------------------

/// What a CO-OPS body says before its data is read. The data and metadata
/// APIs answer a refused request with HTTP 200 and a message: data
/// `{"error": {"message": "..."}}`, metadata `{"errorMsg": "..."}`, a bad
/// product name plain text. The message decides, in this order:
///  - "No data was found" or "No Predictions data was found": NoData;
///  - "Range Limit Exceeded": range_exceeded;
///  - "The station is not a valid station or there is system error":
///    station_or_system;
///  - "Wrong Datum", "supported Datum values", "There is no ... for the
///    station": datum_unavailable;
///  - "Wrong Date", "Wrong Product", "Invalid type specified":
///    invalid_parameter;
///  - anything else (an API gateway's `{"message": "..."}` too): unknown.
/// Markup is unexpected_content (sniff_markup). Every other body, JSON that
/// does not parse included, is a Payload: its parser reports what is wrong.
/// `origin` is coops_data or coops_metadata and is only carried into the
/// ServiceError.
[[nodiscard]] BodyClass classify_body(Origin origin, std::string_view body,
                                      std::string_view content_type);

/// A parsed body's series, by quantity token: one, or for wind the three
/// columns wind_speed, wind_direction and wind_gust. Each series is in time
/// order and has the unit CO-OPS delivers in metric and, for a water level,
/// the datum requested.
using Columns = std::map<std::string, core::TimeSeries, std::less<>>;

/// Parses a datagetter body for `series` (classify_body said Payload): the
/// `{"metadata": ..., "data": [...]}` envelope, or `{"predictions": [...]}`
/// for predictions. Each record's `t` ("yyyy-MM-dd HH:mm", UTC) through
/// core::parse_utc_datetime; each value, a JSON string, through
/// detail::parse_double, where "" is a Missing sample. Columns (unit):
///   water levels: water_level, predictions water_level_prediction (m, the
///     datum asked for), from `v`;
///   air_temperature, water_temperature (degC), air_pressure (hPa),
///     humidity -> relative_humidity (percent), visibility (km),
///     conductivity (mS/cm), from `v`;
///   wind: wind_speed `s` and wind_gust `g` (m/s), wind_direction `d`
///     (degree); `dr` is ignored;
///   salinity: salinity `s` (PSU); the specific gravity `g` is ignored;
///   air_gap (m, no datum: the API takes none), from `v`.
/// mS/cm and PSU are outside the unit table: `unrecognized_unit`.
/// Warnings: `suspect_samples_kept` (`coops:f`) for records with a nonzero
/// quality flag, `provisional_samples` (`coops:q`) for `q` "p", and
/// times_reordered / duplicate_times_dropped / conflicting_duplicate_times
/// (`coops:<column>`) when the records are not in strictly increasing time
/// (core::normalize). Members other than these are ignored.
/// Errors (io::Error): ParseError `not_json`; `too_large` (nesting past
/// max_json_depth, a body over ctx.limits.max_text_bytes, or more samples
/// than max_elements); `bad_member` (no data array, a record that is not an
/// object, `t` or a value missing or not a string, a repeated member);
/// `bad_date`; `bad_number` or `out_of_range` for a value; `empty_input`;
/// Cancelled (ctx.stop, polled every 4096 records).
[[nodiscard]] std::expected<Read<Columns>, Error> parse_data(
    std::string_view body, const Series& series, const ReadContext& ctx);

/// One station of a station list (water level, met or air gap).
struct StationRecord {
  /// Name and position as listed; validity from `details.established` and
  /// `details.removed` ("" or null: unknown and ongoing; never v4's 2050, L9).
  core::GaugeStation<core::provider::Coops> station;
  /// Observed series by sensor, status 1 only: a primary water-level sensor
  /// (Acoustic, Microwave, Pressure, SAE or Backup WL) gives the 6-minute and
  /// hourly series, the Tsunami WL sensor the 1-minute one.
  core::coops::ObservedSet observed;
  /// Met products by sensor, status 1 only (Conductivity gives conductivity
  /// and salinity, AIR GAP air_gap).
  core::coops::MetSet met;
  /// The list's `greatlakes`: CO-OPS serves IGLD there, which datums.json does
  /// not name, and its station datum is IGLD85.
  bool great_lakes;
  friend bool operator==(const StationRecord&, const StationRecord&) = default;
};

/// Parses a station list (`{"stations": [...]}`). A station whose id is not a
/// CO-OPS id, whose name is not station text, whose position is not a
/// Location, whose dates do not parse or are inverted, or whose members have
/// the wrong JSON type is left out with a `station_skipped` warning. Sensor
/// names outside the pinned table are `unknown_sensor` (by name, counted).
/// Errors: as parse_data, plus `bad_member` when there is no stations array.
[[nodiscard]] std::expected<Read<std::vector<StationRecord>>, Error>
parse_stations(std::string_view body, const ReadContext& ctx);

/// One station of the tide-prediction list.
struct PredictionStation {
  core::StationId<core::provider::Coops> id;
  /// Type R (harmonic): any_interval; S (subordinate): hilo_only.
  core::coops::Predictions predictions;
  friend bool operator==(const PredictionStation&,
                         const PredictionStation&) = default;
};

/// Parses the tide-prediction list. A station whose id is not a CO-OPS id
/// (the list holds TEC ids) or whose type is not R or S is left out with
/// `station_skipped`. Errors as parse_stations.
[[nodiscard]] std::expected<Read<std::vector<PredictionStation>>, Error>
parse_prediction_stations(std::string_view body, const ReadContext& ctx);

/// What a station's datums.json says.
struct StationDatums {
  /// The listed datums the data API takes (core::coops::Datum). STND is
  /// served at every station whether listed or not (core::offered).
  core::coops::DatumSet offered;
  /// The heights, relative to STND, as a table; nullopt when MSL is not
  /// listed (Great Lakes stations list only their low water datum; air-gap
  /// stations have `"datums": null`).
  std::optional<core::DatumTable> table;
  friend bool operator==(const StationDatums&, const StationDatums&) = default;
};

/// Parses datums.json (requested in metric: `units` must be "meters", else
/// FormatError `noncanonical_unit`). Names map by a pinned table: MHHW, MHW,
/// MTL, MSL, MLW, MLLW, NAVD88, STND, NGVD29 (in the table, never offered:
/// decision 32), IGLD and IGLD85. DTL, the ranges (GT, MN, DHQ, DLQ) and the
/// low water datums (LWD, GL_LWD, CRD) are skipped; any other name is a
/// `datum_unknown` warning. A row whose value is null is skipped. Conflicting
/// rows and non-finite heights are FormatError `inconsistent_metadata`.
/// Errors otherwise as parse_data.
[[nodiscard]] std::expected<Read<StationDatums>, Error> parse_datums(
    std::string_view body, const ReadContext& ctx);

}  // namespace mov::io::wire::coops
