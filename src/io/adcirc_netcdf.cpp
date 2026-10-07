// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/adcirc_netcdf.hpp"

#include <algorithm>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <ranges>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/detail/ascii.hpp"
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
#include "mov/io/warning.hpp"
#include "model_netcdf.hpp"

namespace mov::io {

namespace {

using detail::cut_at_nul;
using detail::fail;
using detail::format_error;

// ---- the file's structure ---------------------------------------------------

struct Structure {
  nc::DimInfo time_dim;
  nc::DimInfo station_dim;
  nc::VarInfo time_var;
};

// The data variables of a kind: the second is the partner of a vector.
struct KindVariables {
  nc::NcNameRef first{"zeta"};
  std::optional<nc::NcNameRef> second{};
};

KindVariables variables_of(AdcircKind kind) {
  switch (kind) {
    case AdcircKind::elevation:
      return {.first = "zeta", .second = std::nullopt};
    case AdcircKind::velocity:
      return {.first = "u-vel", .second = nc::NcNameRef{"v-vel"}};
    case AdcircKind::pressure:
      return {.first = "pressure", .second = std::nullopt};
    case AdcircKind::wind:
      return {.first = "windx", .second = nc::NcNameRef{"windy"}};
  }
  return {.first = "zeta", .second = std::nullopt};
}

// Global attribute `model` is "ADCIRC" (LF 1.1: how v4 tells the file apart).
std::expected<void, Error> require_model(const nc::File& file) {
  auto model = file.text_att(nc::global, "model");
  if (not model) {
    return fail(std::move(model.error()));
  }
  const bool adcirc =
      *model and core::detail::trim(cut_at_nul(**model)) == "ADCIRC";
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

// The first of zeta, u-vel, pressure, windx the file has (LF 3.2). A vector
// needs its partner; a lone v-vel or windy is another kind of file.
std::expected<AdcircKind, Error> detect_kind(const nc::File& file) {
  for (const AdcircKind kind :
       {AdcircKind::elevation, AdcircKind::velocity, AdcircKind::pressure,
        AdcircKind::wind}) {
    const KindVariables names = variables_of(kind);
    const auto present = has_var(file, names.first);
    if (not present) {
      return std::unexpected{present.error()};
    }
    if (not *present) {
      continue;
    }
    if (names.second) {
      const auto partner = has_var(file, *names.second);
      if (not partner) {
        return std::unexpected{partner.error()};
      }
      if (not *partner) {
        return fail(format_error(FormatErrc::partner_variable_missing,
                                 std::string{names.second->view()}));
      }
    }
    return kind;
  }
  for (const nc::NcNameRef lone : {nc::NcNameRef{"v-vel"}, nc::NcNameRef{"windy"}}) {
    const auto present = has_var(file, lone);
    if (not present) {
      return std::unexpected{present.error()};
    }
    if (*present) {
      return fail(
          format_error(FormatErrc::not_this_format, std::string{lone.view()}));
    }
  }
  return fail(format_error(FormatErrc::missing_variable, "zeta"));
}

// The data variables of `kind`, each over (time, station).
std::expected<std::vector<nc::VarInfo>, Error> data_variables(
    const nc::File& file, const Structure& structure, AdcircKind kind) {
  const KindVariables names = variables_of(kind);
  std::vector<nc::VarInfo> vars;
  auto first = detail::require_var(file, names.first);
  if (not first) {
    return std::unexpected{std::move(first.error())};
  }
  vars.push_back(*std::move(first));
  if (names.second) {
    auto second = detail::require_var(file, *names.second);
    if (not second) {
      // The first is there: the second is its partner.
      const auto* missing = std::get_if<FormatError>(&second.error());
      if (missing != nullptr and
          missing->code == FormatErrc::missing_variable) {
        return fail(format_error(FormatErrc::partner_variable_missing,
                                 std::string{names.second->view()}));
      }
      return std::unexpected{std::move(second.error())};
    }
    vars.push_back(*std::move(second));
  }
  for (const nc::VarInfo& var : vars) {
    if (auto shape = detail::require_shape(
            var, {structure.time_dim.id, structure.station_dim.id});
        not shape) {
      return std::unexpected{std::move(shape.error())};
    }
  }
  return vars;
}

detail::StationVariables station_variables(
    const nc::DimInfo& dim, bool names) {
  return {.dim = dim,
          .x = "x",
          .y = "y",
          .names = names ? std::optional<nc::NcNameRef>{nc::NcNameRef{"station_name"}}
                         : std::nullopt,
          .source = core::DataSource::adcirc};
}

std::expected<bool, Error> has_names(const nc::File& file) {
  return has_var(file, "station_name");
}

// ---- the clock ----------------------------------------------------------------

struct Clock {
  CfClock clock;
  std::vector<Warning> warnings;
};

std::expected<Clock, Error> clock_of(const nc::File& file,
                                     const Structure& structure,
                                     const std::optional<core::Time>& given) {
  if (given) {
    // ADCIRC counts seconds from its cold start; the file's own record of it
    // is not consulted (it is often a placeholder).
    auto clock = detail::make_clock(
        {.unit = CfTimeUnit::second, .epoch = *given},
        CfCalendar::proleptic_gregorian, structure.time_var.name.view());
    if (not clock) {
      return std::unexpected{std::move(clock.error())};
    }
    return Clock{.clock = *clock, .warnings = {}};
  }
  const std::string variable{structure.time_var.name.view()};
  const auto required = [&](std::string subject) {
    return fail(
        format_error(FormatErrc::cold_start_required, std::move(subject)));
  };
  auto text = detail::optional_text(file, structure.time_var.name, "units");
  if (not text) {
    return std::unexpected{std::move(text.error())};
  }
  if (not *text) {
    return required(variable + ":units");
  }
  const std::string units_text{cut_at_nul(**text)};
  auto parsed = parse_cf_time_units(units_text);
  if (not parsed) {
    return required(units_text);  // a placeholder such as "seconds since Met"
  }
  auto calendar_text =
      detail::optional_text(file, structure.time_var.name, "calendar");
  if (not calendar_text) {
    return std::unexpected{std::move(calendar_text.error())};
  }
  const std::optional<std::string_view> calendar_view =
      *calendar_text ? std::optional<std::string_view>{**calendar_text}
                     : std::nullopt;
  const auto calendar = parse_cf_calendar(calendar_view);
  if (not calendar) {
    return fail(format_error(FormatErrc::unsupported_calendar,
                             variable + ":calendar"));
  }
  auto clock = detail::make_clock(parsed->value, *calendar, variable);
  if (not clock) {
    return std::unexpected{std::move(clock.error())};
  }
  Clock out{.clock = *clock, .warnings = std::move(parsed->warnings)};
  out.warnings.push_back(
      {.code = WarningCode::epoch_used, .subject = units_text, .count = 1});
  return out;
}

// ---- classifying values -------------------------------------------------------

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

// ---- reading one data variable ---------------------------------------------------

struct Gathered {
  std::vector<core::Column> columns;  // [selected position]
  std::size_t nonfinite{0};
};

// The state a vector's two components share (N7): the first records which of
// its cells were fill; the second reads that, and a fill in either component
// empties both.
struct VectorState {
  std::vector<std::vector<std::uint8_t>> fill;  // [position][time]
  std::vector<core::Column>* first{nullptr};    // set once the first is read
};

// What the data variables of a read have in common.
struct ValueSource {
  const nc::File& file;
  AdcircKind kind;
  std::size_t times;
  std::span<const std::size_t> selection;
  const std::optional<detail::GroupingPolicy>& policy;
  const StopToken& stop;
};

std::expected<Gathered, Error> read_component(const ValueSource& source,
                                              const nc::VarInfo& var,
                                              VectorState* vector) {
  const nc::File& file = source.file;
  const auto type_mismatch = [&]() -> std::expected<void, Error> {
    return fail(detail::nc_fault(file, WrapperFault::type_mismatch,
                                 NcOp::get_var, var.name.view()));
  };
  Gathered out{.columns =
                   detail::empty_columns(source.selection.size(), source.times)};
  // Nearby stations share a read, as far as the file's chunks make that cheap.
  const auto groups =
      detail::plan_groups(file, var, source.selection, source.policy);
  if (not groups) {
    return std::unexpected{groups.error()};
  }
  const detail::GatherPlan plan{.variable = var.name,
                                .times = source.times,
                                .layer = std::nullopt,
                                .groups = *groups};
  const auto run = [&]<nc::Numeric T>() -> std::expected<void, Error> {
    if constexpr (std::same_as<T, std::int64_t>) {
      return type_mismatch();  // a double cannot hold every value
    } else {
      const auto mask = file.masking<T>(var.name);
      if (not mask) {
        return fail(mask.error());
      }
      const auto store = [&](std::size_t position, std::size_t t, T raw) {
        const Cell cell = classify(source.kind, *mask, raw, out.nonfinite);
        const bool second = vector != nullptr and vector->first != nullptr;
        if (second and (cell.fill or vector->fill[position][t] != 0)) {
          out.columns[position][t] = core::Missing{};
          (*vector->first)[position][t] = core::Missing{};
          return;
        }
        out.columns[position][t] = cell.sample;
        if (vector != nullptr and not second) {  // the first component
          vector->fill[position][t] = cell.fill ? 1 : 0;
        }
      };
      return detail::gather<T>(file, plan, source.stop, store);
    }
  };
  const auto done = nc::dispatch_numeric(var.type, run, type_mismatch);
  if (not done) {
    return std::unexpected{done.error()};
  }
  return out;
}

// The columns of every data variable, and what was not finite in them.
struct Values {
  std::vector<core::Variable> variables;
  std::size_t nonfinite{0};
  std::string nonfinite_in;  // the first variable that had one
};

std::expected<Values, Error> read_values(const ValueSource& source,
                                         std::span<const nc::VarInfo> vars) {
  Values out;
  out.variables.reserve(vars.size());  // VectorState points at variables[0]
  std::vector<core::SeriesMeta> schema = detail::adcirc_schema(source.kind);
  VectorState vector;
  if (vars.size() > 1) {
    vector.fill.assign(source.selection.size(),
                       std::vector<std::uint8_t>(source.times, 0));
  }
  for (std::size_t c = 0; c < vars.size(); ++c) {
    auto gathered =
        read_component(source, vars[c], vars.size() > 1 ? &vector : nullptr);
    if (not gathered) {
      return std::unexpected{std::move(gathered.error())};
    }
    if (gathered->nonfinite > 0 and out.nonfinite == 0) {
      out.nonfinite_in = std::string{vars[c].name.view()};
    }
    out.nonfinite += gathered->nonfinite;
    out.variables.push_back({.meta = std::move(schema[c]),
                             .per_station = std::move(gathered->columns)});
    vector.first = &out.variables.front().per_station;
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

// ---- inspect ---------------------------------------------------------------------

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
  auto vars = data_variables(*file, *structure, *kind);
  if (not vars) {
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
  std::vector<std::size_t> all(structure->station_dim.length);
  std::ranges::copy(std::views::iota(std::size_t{0}, all.size()), all.begin());
  auto stations = detail::read_stations(
      *file, station_variables(structure->station_dim, *names), crs, all,
      ctx.stop);
  if (not stations) {
    return std::unexpected{std::move(stations.error())};
  }
  auto units = detail::optional_text(*file, structure->time_var.name, "units");
  if (not units) {
    return std::unexpected{std::move(units.error())};
  }
  AdcircNcCatalog catalog{.kind = *kind,
                          .variables = {},
                          .stations = std::move(stations->value),
                          .times = structure->time_dim.length,
                          .time_units = std::nullopt,
                          .parsed_time_units = std::nullopt};
  for (const nc::VarInfo& var : *vars) {
    catalog.variables.emplace_back(var.name.view());
  }
  if (*units) {
    catalog.time_units = std::string{cut_at_nul(**units)};
    if (auto parsed = parse_cf_time_units(*catalog.time_units)) {
      catalog.parsed_time_units = parsed->value;
      for (Warning& w : parsed->warnings) {
        stations->warnings.push_back(std::move(w));
      }
    }
  }
  return Read<AdcircNcCatalog>{.value = std::move(catalog),
                               .warnings = std::move(stations->warnings)};
}

// ---- read ------------------------------------------------------------------------

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
  auto values = read_values(
      {.file = file,
       .kind = request.kind,
       .times = structure.time_dim.length,
       .selection = selection,
       .policy = policy,
       .stop = ctx.stop},
      setup->vars);
  if (not values) {
    return std::unexpected{std::move(values.error())};
  }
  std::vector<Warning> warnings = std::move(clock->warnings);
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
