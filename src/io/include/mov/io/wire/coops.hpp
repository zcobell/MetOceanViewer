// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <array>
#include <cstddef>
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
#include "mov/core/quantity.hpp"
#include "mov/core/required.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/error.hpp"
#include "mov/io/part.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"

/// NOAA CO-OPS on the wire (docs/providers-design.md section 4): the requests
/// of a core::coops product and the readers of the responses. Every request
/// asks for UTC (`time_zone=gmt`), metric units and JSON; a water level is
/// requested in its datum (decision 13: nothing is shifted here). The bodies'
/// facts are pinned by the recordings in tests/fixtures/io/coops/
/// (provenance.txt).
namespace mov::io::wire::coops {

// ---- what one exchange asks for
// -------------------------------------------------

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

/// What one datagetter exchange asks for and its body holds: a product that
/// is not a pair. There is no default series (the datum is Required, as in
/// core::coops::WaterLevel).
using Series = std::variant<Observation, Prediction,
                            core::coops::Meteorological, core::coops::AirGap>;
static_assert(not std::is_default_constructible_v<Series>);

/// A product of one exchange.
struct Single {
  Series series;
  friend constexpr bool operator==(const Single&, const Single&) = default;
};

/// Observed and predicted water levels on one grid, in one datum: two
/// exchanges whose series both follow from the grid.
struct Pair {
  core::coops::PairedGrid grid;
  core::Required<core::coops::Datum> datum;

  [[nodiscard]] constexpr Observation observed() const noexcept {
    return {.series = core::coops::observed(grid), .datum = datum};
  }
  [[nodiscard]] constexpr Prediction predicted() const noexcept {
    return {.interval = core::coops::predicted(grid), .datum = datum};
  }
  friend constexpr bool operator==(Pair, Pair) = default;
};

using ProductSeries = std::variant<Single, Pair>;

[[nodiscard]] constexpr ProductSeries series_of(
    const core::coops::Product& product) noexcept {
  using namespace core::coops;
  return std::visit(
      core::Overloaded{
          [](const WaterLevel& w) -> ProductSeries {
            const Datum datum = w.datum.get();
            return std::visit(
                core::Overloaded{
                    [datum](ObservedLevel o) -> ProductSeries {
                      return Single{.series = Observation{.series = o.series,
                                                          .datum = datum}};
                    },
                    [datum](PredictedLevel p) -> ProductSeries {
                      return Single{.series = Prediction{.interval = p.interval,
                                                         .datum = datum}};
                    },
                    [datum](ObservedVsPredicted p) -> ProductSeries {
                      return Pair{.grid = p.grid, .datum = datum};
                    }},
                w.kind);
          },
          [](Meteorological m) -> ProductSeries { return Single{.series = m}; },
          [](AirGap a) -> ProductSeries { return Single{.series = a}; }},
      product);
}

/// The datagetter `product=` value of a met product.
[[nodiscard]] constexpr std::string_view product_name(
    core::coops::Met product) noexcept {
  using core::coops::Met;
  switch (product) {
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
      break;
  }
  return "visibility";
}

/// The datagetter `product=` value of a series.
[[nodiscard]] constexpr std::string_view product_name(
    const Series& series) noexcept {
  using namespace core::coops;
  return std::visit(
      core::Overloaded{
          [](Observation o) -> std::string_view {
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
          [](Prediction) -> std::string_view { return "predictions"; },
          [](Meteorological m) { return product_name(m.product); },
          [](AirGap) -> std::string_view { return "air_gap"; }},
      series);
}

/// The datagetter `interval=` value of a prediction interval; nullopt for
/// six minutes, the API's default.
[[nodiscard]] constexpr std::optional<std::string_view> interval_token(
    core::coops::PredictionInterval interval) noexcept {
  using core::coops::PredictionInterval;
  switch (interval) {
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
}

/// The `interval=` value of a met interval; nullopt for six minutes.
[[nodiscard]] constexpr std::optional<std::string_view> interval_token(
    core::coops::MetInterval interval) noexcept {
  switch (interval) {
    case core::coops::MetInterval::six_minute:
      return std::nullopt;
    case core::coops::MetInterval::hourly:
      break;
  }
  return "h";
}

/// The `interval=` value of a series; nullopt where the default is meant or
/// the product takes none (observed water level, whose interval is its
/// product, and air gap).
[[nodiscard]] constexpr std::optional<std::string_view> interval_token(
    const Series& series) noexcept {
  using Token = std::optional<std::string_view>;
  return std::visit(
      core::Overloaded{
          [](Observation) -> Token { return std::nullopt; },
          [](Prediction p) { return interval_token(p.interval); },
          [](core::coops::Meteorological m) {
            return interval_token(m.interval.get());
          },
          [](core::coops::AirGap) -> Token { return std::nullopt; }},
      series);
}

/// The datagetter `datum=` value of a series: water levels only.
[[nodiscard]] constexpr std::optional<core::coops::Datum> datum_of(
    const Series& series) noexcept {
  using Datum = std::optional<core::coops::Datum>;
  return std::visit(
      core::Overloaded{
          [](Observation o) -> Datum { return o.datum.get(); },
          [](Prediction p) -> Datum { return p.datum.get(); },
          [](core::coops::Meteorological) -> Datum { return std::nullopt; },
          [](core::coops::AirGap) -> Datum { return std::nullopt; }},
      series);
}

// ---- targets
// ------------------------------------------------------------------------

/// Why a window cannot be asked for.
enum class TargetErrc : std::uint8_t {
  no_whole_minute,          // the window holds no whole minute
  outside_four_digit_years  // `yyyyMMdd` spells years 0000-9999 only
};

struct TargetError {
  TargetErrc reason;
  core::TimeRange window;
  friend bool operator==(const TargetError&, const TargetError&) = default;
};

/// The exchange of a single product over one window.
struct SingleTarget {
  Series series;
  Composed target;
  friend bool operator==(const SingleTarget&, const SingleTarget&) = default;
};

/// The two exchanges of observed versus predicted over one window; their
/// series are pair.observed() and pair.predicted().
struct PairTargets {
  Pair pair;
  Composed observed;
  Composed predicted;
  friend bool operator==(const PairTargets&, const PairTargets&) = default;
};

using WindowTargets = std::variant<SingleTarget, PairTargets>;

/// The datagetter requests of `product` at `station` over `window` (one unit
/// of the plan, section 5.2). Parameters in this order: product, application
/// (MetOceanViewer), station, begin_date, end_date, datum (water levels
/// only), interval (when not the default), time_zone (gmt), units (metric),
/// format (json). The API's range is inclusive and its times whole minutes,
/// so the half-open window [b, e) is sent as the minutes it holds: begin_date
/// the first whole minute at or after b, end_date the last before e, as
/// `yyyyMMdd HH:mm`. Adjacent windows never share a sample (v4 bug L1).
[[nodiscard]] std::expected<WindowTargets, TargetError> targets(
    const core::StationId<core::provider::Coops>& station,
    const core::coops::Product& product, core::TimeRange window);

/// The metadata API's station lists: the water-level and met lists (and the
/// air-gap one, whose bridges are in neither) give the stations and their
/// sensors, the tide-prediction list each station's prediction type
/// (section 3.3).
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
// -----------------------------------------------------------------------------

/// The two CO-OPS APIs a body can come from.
enum class Api : std::uint8_t { data, metadata };

[[nodiscard]] constexpr Origin origin_of(Api api) noexcept {
  switch (api) {
    case Api::data:
      return Origin::coops_data;
    case Api::metadata:
      break;
  }
  return Origin::coops_metadata;
}

/// What a CO-OPS body says before its data is read. A refused request is
/// HTTP 200 with a message: data `{"error": {"message": "..."}}`, metadata
/// `{"errorMsg": "..."}`, a bad product name plain text, an API gateway's
/// failure `{"message": "..."}`. The message decides, in this order:
///  - "No data was found", "No Predictions data was found": NoData;
///  - "Range Limit Exceeded": range_exceeded;
///  - "The station is not a valid station or there is system error":
///    station_or_system;
///  - "Wrong Datum", "supported Datum values", "There is no <datum> for the
///    station": datum_unavailable;
///  - "Wrong Date", "Wrong Product", "Invalid type specified":
///    invalid_parameter;
///  - anything else: unknown; but the gateway's own `{"message": ...}`
///    (no error member, no data member) is `gateway`, a transient failure.
/// Markup is unexpected_content (sniff_markup). Every other body, JSON that
/// does not parse included, is a Payload. A leading byte order mark is
/// skipped.
[[nodiscard]] BodyClass classify_body(Api api, std::string_view body,
                                      std::string_view content_type);

/// A datagetter body, read: its series as a Part (io/part.hpp: one column,
/// or for wind wind_speed, wind_direction and wind_gust, each keyed by its
/// own quantity), or the service's NoData or refusal.
using DataReply = std::variant<Read<Part>, NoData, ServiceError>;

/// Classifies a datagetter body (classify_body, Api::data) and parses a
/// Payload for `series`: the one entry point, so no body is parsed before it
/// is classified.
///
/// The envelope is `{"metadata": ..., "data": [...]}`, or
/// `{"predictions": [...]}` for predictions. Each record's `t` is exactly
/// "yyyy-MM-dd HH:mm" (UTC); each value is a JSON string read by
/// detail::parse_double, "" a Missing sample. The columns (metric units):
///   water levels: water_level, predictions water_level_prediction (m, the
///     datum asked for), from `v`;
///   air_temperature, water_temperature (degC), air_pressure (hPa),
///     humidity -> relative_humidity (percent), visibility (km),
///     conductivity (mS/cm), from `v`;
///   wind: wind_speed `s` and wind_gust `g` (m/s), wind_direction `d`
///     (degree);
///   salinity `s` (PSU, the unit "1"); the specific gravity `g` is ignored;
///   air_gap (m, no datum), from `v`.
/// Members other than these and `f`, `q` are skipped unread. Warnings:
/// `suspect_samples_kept` (`coops:f`) for records with a nonzero quality
/// flag, `provisional_samples` (`coops:q`) for `q` "p", and, when the records
/// are not in strictly increasing time, one times_reordered /
/// duplicate_times_dropped / conflicting_duplicate_times about the product
/// (`coops:<product>`, e.g. `coops:wind`: a record conflicts when any column
/// differs).
/// Errors (io::Error): ParseError `not_json`; `too_large` (nesting past
/// max_json_depth, a body over ctx.limits.max_text_bytes, or more samples
/// than max_elements); `bad_member` (no data array, a record that is not an
/// object, `t` or a value missing or not a string, a repeated member);
/// `bad_date`; `bad_number` or `out_of_range` for a value; `empty_input`;
/// Cancelled (ctx.stop).
[[nodiscard]] std::expected<DataReply, Error> read_data(
    std::string_view body, std::string_view content_type, const Series& series,
    const ReadContext& ctx);

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
  /// and salinity).
  core::coops::MetSet met;
  /// An AIR GAP sensor with status 1.
  bool air_gap = false;
  /// Datums the list itself tells: IGLD85 at a Great Lakes station
  /// (`greatlakes` true), which serves IGLD while its datums.json does not
  /// name it (section 1.1); otherwise none.
  core::coops::DatumSet datums;
  friend bool operator==(const StationRecord&, const StationRecord&) = default;
};

/// Parses a station list (`{"stations": [...]}`). A station whose id is not a
/// CO-OPS id, whose name is not station text, whose position is not a
/// Location, whose dates do not parse or are inverted, whose sensors are
/// malformed, or whose members have the wrong JSON type, is left out:
/// `station_skipped`, one warning per reason (subject `id`, `name`,
/// `position`, `dates`, `sensors`, `member`) with the count. Sensor names
/// outside the pinned table are `unknown_sensor` (by name, counted). A
/// station's sensor list holds at most 256 sensors (`too_large`). Errors: as
/// read_data, plus `bad_member` when there is no stations array.
[[nodiscard]] std::expected<Read<std::vector<StationRecord>>, Error>
parse_stations(std::string_view body, const ReadContext& ctx);

/// The tide prediction a station has, by its type in the list.
enum class PredictionType : std::uint8_t {
  harmonic,    // type R: any interval
  subordinate  // type S: high and low waters only
};

/// One station of the tide-prediction list.
struct PredictionStation {
  core::StationId<core::provider::Coops> id;
  PredictionType type;
  friend bool operator==(const PredictionStation&,
                         const PredictionStation&) = default;
};

/// Parses the tide-prediction list. A station whose id is not a CO-OPS id
/// (the list holds TEC ids), or whose type is not R or S, is left out:
/// `station_skipped` by reason (`id`, `type`). Errors as parse_stations.
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
/// Errors otherwise as read_data.
[[nodiscard]] std::expected<Read<StationDatums>, Error> parse_datums(
    std::string_view body, const ReadContext& ctx);

}  // namespace mov::io::wire::coops
