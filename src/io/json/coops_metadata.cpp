// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The CO-OPS metadata API: station lists and datums.json
// (docs/providers-design.md sections 3.3 and 4.3).

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "document.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/reporting.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/coops.hpp"

namespace mov::io::wire::coops {

namespace {

using json::Body;
using json::Json;
using StationId = core::StationId<core::provider::Coops>;

// What a station costs against ReadLimits::max_elements (core-design.md
// section 5): as much as sixteen samples.
constexpr std::size_t elements_per_station = 16;

std::expected<void, Error> check_station_limit(const Body& body,
                                               std::size_t index,
                                               const Json& element,
                                               const ReadContext& ctx) {
  const std::size_t used = (index + 1) * elements_per_station;
  if (used > ctx.limits.max_elements) {
    return detail::fail(
        ParseError::make(ParseErrc::too_large, body.where(element),
                         std::to_string(used) + " elements, limit " +
                             std::to_string(ctx.limits.max_elements)));
  }
  return {};
}

// The array a list or datums.json holds, after the parse streamed it: its
// absence, or any other type, is `bad_member`.
std::expected<void, Error> require_array(const Body& body, const Json& document,
                                         std::string_view key) {
  const Json* array = json::find_member(document, key);
  if (array == nullptr or not array->is_array()) {
    return detail::fail(body.error_at(ParseErrc::bad_member,
                                      array == nullptr ? document : *array));
  }
  return {};
}

// ---- sensors
// -------------------------------------------------------------------

// What an active sensor offers.
struct SensorOffer {
  core::coops::ObservedSet observed;
  core::coops::MetSet met;
};

struct SensorRow {
  std::string_view name;
  SensorOffer offer;
};

using core::coops::Met;
using core::coops::Observed;

constexpr SensorOffer primary_water_level{
    .observed = {Observed::six_minute, Observed::hourly}, .met = {}};

// The pinned sensor names (live lists of 2026-10-15). The last four offer
// nothing a product needs.
constexpr std::array sensor_table{
    SensorRow{.name = "Acoustic WL", .offer = primary_water_level},
    SensorRow{.name = "Microwave WL", .offer = primary_water_level},
    SensorRow{.name = "Pressure WL", .offer = primary_water_level},
    SensorRow{.name = "SAE WL", .offer = primary_water_level},
    SensorRow{.name = "Backup WL", .offer = primary_water_level},
    SensorRow{.name = "Tsunami WL",
              .offer = {.observed = {Observed::one_minute}, .met = {}}},
    SensorRow{.name = "Wind", .offer = {.observed = {}, .met = {Met::wind}}},
    SensorRow{.name = "Air Temperature",
              .offer = {.observed = {}, .met = {Met::air_temperature}}},
    SensorRow{.name = "Water Temperature",
              .offer = {.observed = {}, .met = {Met::water_temperature}}},
    SensorRow{.name = "Barometric Pressure",
              .offer = {.observed = {}, .met = {Met::air_pressure}}},
    SensorRow{.name = "Relative Humidity",
              .offer = {.observed = {}, .met = {Met::humidity}}},
    SensorRow{
        .name = "Conductivity",
        .offer = {.observed = {}, .met = {Met::conductivity, Met::salinity}}},
    SensorRow{.name = "Visibility",
              .offer = {.observed = {}, .met = {Met::visibility}}},
    SensorRow{.name = "AIR GAP",
              .offer = {.observed = {}, .met = {Met::air_gap}}},
    SensorRow{.name = "site", .offer = {}},
    SensorRow{.name = "Rain Fall", .offer = {}},
    SensorRow{.name = "ANALOG SENSOR 1", .offer = {}},
    SensorRow{.name = "ANALOG SENSOR 2", .offer = {}},
};

// The CO-OPS status of a sensor in service.
constexpr std::int64_t active_status = 1;

// ---- one station
// ------------------------------------------------------------------

// Why a station is left out; the reader only counts it.
struct Skip {};

template <class T>
using Kept = std::expected<T, Skip>;

// A string member that may be absent or null; Skip for any other type.
Kept<std::optional<std::string_view>> optional_text(const Json& object,
                                                    std::string_view name) {
  const Json* member = json::find_member(object, name);
  if (member == nullptr or member->is_null()) {
    return std::optional<std::string_view>{};
  }
  const std::string* text = json::as_string(*member);
  if (text == nullptr) {
    return std::unexpected{Skip{}};
  }
  return std::optional<std::string_view>{*text};
}

// "yyyy-mm-dd hh:mm:ss" as its day; "" or null is unknown.
Kept<std::optional<std::chrono::sys_days>> day_of(const Json& details,
                                                  std::string_view name) {
  const auto text = optional_text(details, name);
  if (not text) {
    return std::unexpected{Skip{}};
  }
  if (not *text or (*text)->empty()) {
    return std::optional<std::chrono::sys_days>{};
  }
  const auto time = core::parse_utc_datetime(**text);
  if (not time) {
    return std::unexpected{Skip{}};
  }
  return std::optional<std::chrono::sys_days>{
      std::chrono::floor<std::chrono::days>(*time)};
}

// `details.established` and `details.removed` (no details: unknown,
// ongoing).
Kept<core::ValidRange> validity_of(const Json& station) {
  const Json* details = json::find_member(station, "details");
  if (details == nullptr or details->is_null()) {
    return core::ValidRange{};
  }
  if (not details->is_object()) {
    return std::unexpected{Skip{}};
  }
  const auto first = day_of(*details, "established");
  const auto last = day_of(*details, "removed");
  if (not first or not last) {
    return std::unexpected{Skip{}};
  }
  const auto range = core::ValidRange::make({.first = *first, .last = *last});
  if (not range) {
    return std::unexpected{Skip{}};
  }
  return *range;
}

Kept<core::Location> location_of(const Json& station) {
  const Json* lat = json::find_member(station, "lat");
  const Json* lng = json::find_member(station, "lng");
  const std::optional<double> y =
      lat == nullptr ? std::nullopt : json::as_double(*lat);
  const std::optional<double> x =
      lng == nullptr ? std::nullopt : json::as_double(*lng);
  if (not y or not x) {
    return std::unexpected{Skip{}};
  }
  const auto location = core::Location::make({.lat = *y, .lon = *x});
  if (not location) {
    return std::unexpected{Skip{}};
  }
  return *location;
}

// Sensor names outside the table, with how often each was met.
using UnknownSensors = std::map<std::string, std::size_t, std::less<>>;

// A sensor's status: an integer (nlohmann reads one without a sign as
// unsigned); only whether it is the active status matters.
std::optional<bool> active_of(const Json& sensor) {
  const Json* status = json::find_member(sensor, "status");
  if (status == nullptr) {
    return std::nullopt;
  }
  if (const auto* code = status->get_ptr<const Json::number_integer_t*>()) {
    return *code == active_status;
  }
  if (const auto* code = status->get_ptr<const Json::number_unsigned_t*>()) {
    return std::cmp_equal(*code, active_status);
  }
  return std::nullopt;
}

// `sensors.sensors`: absent or null is no sensor.
Kept<SensorOffer> sensors_of(const Json& station, UnknownSensors& unknown) {
  SensorOffer offer;
  const Json* sensors = json::find_member(station, "sensors");
  if (sensors == nullptr or sensors->is_null()) {
    return offer;
  }
  if (not sensors->is_object()) {
    return std::unexpected{Skip{}};
  }
  const Json* list = json::find_member(*sensors, "sensors");
  if (list == nullptr or list->is_null()) {
    return offer;
  }
  if (not list->is_array()) {
    return std::unexpected{Skip{}};
  }
  for (const Json& sensor : *list) {
    const Json* name = json::find_member(sensor, "name");
    const std::string* text =
        name == nullptr ? nullptr : json::as_string(*name);
    const std::optional<bool> active = active_of(sensor);
    if (text == nullptr or not active) {
      return std::unexpected{Skip{}};
    }
    const auto row = std::ranges::find(sensor_table, std::string_view{*text},
                                       &SensorRow::name);
    if (row == sensor_table.end()) {
      ++unknown[*text];
    } else if (*active) {
      offer.observed = offer.observed | row->offer.observed;
      offer.met = offer.met | row->offer.met;
    }
  }
  return offer;
}

Kept<bool> great_lakes_of(const Json& station) {
  const Json* member = json::find_member(station, "greatlakes");
  if (member == nullptr or member->is_null()) {
    return false;
  }
  const auto* flag = member->get_ptr<const Json::boolean_t*>();
  if (flag == nullptr) {
    return std::unexpected{Skip{}};
  }
  return *flag;
}

Kept<StationRecord> station_of(const Json& station, std::string_view raw_id,
                               UnknownSensors& unknown) {
  const auto id = StationId::make(raw_id);
  const auto name_text = optional_text(station, "name");
  if (not id or not name_text) {
    return std::unexpected{Skip{}};
  }
  auto name = core::StationText::make(std::string{name_text->value_or("")});
  const auto location = location_of(station);
  const auto validity = validity_of(station);
  const auto great_lakes = great_lakes_of(station);
  if (not name or not location or not validity or not great_lakes) {
    return std::unexpected{Skip{}};
  }
  // A station left out does not report its sensor names.
  UnknownSensors unknown_here;
  const auto offer = sensors_of(station, unknown_here);
  if (not offer) {
    return std::unexpected{Skip{}};
  }
  for (const auto& [sensor, count] : unknown_here) {
    unknown[sensor] += count;
  }
  return StationRecord{.station = {.id = *id,
                                   .name = *std::move(name),
                                   .location = *location,
                                   .validity = *validity},
                       .observed = offer->observed,
                       .met = offer->met,
                       .great_lakes = *great_lakes};
}

// The raw id of a list element, as the warning's subject: "" when it has no
// string id.
std::string_view raw_id_of(const Json& station) {
  const Json* id = json::find_member(station, "id");
  const std::string* text = id == nullptr ? nullptr : json::as_string(*id);
  return text == nullptr ? std::string_view{} : std::string_view{*text};
}

void skipped(std::vector<Warning>& warnings, std::string_view raw_id) {
  warnings.push_back({.code = WarningCode::station_skipped,
                      .subject = detail::subject_of(raw_id)});
}

// ---- datums
// ------------------------------------------------------------------------

// A datums.json name: a datum, or a name known to carry none for the table.
struct DatumName {
  std::string_view name;
  std::optional<core::VerticalDatum> datum;
};

using core::VerticalDatum;

constexpr std::array datum_names{
    DatumName{.name = "MHHW", .datum = VerticalDatum::mhhw},
    DatumName{.name = "MHW", .datum = VerticalDatum::mhw},
    DatumName{.name = "MTL", .datum = VerticalDatum::mtl},
    DatumName{.name = "MSL", .datum = VerticalDatum::msl},
    DatumName{.name = "MLW", .datum = VerticalDatum::mlw},
    DatumName{.name = "MLLW", .datum = VerticalDatum::mllw},
    DatumName{.name = "NAVD88", .datum = VerticalDatum::navd88},
    DatumName{.name = "NGVD29", .datum = VerticalDatum::ngvd29},
    DatumName{.name = "STND", .datum = VerticalDatum::stnd},
    DatumName{.name = "IGLD", .datum = VerticalDatum::igld85},
    DatumName{.name = "IGLD85", .datum = VerticalDatum::igld85},
    // Not datums the API or the table take: DTL; the ranges; the low water
    // datums of lakes and rivers.
    DatumName{.name = "DTL", .datum = std::nullopt},
    DatumName{.name = "GT", .datum = std::nullopt},
    DatumName{.name = "MN", .datum = std::nullopt},
    DatumName{.name = "DHQ", .datum = std::nullopt},
    DatumName{.name = "DLQ", .datum = std::nullopt},
    DatumName{.name = "LWD", .datum = std::nullopt},
    DatumName{.name = "GL_LWD", .datum = std::nullopt},
    DatumName{.name = "CRD", .datum = std::nullopt},
};

FormatError inconsistent(const core::DatumTableError& e) {
  const std::string subject = std::visit(
      core::Overloaded{[](core::MissingMsl) { return std::string{"MSL"}; },
                       [](core::ConflictingHeight c) {
                         return std::string{core::to_string(c.datum)};
                       },
                       [](core::NonFiniteHeight n) {
                         return std::string{core::to_string(n.datum)};
                       }},
      e);
  return detail::format_error(FormatErrc::inconsistent_metadata, subject);
}

class DatumsReader {
 public:
  DatumsReader(const Body& body, const ReadContext& ctx)
      : body_{body}, ctx_{ctx} {}

  std::expected<Read<StationDatums>, Error> read() && {
    const auto document = json::parse_streaming(
        body_, "datums", ctx_,
        [this](std::size_t index, Json& row) { return add(index, row); });
    if (not document) {
      return std::unexpected{document.error()};
    }
    if (auto units = check_units(*document); not units) {
      return std::unexpected{std::move(units).error()};
    }
    const Json* datums = json::find_member(*document, "datums");
    if (datums == nullptr or not(datums->is_null() or datums->is_array())) {
      return detail::fail(body_.error_at(
          ParseErrc::bad_member, datums == nullptr ? *document : *datums));
    }
    return std::move(*this).datums();
  }

 private:
  [[nodiscard]] std::expected<void, Error> check_units(
      const Json& document) const {
    const auto units = json::string_member(body_, document, "units");
    if (not units) {
      return detail::fail(units.error());
    }
    if (*units != "meters") {
      return detail::fail(
          detail::format_error(FormatErrc::noncanonical_unit,
                               "units: " + detail::subject_of(*units)));
    }
    return {};
  }

  std::expected<void, Error> add(std::size_t index, const Json& row) {
    if (index + 1 > ctx_.limits.max_elements) {
      return detail::fail(
          ParseError::make(ParseErrc::too_large, body_.where(row),
                           std::to_string(index + 1) + " elements, limit " +
                               std::to_string(ctx_.limits.max_elements)));
    }
    if (not row.is_object()) {
      return detail::fail(body_.error_at(ParseErrc::bad_member, row));
    }
    const auto name = json::string_member(body_, row, "name");
    if (not name) {
      return detail::fail(name.error());
    }
    const Json* value = json::find_member(row, "value");
    if (value == nullptr) {
      return detail::fail(body_.error_at(ParseErrc::bad_member, row));
    }
    if (value->is_null()) {
      return {};
    }
    const std::optional<double> metres = json::as_double(*value);
    if (not metres) {
      return detail::fail(body_.error_at(ParseErrc::bad_member, *value));
    }
    const auto known = std::ranges::find(datum_names, *name, &DatumName::name);
    if (known == datum_names.end()) {
      warnings_.push_back({.code = WarningCode::datum_unknown,
                           .subject = detail::subject_of(*name)});
      return {};
    }
    if (known->datum) {
      heights_.push_back(
          {.datum = *known->datum,
           .height = core::Length::in(*metres, core::LengthUnit::meter)});
    }
    return {};
  }

  std::expected<Read<StationDatums>, Error> datums() && {
    StationDatums out{.offered = {}, .table = std::nullopt};
    for (const core::DatumHeight& row : heights_) {
      if (const auto datum = core::coops::from_vertical(row.datum)) {
        out.offered.insert(*datum);
      }
    }
    auto table = core::DatumTable::from_heights(VerticalDatum::stnd, heights_);
    if (table) {
      out.table = *table;
    } else if (not std::holds_alternative<core::MissingMsl>(table.error())) {
      return detail::fail(inconsistent(table.error()));
    }
    return Read<StationDatums>{.value = out, .warnings = std::move(warnings_)};
  }

  const Body& body_;
  const ReadContext& ctx_;
  std::vector<core::DatumHeight> heights_;
  std::vector<Warning> warnings_;
};

}  // namespace

std::expected<Read<std::vector<StationRecord>>, Error> parse_stations(
    std::string_view body, const ReadContext& ctx) {
  const Body text{body};
  Read<std::vector<StationRecord>> out{.value = {}, .warnings = {}};
  UnknownSensors unknown;
  const auto document = json::parse_streaming(
      text, "stations", ctx,
      [&](std::size_t index, Json& station) -> std::expected<void, Error> {
        if (auto room = check_station_limit(text, index, station, ctx);
            not room) {
          return room;
        }
        const std::string_view raw_id = raw_id_of(station);
        auto record = station_of(station, raw_id, unknown);
        if (record) {
          out.value.push_back(*std::move(record));
        } else {
          skipped(out.warnings, raw_id);
        }
        return {};
      });
  if (not document) {
    return std::unexpected{document.error()};
  }
  if (auto list = require_array(text, *document, "stations"); not list) {
    return std::unexpected{std::move(list).error()};
  }
  for (const auto& [name, count] : unknown) {
    out.warnings.push_back({.code = WarningCode::unknown_sensor,
                            .subject = detail::subject_of(name),
                            .count = count});
  }
  return out;
}

std::expected<Read<std::vector<PredictionStation>>, Error>
parse_prediction_stations(std::string_view body, const ReadContext& ctx) {
  const Body text{body};
  Read<std::vector<PredictionStation>> out{.value = {}, .warnings = {}};
  const auto document = json::parse_streaming(
      text, "stations", ctx,
      [&](std::size_t index, Json& station) -> std::expected<void, Error> {
        if (auto room = check_station_limit(text, index, station, ctx);
            not room) {
          return room;
        }
        const std::string_view raw_id = raw_id_of(station);
        const auto id = StationId::make(raw_id);
        const auto type = optional_text(station, "type");
        const auto kind =
            type ? type->value_or(std::string_view{}) : std::string_view{};
        if (not id or (kind != "R" and kind != "S")) {
          skipped(out.warnings, raw_id);
          return {};
        }
        out.value.push_back(
            {.id = *id,
             .predictions = kind == "R" ? core::coops::Predictions::any_interval
                                        : core::coops::Predictions::hilo_only});
        return {};
      });
  if (not document) {
    return std::unexpected{document.error()};
  }
  if (auto list = require_array(text, *document, "stations"); not list) {
    return std::unexpected{std::move(list).error()};
  }
  return out;
}

std::expected<Read<StationDatums>, Error> parse_datums(std::string_view body,
                                                       const ReadContext& ctx) {
  const Body text{body};
  return DatumsReader{text, ctx}.read();
}

}  // namespace mov::io::wire::coops
