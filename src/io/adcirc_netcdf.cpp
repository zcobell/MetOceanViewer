// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/adcirc_netcdf.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "model_netcdf.hpp"
#include "mov/core/ascii.hpp"
#include "mov/core/hwm.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/io/detail/adcirc_schema.hpp"
#include "mov/io/detail/station_groups.hpp"
#include "mov/io/detail/table_error.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/projection.hpp"
#include "mov/io/warning.hpp"

namespace mov::io {

namespace {

using detail::cut_at_nul;
using detail::fail;
using detail::format_error;

// ---- the data variables of each kind
// ------------------------------------------

// One table: the names, and for each kind (in AdcircKind's order) which of
// them it reads. A vector's second component is the partner of its first.
constexpr std::array<nc::NcNameRef, 6> data_names{"zeta",     "u-vel", "v-vel",
                                                  "pressure", "windx", "windy"};

struct Components {
  std::size_t first;
  std::size_t count;
};

constexpr std::array<Components, 4> components_of_kind{
    {{.first = 0, .count = 1},
     {.first = 1, .count = 2},
     {.first = 3, .count = 1},
     {.first = 4, .count = 2}}};

static_assert(static_cast<std::size_t>(AdcircKind::elevation) == 0 and
              static_cast<std::size_t>(AdcircKind::velocity) == 1 and
              static_cast<std::size_t>(AdcircKind::pressure) == 2 and
              static_cast<std::size_t>(AdcircKind::wind) == 3);

constexpr std::array<AdcircKind, 4> kinds_in_search_order{
    AdcircKind::elevation, AdcircKind::velocity, AdcircKind::pressure,
    AdcircKind::wind};

std::span<const nc::NcNameRef> names_of(AdcircKind kind) {
  const Components c = components_of_kind[static_cast<std::size_t>(kind)];
  return std::span{data_names}.subspan(c.first, c.count);
}

// ---- the file's structure ---------------------------------------------------

struct Structure {
  nc::DimInfo time_dim;
  nc::DimInfo station_dim;
  nc::VarInfo time_var;
};

// Global attribute `model` is "ADCIRC" (LF 1.1: how v4 tells the file apart).
// An attribute that is no text is not that.
std::expected<void, Error> require_model(const nc::File& file) {
  auto model = detail::optional_text(file, nc::global, "model");
  if (not model) {
    return std::unexpected{std::move(model.error())};
  }
  const bool adcirc =
      *model and core::ascii::trim(cut_at_nul(**model)) == "ADCIRC";
  if (not adcirc) {
    return fail(format_error(FormatErrc::not_this_format, "model"));
  }
  return {};
}

std::expected<Structure, Error> structure_of(const nc::File& file) {
  if (auto model = require_model(file); not model) {
    return std::unexpected{std::move(model.error())};
  }
  auto time_dim = detail::require_dim(file, "time");
  if (not time_dim) {
    return std::unexpected{std::move(time_dim.error())};
  }
  auto station_dim = detail::require_dim(file, "station");
  if (not station_dim) {
    return std::unexpected{std::move(station_dim.error())};
  }
  auto time_var = detail::require_var(file, "time");
  if (not time_var) {
    return std::unexpected{std::move(time_var.error())};
  }
  if (auto shape = detail::require_shape(*time_var, {time_dim->id});
      not shape) {
    return std::unexpected{std::move(shape.error())};
  }
  return Structure{.time_dim = *std::move(time_dim),
                   .station_dim = *std::move(station_dim),
                   .time_var = *std::move(time_var)};
}

std::expected<bool, Error> has_var(const nc::File& file, nc::NcNameRef name) {
  auto found = file.find_var(name);
  if (not found) {
    return fail(std::move(found.error()));
  }
  return found->has_value();
}

// The first of zeta, u-vel, pressure, windx the file has (LF 3.2). Whether a
// vector has its partner is data_variables' business; a lone v-vel or windy
// is another kind of file.
std::expected<AdcircKind, Error> detect_kind(const nc::File& file) {
  for (const AdcircKind kind : kinds_in_search_order) {
    const auto present = has_var(file, names_of(kind).front());
    if (not present) {
      return std::unexpected{present.error()};
    }
    if (*present) {
      return kind;
    }
  }
  for (const nc::NcNameRef lone : {data_names[2], data_names[5]}) {
    const auto present = has_var(file, lone);
    if (not present) {
      return std::unexpected{present.error()};
    }
    if (*present) {
      return fail(
          format_error(FormatErrc::not_this_format, std::string{lone.view()}));
    }
  }
  return fail(format_error(FormatErrc::missing_variable,
                           std::string{data_names.front().view()}));
}

// The data variables of `kind`, each over (time, station): the first missing
// is `missing_variable`, a later one the first's partner.
std::expected<std::vector<nc::VarInfo>, Error> data_variables(
    const nc::File& file, const Structure& structure, AdcircKind kind) {
  std::vector<nc::VarInfo> vars;
  for (const nc::NcNameRef name : names_of(kind)) {
    auto var = detail::require_var(file, name);
    const auto* missing =
        var ? nullptr : std::get_if<FormatError>(&var.error());
    if (missing != nullptr and missing->code == FormatErrc::missing_variable and
        not vars.empty()) {
      return fail(format_error(FormatErrc::partner_variable_missing,
                               std::string{name.view()}));
    }
    if (not var) {
      return std::unexpected{std::move(var.error())};
    }
    if (auto shape = detail::require_shape(
            *var, {structure.time_dim.id, structure.station_dim.id});
        not shape) {
      return std::unexpected{std::move(shape.error())};
    }
    vars.push_back(*std::move(var));
  }
  return vars;
}

std::expected<bool, Error> has_names(const nc::File& file) {
  return has_var(file, "station_name");
}

detail::StationVariables station_variables(const nc::DimInfo& dim, bool names) {
  return {
      .dim = dim,
      .x = "x",
      .y = "y",
      .names = names
                   ? std::optional<nc::NcNameRef>{nc::NcNameRef{"station_name"}}
                   : std::nullopt,
      .time_dim = std::nullopt,
      .source = core::DataSource::adcirc};
}

// ---- the CRS the caller gave, against the one the file says
// ---------------------

// ADCIRC's ICS: 1 is Cartesian coordinates (a projected CRS), 2 spherical
// (longitude and latitude). Any other value, or no `ics`, says nothing.
std::expected<std::optional<int>, Error> read_ics(const nc::File& file) {
  const auto ics = file.numeric_att<std::int32_t>(nc::global, "ics");
  if (ics) {
    return *ics and (*ics)->size() == 1 ? std::optional<int>{(*ics)->front()}
                                        : std::nullopt;
  }
  const auto* fault = std::get_if<WrapperFault>(&ics.error().status);
  if (fault != nullptr and (*fault == WrapperFault::type_mismatch or
                            *fault == WrapperFault::count_mismatch)) {
    return std::optional<int>{};
  }
  return fail(ics.error());
}

constexpr int ics_cartesian = 1;
constexpr int ics_spherical = 2;

// A `crs_mismatch` warning when `ics` and the kind of `crs` disagree: the
// positions, and the direction of the vectors, would be wrong.
std::expected<std::vector<Warning>, Error> ics_warnings(const nc::File& file,
                                                        core::Epsg crs,
                                                        CrsKind grid) {
  auto ics = read_ics(file);
  if (not ics) {
    return std::unexpected{std::move(ics.error())};
  }
  const int says = ics->value_or(0);
  const bool says_spherical = says == ics_spherical;
  const bool says_cartesian = says == ics_cartesian;
  const bool geographic = grid == CrsKind::geographic;
  const bool mismatch =
      (says_spherical and not geographic) or (says_cartesian and geographic);
  if (mismatch) {
    return std::vector<Warning>{
        {.code = WarningCode::crs_mismatch,
         .subject = std::format("ics {} but EPSG:{} is {}", says, crs.code(),
                                geographic ? "geographic" : "projected"),
         .count = 1}};
  }
  return std::vector<Warning>{};
}

// ---- the clock
// ----------------------------------------------------------------

struct Clock {
  CfClock clock;
  std::vector<Warning> warnings;
};

// The `units` of `time`, as text, and what it says if it is a CF time unit.
struct UnitsOfTime {
  std::optional<std::string> text;
  std::optional<Read<CfTimeUnits>> parsed;
};

std::expected<UnitsOfTime, Error> read_units(const nc::File& file,
                                             const nc::VarInfo& time) {
  auto text = detail::optional_text(file, time.name, "units");
  if (not text) {
    return std::unexpected{std::move(text.error())};
  }
  UnitsOfTime out{.text = std::nullopt, .parsed = std::nullopt};
  if (*text) {
    out.text = std::string{cut_at_nul(**text)};
    if (auto parsed = parse_cf_time_units(*out.text)) {
      out.parsed = *std::move(parsed);
    }
  }
  return out;
}

// ADCIRC counts seconds from its cold start. A given one wins; the epoch of
// `units` is then only checked against it.
std::expected<Clock, Error> clock_from_cold_start(const Structure& structure,
                                                  const UnitsOfTime& units,
                                                  core::Time given) {
  auto clock = detail::make_clock({.unit = CfTimeUnit::second, .epoch = given},
                                  CfCalendar::proleptic_gregorian,
                                  structure.time_var.name.view());
  if (not clock) {
    return std::unexpected{std::move(clock.error())};
  }
  Clock out{.clock = *clock, .warnings = {}};
  if (units.text and units.parsed and
      std::chrono::abs(units.parsed->value.epoch - given) >
          std::chrono::seconds{1}) {
    out.warnings.push_back({.code = WarningCode::cold_start_differs,
                            .subject = *units.text,
                            .count = 1});
  }
  return out;
}

// No cold start: the unit and epoch of `units`, if they are a CF time unit and
// not the placeholder NCDATE leaves.
std::expected<Clock, Error> clock_from_units(const nc::File& file,
                                             const Structure& structure,
                                             UnitsOfTime units) {
  const std::string variable{structure.time_var.name.view()};
  if (not units.text) {
    return fail(
        format_error(FormatErrc::cold_start_required, variable + ":units"));
  }
  if (not units.parsed) {
    return fail(format_error(FormatErrc::cold_start_required, *units.text));
  }
  const auto calendar = detail::read_calendar(file, structure.time_var);
  if (not calendar) {
    return std::unexpected{calendar.error()};
  }
  auto clock = detail::make_clock(units.parsed->value, *calendar, variable);
  if (not clock) {
    return std::unexpected{std::move(clock.error())};
  }
  Clock out{.clock = *clock, .warnings = std::move(units.parsed->warnings)};
  out.warnings.push_back(
      {.code = WarningCode::epoch_used, .subject = *units.text, .count = 1});
  return out;
}

std::expected<Clock, Error> clock_of(const nc::File& file,
                                     const Structure& structure,
                                     const std::optional<core::Time>& given) {
  auto units = read_units(file, structure.time_var);
  if (not units) {
    return std::unexpected{std::move(units.error())};
  }
  if (given) {
    return clock_from_cold_start(structure, *units, *given);
  }
  return clock_from_units(file, structure, *std::move(units));
}

// ---- classifying values
// -------------------------------------------------------

// One raw value, classified. `fill` marks a value the model or the file says
// is no data (as opposed to NaN): in a vector it takes the partner with it.
struct Cell {
  core::Sample sample{};
  bool fill{false};
};

template <nc::Numeric T>
double unpacked(const nc::Masking<T>& mask, T raw) {
  const double wide = nc::detail::widen(raw);
  const double scaled = mask.scale ? wide * *mask.scale : wide;
  return mask.offset ? scaled + *mask.offset : scaled;
}

// C9: elevation at or below -999 is Dry (ADCIRC's fill is -99999), whatever
// the _FillValue attribute says; for every other output it is fill, so
// Missing. Anything else the attributes mask is Missing; NaN and infinities
// too, but they are counted.
template <nc::Numeric T>
Cell classify(AdcircKind kind, const nc::Masking<T>& mask, T raw,
              std::size_t& nonfinite) {
  const double physical = unpacked(mask, raw);
  if (not std::isfinite(physical)) {
    ++nonfinite;
    return {.sample = core::Missing{}, .fill = false};
  }
  if (core::is_dry(physical)) {
    return kind == AdcircKind::elevation
               ? Cell{.sample = core::Dry{}, .fill = false}
               : Cell{.sample = core::Missing{}, .fill = true};
  }
  if (mask.masks(raw)) {
    return {.sample = core::Missing{}, .fill = true};
  }
  return {.sample = mask.apply(raw), .fill = false};
}

// ---- reading the data variables
// -------------------------------------------------

// The columns of one data variable for the selected stations, and which of
// their cells were fill (flat, [position * times + time]; N7 needs it).
struct Gathered {
  std::vector<core::Column> columns;  // [selected position]
  std::vector<bool> fill;
  std::size_t times{0};
  std::size_t nonfinite{0};
};

// What the data variables of a read have in common.
struct ValueSource {
  const nc::File& file;
  AdcircKind kind;
  CrsKind grid;
  std::size_t times;
  int station_dim;
  std::span<const std::size_t> selection;
  std::optional<detail::GroupingPolicy> policy;
  const StopToken& stop;
};

std::expected<Gathered, Error> read_component(const ValueSource& source,
                                              const nc::VarInfo& var) {
  const std::size_t selected = source.selection.size();
  Gathered out{.columns = detail::empty_columns(selected, source.times),
               .fill = std::vector<bool>(selected * source.times, false),
               .times = source.times,
               .nonfinite = 0};
  // Nearby stations share a read, as far as the file's chunks make that cheap.
  const auto groups = detail::plan_groups(source.file, var, source.station_dim,
                                          source.selection, source.policy);
  if (not groups) {
    return std::unexpected{groups.error()};
  }
  const detail::GatherPlan plan{.variable = var.name,
                                .times = source.times,
                                .layer = std::nullopt,
                                .groups = *groups};
  const auto run = [&]<nc::Numeric T>() -> std::expected<void, Error> {
    const auto mask = source.file.masking<T>(var.name);
    if (not mask) {
      return fail(mask.error());
    }
    return detail::gather<T>(
        source.file, plan, source.stop,
        [&](std::size_t position, std::size_t t, T raw) {
          const Cell cell = classify(source.kind, *mask, raw, out.nonfinite);
          out.columns[position][t] = cell.sample;
          out.fill[(position * source.times) + t] = cell.fill;
        });
  };
  if (auto done = detail::dispatch_model_numeric(source.file, var, run);
      not done) {
    return std::unexpected{std::move(done.error())};
  }
  return out;
}

// N7: a fill in either component of a vector makes both Missing. (A NaN is
// not fill: it empties its own component only, and the magnitude is Missing
// either way.)
void combine_partner_fill(Gathered& first, Gathered& second) {
  for (std::size_t position = 0; position < first.columns.size(); ++position) {
    for (std::size_t t = 0; t < first.times; ++t) {
      const std::size_t i = (position * first.times) + t;
      if (first.fill[i] or second.fill[i]) {
        first.columns[position][t] = core::Missing{};
        second.columns[position][t] = core::Missing{};
      }
    }
  }
}

// The columns of every data variable, and what was not finite in them.
struct Values {
  std::vector<core::Variable> variables;
  std::size_t nonfinite{0};
  std::string nonfinite_in;  // the first variable that had one
};

std::expected<Values, Error> read_values(const ValueSource source,
                                         std::span<const nc::VarInfo> vars) {
  std::vector<Gathered> parts;
  parts.reserve(vars.size());
  Values out;
  for (const nc::VarInfo& var : vars) {
    auto gathered = read_component(source, var);
    if (not gathered) {
      return std::unexpected{std::move(gathered.error())};
    }
    if (gathered->nonfinite > 0 and out.nonfinite == 0) {
      out.nonfinite_in = std::string{var.name.view()};
    }
    out.nonfinite += gathered->nonfinite;
    parts.push_back(*std::move(gathered));
  }
  if (parts.size() == 2) {
    combine_partner_fill(parts[0], parts[1]);
  }
  std::vector<core::SeriesMeta> schema =
      detail::adcirc_schema(source.kind, source.grid);
  for (std::size_t c = 0; c < parts.size(); ++c) {
    out.variables.push_back({.meta = std::move(schema[c]),
                             .per_station = std::move(parts[c].columns)});
  }
  return out;
}

// What a read finds out before it reads values: the open file, its structure,
// and the data variables of the kind asked for.
struct Setup {
  nc::File file;
  Structure structure;
  std::vector<nc::VarInfo> vars;
};

std::expected<Setup, Error> set_up(const std::filesystem::path& path,
                                   const AdcircNcRequest& request,
                                   const ReadContext& ctx) {
  auto file = nc::File::open(path, ctx.limits);
  if (not file) {
    return fail(std::move(file.error()));
  }
  auto structure = structure_of(*file);
  if (not structure) {
    return std::unexpected{std::move(structure.error())};
  }
  if (auto ok = detail::require_selection(request.stations,
                                          structure->station_dim.length);
      not ok) {
    return std::unexpected{std::move(ok.error())};
  }
  auto vars = data_variables(*file, *structure, request.kind);
  if (not vars) {
    return std::unexpected{std::move(vars.error())};
  }
  if (auto size = detail::check_result_size(
          *file, vars->front().name.view(), request.stations.indices().size(),
          structure->time_dim.length, vars->size());
      not size) {
    return std::unexpected{std::move(size.error())};
  }
  return Setup{.file = *std::move(file),
               .structure = *std::move(structure),
               .vars = *std::move(vars)};
}

}  // namespace

std::vector<std::string> adcirc_variables(AdcircKind kind) {
  std::vector<std::string> names;
  for (const nc::NcNameRef name : names_of(kind)) {
    names.emplace_back(name.view());
  }
  return names;
}

// ---- inspect
// ---------------------------------------------------------------------

std::expected<Read<AdcircNcCatalog>, Error> inspect_adcirc_netcdf(
    const std::filesystem::path& path, core::Epsg crs, const ReadContext& ctx) {
  auto file = nc::File::open(path, ctx.limits);
  if (not file) {
    return fail(std::move(file.error()));
  }
  auto structure = structure_of(*file);
  if (not structure) {
    return std::unexpected{std::move(structure.error())};
  }
  auto kind = detect_kind(*file);
  if (not kind) {
    return std::unexpected{std::move(kind.error())};
  }
  if (auto vars = data_variables(*file, *structure, *kind); not vars) {
    return std::unexpected{std::move(vars.error())};
  }
  const auto names = has_names(*file);
  if (not names) {
    return std::unexpected{names.error()};
  }
  if (auto size = detail::check_station_count(*file, structure->station_dim);
      not size) {
    return std::unexpected{std::move(size.error())};
  }
  const core::StationSelection all =
      core::StationSelection::all(structure->station_dim.length);
  auto stations = detail::read_stations(
      *file, station_variables(structure->station_dim, *names), crs,
      all.indices(), ctx.stop);
  if (not stations) {
    return std::unexpected{std::move(stations.error())};
  }
  auto units = read_units(*file, structure->time_var);
  if (not units) {
    return std::unexpected{std::move(units.error())};
  }
  const auto grid = crs_kind(crs);
  if (not grid) {
    return fail(detail::to_format_error(grid.error(), std::nullopt));
  }
  auto mismatch = ics_warnings(*file, crs, *grid);
  if (not mismatch) {
    return std::unexpected{std::move(mismatch.error())};
  }
  AdcircNcCatalog catalog{.kind = *kind,
                          .stations = std::move(stations->value),
                          .times = structure->time_dim.length,
                          .time_units = std::nullopt};
  std::vector<Warning> warnings = std::move(stations->warnings);
  detail::append(warnings, *std::move(mismatch));
  if (units->text) {
    catalog.time_units = TimeUnitsAttr{
        .text = *units->text,
        .parsed =
            units->parsed ? std::optional{units->parsed->value} : std::nullopt};
    if (units->parsed) {
      detail::append(warnings, std::move(units->parsed->warnings));
    }
  }
  return Read<AdcircNcCatalog>{.value = std::move(catalog),
                               .warnings = std::move(warnings)};
}

// ---- read
// ------------------------------------------------------------------------

namespace detail {

std::expected<Read<core::StationTable>, Error> read_adcirc_netcdf(
    const std::filesystem::path& path, const AdcircNcRequest& request,
    const ReadContext& ctx, std::optional<GroupingPolicy> policy) {
  auto setup = set_up(path, request, ctx);
  if (not setup) {
    return std::unexpected{std::move(setup.error())};
  }
  const nc::File& file = setup->file;
  const Structure& structure = setup->structure;
  const auto grid = crs_kind(request.crs);
  if (not grid) {
    return fail(to_format_error(grid.error(), std::nullopt));
  }
  auto clock = clock_of(file, structure, request.cold_start);
  if (not clock) {
    return std::unexpected{std::move(clock.error())};
  }
  auto axis = read_time_axis(file, structure.time_var, clock->clock, ctx.stop);
  if (not axis) {
    return std::unexpected{std::move(axis.error())};
  }
  const auto names = has_names(file);
  if (not names) {
    return std::unexpected{names.error()};
  }
  const std::span<const std::size_t> selection = request.stations.indices();
  auto stations =
      read_stations(file, station_variables(structure.station_dim, *names),
                    request.crs, selection, ctx.stop);
  if (not stations) {
    return std::unexpected{std::move(stations.error())};
  }
  auto mismatch = ics_warnings(file, request.crs, *grid);
  if (not mismatch) {
    return std::unexpected{std::move(mismatch.error())};
  }
  auto values = read_values({.file = file,
                             .kind = request.kind,
                             .grid = *grid,
                             .times = structure.time_dim.length,
                             .station_dim = structure.station_dim.id,
                             .selection = selection,
                             .policy = policy,
                             .stop = ctx.stop},
                            setup->vars);
  if (not values) {
    return std::unexpected{std::move(values.error())};
  }
  std::vector<Warning> warnings = std::move(*mismatch);
  append(warnings, std::move(clock->warnings));
  append_if_counted(warnings, {.code = WarningCode::nonfinite_masked,
                               .subject = std::move(values->nonfinite_in),
                               .count = values->nonfinite});
  return assemble_table(std::move(values->variables), *std::move(axis),
                        *std::move(stations), std::move(warnings));
}

}  // namespace detail

std::expected<Read<core::StationTable>, Error> read_adcirc_netcdf(
    const std::filesystem::path& path, const AdcircNcRequest& request,
    const ReadContext& ctx) {
  return detail::read_adcirc_netcdf(path, request, ctx, std::nullopt);
}

}  // namespace mov::io
