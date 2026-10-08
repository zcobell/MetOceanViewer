// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The legacy v4 station netCDF (docs/legacy-formats.md section 5, docs/
// station-netcdf.md section 11): dimensions `numStations` and
// `stationNameLen`, `stationName` (and `stationId`) rows, `stationXCoordinate`
// / `stationYCoordinate`, and per station N `stationLength_N`, `time_station_N`
// (seconds since the `referenceDate` attribute) and `data_station_N`.
//
// What v4 got wrong is not repeated (plan 1.2): the row stride is the file's
// (B7), `referenceDate` is read at its real length (B8), the default fill is
// masked (B9), a missing EPSG is an assumed 4326 with a warning and a wrong
// type is an error, never a code (B10), and whatever the writer left after the
// NUL of a name is cut (core-design.md C14).

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "model_netcdf.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/detail/ascii.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/io/cf_time.hpp"
#include "mov/io/detail/legacy_station_names.hpp"
#include "mov/io/detail/station_names.hpp"
#include "mov/io/detail/table_error.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/projection.hpp"
#include "mov/io/read.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "station_netcdf_dialects.hpp"
#include "station_netcdf_reader.hpp"

namespace mov::io::detail::station_nc {

namespace {

constexpr std::string_view time_prefix = "time_station_";
constexpr std::string_view data_prefix = "data_station_";
constexpr std::string_view length_prefix = "stationLength_";
constexpr std::array<int, 2> widths{4, 6};
constexpr std::size_t stations_per_stop_poll = 256;
/// `referenceDate` is `yyyy-MM-dd hh:mm:ss`: v4 read these 19 characters.
constexpr std::size_t reference_date_chars = 19;
constexpr std::string_view default_reference = "1970-01-01 00:00:00";

// ---- the variables that are not per station ---------------------------------

struct Base {
  nc::DimInfo stations;
  nc::VarInfo x;
  nc::VarInfo y;
  nc::VarInfo name;
  std::optional<nc::VarInfo> id;
};

/// A char variable over (numStations, length).
std::expected<nc::VarInfo, Error> char_rows(nc::VarInfo var,
                                            const nc::DimInfo& stations) {
  if (var.type != nc::Type::char_) {
    return invalid(FormatErrc::bad_encoding, std::string{var.name.view()});
  }
  if (var.dims.size() != 2 or var.dims[0].id != stations.id) {
    return invalid(FormatErrc::dimension_mismatch,
                   std::string{var.name.view()});
  }
  return var;
}

std::expected<nc::VarInfo, Error> over_stations(nc::VarInfo var,
                                                const nc::DimInfo& stations) {
  return require_shape(var, {stations.id}).transform([&] { return var; });
}

/// The variables as they are in the file, before their shapes are checked.
struct Found {
  nc::DimInfo stations;
  nc::VarInfo x;
  nc::VarInfo y;
  nc::VarInfo name;
  std::optional<nc::VarInfo> id;
};

std::expected<Found, Error> find_variables(const nc::File& file) {
  auto stations = require_dim(file, "numStations");
  if (not stations) {
    return std::unexpected{std::move(stations).error()};
  }
  auto x = require_var(file, "stationXCoordinate");
  if (not x) {
    return std::unexpected{std::move(x).error()};
  }
  auto y = require_var(file, "stationYCoordinate");
  if (not y) {
    return std::unexpected{std::move(y).error()};
  }
  auto name = require_var(file, "stationName");
  if (not name) {
    return std::unexpected{std::move(name).error()};
  }
  auto id = file.find_var("stationId");
  if (not id) {
    return fail(std::move(id).error());
  }
  return Found{.stations = *std::move(stations),
               .x = *std::move(x),
               .y = *std::move(y),
               .name = *std::move(name),
               .id = *std::move(id)};
}

std::expected<Base, Error> find_base(const nc::File& file) {
  auto found = find_variables(file);
  if (not found) {
    return std::unexpected{std::move(found).error()};
  }
  const nc::DimInfo& stations = found->stations;
  auto x = over_stations(std::move(found->x), stations);
  if (not x) {
    return std::unexpected{std::move(x).error()};
  }
  auto y = over_stations(std::move(found->y), stations);
  if (not y) {
    return std::unexpected{std::move(y).error()};
  }
  auto name = char_rows(std::move(found->name), stations);
  if (not name) {
    return std::unexpected{std::move(name).error()};
  }
  std::optional<nc::VarInfo> id;
  if (found->id) {
    auto rows = char_rows(*std::move(found->id), stations);
    if (not rows) {
      return std::unexpected{std::move(rows).error()};
    }
    id = *std::move(rows);
  }
  return Base{.stations = std::move(found->stations),
              .x = *std::move(x),
              .y = *std::move(y),
              .name = *std::move(name),
              .id = std::move(id)};
}

/// The digits of the station numbers: those of the first station's time
/// variable (v4 writes four, a CRMS-style file six).
std::expected<int, Error> number_width(const nc::File& file) {
  for (const int width : widths) {
    const std::string name = legacy_variable_name(time_prefix, 1, width);
    const auto ref = nc::NcName::make(name);
    if (not ref) {
      continue;
    }
    auto found = file.find_var(*ref);
    if (not found) {
      return fail(std::move(found).error());
    }
    if (*found) {
      return width;
    }
  }
  return invalid(FormatErrc::missing_variable,
                 legacy_variable_name(time_prefix, 1, widths.front()), 0);
}

/// Dialect A carries the marks of `Hmdf::writeNetcdf`.
std::expected<LegacyDialect, Error> dialect_of(const nc::File& file,
                                               const Base& base) {
  if (base.id) {
    return LegacyDialect::a;
  }
  return text_of(file, nc::global, "fileformat")
      .transform([](const std::optional<std::string>& text) {
        return text ? LegacyDialect::a : LegacyDialect::b;
      });
}

// ---- the CRS ----------------------------------------------------------------

bool is_type_mismatch(const NcError& e) {
  const auto* fault = std::get_if<WrapperFault>(&e.status);
  return fault != nullptr and *fault == WrapperFault::type_mismatch;
}

constexpr nc::NcNameRef x_var{"stationXCoordinate"};
constexpr nc::NcNameRef epsg_att{"HorizontalProjectionEPSG"};

template <class T>
std::expected<std::optional<std::int64_t>, NcError> epsg_as(
    const nc::File& file) {
  auto values = file.numeric_att<T>(x_var, epsg_att);
  if (not values) {
    return std::unexpected{std::move(values).error()};
  }
  if (not *values) {
    return std::optional<std::int64_t>{};
  }
  if ((*values)->size() != 1) {
    return std::unexpected{
        NcError{.status = WrapperFault::count_mismatch,
                .op = NcOp::get_att,
                .object = "stationXCoordinate:HorizontalProjectionEPSG",
                .file = file.path()}};
  }
  return std::optional<std::int64_t>{(*values)->front()};
}

/// The EPSG code of `stationXCoordinate`, any integer type; nullopt when the
/// attribute is absent. A text or floating-point attribute is the first
/// attempt's type_mismatch (B10: an error, never a code).
std::expected<std::optional<std::int64_t>, Error> read_epsg(
    const nc::File& file) {
  auto as_int = epsg_as<std::int32_t>(file);
  if (as_int or not is_type_mismatch(as_int.error())) {
    return as_int.transform_error(lift<Error>);
  }
  if (auto wide = epsg_as<std::int64_t>(file);
      wide or not is_type_mismatch(wide.error())) {
    return wide.transform_error(lift<Error>);
  }
  if (auto small = epsg_as<std::int16_t>(file);
      small or not is_type_mismatch(small.error())) {
    return small.transform_error(lift<Error>);
  }
  if (auto tiny = epsg_as<std::int8_t>(file);
      tiny or not is_type_mismatch(tiny.error())) {
    return tiny.transform_error(lift<Error>);
  }
  return fail(std::move(as_int).error());
}

/// The file's CRS: its EPSG code, or 4326 with a warning when it has none.
std::expected<Read<core::Epsg>, Error> crs_of(const nc::File& file) {
  auto code = read_epsg(file);
  if (not code) {
    return std::unexpected{std::move(code).error()};
  }
  if (not *code) {
    return Read<core::Epsg>{.value = core::Epsg::wgs84(),
                            .warnings = {{.code = WarningCode::crs_assumed,
                                          .subject = "EPSG:4326"}}};
  }
  const std::string subject = "EPSG:" + std::to_string(**code);
  constexpr std::int64_t largest = 999'999'999;  // nine digits, as v5 reads
  if (**code <= 0 or **code > largest) {
    return invalid(FormatErrc::unsupported_crs, subject);
  }
  const auto epsg = core::Epsg::make(static_cast<int>(**code));
  if (not epsg) {
    return invalid(FormatErrc::unsupported_crs, subject);
  }
  return Read<core::Epsg>{.value = *epsg, .warnings = {}};
}

/// What turns the file's x, y into Locations: nothing for EPSG:4326, a
/// Projector for any other CRS (legacy files are often in UTM).
std::expected<std::optional<Projector>, Error> projector_of(core::Epsg epsg) {
  if (epsg == core::Epsg::wgs84()) {
    return std::optional<Projector>{};
  }
  auto projector = Projector::make(epsg);
  if (not projector) {
    return fail(to_format_error(projector.error(), std::nullopt));
  }
  return std::optional<Projector>{*std::move(projector)};
}

// ---- the stations -----------------------------------------------------------

struct Cleaned {
  std::string text;
  bool replaced;
};

/// A name: cut at the first NUL (the writer's junk follows it), white space
/// collapsed (v4's `simplified()`, A8), bytes that are not UTF-8 replaced.
Cleaned clean_name(std::string_view row) {
  const std::string simple = simplified(cut_at_nul(row));
  auto cleaned = replace_invalid_utf8(simple);
  return {.text = std::string{cleaned.text.view()},
          .replaced = cleaned.replaced};
}

/// An id: cut at the first NUL and trimmed.
Cleaned clean_id(std::string_view row) {
  const std::string_view trimmed_id = core::detail::trim(cut_at_nul(row));
  auto cleaned = replace_invalid_utf8(trimmed_id);
  return {.text = std::string{cleaned.text.view()},
          .replaced = cleaned.replaced};
}

struct Names {
  std::vector<std::string> ids;
  std::vector<std::string> names;
  std::size_t replaced{0};
};

std::expected<Names, Error> read_names(const nc::File& file, const Base& base,
                                       const StopToken& stop) {
  auto parts =
      collect([&] { return file.read_char_rows(base.name.name, stop); },
              [&]() -> std::expected<std::vector<std::string>, Error> {
                if (not base.id) {
                  return std::vector<std::string>{};
                }
                return file.read_char_rows(base.id->name, stop);
              });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  const auto& [name_rows, id_rows] = *parts;
  Names out;
  out.ids.reserve(name_rows.size());
  out.names.reserve(name_rows.size());
  for (std::size_t i = 0; i < name_rows.size(); ++i) {
    const Cleaned name = clean_name(name_rows[i]);
    Cleaned id =
        base.id ? clean_id(id_rows[i]) : Cleaned{.text = "", .replaced = false};
    out.replaced += (name.replaced ? 1U : 0U) + (id.replaced ? 1U : 0U);
    // The id is the stationId, else the name, else the decimal index.
    if (id.text.empty()) {
      id.text = name.text.empty() ? std::to_string(i) : name.text;
    }
    out.names.push_back(name.text.empty() ? "Station " + id.text : name.text);
    out.ids.push_back(std::move(id.text));
  }
  return out;
}

/// The Locations of every station, projected from `epsg`.
std::expected<Read<std::vector<core::FileStation>>, Error> stations_of(
    const nc::File& file, const Base& base, core::Epsg epsg,
    const StopToken& stop) {
  auto parts = collect([&] { return read_names(file, base, stop); },
                       [&] { return coordinate(file, base.x, stop); },
                       [&] { return coordinate(file, base.y, stop); },
                       [&] { return projector_of(epsg); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  auto& [names, xs, ys, projector] = *parts;
  const UniqueIds unique = uniquify_ids(names.ids);
  Read<std::vector<core::FileStation>> out{.value = {}, .warnings = {}};
  out.value.reserve(names.ids.size());
  for (std::size_t i = 0; i < names.ids.size(); ++i) {
    auto position = place(ys[i], xs[i], projector, i);
    if (not position) {
      return std::unexpected{std::move(position).error()};
    }
    auto key = core::StationKey::make(unique.ids[i]);
    auto text = core::StationText::make(names.names[i]);
    if (not key) {
      return fail(to_format_error(key.error(), i));
    }
    if (not text) {
      return fail(to_format_error(text.error(), i));
    }
    out.value.push_back({.id = *std::move(key),
                         .name = *std::move(text),
                         .location = position->location,
                         .native = position->native,
                         .source = std::nullopt});
  }
  append_if_counted(out.warnings, {.code = WarningCode::invalid_utf8_replaced,
                                   .subject = {},
                                   .count = names.replaced});
  for (const RenamedName& r : unique.renamed) {
    out.warnings.push_back({.code = WarningCode::duplicate_station_id_renamed,
                            .subject = subject_of(r.name),
                            .count = r.count});
  }
  if (projector) {
    if (auto w = projector->approximation_warning()) {
      out.warnings.push_back(*std::move(w));
    }
  }
  return out;
}

// ---- the per-station variables ----------------------------------------------

/// Variable `<prefix><number>` of station `i` (0-based), or missing_variable
/// naming it and the station.
std::expected<nc::VarInfo, Error> station_var(const nc::File& file,
                                              std::string_view prefix,
                                              std::size_t i, int width) {
  const std::string name = legacy_variable_name(prefix, i + 1, width);
  const auto ref = nc::NcName::make(name);
  if (not ref) {
    return invalid(FormatErrc::missing_variable, subject_of(name), i);
  }
  auto found = file.find_var(*ref);
  if (not found) {
    return fail(std::move(found).error());
  }
  if (not *found) {
    return invalid(FormatErrc::missing_variable, name, i);
  }
  return **std::move(found);
}

/// The time and data variables of station `i`, both over its
/// `stationLength_N` dimension.
struct StationVars {
  nc::VarInfo time;
  nc::VarInfo data;
};

std::expected<StationVars, Error> station_vars(const nc::File& file,
                                               std::size_t i, int width) {
  auto time = station_var(file, time_prefix, i, width);
  if (not time) {
    return std::unexpected{std::move(time).error()};
  }
  auto data = station_var(file, data_prefix, i, width);
  if (not data) {
    return std::unexpected{std::move(data).error()};
  }
  const std::string length = legacy_variable_name(length_prefix, i + 1, width);
  if (time->dims.size() != 1 or time->dims[0].name != length) {
    return invalid(FormatErrc::dimension_mismatch,
                   subject_of(time->name.view()), i);
  }
  if (data->dims.size() != 1 or data->dims[0].id != time->dims[0].id) {
    return invalid(FormatErrc::dimension_mismatch,
                   subject_of(data->name.view()), i);
  }
  return StationVars{.time = *std::move(time), .data = *std::move(data)};
}

// ---- time -------------------------------------------------------------------

/// The epoch of `referenceDate`: its first 19 characters, `yyyy-MM-dd hh:mm:ss`
/// (B8: whatever the attribute's length, and a `T` is accepted). ParseError
/// `bad_date` when they are not a date.
std::expected<core::Time, Error> parse_reference(std::string_view text) {
  const std::string_view date =
      core::detail::trim(cut_at_nul(text)).substr(0, reference_date_chars);
  const auto parsed = core::parse_utc_datetime(date);
  if (not parsed) {
    return std::unexpected{Error{
        ParseError::make(ParseErrc::bad_date,
                         {.line = 1, .column = parsed.error().column}, text)}};
  }
  return *parsed;
}

/// What the time variables say about their clock, summed over the stations.
struct TimeNotes {
  std::vector<core::Time> epochs;
  std::size_t epoch_defaulted{0};
  std::vector<Warning> time_zones;  // tz_assumed_utc, one per distinct text
};

std::expected<void, Error> note_time(const nc::File& file,
                                     const nc::VarInfo& time,
                                     TimeNotes& notes) {
  auto parts =
      collect([&] { return text_of(file, time.name, "referenceDate"); },
              [&] { return text_of(file, time.name, "timezone"); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  const auto& [reference, zone] = *parts;
  auto epoch = reference ? parse_reference(*reference)
                         : parse_reference(default_reference);
  if (not epoch) {
    return std::unexpected{std::move(epoch).error()};
  }
  notes.epochs.push_back(*epoch);
  notes.epoch_defaulted += reference ? 0U : 1U;
  if (zone) {
    const std::string_view tz = core::detail::trim(*zone);
    if (not core::detail::equal_ignore_case(tz, "utc") and
        not core::detail::equal_ignore_case(tz, "gmt")) {
      const std::string subject = subject_of(tz);
      const auto known =
          std::ranges::find(notes.time_zones, subject, &Warning::subject);
      if (known != notes.time_zones.end()) {
        ++known->count;
      } else {
        notes.time_zones.push_back(
            {.code = WarningCode::tz_assumed_utc, .subject = subject});
      }
    }
  }
  return {};
}

// ---- the column's metadata --------------------------------------------------

/// The units and datum texts of the stations' data variables: the first
/// station's, which the others must repeat.
struct MetaNotes {
  std::string units;
  std::string datum;
};

std::expected<void, Error> note_meta(const nc::File& file,
                                     const nc::VarInfo& data, std::size_t i,
                                     MetaNotes& notes) {
  auto parts = collect([&] { return text_of(file, data.name, "units"); },
                       [&] { return text_of(file, data.name, "datum"); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  const auto& [units, datum] = *parts;
  const std::string u{core::detail::trim(units.value_or(""))};
  const std::string d = to_upper_ascii(core::detail::trim(datum.value_or("")));
  if (i == 0) {
    notes = {.units = u, .datum = d};
    return {};
  }
  if (u != notes.units) {
    return invalid(FormatErrc::inconsistent_metadata, "units", i);
  }
  if (d != notes.datum) {
    return invalid(FormatErrc::inconsistent_metadata, "datum", i);
  }
  return {};
}

Read<core::SeriesMeta> meta_of(const MetaNotes& notes) {
  Read<std::optional<core::Unit>> unit{.value = std::nullopt, .warnings = {}};
  if (not notes.units.empty()) {
    unit.value = core::parse_unit(notes.units);
    if (unit.value) {
      const auto* other = std::get_if<core::OtherUnit>(&*unit.value);
      if (other != nullptr and not core::is_canonical_other(*other)) {
        unit.warnings.push_back({.code = WarningCode::unrecognized_unit,
                                 .subject = subject_of(other->symbol())});
      }
    }
  }
  Read<core::SeriesMeta> out = with_datum(
      core::SeriesMeta::make({.quantity = core::GenericQuantity::value(),
                              .label = {},
                              .unit = std::move(unit.value)}),
      notes.datum.empty() ? std::nullopt : std::optional{notes.datum}, "value");
  out.warnings =
      concatenated(std::move(unit.warnings), std::move(out.warnings));
  return out;
}

/// Everything per station that a catalog holds besides the position.
struct Sweep {
  std::vector<std::size_t> samples;
  TimeNotes time;
  MetaNotes meta;
};

std::expected<Sweep, Error> sweep_stations(const nc::File& file,
                                           std::size_t count, int width,
                                           const StopToken& stop) {
  Sweep out;
  out.samples.reserve(count);
  out.time.epochs.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    if (i % stations_per_stop_poll == 0 and stop.stop_requested()) {
      return fail(Cancelled{});
    }
    auto vars = station_vars(file, i, width);
    if (not vars) {
      return std::unexpected{std::move(vars).error()};
    }
    if (auto noted = note_time(file, vars->time, out.time); not noted) {
      return std::unexpected{std::move(noted).error()};
    }
    if (auto noted = note_meta(file, vars->data, i, out.meta); not noted) {
      return std::unexpected{std::move(noted).error()};
    }
    out.samples.push_back(vars->time.dims[0].length);
  }
  return out;
}

}  // namespace

std::expected<Read<LegacyOpened>, Error> open_legacy(const nc::File& file,
                                                     const StopToken& stop) {
  auto base = find_base(file);
  if (not base) {
    return std::unexpected{std::move(base).error()};
  }
  if (auto count = check_station_count(file, base->stations); not count) {
    return std::unexpected{std::move(count).error()};
  }
  if (base->stations.length == 0) {
    return invalid(FormatErrc::empty_collection, "numStations");
  }
  auto parts =
      collect([&] { return dialect_of(file, *base); },
              [&] { return number_width(file); }, [&] { return crs_of(file); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  auto& [dialect, width, crs] = *parts;
  auto stations = stations_of(file, *base, crs.value, stop);
  if (not stations) {
    return std::unexpected{std::move(stations).error()};
  }
  auto sweep = sweep_stations(file, stations->value.size(), width, stop);
  if (not sweep) {
    return std::unexpected{std::move(sweep).error()};
  }
  Read<core::SeriesMeta> meta = meta_of(sweep->meta);

  std::vector<Warning> warnings{{.code = WarningCode::legacy_dialect}};
  append(warnings, std::move(crs.warnings));
  append(warnings, std::move(sweep->time.time_zones));
  append_if_counted(warnings, {.code = WarningCode::epoch_used,
                               .subject = std::string{default_reference},
                               .count = sweep->time.epoch_defaulted});
  append(warnings, std::move(stations->warnings));
  append(warnings, std::move(meta.warnings));

  const LegacyOrigin origin{.dialect = dialect};
  StationNcCatalog catalog{
      .origin = origin, .stations = {}, .schema = {std::move(meta.value)}};
  catalog.stations.reserve(stations->value.size());
  for (std::size_t i = 0; i < stations->value.size(); ++i) {
    catalog.stations.push_back({.station = std::move(stations->value[i]),
                                .samples = sweep->samples[i]});
  }
  return Read<LegacyOpened>{.value = {.width = width,
                                      .epochs = std::move(sweep->time.epochs),
                                      .catalog = std::move(catalog)},
                            .warnings = std::move(warnings)};
}

std::expected<Read<core::StationTable>, Error> read_legacy(
    const nc::File& file, const LegacyOpened& opened,
    std::span<const std::size_t> selected, const StopToken& stop) {
  std::size_t total = 0;
  for (const std::size_t i : selected) {
    total += opened.catalog.stations[i].samples;
  }
  if (auto fits = check_result_size(file, "data_station", 1, total, 1);
      not fits) {
    return std::unexpected{std::move(fits).error()};
  }
  std::vector<core::TimeAxis> axes;
  std::vector<core::Column> columns;
  std::vector<core::StationRow> rows;
  axes.reserve(selected.size());
  columns.reserve(selected.size());
  rows.reserve(selected.size());
  for (std::size_t p = 0; p < selected.size(); ++p) {
    if (stop.stop_requested()) {
      return fail(Cancelled{});
    }
    const std::size_t i = selected[p];
    const std::size_t n = opened.catalog.stations[i].samples;
    auto vars = station_vars(file, i, opened.width);
    if (not vars) {
      return std::unexpected{std::move(vars).error()};
    }
    core::TimeAxis axis;
    core::Column column;
    if (n > 0) {
      const nc::Slab slab{{.start = 0, .count = n}};
      auto times = read_masked(file, vars->time, slab, RowRole::times, stop);
      if (not times) {
        return std::unexpected{std::move(times).error()};
      }
      auto clock =
          make_clock({.unit = CfTimeUnit::second, .epoch = opened.epochs[i]},
                     CfCalendar::proleptic_gregorian, vars->time.name.view());
      if (not clock) {
        return std::unexpected{std::move(clock).error()};
      }
      auto made = axis_of(*times, *clock, vars->time.name.view(), i);
      if (not made) {
        return std::unexpected{std::move(made).error()};
      }
      axis = *std::move(made);
      auto values = file.read_samples(vars->data.name, slab, stop);
      if (not values) {
        return std::unexpected{std::move(values).error()};
      }
      column = *std::move(values);
    }
    axes.push_back(std::move(axis));
    columns.push_back(std::move(column));
    rows.push_back({.station = opened.catalog.stations[i].station, .axis = p});
  }
  std::vector<core::Variable> variables;
  variables.push_back({.meta = opened.catalog.schema.front(),
                       .per_station = std::move(columns)});
  auto table = core::StationTable::make(std::move(variables), std::move(axes),
                                        std::move(rows));
  if (not table) {
    return fail(to_format_error(table.error()));
  }
  return pure(*std::move(table));
}

}  // namespace mov::io::detail::station_nc
