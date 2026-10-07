// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/dflow.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <ranges>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "mov/core/vector_series.hpp"
#include "mov/io/cf_time.hpp"
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
using detail::simplified;

struct DerivedRow {
  DflowDerived derived;
  std::string_view token;
  std::string_view long_name;
};

// In the order v4 listed them after the file's own variables.
constexpr std::array<DerivedRow, 5> derived_rows{{
    {.derived = DflowDerived::current_speed_3d,
     .token = "3D_current_speed",
     .long_name = "3D current speed"},
    {.derived = DflowDerived::current_speed_2d,
     .token = "2D_current_speed",
     .long_name = "2D current speed"},
    {.derived = DflowDerived::current_direction_2d,
     .token = "2D_current_direction",
     .long_name = "2D current direction"},
    {.derived = DflowDerived::wind_speed,
     .token = "wind_speed",
     .long_name = "Wind speed"},
    {.derived = DflowDerived::wind_direction,
     .token = "wind_direction",
     .long_name = "Wind direction"},
}};

const DerivedRow& row_of(DflowDerived d) {
  return *std::ranges::find(derived_rows, d, &DerivedRow::derived);
}

// The variables a derived one is computed from (u, v and for the 3-D speed
// w), by the names D-Flow FM gives them.
struct Inputs {
  std::array<std::string_view, 3> names;
  std::size_t count{0};
};

Inputs inputs_of(DflowDerived d) {
  switch (d) {
    case DflowDerived::current_speed_2d:
    case DflowDerived::current_direction_2d:
      return {.names = {"x_velocity", "y_velocity", ""}, .count = 2};
    case DflowDerived::current_speed_3d:
      return {.names = {"x_velocity", "y_velocity", "z_velocity"}, .count = 3};
    case DflowDerived::wind_speed:
    case DflowDerived::wind_direction:
      return {.names = {"windx", "windy", ""}, .count = 2};
  }
  return {.names = {}, .count = 0};
}

// ---- the file's structure -----------------------------------------------------

struct Structure {
  nc::DimInfo time_dim;
  nc::DimInfo stations_dim;
  std::optional<nc::DimInfo> laydim;
  nc::VarInfo time_var;
};

std::expected<Structure, Error> structure_of(const nc::File& file) {
  auto time_dim = detail::require_dim(file, "time");
  if (not time_dim) {
    return std::unexpected{std::move(time_dim.error())};
  }
  auto stations_dim = detail::require_dim(file, "stations");
  if (not stations_dim) {
    return std::unexpected{std::move(stations_dim.error())};
  }
  auto name_len = detail::require_dim(file, "name_len");
  if (not name_len) {
    return std::unexpected{std::move(name_len.error())};
  }
  // The layer count is laydim's, whatever laydimw is (B12).
  auto laydim = file.find_dim("laydim");
  if (not laydim) {
    return fail(std::move(laydim.error()));
  }
  auto time_var = detail::require_var(file, "time");
  if (not time_var) {
    return std::unexpected{std::move(time_var.error())};
  }
  if (auto shape = detail::require_shape(*time_var, {time_dim->id}); not shape) {
    return std::unexpected{std::move(shape.error())};
  }
  auto names = detail::require_var(file, "station_name");
  if (not names) {
    return std::unexpected{std::move(names.error())};
  }
  if (auto shape =
          detail::require_shape(*names, {stations_dim->id, name_len->id});
      not shape) {
    return std::unexpected{std::move(shape.error())};
  }
  return Structure{.time_dim = *std::move(time_dim),
                   .stations_dim = *std::move(stations_dim),
                   .laydim = *std::move(laydim),
                   .time_var = *std::move(time_var)};
}

detail::StationVariables station_variables(const nc::DimInfo& dim) {
  return {.dim = dim,
          .x = "station_x_coordinate",
          .y = "station_y_coordinate",
          .names = nc::NcNameRef{"station_name"},
          .source = core::DataSource::dflowfm};
}

// ---- the clock ------------------------------------------------------------------

struct TimeSetup {
  CfTimeUnits units;
  CfCalendar calendar;
  CfClock clock;
  std::vector<Warning> warnings;
};

std::expected<TimeSetup, Error> time_of(const nc::File& file,
                                   const Structure& structure) {
  const std::string variable{structure.time_var.name.view()};
  auto text = detail::optional_text(file, structure.time_var.name, "units");
  if (not text) {
    return std::unexpected{std::move(text.error())};
  }
  if (not *text) {
    return fail(
        format_error(FormatErrc::missing_attribute, variable + ":units"));
  }
  auto parsed = parse_cf_time_units(cut_at_nul(**text));
  if (not parsed) {
    return fail(std::move(parsed.error()));
  }
  auto calendar_text =
      detail::optional_text(file, structure.time_var.name, "calendar");
  if (not calendar_text) {
    return std::unexpected{std::move(calendar_text.error())};
  }
  const auto calendar = parse_cf_calendar(
      *calendar_text ? std::optional<std::string_view>{**calendar_text}
                     : std::nullopt);
  if (not calendar) {
    return fail(format_error(FormatErrc::unsupported_calendar,
                             variable + ":calendar"));
  }
  auto clock = detail::make_clock(parsed->value, *calendar, variable);
  if (not clock) {
    return std::unexpected{std::move(clock.error())};
  }
  return TimeSetup{.units = parsed->value,
              .calendar = *calendar,
              .clock = *clock,
              .warnings = std::move(parsed->warnings)};
}

// ---- the variables of the file ------------------------------------------------------

// A variable the file offers: over (time, stations), or (time, stations,
// laydim) with that many layers.
struct Listed {
  nc::VarInfo info;
  std::optional<std::size_t> layers;
};

bool readable(nc::Type t) {
  return t == nc::Type::byte or t == nc::Type::short_ or t == nc::Type::int_ or
         t == nc::Type::float_ or t == nc::Type::double_;
}

std::optional<std::size_t> layers_of(const nc::VarInfo& var,
                                     const Structure& structure,
                                     bool& offered) {
  const auto& dims = var.dims;
  offered = false;
  if (dims.size() < 2 or dims[0].id != structure.time_dim.id or
      dims[1].id != structure.stations_dim.id) {
    return std::nullopt;
  }
  if (dims.size() == 2) {
    offered = true;
    return std::nullopt;
  }
  // Variables on laydimw (layer interfaces) are not offered.
  if (dims.size() == 3 and structure.laydim and
      dims[2].id == structure.laydim->id) {
    offered = true;
    return structure.laydim->length;
  }
  return std::nullopt;
}

struct Listing {
  std::vector<Listed> variables;
  std::vector<Warning> warnings;
};

std::expected<Listing, Error> list_variables(const nc::File& file,
                                             const Structure& structure) {
  auto all = file.variables();
  if (not all) {
    return fail(std::move(all.error()));
  }
  Listing out;
  for (nc::VarInfo& var : *all) {
    bool offered = false;
    const auto layers = layers_of(var, structure, offered);
    if (not offered) {
      continue;
    }
    if (not readable(var.type)) {
      out.warnings.push_back({.code = WarningCode::skipped_variable,
                              .subject = std::string{var.name.view()},
                              .count = 1});
      continue;
    }
    out.variables.push_back({.info = std::move(var), .layers = layers});
  }
  return out;
}

const Listed* find_listed(const std::vector<Listed>& listed,
                          std::string_view name) {
  const auto it = std::ranges::find_if(
      listed, [name](const Listed& l) { return l.info.name == name; });
  return it == listed.end() ? nullptr : &*it;
}

// The Flat or Layered entry for `source`, from variables of one shape.
DflowVariable entry_for(DflowSource source, std::string long_name,
                        std::optional<std::size_t> layers) {
  if (layers) {
    return Layered{.source = std::move(source),
                   .long_name = std::move(long_name),
                   .layers = *layers};
  }
  return Flat{.source = std::move(source), .long_name = std::move(long_name)};
}

// Whether every input of `d` is in the file with one shape: the shape.
struct Shape {
  bool ok;
  std::optional<std::size_t> layers;
};

Shape shape_of_inputs(const std::vector<Listed>& listed, DflowDerived d) {
  const Inputs inputs = inputs_of(d);
  std::optional<Shape> shape;
  for (std::size_t i = 0; i < inputs.count; ++i) {
    const Listed* l = find_listed(listed, inputs.names[i]);
    if (l == nullptr or (shape and shape->layers != l->layers)) {
      return {.ok = false, .layers = std::nullopt};
    }
    shape = Shape{.ok = true, .layers = l->layers};
  }
  return shape.value_or(Shape{.ok = false, .layers = std::nullopt});
}

// The 3-D speed is for layered variables only.
bool derivable(const std::vector<Listed>& listed, DflowDerived d,
               Shape& shape) {
  shape = shape_of_inputs(listed, d);
  return shape.ok and (d != DflowDerived::current_speed_3d or
                       shape.layers.has_value());
}

std::expected<std::vector<DflowVariable>, Error> variables_of(
    const nc::File& file, const std::vector<Listed>& listed) {
  std::vector<DflowVariable> out;
  for (const Listed& l : listed) {
    auto label = detail::optional_text(file, l.info.name, "long_name");
    if (not label) {
      return std::unexpected{std::move(label.error())};
    }
    std::string long_name =
        *label and not cut_at_nul(**label).empty()
            ? simplified(cut_at_nul(**label))
            : std::string{l.info.name.view()};
    out.push_back(entry_for(l.info.name, std::move(long_name), l.layers));
  }
  for (const DerivedRow& row : derived_rows) {
    Shape shape{.ok = false, .layers = std::nullopt};
    if (derivable(listed, row.derived, shape)) {
      out.push_back(entry_for(row.derived, std::string{row.long_name},
                              shape.layers));
    }
  }
  return out;
}

// ---- one variable's meta -------------------------------------------------------------------

struct NamedQuantity {
  std::string_view name;
  core::Quantity quantity;
};

// D-Flow FM's names for the registry quantities (LF section 4, SN section 6).
constexpr std::array<NamedQuantity, 5> dflow_quantities{{
    {.name = "waterlevel", .quantity = core::Quantity::water_level},
    {.name = "x_velocity", .quantity = core::Quantity::current_u},
    {.name = "y_velocity", .quantity = core::Quantity::current_v},
    {.name = "windx", .quantity = core::Quantity::wind_u},
    {.name = "windy", .quantity = core::Quantity::wind_v},
}};

struct Described {
  core::SeriesMeta meta;
  std::vector<Warning> warnings;
};

std::expected<Described, Error> describe_variable(const nc::File& file,
                                                  const nc::VarInfo& var) {
  const std::string_view name = var.name.view();
  auto label = detail::optional_text(file, var.name, "long_name");
  auto standard = detail::optional_text(file, var.name, "standard_name");
  auto units = detail::optional_text(file, var.name, "units");
  if (not label) {
    return std::unexpected{std::move(label.error())};
  }
  if (not standard) {
    return std::unexpected{std::move(standard.error())};
  }
  if (not units) {
    return std::unexpected{std::move(units.error())};
  }
  Described out{.meta = {}, .warnings = {}};

  core::QuantityId quantity;
  if (const auto it =
          std::ranges::find(dflow_quantities, name, &NamedQuantity::name);
      it != dflow_quantities.end()) {
    quantity = it->quantity;
  } else if (const auto registry = core::parse_quantity_token(name)) {
    quantity = *registry;
  } else if (auto generic = core::GenericQuantity::parse(
                 {.token = name,
                  .standard_name =
                      *standard ? cut_at_nul(**standard) : std::string_view{}})) {
    quantity = *std::move(generic);
  } else {
    quantity = core::GenericQuantity::value();
    out.warnings.push_back({.code = WarningCode::unknown_quantity,
                            .subject = std::string{name},
                            .count = 1});
  }

  std::optional<core::Unit> unit;
  if (*units) {
    unit = core::parse_unit(cut_at_nul(**units));
  }
  if (not unit) {
    if (const auto* registry = std::get_if<core::Quantity>(&quantity)) {
      unit = core::canonical_unit(*registry);
    }
  } else if (const auto* other = std::get_if<core::OtherUnit>(&*unit);
             other != nullptr and not core::is_canonical_other(*other)) {
    out.warnings.push_back({.code = WarningCode::unrecognized_unit,
                            .subject = std::string{name},
                            .count = 1});
  }
  out.meta = core::SeriesMeta::make(
      {.quantity = std::move(quantity),
       .label = *label and not cut_at_nul(**label).empty()
                    ? simplified(cut_at_nul(**label))
                    : std::string{name},
       .unit = std::move(unit)});
  return out;
}

// ---- reading one variable ------------------------------------------------------------------

struct Component {
  std::string name;
  core::SeriesMeta meta;
  std::vector<core::Column> columns;  // [selected position]
  std::size_t nonfinite{0};
  std::vector<Warning> warnings;
};

// D-Flow has no dry sentinel: a value is Missing only by the attributes of
// its variable (Masking) or because it is not finite (B12: v4 compared with a
// hard-coded -999).
template <nc::Numeric T>
core::Sample sample_of(const nc::Masking<T>& mask, T raw,
                       std::size_t& nonfinite) {
  if (not std::isfinite(nc::detail::widen(raw))) {
    ++nonfinite;
    return core::Missing{};
  }
  return mask.apply(raw);
}

std::expected<Component, Error> read_component(
    const nc::File& file, const Listed& listed,
    std::optional<std::size_t> layer, std::size_t times,
    std::span<const std::size_t> selection, const StopToken& stop) {
  const nc::VarInfo& var = listed.info;
  auto described = describe_variable(file, var);
  if (not described) {
    return std::unexpected{std::move(described.error())};
  }
  Component out{.name = std::string{var.name.view()},
                .meta = std::move(described->meta),
                .columns = detail::empty_columns(selection.size(), times),
                .nonfinite = 0,
                .warnings = std::move(described->warnings)};
  // Nearby stations share a read, as far as the file's chunks make that cheap.
  const auto groups = detail::plan_groups(file, var, selection, std::nullopt);
  if (not groups) {
    return std::unexpected{groups.error()};
  }
  const detail::GatherPlan plan{
      .variable = var.name, .times = times, .layer = layer, .groups = *groups};
  const auto run = [&]<nc::Numeric T>() -> std::expected<void, Error> {
    const auto mask = file.masking<T>(var.name);
    if (not mask) {
      return fail(mask.error());
    }
    return detail::gather<T>(
        file, plan, stop, [&](std::size_t position, std::size_t t, T raw) {
          out.columns[position][t] = sample_of(*mask, raw, out.nonfinite);
        });
  };
  const auto done = nc::dispatch_numeric(
      var.type, run, [&]() -> std::expected<void, Error> {
        return fail(detail::nc_fault(file, WrapperFault::type_mismatch,
                                     NcOp::get_var, var.name.view()));
      });
  if (not done) {
    return std::unexpected{done.error()};
  }
  return out;
}

// ---- derived variables -------------------------------------------------------------------

FormatError alignment_error(std::string_view subject) {
  return format_error(FormatErrc::noncanonical_unit, std::string{subject});
}

std::expected<core::TimeSeries, Error> series_of(const core::TimeAxis& axis,
                                                 const Component& component,
                                                 std::size_t position) {
  auto series = core::TimeSeries::make(axis, component.columns[position],
                                       component.meta);
  if (not series) {
    return fail(format_error(FormatErrc::dimension_mismatch, component.name));
  }
  return *std::move(series);
}

// The derived series of one station.
std::expected<core::TimeSeries, Error> derive_one(
    DflowDerived d, const core::TimeAxis& axis,
    const std::vector<Component>& components, std::size_t position) {
  std::vector<core::TimeSeries> series;
  for (const Component& c : components) {
    auto s = series_of(axis, c, position);
    if (not s) {
      return std::unexpected{std::move(s.error())};
    }
    series.push_back(*std::move(s));
  }
  const std::string subject = components[0].name + ", " + components[1].name;
  auto vector = core::VectorSeries::make(std::move(series[0]), std::move(series[1]));
  if (not vector) {
    return fail(alignment_error(subject));
  }
  switch (d) {
    case DflowDerived::current_speed_2d:
    case DflowDerived::wind_speed:
      return vector->magnitude();
    case DflowDerived::current_direction_2d:
    case DflowDerived::wind_direction:
      return vector->cartesian_direction();
    case DflowDerived::current_speed_3d: {
      auto speed = core::magnitude3(*vector, series[2]);
      if (not speed) {
        return fail(alignment_error(subject + ", " + components[2].name));
      }
      return *std::move(speed);
    }
  }
  return fail(format_error(FormatErrc::dimension_mismatch, subject));
}

std::expected<core::Variable, Error> derive(
    DflowDerived d, const core::TimeAxis& axis,
    std::vector<Component>& components, std::size_t selected) {
  // The meta is that of an empty station's series, so it exists with no
  // station selected.
  const core::TimeAxis none;
  std::vector<Component> shapes;
  shapes.reserve(components.size());
  for (const Component& c : components) {
    shapes.push_back({.name = c.name,
                      .meta = c.meta,
                      .columns = {core::Column{}},
                      .nonfinite = 0,
                      .warnings = {}});
  }
  auto meta = derive_one(d, none, shapes, 0);
  if (not meta) {
    return std::unexpected{std::move(meta.error())};
  }
  core::Variable out{.meta = meta->meta(), .per_station = {}};
  out.per_station.reserve(selected);
  for (std::size_t position = 0; position < selected; ++position) {
    auto series = derive_one(d, axis, components, position);
    if (not series) {
      return std::unexpected{std::move(series.error())};
    }
    out.per_station.push_back(std::move(*series).into_parts().samples);
  }
  return out;
}

}  // namespace

// ---- the vocabulary ---------------------------------------------------------------------------

std::string_view to_token(DflowDerived d) noexcept { return row_of(d).token; }

std::optional<DflowDerived> parse_dflow_derived(
    std::string_view token) noexcept {
  const auto it = std::ranges::find(derived_rows, token, &DerivedRow::token);
  if (it == derived_rows.end()) {
    return std::nullopt;
  }
  return it->derived;
}

std::expected<Layer, FormatError> Layer::make(const Layered& v,
                                              std::size_t one_based) {
  if (one_based < 1 or one_based > v.layers) {
    return std::unexpected{format_error(FormatErrc::layer_out_of_range,
                                        v.long_name, std::nullopt, one_based)};
  }
  return Layer{one_based - 1};
}

// ---- inspect ------------------------------------------------------------------------------------

std::expected<Read<DflowCatalog>, Error> inspect_dflow(
    const std::filesystem::path& path, core::Epsg crs, const ReadContext& ctx) {
  auto file = nc::File::open(path, ctx.limits);
  if (not file) {
    return fail(std::move(file.error()));
  }
  auto structure = structure_of(*file);
  if (not structure) {
    return std::unexpected{std::move(structure.error())};
  }
  auto time = time_of(*file, *structure);
  if (not time) {
    return std::unexpected{std::move(time.error())};
  }
  auto listing = list_variables(*file, *structure);
  if (not listing) {
    return std::unexpected{std::move(listing.error())};
  }
  auto variables = variables_of(*file, listing->variables);
  if (not variables) {
    return std::unexpected{std::move(variables.error())};
  }
  if (auto size = detail::check_station_count(*file, structure->stations_dim);
      not size) {
    return std::unexpected{std::move(size.error())};
  }
  std::vector<std::size_t> all(structure->stations_dim.length);
  std::ranges::copy(std::views::iota(std::size_t{0}, all.size()), all.begin());
  auto stations = detail::read_stations(
      *file, station_variables(structure->stations_dim), crs, all, ctx.stop);
  if (not stations) {
    return std::unexpected{std::move(stations.error())};
  }
  std::vector<Warning> warnings = std::move(time->warnings);
  for (auto* more : {&stations->warnings, &listing->warnings}) {
    for (Warning& w : *more) {
      warnings.push_back(std::move(w));
    }
  }
  return Read<DflowCatalog>{
      .value = {.stations = std::move(stations->value),
                .variables = *std::move(variables),
                .times = structure->time_dim.length,
                .time_units = time->units,
                .calendar = time->calendar},
      .warnings = std::move(warnings)};
}

// ---- read -----------------------------------------------------------------------------------------

namespace {

// What the request asks for, resolved against the file.
struct Target {
  DflowSource source;
  std::string long_name;
  std::optional<std::size_t> layer;  // zero-based, for a layered variable
};

Target target_of(const DflowChoice& choice) {
  if (const auto* flat = std::get_if<Flat>(&choice)) {
    return {.source = flat->source,
            .long_name = flat->long_name,
            .layer = std::nullopt};
  }
  const auto& at = std::get<AtLayer>(choice);
  return {.source = at.variable.source,
          .long_name = at.variable.long_name,
          .layer = at.layer.zero_based()};
}

// The variables the target reads and that they have the shape it asks for:
// flat for Flat, layered with a layer inside the file's layers for AtLayer.
std::expected<std::vector<const Listed*>, Error> resolve(
    const std::vector<Listed>& listed, const Target& target) {
  std::vector<std::string_view> names;
  if (const auto* name = std::get_if<nc::NcName>(&target.source)) {
    names.push_back(name->view());
  } else {
    const Inputs inputs = inputs_of(std::get<DflowDerived>(target.source));
    names.assign(inputs.names.begin(), inputs.names.begin() +
                                           static_cast<std::ptrdiff_t>(inputs.count));
  }
  std::vector<const Listed*> out;
  for (const std::string_view name : names) {
    const Listed* l = find_listed(listed, name);
    if (l == nullptr) {
      return fail(
          format_error(FormatErrc::missing_variable, std::string{name}));
    }
    if (l->layers.has_value() != target.layer.has_value()) {
      return fail(format_error(FormatErrc::dimension_mismatch,
                               std::string{name}));
    }
    if (l->layers and *target.layer >= *l->layers) {
      return fail(format_error(FormatErrc::layer_out_of_range,
                               std::string{name}, std::nullopt,
                               *target.layer + 1));
    }
    out.push_back(l);
  }
  return out;
}

// What a read finds out before it reads values: the open file, its structure
// and the variables the request needs.
struct Setup {
  nc::File file;
  Structure structure;
  Target target;
  std::vector<const Listed*> inputs;
  Listing listing;  // owns what `inputs` points to
};

std::expected<Setup, Error> set_up(const std::filesystem::path& path,
                                   const DflowRequest& request,
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
                                          structure->stations_dim.length);
      not ok) {
    return std::unexpected{std::move(ok.error())};
  }
  auto listing = list_variables(*file, *structure);
  if (not listing) {
    return std::unexpected{std::move(listing.error())};
  }
  Target target = target_of(request.choice);
  auto inputs = resolve(listing->variables, target);
  if (not inputs) {
    return std::unexpected{std::move(inputs.error())};
  }
  if (auto size = detail::check_result_size(
          *file, inputs->front()->info.name.view(),
          request.stations.indices().size(), structure->time_dim.length,
          inputs->size());
      not size) {
    return std::unexpected{std::move(size.error())};
  }
  return Setup{.file = *std::move(file),
               .structure = *std::move(structure),
               .target = std::move(target),
               .inputs = *std::move(inputs),
               .listing = *std::move(listing)};
}

// The components of the target, read, and what was not finite in them.
struct Values {
  std::vector<Component> components;
  std::vector<Warning> warnings;
  std::size_t nonfinite{0};
  std::string nonfinite_in;  // the first variable that had one
};

std::expected<Values, Error> read_values(
    const Setup& setup, std::span<const std::size_t> selection,
    const StopToken& stop) {
  Values out;
  for (const Listed* input : setup.inputs) {
    auto component =
        read_component(setup.file, *input, setup.target.layer,
                       setup.structure.time_dim.length, selection, stop);
    if (not component) {
      return std::unexpected{std::move(component.error())};
    }
    if (component->nonfinite > 0 and out.nonfinite == 0) {
      out.nonfinite_in = component->name;
    }
    out.nonfinite += component->nonfinite;
    for (Warning& w : component->warnings) {
      out.warnings.push_back(std::move(w));
    }
    out.components.push_back(*std::move(component));
  }
  return out;
}

// The one column of the table: the variable itself, or the derived series.
std::expected<core::Variable, Error> variable_of(
    const Target& target, const core::TimeAxis& axis,
    std::vector<Component>& components, std::size_t selected) {
  if (const auto* derived = std::get_if<DflowDerived>(&target.source)) {
    return derive(*derived, axis, components, selected);
  }
  return core::Variable{.meta = std::move(components[0].meta),
                        .per_station = std::move(components[0].columns)};
}

}  // namespace

std::expected<Read<core::StationTable>, Error> read_dflow(
    const std::filesystem::path& path, const DflowRequest& request,
    const ReadContext& ctx) {
  auto setup = set_up(path, request, ctx);
  if (not setup) {
    return std::unexpected{std::move(setup.error())};
  }
  const std::span<const std::size_t> selection = request.stations.indices();
  auto time = time_of(setup->file, setup->structure);
  if (not time) {
    return std::unexpected{std::move(time.error())};
  }
  auto axis = detail::read_time_axis(setup->file, setup->structure.time_var,
                                     time->clock, ctx.stop);
  if (not axis) {
    return std::unexpected{std::move(axis.error())};
  }
  auto stations = detail::read_stations(
      setup->file, station_variables(setup->structure.stations_dim),
      request.crs, selection, ctx.stop);
  if (not stations) {
    return std::unexpected{std::move(stations.error())};
  }
  auto values = read_values(*setup, selection, ctx.stop);
  if (not values) {
    return std::unexpected{std::move(values.error())};
  }
  auto variable =
      variable_of(setup->target, *axis, values->components, selection.size());
  if (not variable) {
    return std::unexpected{std::move(variable.error())};
  }
  std::vector<Warning> warnings = std::move(time->warnings);
  for (Warning& w : values->warnings) {
    warnings.push_back(std::move(w));
  }
  append_if_counted(warnings, {.code = WarningCode::nonfinite_masked,
                               .subject = std::move(values->nonfinite_in),
                               .count = values->nonfinite});
  std::vector<core::Variable> variables;
  variables.push_back(*std::move(variable));
  return detail::assemble_table(std::move(variables), *std::move(axis),
                                *std::move(stations), std::move(warnings));
}

}  // namespace mov::io
