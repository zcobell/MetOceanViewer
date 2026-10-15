// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The CO-OPS metadata API: station lists and datums.json
// (docs/providers-design.md sections 3.3, 4.3 and 4.8).

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/coops_json.hpp"
#include "mov/io/detail/json.hpp"
#include "mov/io/detail/reporting.hpp"
#include "mov/io/detail/text_position.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/coops.hpp"

namespace mov::io::wire::coops {

namespace {

namespace json = io::detail::json;
using StationId = core::StationId<core::provider::Coops>;

// ---- what is kept
// ------------------------------------------------------------

// The most sensors one station lists: 8 on 2026-10-15.
constexpr std::size_t max_sensors = 256;

constexpr std::array sensor_fields{
    json::Field{.name = "name", .keep = &json::leaf},
    json::Field{.name = "status", .keep = &json::leaf}};
constexpr json::Keep sensor_schema{.fields = sensor_fields};
constexpr json::Keep sensor_list_schema{.element = &sensor_schema,
                                        .max_elements = max_sensors};
constexpr std::array sensors_fields{
    json::Field{.name = "sensors", .keep = &sensor_list_schema}};
constexpr json::Keep sensors_schema{.fields = sensors_fields};
constexpr std::array details_fields{
    json::Field{.name = "established", .keep = &json::leaf},
    json::Field{.name = "removed", .keep = &json::leaf}};
constexpr json::Keep details_schema{.fields = details_fields};
constexpr std::array station_fields{
    json::Field{.name = "id", .keep = &json::leaf},
    json::Field{.name = "name", .keep = &json::leaf},
    json::Field{.name = "lat", .keep = &json::leaf},
    json::Field{.name = "lng", .keep = &json::leaf},
    json::Field{.name = "greatlakes", .keep = &json::leaf},
    json::Field{.name = "details", .keep = &details_schema},
    json::Field{.name = "sensors", .keep = &sensors_schema}};
constexpr json::Keep station_schema{.fields = station_fields};
constexpr json::Keep stations_schema{.element = &station_schema};
constexpr std::array station_list_fields{json::Field{
    .name = "stations", .keep = &stations_schema, .streamed = true}};
constexpr json::Keep station_list_schema{.fields = station_list_fields};

constexpr std::array prediction_station_fields{
    json::Field{.name = "id", .keep = &json::leaf},
    json::Field{.name = "type", .keep = &json::leaf}};
constexpr json::Keep prediction_station_schema{.fields =
                                                   prediction_station_fields};
constexpr json::Keep prediction_stations_schema{.element =
                                                    &prediction_station_schema};
constexpr std::array prediction_list_fields{json::Field{
    .name = "stations", .keep = &prediction_stations_schema, .streamed = true}};
constexpr json::Keep prediction_list_schema{.fields = prediction_list_fields};

constexpr std::array datum_row_fields{
    json::Field{.name = "name", .keep = &json::leaf},
    json::Field{.name = "value", .keep = &json::leaf}};
constexpr json::Keep datum_row_schema{.fields = datum_row_fields};
constexpr json::Keep datum_rows_schema{.element = &datum_row_schema};
constexpr std::array datums_fields{
    json::Field{.name = "units", .keep = &json::leaf},
    json::Field{
        .name = "datums", .keep = &datum_rows_schema, .streamed = true}};
constexpr json::Keep datums_document_schema{.fields = datums_fields};

// ---- limits
// -----------------------------------------------------------------------

// What a station costs against ReadLimits::max_elements (core-design.md
// section 5): as much as sixteen samples.
constexpr std::size_t elements_per_station = 16;

std::expected<void, Error> check_limit(std::string_view body, std::size_t used,
                                       const json::Value& element,
                                       const ReadContext& ctx) {
  if (used > ctx.limits.max_elements) {
    return io::detail::fail(ParseError::make(
        ParseErrc::too_large, io::detail::where_of(body, element.offset),
        std::to_string(used) + " elements, limit " +
            std::to_string(ctx.limits.max_elements)));
  }
  return {};
}

// The streamed array a list or datums.json holds: its absence, or any other
// type, is `bad_member`.
std::expected<void, Error> require_array(std::string_view body,
                                         const json::Value& document,
                                         std::string_view key) {
  const json::Value* array = json::member(document, key);
  if (array == nullptr or json::as_array(*array) == nullptr) {
    return io::detail::fail(
        json::error_at(ParseErrc::bad_member, body,
                       array == nullptr ? document.offset : array->offset));
  }
  return {};
}

// ---- why a station is left out
// ---------------------------------------------------

enum class Skip : std::uint8_t {
  id,
  name,
  position,
  dates,
  sensors,
  member,  // a member of the wrong JSON type
  type     // a tide-prediction type that is not R or S
};

constexpr std::array skip_tokens{
    std::string_view{"id"},       std::string_view{"name"},
    std::string_view{"position"}, std::string_view{"dates"},
    std::string_view{"sensors"},  std::string_view{"member"},
    std::string_view{"type"}};
static_assert(skip_tokens.size() == static_cast<std::size_t>(Skip::type) + 1);

// station_skipped by reason, in the order above.
class Skipped {
 public:
  void add(Skip why) noexcept { ++counts_.at(static_cast<std::size_t>(why)); }

  void report(std::vector<Warning>& warnings) const {
    for (std::size_t i = 0; i < counts_.size(); ++i) {
      append_if_counted(warnings, {.code = WarningCode::station_skipped,
                                   .subject = std::string{skip_tokens.at(i)},
                                   .count = counts_.at(i)});
    }
  }

 private:
  std::array<std::size_t, skip_tokens.size()> counts_{};
};

template <class T>
using Kept = std::expected<T, Skip>;

// ---- sensors
// -----------------------------------------------------------------------

// What an active sensor offers.
struct SensorOffer {
  core::coops::ObservedSet observed;
  core::coops::MetSet met;
  bool air_gap = false;
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
              .offer = {.observed = {}, .met = {}, .air_gap = true}},
    SensorRow{.name = "site", .offer = {}},
    SensorRow{.name = "Rain Fall", .offer = {}},
    SensorRow{.name = "ANALOG SENSOR 1", .offer = {}},
    SensorRow{.name = "ANALOG SENSOR 2", .offer = {}},
};

// The CO-OPS status of a sensor in service.
constexpr std::int64_t active_status = 1;

// Whether a status, an integer, is the active one; nullopt when it is no
// integer.
std::optional<bool> active_of(const json::Value& status) {
  if (const auto code = json::as_int(status)) {
    return *code == active_status;
  }
  if (std::holds_alternative<std::uint64_t>(status.data)) {
    return false;  // past INT64_MAX: not 1
  }
  return std::nullopt;
}

// Sensor names outside the table, with how often each was met.
using UnknownSensors = std::map<std::string, std::size_t, std::less<>>;

// `sensors.sensors`: absent or null is no sensor.
Kept<SensorOffer> sensors_of(const json::Value& station,
                             UnknownSensors& unknown) {
  SensorOffer offer;
  const json::Value* sensors = json::member(station, "sensors");
  if (sensors == nullptr or json::is_null(*sensors)) {
    return offer;
  }
  if (json::as_object(*sensors) == nullptr) {
    return std::unexpected{Skip::sensors};
  }
  const json::Value* list = json::member(*sensors, "sensors");
  if (list == nullptr or json::is_null(*list)) {
    return offer;
  }
  const json::Array* entries = json::as_array(*list);
  if (entries == nullptr) {
    return std::unexpected{Skip::sensors};
  }
  for (const json::Value& entry : *entries) {
    const json::Value* name = json::member(entry, "name");
    const json::Value* status = json::member(entry, "status");
    const std::string* text =
        name == nullptr ? nullptr : json::as_string(*name);
    const std::optional<bool> active =
        status == nullptr ? std::nullopt : active_of(*status);
    if (text == nullptr or not active) {
      return std::unexpected{Skip::sensors};
    }
    const auto row = std::ranges::find(sensor_table, std::string_view{*text},
                                       &SensorRow::name);
    if (row == sensor_table.end()) {
      ++unknown[*text];
    } else if (*active) {
      offer.observed = offer.observed | row->offer.observed;
      offer.met = offer.met | row->offer.met;
      offer.air_gap = offer.air_gap or row->offer.air_gap;
    }
  }
  return offer;
}

// ---- one station
// --------------------------------------------------------------------

// A string member that may be absent or null.
Kept<std::optional<std::string_view>> optional_text(const json::Value& object,
                                                    std::string_view name) {
  const json::Value* value = json::member(object, name);
  if (value == nullptr or json::is_null(*value)) {
    return std::optional<std::string_view>{};
  }
  const std::string* text = json::as_string(*value);
  if (text == nullptr) {
    return std::unexpected{Skip::member};
  }
  return std::optional<std::string_view>{*text};
}

// "yyyy-mm-dd hh:mm:ss" as its day; "" or null is unknown.
Kept<std::optional<std::chrono::sys_days>> day_of(const json::Value& object,
                                                  std::string_view name) {
  const auto text = optional_text(object, name);
  if (not text) {
    return std::unexpected{Skip::dates};
  }
  if (not *text or (*text)->empty()) {
    return std::optional<std::chrono::sys_days>{};
  }
  const auto time = core::parse_utc_datetime(**text);
  if (not time) {
    return std::unexpected{Skip::dates};
  }
  return std::optional<std::chrono::sys_days>{
      std::chrono::floor<std::chrono::days>(*time)};
}

// `details.established` and `details.removed` (no details: unknown,
// ongoing).
Kept<core::ValidRange> validity_of(const json::Value& station) {
  const json::Value* details = json::member(station, "details");
  if (details == nullptr or json::is_null(*details)) {
    return core::ValidRange{};
  }
  if (json::as_object(*details) == nullptr) {
    return std::unexpected{Skip::dates};
  }
  const auto first = day_of(*details, "established");
  if (not first) {
    return std::unexpected{first.error()};
  }
  const auto last = day_of(*details, "removed");
  if (not last) {
    return std::unexpected{last.error()};
  }
  const auto range = core::ValidRange::make({.first = *first, .last = *last});
  if (not range) {
    return std::unexpected{Skip::dates};
  }
  return *range;
}

Kept<core::Location> location_of(const json::Value& station) {
  const json::Value* lat = json::member(station, "lat");
  const json::Value* lng = json::member(station, "lng");
  const std::optional<double> y =
      lat == nullptr ? std::nullopt : json::as_double(*lat);
  const std::optional<double> x =
      lng == nullptr ? std::nullopt : json::as_double(*lng);
  if (not y or not x) {
    return std::unexpected{Skip::position};
  }
  const auto location = core::Location::make({.lat = *y, .lon = *x});
  if (not location) {
    return std::unexpected{Skip::position};
  }
  return *location;
}

// IGLD85 at a Great Lakes station (`greatlakes` true).
Kept<core::coops::DatumSet> listed_datums(const json::Value& station) {
  const json::Value* member = json::member(station, "greatlakes");
  if (member == nullptr or json::is_null(*member)) {
    return core::coops::DatumSet{};
  }
  const bool* flag = std::get_if<bool>(&member->data);
  if (flag == nullptr) {
    return std::unexpected{Skip::member};
  }
  return *flag ? core::coops::DatumSet{core::coops::Datum::igld85}
               : core::coops::DatumSet{};
}

// The raw id of a list element: "" when it has no string id.
std::string_view raw_id_of(const json::Value& station) {
  const json::Value* id = json::member(station, "id");
  const std::string* text = id == nullptr ? nullptr : json::as_string(*id);
  return text == nullptr ? std::string_view{} : std::string_view{*text};
}

Kept<StationRecord> station_of(const json::Value& station,
                               UnknownSensors& unknown) {
  if (json::as_object(station) == nullptr) {
    return std::unexpected{Skip::member};
  }
  const auto id = StationId::make(raw_id_of(station));
  if (not id) {
    return std::unexpected{Skip::id};
  }
  const auto name_text = optional_text(station, "name");
  if (not name_text) {
    return std::unexpected{Skip::name};
  }
  auto name = core::StationText::make(std::string{name_text->value_or("")});
  if (not name) {
    return std::unexpected{Skip::name};
  }
  const auto location = location_of(station);
  if (not location) {
    return std::unexpected{location.error()};
  }
  const auto validity = validity_of(station);
  if (not validity) {
    return std::unexpected{validity.error()};
  }
  const auto datums = listed_datums(station);
  if (not datums) {
    return std::unexpected{datums.error()};
  }
  // A station left out does not report its sensor names.
  UnknownSensors unknown_here;
  const auto offer = sensors_of(station, unknown_here);
  if (not offer) {
    return std::unexpected{offer.error()};
  }
  for (const auto& [sensor_name, count] : unknown_here) {
    unknown[sensor_name] += count;
  }
  return StationRecord{.station = {.id = *id,
                                   .name = *std::move(name),
                                   .location = *location,
                                   .validity = *validity},
                       .observed = offer->observed,
                       .met = offer->met,
                       .air_gap = offer->air_gap,
                       .datums = *datums};
}

Kept<PredictionStation> prediction_station_of(const json::Value& station) {
  if (json::as_object(station) == nullptr) {
    return std::unexpected{Skip::member};
  }
  const auto id = StationId::make(raw_id_of(station));
  if (not id) {
    return std::unexpected{Skip::id};
  }
  const auto type = optional_text(station, "type");
  if (not type) {
    return std::unexpected{Skip::type};
  }
  const std::string_view kind = type->value_or(std::string_view{});
  if (kind == "R") {
    return PredictionStation{.id = *id, .type = PredictionType::harmonic};
  }
  if (kind == "S") {
    return PredictionStation{.id = *id, .type = PredictionType::subordinate};
  }
  return std::unexpected{Skip::type};
}

// ---- datums
// ---------------------------------------------------------------------------

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
  return io::detail::format_error(FormatErrc::inconsistent_metadata, subject);
}

class DatumsReader {
 public:
  DatumsReader(std::string_view body, const ReadContext& ctx)
      : body_{body}, ctx_{ctx} {}

  std::expected<Read<StationDatums>, Error> read() && {
    const auto document =
        json::parse(body_, datums_document_schema, ctx_,
                    [this](std::size_t index, const json::Value& row) {
                      return add(index, row);
                    });
    if (not document) {
      return std::unexpected{document.error()};
    }
    if (auto units = check_units(document->root); not units) {
      return std::unexpected{std::move(units).error()};
    }
    const json::Value* datums = json::member(document->root, "datums");
    if (datums == nullptr or
        (not json::is_null(*datums) and json::as_array(*datums) == nullptr)) {
      return io::detail::fail(json::error_at(
          ParseErrc::bad_member, body_,
          datums == nullptr ? document->root.offset : datums->offset));
    }
    return std::move(*this).datums();
  }

 private:
  [[nodiscard]] std::expected<void, Error> check_units(
      const json::Value& document) const {
    const json::Value* units = json::member(document, "units");
    const std::string* text =
        units == nullptr ? nullptr : json::as_string(*units);
    if (text == nullptr) {
      return io::detail::fail(
          json::error_at(ParseErrc::bad_member, body_,
                         units == nullptr ? document.offset : units->offset));
    }
    if (*text != "meters") {
      return io::detail::fail(
          io::detail::format_error(FormatErrc::noncanonical_unit,
                                   "units: " + io::detail::subject_of(*text)));
    }
    return {};
  }

  std::expected<void, Error> add(std::size_t index, const json::Value& row) {
    if (auto room = check_limit(body_, index + 1, row, ctx_); not room) {
      return room;
    }
    if (json::as_object(row) == nullptr) {
      return io::detail::fail(
          json::error_at(ParseErrc::bad_member, body_, row.offset));
    }
    const json::Value* name = json::member(row, "name");
    const std::string* text =
        name == nullptr ? nullptr : json::as_string(*name);
    if (text == nullptr) {
      return io::detail::fail(
          json::error_at(ParseErrc::bad_member, body_,
                         name == nullptr ? row.offset : name->offset));
    }
    const json::Value* value = json::member(row, "value");
    if (value == nullptr) {
      return io::detail::fail(
          json::error_at(ParseErrc::bad_member, body_, row.offset));
    }
    if (json::is_null(*value)) {
      return {};
    }
    const std::optional<double> metres = json::as_double(*value);
    if (not metres) {
      return io::detail::fail(
          json::error_at(ParseErrc::bad_member, body_, value->offset));
    }
    const auto known = std::ranges::find(datum_names, std::string_view{*text},
                                         &DatumName::name);
    if (known == datum_names.end()) {
      warnings_.push_back({.code = WarningCode::datum_unknown,
                           .subject = io::detail::subject_of(*text)});
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
      return io::detail::fail(inconsistent(table.error()));
    }
    return Read<StationDatums>{.value = out, .warnings = std::move(warnings_)};
  }

  std::string_view body_;
  const ReadContext& ctx_;
  std::vector<core::DatumHeight> heights_;
  std::vector<Warning> warnings_;
};

}  // namespace

std::expected<Read<std::vector<StationRecord>>, Error> parse_stations(
    std::string_view body, const ReadContext& ctx) {
  Read<std::vector<StationRecord>> out{.value = {}, .warnings = {}};
  UnknownSensors unknown;
  Skipped skipped;
  const auto document = json::parse(
      body, station_list_schema, ctx,
      [&](std::size_t index,
          const json::Value& element) -> std::expected<void, Error> {
        if (auto room = check_limit(body, (index + 1) * elements_per_station,
                                    element, ctx);
            not room) {
          return room;
        }
        auto record = station_of(element, unknown);
        if (record) {
          out.value.push_back(*std::move(record));
        } else {
          skipped.add(record.error());
        }
        return {};
      });
  if (not document) {
    return std::unexpected{document.error()};
  }
  if (auto list = require_array(body, document->root, "stations"); not list) {
    return std::unexpected{std::move(list).error()};
  }
  skipped.report(out.warnings);
  for (const auto& [name, count] : unknown) {
    out.warnings.push_back({.code = WarningCode::unknown_sensor,
                            .subject = io::detail::subject_of(name),
                            .count = count});
  }
  return out;
}

std::expected<Read<std::vector<PredictionStation>>, Error>
parse_prediction_stations(std::string_view body, const ReadContext& ctx) {
  Read<std::vector<PredictionStation>> out{.value = {}, .warnings = {}};
  Skipped skipped;
  const auto document = json::parse(
      body, prediction_list_schema, ctx,
      [&](std::size_t index,
          const json::Value& element) -> std::expected<void, Error> {
        if (auto room = check_limit(body, (index + 1) * elements_per_station,
                                    element, ctx);
            not room) {
          return room;
        }
        auto station_type = prediction_station_of(element);
        if (station_type) {
          out.value.push_back(*station_type);
        } else {
          skipped.add(station_type.error());
        }
        return {};
      });
  if (not document) {
    return std::unexpected{document.error()};
  }
  if (auto list = require_array(body, document->root, "stations"); not list) {
    return std::unexpected{std::move(list).error()};
  }
  skipped.report(out.warnings);
  return out;
}

std::expected<Read<StationDatums>, Error> parse_datums(std::string_view body,
                                                       const ReadContext& ctx) {
  return DatumsReader{body, ctx}.read();
}

}  // namespace mov::io::wire::coops

namespace mov::io::detail::coops {

const json::Keep& station_list_keep() {
  return wire::coops::station_list_schema;
}
const json::Keep& prediction_list_keep() {
  return wire::coops::prediction_list_schema;
}
const json::Keep& datums_keep() { return wire::coops::datums_document_schema; }

}  // namespace mov::io::detail::coops
