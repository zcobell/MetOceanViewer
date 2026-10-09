// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The legacy v4 station netCDF (docs/legacy-formats.md section 5, docs/
// station-netcdf.md section 11): dimensions `numStations` and
// `stationNameLen`, `stationName` (and `stationId`) rows, `stationXCoordinate`
// / `stationYCoordinate`, and per station N `stationLength_N`, `time_station_N`
// (seconds since the `referenceDate` attribute) and `data_station_N`.
//
// What v4 got wrong is not repeated: the row stride is the file's (v4 assumed
// 200 and overran its buffer), `referenceDate` is read at its real length (v4
// read it into a fixed 80-byte buffer), the default fill is masked (v4 never
// masked it), a missing EPSG is an assumed 4326 with a warning and a wrong
// type is an error (v4 returned netCDF error codes as EPSG codes), and
// whatever the writer left after the NUL of a name is cut.

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
#include "mov/core/ascii.hpp"
#include "mov/core/datum.hpp"
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
#include "station_netcdf_shared.hpp"

namespace mov::io::detail::station_nc {

namespace {

constexpr std::string_view time_prefix = "time_station_";
constexpr std::string_view data_prefix = "data_station_";
constexpr std::string_view length_prefix = "stationLength_";
constexpr std::array<StationNumberWidth, 2> widths{StationNumberWidth::four,
                                                   StationNumberWidth::six};
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
    return fail(
        format_error(FormatErrc::bad_encoding, std::string{var.name.view()}));
  }
  if (var.dims.size() != 2 or var.dims[0].id != stations.id) {
    return fail(format_error(FormatErrc::dimension_mismatch,
                             std::string{var.name.view()}));
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
std::expected<StationNumberWidth, Error> number_width(const nc::File& file) {
  for (const StationNumberWidth width : widths) {
    const std::string name =
        legacy_variable_name(time_prefix, 1, digits_of(width));
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
  return fail(format_error(
      FormatErrc::missing_variable,
      legacy_variable_name(time_prefix, 1, digits_of(widths.front())), 0));
}

/// What the file says about itself: v4's writer puts a global `fileformat` and
/// a `stationId` variable in its files; v4's reader accepted files without.
std::expected<LegacyOrigin, Error> origin_of(const nc::File& file,
                                             const Base& base,
                                             StationNumberWidth width) {
  return optional_text(file, nc::global, "fileformat")
      .transform([&](std::optional<std::string> format) {
        return LegacyOrigin{.fileformat = std::move(format),
                            .has_station_ids = base.id.has_value(),
                            .width = width};
      });
}

// ---- the CRS ----------------------------------------------------------------

constexpr nc::NcNameRef x_var{"stationXCoordinate"};
constexpr nc::NcNameRef epsg_att{"HorizontalProjectionEPSG"};
constexpr std::string_view epsg_object =
    "stationXCoordinate:HorizontalProjectionEPSG";

/// The EPSG code of `stationXCoordinate`, any signed integer type; nullopt
/// when the attribute is absent. A text, floating-point or unsigned attribute
/// is `type_mismatch`: an error, never a code (v4 returned netCDF error codes
/// as EPSG codes).
std::expected<std::optional<std::int64_t>, Error> read_epsg(
    const nc::File& file) {
  auto values = int_att(file, x_var, epsg_att, epsg_object);
  if (not values) {
    return std::unexpected{std::move(values).error()};
  }
  if (not *values) {
    return std::optional<std::int64_t>{};
  }
  if ((*values)->size() != 1) {
    return fail(nc_fault(file, WrapperFault::count_mismatch, NcOp::get_att,
                         epsg_object));
  }
  return std::optional<std::int64_t>{(*values)->front()};
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
    return fail(format_error(FormatErrc::unsupported_crs, subject));
  }
  const auto epsg = core::Epsg::make(static_cast<int>(**code));
  if (not epsg) {
    return fail(format_error(FormatErrc::unsupported_crs, subject));
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

/// A name: cut at the first NUL (the writer's junk follows it), white space
/// collapsed (v4's `simplified()`), bytes that are not UTF-8 replaced.
CleanedText clean_name(std::string_view row) {
  return replace_invalid_utf8(simplified(cut_at_nul(row)));
}

/// An id: cut at the first NUL and trimmed.
CleanedText clean_id(std::string_view row) {
  return replace_invalid_utf8(core::ascii::trim(cut_at_nul(row)));
}

/// The ids and names of the stations. The id is the `stationId` row, else the
/// name, else the decimal index; a name stays as the file has it, empty
/// included.
struct Names {
  std::vector<std::string> ids;
  std::vector<core::StationText> names;
  std::size_t replaced{0};
  /// Stations of a file with `stationId` whose row was empty.
  std::size_t id_substituted{0};
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
    CleanedText name = clean_name(name_rows[i]);
    const CleanedText id = base.id ? clean_id(id_rows[i]) : CleanedText{};
    out.replaced += (name.replaced ? 1U : 0U) + (id.replaced ? 1U : 0U);
    std::string id_text{id.text.view()};
    if (id_text.empty()) {
      id_text =
          name.text.empty() ? std::to_string(i) : std::string{name.text.view()};
      out.id_substituted += base.id ? 1U : 0U;
    }
    out.names.push_back(std::move(name.text));
    out.ids.push_back(std::move(id_text));
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
    if (not key) {
      return fail(to_format_error(key.error(), i));
    }
    out.value.push_back({.id = *std::move(key),
                         .name = std::move(names.names[i]),
                         .location = position->location,
                         .native = position->native,
                         .source = std::nullopt});
  }
  append_if_counted(
      out.warnings,
      {.code = WarningCode::station_id_substituted,
       .subject = base.id ? subject_of(base.id->name.view()) : std::string{},
       .count = names.id_substituted});
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
                                              std::size_t i,
                                              StationNumberWidth width) {
  const std::string name =
      legacy_variable_name(prefix, i + 1, digits_of(width));
  const auto ref = nc::NcName::make(name);
  if (not ref) {
    return fail(
        format_error(FormatErrc::missing_variable, subject_of(name), i));
  }
  auto found = file.find_var(*ref);
  if (not found) {
    return fail(std::move(found).error());
  }
  if (not *found) {
    return fail(format_error(FormatErrc::missing_variable, name, i));
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
                                               std::size_t i,
                                               StationNumberWidth width) {
  auto time_var = station_var(file, time_prefix, i, width);
  if (not time_var) {
    return std::unexpected{std::move(time_var).error()};
  }
  auto data = station_var(file, data_prefix, i, width);
  if (not data) {
    return std::unexpected{std::move(data).error()};
  }
  const std::string length =
      legacy_variable_name(length_prefix, i + 1, digits_of(width));
  if (time_var->dims.size() != 1 or time_var->dims[0].name != length) {
    return fail(format_error(FormatErrc::dimension_mismatch,
                             subject_of(time_var->name.view()), i));
  }
  if (data->dims.size() != 1 or data->dims[0].id != time_var->dims[0].id) {
    return fail(format_error(FormatErrc::dimension_mismatch,
                             subject_of(data->name.view()), i));
  }
  return StationVars{.time = *std::move(time_var), .data = *std::move(data)};
}

// ---- time -------------------------------------------------------------------

/// The clock of `referenceDate`: its epoch and what follows the 19 characters
/// of the date.
struct Reference {
  core::Time epoch;
  /// Text after the date (`Z`, `+02:00`, `local time`): not read, and said so.
  std::string trailing;
};

/// The epoch of `referenceDate`: its first 19 characters, `yyyy-MM-dd hh:mm:ss`
/// (whatever the attribute's length, which v4 did not check; a `T` is
/// accepted). ParseError `bad_date` when they are not a date.
std::expected<Reference, Error> parse_reference(std::string_view text) {
  const std::string_view whole = core::ascii::trim(text);
  const auto parsed =
      core::parse_utc_datetime(whole.substr(0, reference_date_chars));
  if (not parsed) {
    return std::unexpected{Error{
        ParseError::make(ParseErrc::bad_date,
                         {.line = 1, .column = parsed.error().column}, text)}};
  }
  const std::string_view rest =
      whole.size() > reference_date_chars
          ? core::ascii::trim(whole.substr(reference_date_chars))
          : std::string_view{};
  return Reference{.epoch = *parsed, .trailing = std::string{rest}};
}

/// What the time variables say about their clock, summed over the stations.
struct TimeNotes {
  std::vector<core::Time> epochs;
  std::size_t epoch_defaulted{0};
  std::vector<Warning> time_zones;  // tz_assumed_utc, one per distinct text
};

/// `tz_assumed_utc` for a zone (or what trails the date) other than UTC or
/// GMT, one warning per distinct text, counted.
void note_zone(TimeNotes& notes, std::string_view text) {
  const std::string_view zone = core::ascii::trim(text);
  if (zone.empty() or core::ascii::equal_ignore_case(zone, "utc") or
      core::ascii::equal_ignore_case(zone, "gmt")) {
    return;
  }
  const std::string subject = subject_of(zone);
  const auto known =
      std::ranges::find(notes.time_zones, subject, &Warning::subject);
  if (known != notes.time_zones.end()) {
    ++known->count;
  } else {
    notes.time_zones.push_back(
        {.code = WarningCode::tz_assumed_utc, .subject = subject});
  }
}

std::expected<void, Error> note_time(const nc::File& file,
                                     const nc::VarInfo& time_var,
                                     TimeNotes& notes) {
  auto parts = collect(
      [&] { return optional_text(file, time_var.name, "referenceDate"); },
      [&] { return optional_text(file, time_var.name, "timezone"); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  const auto& [reference, zone] = *parts;
  auto parsed = parse_reference(reference ? *reference : default_reference);
  if (not parsed) {
    return std::unexpected{std::move(parsed).error()};
  }
  notes.epochs.push_back(parsed->epoch);
  notes.epoch_defaulted += reference ? 0U : 1U;
  note_zone(notes, parsed->trailing);
  if (zone) {
    note_zone(notes, *zone);
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

std::expected<MetaNotes, Error> meta_notes_of(const nc::File& file,
                                              const nc::VarInfo& data) {
  auto parts = collect([&] { return optional_text(file, data.name, "units"); },
                       [&] { return optional_text(file, data.name, "datum"); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  const auto& [units, datum] = *parts;
  return MetaNotes{.units = std::string{core::ascii::trim(units.value_or(""))},
                   .datum = to_upper_ascii(datum_text(datum.value_or("")))};
}

/// The first station's notes are the file's; a later station that says
/// something else is `inconsistent_metadata`.
std::expected<void, Error> note_meta(const nc::File& file,
                                     const nc::VarInfo& data, std::size_t i,
                                     std::optional<MetaNotes>& notes) {
  auto current = meta_notes_of(file, data);
  if (not current) {
    return std::unexpected{std::move(current).error()};
  }
  if (not notes) {
    notes = *std::move(current);
    return {};
  }
  if (current->units != notes->units) {
    return fail(format_error(FormatErrc::inconsistent_metadata, "units", i));
  }
  if (current->datum != notes->datum) {
    return fail(format_error(FormatErrc::inconsistent_metadata, "datum", i));
  }
  return {};
}

Read<core::SeriesMeta> meta_of(const MetaNotes& notes) {
  Read<std::optional<core::Unit>> unit = parsed_unit(
      notes.units.empty() ? std::nullopt : std::optional{notes.units});
  return std::move(unit).and_then([&](std::optional<core::Unit> u) {
    return with_datum(
        core::SeriesMeta::make({.quantity = core::GenericQuantity::value(),
                                .label = {},
                                .unit = std::move(u)}),
        notes.datum.empty() ? std::nullopt : std::optional{notes.datum},
        "value");
  });
}

/// Everything per station that a catalog holds besides the position.
struct Sweep {
  std::vector<std::size_t> samples;
  TimeNotes time;
  std::optional<MetaNotes> meta;
};

std::expected<Sweep, Error> sweep_stations(const nc::File& file,
                                           std::size_t count,
                                           StationNumberWidth width,
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

/// One station's series as the file has it.
struct Series {
  core::TimeAxis axis;
  core::Column column;
};

/// The times and values of station `i` (`n` samples); the times are not yet
/// in order.
std::expected<Series, Error> read_series(const nc::File& file,
                                         const LegacyOpened& opened,
                                         std::size_t i, const StopToken& stop) {
  const std::size_t n = opened.catalog.stations[i].samples;
  auto vars = station_vars(file, i, opened.width);
  if (not vars) {
    return std::unexpected{std::move(vars).error()};
  }
  if (n == 0) {
    return Series{.axis = {}, .column = {}};
  }
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
  auto axis = times_of(*times, *clock, vars->time.name.view(), i);
  if (not axis) {
    return std::unexpected{std::move(axis).error()};
  }
  auto values = file.read_samples(vars->data.name, slab, stop);
  if (not values) {
    return std::unexpected{std::move(values).error()};
  }
  return Series{.axis = *std::move(axis), .column = *std::move(values)};
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
    return fail(format_error(FormatErrc::empty_collection, "numStations"));
  }
  auto parts =
      collect([&] { return number_width(file); }, [&] { return crs_of(file); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  auto& [width, crs_text] = *parts;
  auto origin = origin_of(file, *base, width);
  if (not origin) {
    return std::unexpected{std::move(origin).error()};
  }
  auto stations = stations_of(file, *base, crs_text.value, stop);
  if (not stations) {
    return std::unexpected{std::move(stations).error()};
  }
  auto sweep = sweep_stations(file, stations->value.size(), width, stop);
  if (not sweep) {
    return std::unexpected{std::move(sweep).error()};
  }
  if (not sweep->meta) {
    return fail(format_error(FormatErrc::empty_collection, "numStations"));
  }
  Read<core::SeriesMeta> meta = meta_of(*sweep->meta);

  // The order of SN 11: a legacy file, the CRS, the clock (zones, then an
  // epoch that was not given), the stations, the column.
  std::vector<Warning> warnings{{.code = WarningCode::legacy_dialect}};
  append(warnings, std::move(crs_text.warnings));
  append(warnings, std::move(sweep->time.time_zones));
  append_if_counted(warnings, {.code = WarningCode::epoch_used,
                               .subject = std::string{default_reference},
                               .count = sweep->time.epoch_defaulted});
  append(warnings, std::move(stations->warnings));
  append(warnings, std::move(meta.warnings));

  StationNcCatalog catalog{.origin = *std::move(origin),
                           .stations = {},
                           .schema = {std::move(meta.value)}};
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
  std::vector<std::size_t> counts;
  counts.reserve(opened.catalog.stations.size());
  for (const CatalogStation& c : opened.catalog.stations) {
    counts.push_back(c.samples);
  }
  if (auto fits = check_rows_size(file, "data_station",
                                  sum_selected(counts, selected), 1);
      not fits) {
    return std::unexpected{std::move(fits).error()};
  }
  std::vector<core::TimeAxis> axes;
  std::vector<core::Column> columns;
  std::vector<core::StationRow> rows;
  axes.reserve(selected.size());
  columns.reserve(selected.size());
  rows.reserve(selected.size());
  core::NormalizeReport report;
  for (std::size_t p = 0; p < selected.size(); ++p) {
    if (stop.stop_requested()) {
      return fail(Cancelled{});
    }
    const std::size_t i = selected[p];
    auto series = read_series(file, opened, i, stop);
    if (not series) {
      return std::unexpected{std::move(series).error()};
    }
    std::array<core::Column*, 1> over{&series->column};
    merge_reports(report, normalize_columns(series->axis, over));
    axes.push_back(std::move(series->axis));
    columns.push_back(std::move(series->column));
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
  Read<core::StationTable> out = pure(*std::move(table));
  add_normalize_warnings(out.warnings, report, "time_station");
  return out;
}

}  // namespace mov::io::detail::station_nc
