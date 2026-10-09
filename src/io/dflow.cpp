// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/dflow.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "model_netcdf.hpp"
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
#include "mov/io/projection.hpp"
#include "mov/io/warning.hpp"

namespace mov::io {

namespace {

using detail::fail;
using detail::format_error;
using detail::simplified;

// ---- the derived variables
// ------------------------------------------------------

constexpr std::array<std::string_view, 2> current_2d_inputs{"x_velocity",
                                                            "y_velocity"};
constexpr std::array<std::string_view, 3> current_3d_inputs{
    "x_velocity", "y_velocity", "z_velocity"};
constexpr std::array<std::string_view, 2> wind_inputs{"windx", "windy"};

struct DerivedRow {
  DflowDerived derived;
  std::string_view token;
  std::string_view long_name;
  std::span<const std::string_view> inputs;  // u, v and for the 3-D speed w
};

// Indexed by the enumerator (checked below).
constexpr std::array<DerivedRow, 5> derived_rows{{
    {.derived = DflowDerived::current_speed_2d,
     .token = "2D_current_speed",
     .long_name = "2D current speed",
     .inputs = current_2d_inputs},
    {.derived = DflowDerived::current_direction_2d,
     .token = "2D_current_direction",
     .long_name = "2D current direction",
     .inputs = current_2d_inputs},
    {.derived = DflowDerived::current_speed_3d,
     .token = "3D_current_speed",
     .long_name = "3D current speed",
     .inputs = current_3d_inputs},
    {.derived = DflowDerived::wind_speed,
     .token = "wind_speed",
     .long_name = "Wind speed",
     .inputs = wind_inputs},
    {.derived = DflowDerived::wind_direction,
     .token = "wind_direction",
     .long_name = "Wind direction",
     .inputs = wind_inputs},
}};

[[nodiscard]] constexpr bool rows_in_enumerator_order() noexcept {
  for (std::size_t i = 0; i < derived_rows.size(); ++i) {
    if (static_cast<std::size_t>(derived_rows[i].derived) != i) {
      return false;
    }
  }
  return true;
}
static_assert(rows_in_enumerator_order());

const DerivedRow& row_of(DflowDerived d) {
  return derived_rows[static_cast<std::size_t>(d)];
}

// The order v4 listed them in after the file's own variables.
constexpr std::array<DflowDerived, 5> catalog_order{
    DflowDerived::current_speed_3d, DflowDerived::current_speed_2d,
    DflowDerived::current_direction_2d, DflowDerived::wind_speed,
    DflowDerived::wind_direction};

// ---- the file's structure
// -----------------------------------------------------

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
  // The layer count is laydim's, whatever laydimw is (v4 checked laydimw but
  // read laydim).
  auto laydim = file.find_dim("laydim");
  if (not laydim) {
    return fail(std::move(laydim.error()));
  }
  auto time_var = detail::require_var(file, "time");
  if (not time_var) {
    return std::unexpected{std::move(time_var.error())};
  }
  if (auto shape = detail::require_shape(*time_var, {time_dim->id});
      not shape) {
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

detail::StationVariables station_variables(const Structure& structure) {
  return {.dim = structure.stations_dim,
          .x = "station_x_coordinate",
          .y = "station_y_coordinate",
          .names = nc::NcNameRef{"station_name"},
          .time_dim = structure.time_dim.id,
          .source = core::DataSource::dflowfm};
}

// ---- the variables of the file
// ---------------------------------------------------

// How a variable lies over the dimensions: not at all like a data variable,
// over (time, stations), or over (time, stations, laydim) with that many
// layers. One type, so no caller has a flag and an out-parameter to keep in
// step.
struct VarShape {
  enum class Offer : std::uint8_t { no, flat, layered };
  Offer offer{Offer::no};
  std::size_t layers{0};  // of a layered variable
  friend bool operator==(const VarShape&, const VarShape&) = default;
};

VarShape shape_of(const nc::VarInfo& var, const Structure& structure) {
  const auto& dims = var.dims;
  const bool over_time_and_stations = dims.size() >= 2 and
                                      dims[0].id == structure.time_dim.id and
                                      dims[1].id == structure.stations_dim.id;
  if (not over_time_and_stations) {
    return {};
  }
  if (dims.size() == 2) {
    return {.offer = VarShape::Offer::flat, .layers = 0};
  }
  // Variables on laydimw (layer interfaces) are not offered.
  if (dims.size() == 3 and structure.laydim and
      dims[2].id == structure.laydim->id) {
    return {.offer = VarShape::Offer::layered,
            .layers = structure.laydim->length};
  }
  return {};
}

// A variable the file offers.
struct Listed {
  nc::VarInfo info;
  VarShape shape;

  [[nodiscard]] std::optional<std::size_t> layers() const {
    return shape.offer == VarShape::Offer::layered ? std::optional{shape.layers}
                                                   : std::nullopt;
  }
};

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
    const VarShape shape = shape_of(var, structure);
    if (shape.offer == VarShape::Offer::no) {
      continue;
    }
    if (not nc::sample_readable(var.type)) {
      out.warnings.push_back({.code = WarningCode::skipped_variable,
                              .subject = std::string{var.name.view()},
                              .count = 1});
      continue;
    }
    out.variables.push_back({.info = std::move(var), .shape = shape});
  }
  return out;
}

const Listed* find_listed(std::span<const Listed> listed,
                          std::string_view name) {
  const auto it = std::ranges::find_if(
      listed, [name](const Listed& l) { return l.info.name == name; });
  return it == listed.end() ? nullptr : &*it;
}

// The shape that all the inputs of `d` have, if they are all in the file and
// agree. (The 3-D speed needs layered inputs.)
std::optional<VarShape> common_shape(std::span<const Listed> listed,
                                     const DerivedRow& row) {
  // The shapes of the inputs, in order (a transform that stops at an input the
  // file does not have; ranges::to is not on every standard library yet).
  std::vector<VarShape> shapes;
  shapes.reserve(row.inputs.size());
  for (const std::string_view name : row.inputs) {
    const Listed* l = find_listed(listed, name);
    if (l == nullptr) {
      return std::nullopt;
    }
    shapes.push_back(l->shape);
  }
  if (std::ranges::adjacent_find(shapes, std::ranges::not_equal_to{}) !=
      shapes.end()) {
    return std::nullopt;
  }
  if (row.derived == DflowDerived::current_speed_3d and
      shapes.front().offer != VarShape::Offer::layered) {
    return std::nullopt;
  }
  return shapes.front();
}

// The Flat or Layered entry for `source`.
DflowVariable entry_for(DflowSource source, std::string long_name,
                        const VarShape& shape) {
  if (shape.offer == VarShape::Offer::layered) {
    return Layered{.source = std::move(source),
                   .long_name = std::move(long_name),
                   .layers = shape.layers};
  }
  return Flat{.source = std::move(source), .long_name = std::move(long_name)};
}

std::expected<std::vector<DflowVariable>, Error> variables_of(
    const nc::File& file, std::span<const Listed> listed) {
  std::vector<DflowVariable> out;
  out.reserve(listed.size() + catalog_order.size());
  for (const Listed& l : listed) {
    auto label = detail::optional_text(file, l.info.name, "long_name");
    if (not label) {
      return std::unexpected{std::move(label.error())};
    }
    std::string long_name = *label and not(*label)->empty()
                                ? simplified(**label)
                                : std::string{l.info.name.view()};
    out.push_back(entry_for(l.info.name, std::move(long_name), l.shape));
  }
  for (const DflowDerived d : catalog_order) {
    const DerivedRow& row = row_of(d);
    if (const auto shape = common_shape(listed, row)) {
      out.push_back(entry_for(d, std::string{row.long_name}, *shape));
    }
  }
  return out;
}

// ---- one variable's meta
// -------------------------------------------------------------

struct NamedQuantity {
  std::string_view name;
  core::Quantity quantity;
  std::string_view grid_token;  // what it is on a projected grid
};

// D-Flow FM's names for the registry quantities (LF section 4, SN section 6).
// On a projected grid the components are along the grid's axes: CF's names
// for those.
constexpr std::array<NamedQuantity, 5> dflow_quantities{{
    {.name = "waterlevel",
     .quantity = core::Quantity::water_level,
     .grid_token = ""},
    {.name = "x_velocity",
     .quantity = core::Quantity::current_u,
     .grid_token = "sea_water_x_velocity"},
    {.name = "y_velocity",
     .quantity = core::Quantity::current_v,
     .grid_token = "sea_water_y_velocity"},
    {.name = "windx",
     .quantity = core::Quantity::wind_u,
     .grid_token = "x_wind"},
    {.name = "windy",
     .quantity = core::Quantity::wind_v,
     .grid_token = "y_wind"},
}};

struct Described {
  core::SeriesMeta meta;
  std::vector<Warning> warnings;
};

// The quantity of variable `name`: the table's (or its grid-relative generic
// on a projected grid), a registry token, a token of its own, or `value`.
core::QuantityId quantity_of(std::string_view name,
                             std::string_view standard_name, CrsKind grid,
                             std::vector<Warning>& warnings) {
  const auto named =
      std::ranges::find(dflow_quantities, name, &NamedQuantity::name);
  if (named != dflow_quantities.end()) {
    if (grid == CrsKind::projected and not named->grid_token.empty()) {
      return core::GenericQuantity::parse({.token = named->grid_token,
                                           .standard_name = named->grid_token})
          .value_or(core::GenericQuantity::value());
    }
    return named->quantity;
  }
  if (const auto registry = core::parse_quantity_token(name)) {
    return *registry;
  }
  if (auto generic = core::GenericQuantity::parse(
          {.token = name, .standard_name = standard_name})) {
    return *std::move(generic);
  }
  warnings.push_back({.code = WarningCode::unknown_quantity,
                      .subject = std::string{name},
                      .count = 1});
  return core::GenericQuantity::value();
}

// The unit of the `units` attribute (parsed_unit); absent, the registry
// quantity's own.
std::optional<core::Unit> unit_of(const std::optional<std::string>& units,
                                  const core::QuantityId& quantity,
                                  std::vector<Warning>& warnings) {
  Read<std::optional<core::Unit>> unit = detail::parsed_unit(units);
  if (not unit.value) {
    if (const auto* registry = std::get_if<core::Quantity>(&quantity)) {
      return core::canonical_unit(*registry);
    }
    return std::nullopt;
  }
  append(warnings, std::move(unit.warnings));
  return std::move(unit.value);
}

std::expected<Described, Error> describe_variable(const nc::File& file,
                                                  const nc::VarInfo& var,
                                                  CrsKind grid) {
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
  core::QuantityId quantity = quantity_of(
      name, *standard ? std::string_view{**standard} : std::string_view{}, grid,
      out.warnings);
  std::optional<core::Unit> unit = unit_of(*units, quantity, out.warnings);
  out.meta = core::SeriesMeta::make({.quantity = std::move(quantity),
                                     .label = *label and not(*label)->empty()
                                                  ? simplified(**label)
                                                  : std::string{name},
                                     .unit = std::move(unit)});
  return out;
}

// ---- reading one variable
// ------------------------------------------------------------

struct Component {
  std::string name;
  core::SeriesMeta meta;
  std::vector<core::Column> columns;  // [selected position]
  std::size_t nonfinite{0};
  std::vector<Warning> warnings;
};

// D-Flow has no dry sentinel: a value is Missing only by the attributes of
// its variable (Masking) or because it is not finite (v4 compared with a
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

// What every component of a read has in common.
struct ComponentSource {
  const nc::File& file;
  CrsKind grid;
  std::size_t times;
  int station_dim;
  std::optional<std::size_t> layer;
  std::span<const std::size_t> selection;
  const StopToken& stop;
};

std::expected<Component, Error> read_component(const ComponentSource& source,
                                               const nc::VarInfo& var) {
  auto described = describe_variable(source.file, var, source.grid);
  if (not described) {
    return std::unexpected{std::move(described.error())};
  }
  Component out{
      .name = std::string{var.name.view()},
      .meta = std::move(described->meta),
      .columns = detail::empty_columns(source.selection.size(), source.times),
      .nonfinite = 0,
      .warnings = std::move(described->warnings)};
  // Nearby stations share a read, as far as the file's chunks make that cheap.
  const auto groups = detail::plan_groups(source.file, var, source.station_dim,
                                          source.selection, std::nullopt);
  if (not groups) {
    return std::unexpected{groups.error()};
  }
  const detail::GatherPlan plan{.variable = var.name,
                                .times = source.times,
                                .layer = source.layer,
                                .groups = *groups};
  const auto run = [&]<nc::Numeric T>() -> std::expected<void, Error> {
    const auto mask = source.file.masking<T>(var.name);
    if (not mask) {
      return fail(mask.error());
    }
    return detail::gather<T>(source.file, plan, source.stop,
                             [&](std::size_t position, std::size_t t, T raw) {
                               out.columns[position][t] =
                                   sample_of(*mask, raw, out.nonfinite);
                             });
  };
  if (auto done = detail::dispatch_model_numeric(source.file, var, run);
      not done) {
    return std::unexpected{std::move(done.error())};
  }
  return out;
}

// ---- derived variables
// -------------------------------------------------------------

// The alignment errors of core, as FormatErrors: units that are unknown or
// differ are a unit problem; the rest cannot happen to columns of one axis
// and are a shape problem.
FormatError alignment_error(core::VectorErrc code, std::string subject) {
  switch (code) {
    case core::VectorErrc::unit_unknown:
    case core::VectorErrc::units_differ:
      return format_error(FormatErrc::noncanonical_unit, std::move(subject));
    case core::VectorErrc::not_a_vector_pair:
    case core::VectorErrc::times_differ:
      return format_error(FormatErrc::dimension_mismatch, std::move(subject));
  }
  return format_error(FormatErrc::dimension_mismatch, std::move(subject));
}

FormatError alignment_error(core::VerticalErrc code, std::string subject) {
  switch (code) {
    case core::VerticalErrc::unit_unknown:
    case core::VerticalErrc::units_differ:
      return format_error(FormatErrc::noncanonical_unit, std::move(subject));
    case core::VerticalErrc::not_generic:
    case core::VerticalErrc::times_differ:
      return format_error(FormatErrc::dimension_mismatch, std::move(subject));
  }
  return format_error(FormatErrc::dimension_mismatch, std::move(subject));
}

// Registry components pair as they are; the generic ones of a projected grid
// are declared to be components: they point along the grid's axes.
std::expected<core::VectorSeries, core::VectorErrc> pair_of(
    core::TimeSeries u, core::TimeSeries v) {
  const bool registered =
      std::holds_alternative<core::Quantity>(u.meta().quantity());
  return registered ? core::VectorSeries::make(std::move(u), std::move(v))
                    : core::VectorSeries::assume_components(std::move(u),
                                                            std::move(v));
}

// The derived series of one station. Its input columns are moved into the
// series, so the inputs never exist twice (the axis is copied for the series,
// one station at a time).
std::expected<core::TimeSeries, Error> derive_one(
    DflowDerived d, CrsKind grid, const core::TimeAxis& axis,
    std::vector<Component>& components, std::size_t position) {
  std::vector<core::TimeSeries> series;
  series.reserve(components.size());
  for (Component& c : components) {
    auto s =
        core::TimeSeries::make(axis, std::move(c.columns[position]), c.meta);
    if (not s) {
      return fail(format_error(FormatErrc::dimension_mismatch, c.name));
    }
    series.push_back(*std::move(s));
  }
  const std::string subject = components[0].name + ", " + components[1].name;
  auto vector = pair_of(std::move(series[0]), std::move(series[1]));
  if (not vector) {
    return fail(alignment_error(vector.error(), subject));
  }
  switch (d) {
    case DflowDerived::current_speed_2d:
    case DflowDerived::wind_speed:
      return vector->magnitude();
    case DflowDerived::current_direction_2d:
    case DflowDerived::wind_direction: {
      core::TimeSeries direction = vector->cartesian_direction();
      if (grid == CrsKind::projected) {
        const std::string label =
            "grid-relative " + std::string{direction.meta().label()};
        return std::move(direction).with_label(label);
      }
      return direction;
    }
    case DflowDerived::current_speed_3d: {
      auto speed = core::magnitude3(*vector, series[2]);
      if (not speed) {
        return fail(alignment_error(speed.error(),
                                    subject + ", " + components[2].name));
      }
      return *std::move(speed);
    }
  }
  return fail(format_error(FormatErrc::dimension_mismatch, subject));
}

std::expected<core::Variable, Error> derive(DflowDerived d, CrsKind grid,
                                            const core::TimeAxis& axis,
                                            std::vector<Component>& components,
                                            std::size_t selected) {
  // The meta is that of an empty station's series, so it exists with no
  // station selected.
  std::vector<Component> shapes;
  shapes.reserve(components.size());
  for (const Component& c : components) {
    shapes.push_back({.name = c.name,
                      .meta = c.meta,
                      .columns = {core::Column{}},
                      .nonfinite = 0,
                      .warnings = {}});
  }
  auto meta = derive_one(d, grid, core::TimeAxis{}, shapes, 0);
  if (not meta) {
    return std::unexpected{std::move(meta.error())};
  }
  core::Variable out{.meta = meta->meta(), .per_station = {}};
  out.per_station.reserve(selected);
  for (std::size_t position = 0; position < selected; ++position) {
    auto series = derive_one(d, grid, axis, components, position);
    if (not series) {
      return std::unexpected{std::move(series.error())};
    }
    out.per_station.push_back(std::move(*series).into_parts().samples);
  }
  return out;
}

}  // namespace

// ---- the vocabulary
// ---------------------------------------------------------------

std::string_view to_token(DflowDerived d) noexcept { return row_of(d).token; }

std::optional<DflowDerived> parse_dflow_derived(
    std::string_view token) noexcept {
  const auto it = std::ranges::find(derived_rows, token, &DerivedRow::token);
  if (it == derived_rows.end()) {
    return std::nullopt;
  }
  return it->derived;
}

std::expected<AtLayer, FormatError> AtLayer::make(const Layered& v,
                                                  std::size_t one_based) {
  if (one_based < 1 or one_based > v.layers) {
    return std::unexpected{format_error(FormatErrc::layer_out_of_range,
                                        v.long_name, std::nullopt, one_based)};
  }
  return AtLayer{v.source, one_based - 1};
}

// ---- inspect
// ------------------------------------------------------------------------

namespace {

std::expected<Read<DflowCatalog>, Error> inspect_file(const nc::File& file,
                                                      core::Epsg crs,
                                                      const StopToken& stop) {
  auto structure = structure_of(file);
  if (not structure) {
    return std::unexpected{std::move(structure.error())};
  }
  auto time = detail::clock_of(file, structure->time_var);
  if (not time) {
    return std::unexpected{std::move(time.error())};
  }
  auto listing = list_variables(file, *structure);
  if (not listing) {
    return std::unexpected{std::move(listing.error())};
  }
  auto variables = variables_of(file, listing->variables);
  if (not variables) {
    return std::unexpected{std::move(variables.error())};
  }
  if (auto size = detail::check_station_count(file, structure->stations_dim);
      not size) {
    return std::unexpected{std::move(size.error())};
  }
  const core::StationSelection all =
      core::StationSelection::all(structure->stations_dim.length);
  auto stations = detail::read_stations(file, station_variables(*structure),
                                        crs, all.indices(), stop);
  if (not stations) {
    return std::unexpected{std::move(stations.error())};
  }
  std::vector<Warning> warnings = std::move(time->warnings);
  append(warnings, std::move(stations->warnings));
  append(warnings, std::move(listing->warnings));
  return Read<DflowCatalog>{.value = {.stations = std::move(stations->value),
                                      .variables = *std::move(variables),
                                      .times = structure->time_dim.length,
                                      .time_units = time->value.units(),
                                      .calendar = time->value.calendar()},
                            .warnings = std::move(warnings)};
}

}  // namespace

std::expected<Read<DflowCatalog>, Error> inspect_dflow(
    const std::filesystem::path& path, core::Epsg crs, const ReadContext& ctx) {
  return detail::with_file(path, ctx.limits, [&](const nc::File& file) {
    return inspect_file(file, crs, ctx.stop);
  });
}

// ---- read
// -----------------------------------------------------------------------------

namespace {

// What the request asks for, resolved against nothing yet: the source, and the
// zero-based layer of a layered variable.
struct Target {
  DflowSource source;
  std::optional<std::size_t> layer;
};

Target target_of(const DflowChoice& choice) {
  if (const auto* flat = std::get_if<FlatChoice>(&choice)) {
    return {.source = flat->source, .layer = std::nullopt};
  }
  const auto& at = std::get<AtLayer>(choice);
  return {.source = at.source(), .layer = at.zero_based()};
}

// The variables the target reads, and that they have the shape it asks for:
// flat for FlatChoice, layered with a layer inside the file's layers for
// AtLayer.
std::expected<std::vector<Listed>, Error> resolve(
    std::span<const Listed> listed, const Target& target) {
  std::vector<std::string_view> names;
  if (const auto* name = std::get_if<nc::NcName>(&target.source)) {
    names.push_back(name->view());
  } else {
    const auto inputs = row_of(std::get<DflowDerived>(target.source)).inputs;
    names.assign(inputs.begin(), inputs.end());
  }
  std::vector<Listed> out;
  for (const std::string_view name : names) {
    const Listed* l = find_listed(listed, name);
    if (l == nullptr) {
      return fail(
          format_error(FormatErrc::missing_variable, std::string{name}));
    }
    if (l->layers().has_value() != target.layer.has_value()) {
      return fail(
          format_error(FormatErrc::dimension_mismatch, std::string{name}));
    }
    if (target.layer and *target.layer >= l->shape.layers) {
      return fail(format_error(FormatErrc::layer_out_of_range,
                               std::string{name}, std::nullopt,
                               *target.layer + 1));
    }
    out.push_back(*l);
  }
  return out;
}

// What a read finds out before it reads values: the file's structure and the
// variables the request needs.
struct Setup {
  Structure structure;
  Target target;
  std::vector<Listed> inputs;
};

std::expected<Setup, Error> set_up(const nc::File& file,
                                   const DflowRequest& request) {
  auto structure = structure_of(file);
  if (not structure) {
    return std::unexpected{std::move(structure.error())};
  }
  if (auto ok = detail::require_selection(request.stations,
                                          structure->stations_dim.length);
      not ok) {
    return std::unexpected{std::move(ok.error())};
  }
  auto listing = list_variables(file, *structure);
  if (not listing) {
    return std::unexpected{std::move(listing.error())};
  }
  Target target = target_of(request.choice);
  auto inputs = resolve(listing->variables, target);
  if (not inputs) {
    return std::unexpected{std::move(inputs.error())};
  }
  if (auto size =
          detail::check_result_size(file, inputs->front().info.name.view(),
                                    request.stations.indices().size(),
                                    structure->time_dim.length, inputs->size());
      not size) {
    return std::unexpected{std::move(size.error())};
  }
  return Setup{.structure = *std::move(structure),
               .target = std::move(target),
               .inputs = *std::move(inputs)};
}

// The components of the target, read, and what was not finite in them.
struct Values {
  std::vector<Component> components;
  std::vector<Warning> warnings;
  std::size_t nonfinite{0};
  std::string nonfinite_in;  // the first variable that had one
};

std::expected<Values, Error> read_values(const Setup& setup,
                                         const ComponentSource source) {
  Values out;
  for (const Listed& input : setup.inputs) {
    auto component = read_component(source, input.info);
    if (not component) {
      return std::unexpected{std::move(component.error())};
    }
    if (component->nonfinite > 0 and out.nonfinite == 0) {
      out.nonfinite_in = component->name;
    }
    out.nonfinite += component->nonfinite;
    append(out.warnings, std::move(component->warnings));
    out.components.push_back(*std::move(component));
  }
  return out;
}

// The one column of the table: the variable itself, or the derived series.
std::expected<core::Variable, Error> variable_of(
    const Target& target, CrsKind grid, const core::TimeAxis& axis,
    std::vector<Component>& components, std::size_t selected) {
  if (const auto* derived = std::get_if<DflowDerived>(&target.source)) {
    return derive(*derived, grid, axis, components, selected);
  }
  return core::Variable{.meta = std::move(components[0].meta),
                        .per_station = std::move(components[0].columns)};
}

std::expected<Read<core::StationTable>, Error> read_file(
    const nc::File& file, const DflowRequest& request, const StopToken& stop) {
  auto setup = set_up(file, request);
  if (not setup) {
    return std::unexpected{std::move(setup.error())};
  }
  const std::span<const std::size_t> selection = request.stations.indices();
  const auto grid = crs_kind(request.crs);
  if (not grid) {
    return fail(detail::to_format_error(grid.error(), std::nullopt));
  }
  auto time = detail::clock_of(file, setup->structure.time_var);
  if (not time) {
    return std::unexpected{std::move(time.error())};
  }
  auto axis = detail::read_time_axis(file, setup->structure.time_var,
                                     time->value, stop);
  if (not axis) {
    return std::unexpected{std::move(axis.error())};
  }
  auto stations = detail::read_stations(
      file, station_variables(setup->structure), request.crs, selection, stop);
  if (not stations) {
    return std::unexpected{std::move(stations.error())};
  }
  auto values =
      read_values(*setup, {.file = file,
                           .grid = *grid,
                           .times = setup->structure.time_dim.length,
                           .station_dim = setup->structure.stations_dim.id,
                           .layer = setup->target.layer,
                           .selection = selection,
                           .stop = stop});
  if (not values) {
    return std::unexpected{std::move(values.error())};
  }
  auto variable = variable_of(setup->target, *grid, *axis, values->components,
                              selection.size());
  if (not variable) {
    return std::unexpected{std::move(variable.error())};
  }
  std::vector<Warning> warnings = std::move(time->warnings);
  append(warnings, std::move(values->warnings));
  append_if_counted(warnings, {.code = WarningCode::nonfinite_masked,
                               .subject = std::move(values->nonfinite_in),
                               .count = values->nonfinite});
  std::vector<core::Variable> variables;
  variables.push_back(*std::move(variable));
  return detail::assemble_table(std::move(variables), *std::move(axis),
                                *std::move(stations), std::move(warnings));
}

}  // namespace

std::expected<Read<core::StationTable>, Error> read_dflow(
    const std::filesystem::path& path, const DflowRequest& request,
    const ReadContext& ctx) {
  return detail::with_file(path, ctx.limits, [&](const nc::File& file) {
    return read_file(file, request, ctx.stop);
  });
}

}  // namespace mov::io
