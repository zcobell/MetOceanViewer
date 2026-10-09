// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The v5 station netCDF writer (docs/station-netcdf.md, SN). Everything that
// can be refused is decided first, without touching the disk (plan_write);
// the body of the atomic write then only defines and puts what the plan says.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/detail/utf8.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/version.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "station_netcdf_format.hpp"

namespace mov::io {

namespace {

namespace sn = detail::station_nc;
using core::ColumnIndex;
using core::StationIndex;

std::unexpected<Error> refuse(FormatErrc code, std::string subject) {
  return std::unexpected<Error>{
      FormatError{.code = code, .subject = std::move(subject)}};
}

// ---- the plan
// ------------------------------------------------------------------

/// How one schema column is written.
struct ColumnPlan {
  ColumnIndex column;
  nc::NcName name;
  std::optional<nc::NcName> status;  // engaged iff a sample is Dry
  std::optional<core::Affine> map;   // to the canonical unit, if not identity
  std::optional<std::string> standard_name;
  std::string long_name;
  std::optional<std::string> units;
  std::optional<std::string_view> units_metadata;
  std::optional<std::string_view> datum;
  std::size_t reads_as_fill;  // values written as the _FillValue
};

/// Everything the body writes, decided before the file exists, and what the
/// warnings are made of.
struct Plan {
  StationNcLayout layout;
  std::size_t samples;  // the length of `time` (orthogonal) or `obs`
  std::vector<std::string> ids;
  std::vector<std::string> names;
  std::optional<std::vector<std::string>> providers;
  std::vector<ColumnPlan> columns;
  std::size_t names_substituted;
  std::size_t natives_dropped;
};

/// The writer's warnings, in their documented order, from what the plan says.
std::vector<Warning> warnings_of(const Plan& plan) {
  std::vector<Warning> warnings;
  for (const ColumnPlan& c : plan.columns) {
    if (c.map) {
      warnings.push_back({.code = WarningCode::unit_converted,
                          .subject = std::string{c.name.view()}});
    }
  }
  append_if_counted(warnings, {.code = WarningCode::station_name_substituted,
                               .count = plan.names_substituted});
  append_if_counted(warnings, {.code = WarningCode::native_position_dropped,
                               .count = plan.natives_dropped});
  for (const ColumnPlan& c : plan.columns) {
    append_if_counted(warnings, {.code = WarningCode::value_reads_as_missing,
                                 .subject = std::string{c.name.view()},
                                 .count = c.reads_as_fill});
  }
  return warnings;
}

/// A Unit and how values convert to it.
struct Target {
  std::optional<core::Unit> unit;
  std::optional<core::Affine> map;
};

/// The unit a column is stored in: the canonical unit of a registry quantity
/// (other than `difference`), converted to; anything else as it is.
std::expected<Target, Error> target_unit(const core::SeriesMeta& meta) {
  const auto* registry = std::get_if<core::Quantity>(&meta.quantity());
  const std::optional<core::Unit> canonical =
      registry != nullptr ? core::canonical_unit(*registry) : std::nullopt;
  if (not canonical) {
    return Target{.unit = meta.unit(), .map = std::nullopt};
  }
  const std::string token{core::token(meta.quantity())};
  if (not meta.unit()) {
    return refuse(FormatErrc::noncanonical_unit, token);
  }
  const auto conversion = core::conversion(*meta.unit(), *canonical);
  if (not conversion) {
    return refuse(FormatErrc::noncanonical_unit, token);
  }
  return Target{.unit = canonical,
                .map = *conversion == core::Affine{}
                           ? std::nullopt
                           : std::optional{*conversion}};
}

/// The names of a column: its token and `<token>_status`, both reserved for
/// it whether or not it has Dry samples, so whether a table can be written
/// depends on its schema only. A generic token may not be a name the format
/// uses itself.
struct ColumnNames {
  nc::NcName name;
  nc::NcName status;
};

std::expected<ColumnNames, Error> column_names(const core::QuantityId& q) {
  const std::string_view token = core::token(q);
  auto name = nc::NcName::make(token);
  auto status =
      nc::NcName::make(std::string{token} + std::string{sn::status_suffix});
  if (not sn::writable_token(token, {}) or not name or not status) {
    return refuse(FormatErrc::invalid_variable_name, std::string{token});
  }
  return ColumnNames{.name = *std::move(name), .status = *std::move(status)};
}

/// The number written for a sample: the value in the stored unit, or the
/// _FillValue for Missing and Dry (and, as core::convert makes it Missing, for
/// a value whose conversion is not finite).
double stored(const core::Sample& s, const std::optional<core::Affine>& map) {
  const std::optional<double> x = s.value();
  if (not x) {
    return sn::fill_double;
  }
  if (not map) {
    return *x;
  }
  return core::finite_or_missing((*map)(*x)).value().value_or(sn::fill_double);
}

/// What a pass over a column's samples finds.
struct ColumnFacts {
  bool any_dry{false};
  std::size_t reads_as_fill{0};
};

ColumnFacts facts_of(const core::StationTable& table, ColumnIndex k,
                     const std::optional<core::Affine>& map) {
  ColumnFacts facts;
  for (const StationIndex i : table.stations()) {
    for (const core::Sample& s : table.column(i, k)) {
      facts.any_dry = facts.any_dry or s.is_dry();
      if (s.is_value() and stored(s, map) == sn::fill_double) {
        ++facts.reads_as_fill;
      }
    }
  }
  return facts;
}

/// CF 1.11 3.1.2: a temperature says whether it is on the scale, a
/// difference, or (a generic quantity, which the format knows nothing about)
/// unknown.
std::optional<std::string_view> units_metadata(const Target& target,
                                               const core::QuantityId& q) {
  if (not target.unit or not core::is_temperature(*target.unit)) {
    return std::nullopt;
  }
  if (std::holds_alternative<core::GenericQuantity>(q)) {
    return "temperature: unknown";
  }
  return q == core::QuantityId{core::Quantity::difference}
             ? "temperature: difference"
             : "temperature: on_scale";
}

std::string long_name_of(const core::SeriesMeta& meta) {
  if (not meta.label().empty()) {
    return std::string{meta.label()};
  }
  const auto* registry = std::get_if<core::Quantity>(&meta.quantity());
  return std::string{registry != nullptr ? core::info(*registry).long_name
                                         : core::token(meta.quantity())};
}

std::optional<std::string> standard_name_of(const core::QuantityId& q) {
  const auto* registry = std::get_if<core::Quantity>(&q);
  const auto* generic = std::get_if<core::GenericQuantity>(&q);
  const std::string_view name = registry != nullptr
                                    ? core::info(*registry).standard_name
                                : generic != nullptr ? generic->standard_name()
                                                     : std::string_view{};
  return name.empty() ? std::nullopt : std::optional{std::string{name}};
}

std::optional<std::string> units_text(const std::optional<core::Unit>& unit) {
  if (not unit or core::udunits(*unit).empty()) {
    return std::nullopt;  // an OtherUnit with no symbol says nothing
  }
  return std::string{core::udunits(*unit)};
}

std::expected<ColumnPlan, Error> plan_column(const core::StationTable& table,
                                             ColumnIndex k) {
  const core::SeriesMeta& meta = table.schema()[k.value()];
  auto parts = collect([&] { return column_names(meta.quantity()); },
                       [&] { return target_unit(meta); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  auto& [names, target] = *parts;
  const ColumnFacts facts = facts_of(table, k, target.map);
  return ColumnPlan{
      .column = k,
      .name = std::move(names.name),
      .status =
          facts.any_dry ? std::optional{std::move(names.status)} : std::nullopt,
      .map = target.map,
      .standard_name = standard_name_of(meta.quantity()),
      .long_name = long_name_of(meta),
      .units = units_text(target.unit),
      .units_metadata = units_metadata(target, meta.quantity()),
      .datum = meta.datum() ? std::optional{core::to_string(*meta.datum())}
                            : std::nullopt,
      .reads_as_fill = facts.reads_as_fill};
}

/// Every column must be writable next to the others (writable_token): not a
/// name the format uses, and no column named like another column's status
/// variable, which every column reserves (whether or not it is written).
std::expected<void, Error> check_tokens(const core::StationTable& table) {
  sn::TokenSet taken;
  for (const core::SeriesMeta& meta : table.schema()) {
    const std::string token{core::token(meta.quantity())};
    if (not sn::writable_token(token, taken)) {
      return refuse(FormatErrc::invalid_variable_name, token);
    }
    taken.insert(token);
  }
  return {};
}

/// The station texts; an empty name becomes "Station <id>".
void plan_stations(const core::StationTable& table, Plan& plan) {
  bool any_source = false;
  std::vector<std::string> providers;
  for (const StationIndex i : table.stations()) {
    const core::FileStation& s = table.station(i);
    plan.ids.emplace_back(s.id.view());
    if (s.name.empty()) {
      plan.names.push_back(std::format("Station {}", s.id.view()));
      ++plan.names_substituted;
    } else {
      plan.names.emplace_back(s.name.view());
    }
    plan.natives_dropped += s.native ? std::size_t{1} : std::size_t{0};
    any_source = any_source or s.source.has_value();
    providers.emplace_back(s.source ? core::to_token(*s.source) : "");
  }
  if (any_source) {
    plan.providers = std::move(providers);
  }
}

/// The length of the sample dimension; every station within the limit.
std::expected<std::size_t, Error> sample_length(
    const core::StationTable& table, StationNcLayout layout,
    const detail::StationNcWriteLimits& limits) {
  std::size_t longest = 0;
  for (const StationIndex i : table.stations()) {
    const std::size_t n = table.times(i).size();
    if (n > limits.max_station_samples) {
      return refuse(FormatErrc::too_many_samples,
                    std::string{table.station(i).id.view()});
    }
    longest = std::max(longest, n);
  }
  return layout == StationNcLayout::orthogonal
             ? table.times(StationIndex{0}).size()
             : longest;
}

/// An option text the format can store: UTF-8 without NUL, bounded.
std::expected<void, Error> check_option(std::string_view attribute,
                                        std::string_view text) {
  if (text.size() > StationNcWriteOptions::max_option_bytes or
      text.find('\0') != std::string_view::npos or
      not core::detail::is_valid_utf8(text)) {
    return refuse(FormatErrc::bad_option, ":" + std::string{attribute});
  }
  return {};
}

std::expected<void, Error> check_options(const StationNcWriteOptions& o) {
  if (o.title.empty()) {
    return refuse(FormatErrc::bad_option, ":title");
  }
  using Optional =
      std::pair<std::string_view, const std::optional<std::string>*>;
  const std::array<Optional, 4> optional{{{"institution", &o.institution},
                                          {"source", &o.source},
                                          {"references", &o.references},
                                          {"comment", &o.comment}}};
  auto done = check_option("title", o.title);
  for (const auto& [attribute, text] : optional) {
    if (done and *text) {
      done = check_option(attribute, text->value_or(""));
    }
  }
  return done;
}

std::expected<void, Error> check_collection(const core::StationTable& table) {
  if (table.size() == 0) {
    return refuse(FormatErrc::empty_collection, "station");
  }
  if (table.schema().empty()) {
    return refuse(FormatErrc::no_data_variables, "");
  }
  if (table.total_samples() == 0) {
    return refuse(FormatErrc::no_samples, "");
  }
  return {};
}

/// The table-wide checks, in order; the length of the sample dimension.
std::expected<std::size_t, Error> check_table(
    const core::StationTable& table, const StationNcWriteOptions& options,
    StationNcLayout layout, const detail::StationNcWriteLimits& limits) {
  return check_collection(table)
      .and_then([&] { return check_options(options); })
      .and_then([&] { return check_tokens(table); })
      .and_then([&] { return sample_length(table, layout, limits); });
}

std::expected<Plan, Error> plan_write(
    const core::StationTable& table, const StationNcWriteOptions& options,
    const detail::StationNcWriteLimits& limits) {
  const StationNcLayout layout = choose_layout(table);
  auto samples = check_table(table, options, layout, limits);
  if (not samples) {
    return std::unexpected{std::move(samples).error()};
  }
  Plan plan{.layout = layout,
            .samples = *samples,
            .ids = {},
            .names = {},
            .providers = std::nullopt,
            .columns = {},
            .names_substituted = 0,
            .natives_dropped = 0};
  plan.columns.reserve(table.schema().size());
  for (std::size_t k = 0; k < table.schema().size(); ++k) {
    auto column = plan_column(table, ColumnIndex{k});
    if (not column) {
      return std::unexpected{std::move(column).error()};
    }
    plan.columns.push_back(*std::move(column));
  }
  plan_stations(table, plan);
  return plan;
}

// ---- the body
// -------------------------------------------------------------------

struct TextAtt {
  nc::NcNameRef name;
  std::string_view value;
};

std::expected<void, Error> put_texts(nc::NewFile& file, nc::AttTarget on,
                                     std::span<const TextAtt> atts) {
  for (const TextAtt& att : atts) {
    if (auto done = file.put_att(on, att.name, att.value); not done) {
      return std::unexpected{std::move(done.error())};
    }
  }
  return {};
}

/// Calls each step in order and stops at the first error.
template <class Step, class... Rest>
std::expected<void, Error> in_order(Step&& step, Rest&&... rest) {
  std::expected<void, Error> done = std::forward<Step>(step)();
  if constexpr (sizeof...(Rest) > 0) {
    if (done) {
      return in_order(std::forward<Rest>(rest)...);
    }
  }
  return done;
}

template <class T>
std::expected<void, Error> as_io_error(std::expected<T, NcError> r) {
  if (not r) {
    return std::unexpected{Error{std::move(r.error())}};
  }
  return {};
}

std::size_t longest(std::span<const std::string> texts) {
  std::size_t n = 1;  // a dimension cannot be 0 long
  for (const std::string& t : texts) {
    n = std::max(n, t.size());
  }
  return n;
}

/// The dimensions of the file, in SN 9's order.
struct Dims {
  nc::DimInfo station;
  nc::DimInfo id_len;
  nc::DimInfo name_len;
  std::optional<nc::DimInfo> provider_len;
  nc::DimInfo sample;  // `time` or `obs`
};

std::expected<Dims, Error> define_dims(nc::NewFile& file, const Plan& plan) {
  const auto dim = [&file](nc::NcNameRef name, std::size_t length) {
    return file.define_dim(name, length).transform_error(lift<Error>);
  };
  // netCDF numbers the dimensions in definition order, so the order is SN 9's.
  auto dims = collect(
      [&] { return dim(sn::station_dim, plan.ids.size()); },
      [&] { return dim(sn::id_len_dim, longest(plan.ids)); },
      [&] { return dim(sn::name_len_dim, longest(plan.names)); },
      [&]() -> std::expected<std::optional<nc::DimInfo>, Error> {
        if (not plan.providers) {
          return std::nullopt;
        }
        return dim(sn::provider_len_dim, longest(*plan.providers));
      },
      [&] {
        return dim(plan.layout == StationNcLayout::orthogonal ? sn::time_dim
                                                              : sn::obs_dim,
                   plan.samples);
      });
  if (not dims) {
    return std::unexpected{std::move(dims).error()};
  }
  auto& [station, id_len, name_len, provider_len, sample] = *dims;
  return Dims{.station = std::move(station),
              .id_len = std::move(id_len),
              .name_len = std::move(name_len),
              .provider_len = std::move(provider_len),
              .sample = std::move(sample)};
}

std::expected<void, Error> define_text_var(nc::NewFile& file,
                                           nc::NcNameRef name,
                                           const nc::DimInfo& station,
                                           const nc::DimInfo& length,
                                           std::span<const TextAtt> atts) {
  const std::array<nc::DimInfo, 2> dims{station, length};
  return as_io_error(file.define_char_var(name, dims)).and_then([&] {
    return put_texts(file, name, atts);
  });
}

std::expected<void, Error> define_station_vars(nc::NewFile& file,
                                               const Dims& d) {
  const std::array<TextAtt, 4> id{
      {{.name = "long_name", .value = "station identifier"},
       {.name = "standard_name", .value = "platform_id"},
       {.name = "cf_role", .value = "timeseries_id"},
       {.name = "_Encoding", .value = "utf-8"}}};
  const std::array<TextAtt, 3> name{
      {{.name = "long_name", .value = "station name"},
       {.name = "standard_name", .value = "platform_name"},
       {.name = "_Encoding", .value = "utf-8"}}};
  const std::array<TextAtt, 2> provider{
      {{.name = "long_name", .value = "data provider"},
       {.name = "_Encoding", .value = "utf-8"}}};
  return in_order(
      [&] {
        return define_text_var(file, sn::station_id, d.station, d.id_len, id);
      },
      [&] {
        return define_text_var(file, sn::station_name, d.station, d.name_len,
                               name);
      },
      [&]() -> std::expected<void, Error> {
        if (not d.provider_len) {
          return {};
        }
        return define_text_var(file, sn::station_provider, d.station,
                               *d.provider_len, provider);
      });
}

std::expected<void, Error> define_coordinate(nc::NewFile& file,
                                             nc::NcNameRef name,
                                             const nc::DimInfo& station,
                                             std::span<const TextAtt> atts) {
  const std::array<nc::DimInfo, 1> dims{station};
  return as_io_error(file.define_var<double>(name, dims, {})).and_then([&] {
    return put_texts(file, name, atts);
  });
}

std::expected<void, Error> define_crs(nc::NewFile& file) {
  const auto number = [&file](nc::NcNameRef att, double value) {
    return [&file, att, value] {
      return as_io_error(
          file.put_att(sn::crs, att, std::array<double, 1>{value}));
    };
  };
  const std::array<TextAtt, 1> mapping{
      {{.name = "grid_mapping_name", .value = "latitude_longitude"}}};
  const std::array<TextAtt, 2> wkt{
      {{.name = "crs_wkt", .value = sn::wgs84_wkt},
       {.name = "epsg_code", .value = sn::epsg_4326}}};
  return in_order(
      [&] {
        return as_io_error(file.define_var<std::int32_t>(sn::crs, {}, {}));
      },
      [&] { return put_texts(file, sn::crs, mapping); },
      number("longitude_of_prime_meridian", 0.0),
      number("semi_major_axis", sn::wgs84_semi_major_axis),
      number("inverse_flattening", sn::wgs84_inverse_flattening),
      [&] { return put_texts(file, sn::crs, wkt); });
}

/// (r, c): c = min(n_cols, 65536), r = clamp(65536 / c, 1, n_station).
std::vector<std::size_t> sample_chunks(const Dims& d) {
  const std::size_t c = std::min(d.sample.length, sn::chunk_elements);
  const std::size_t r =
      std::clamp<std::size_t>(sn::chunk_elements / c, 1, d.station.length);
  return {r, c};
}

std::expected<void, Error> define_time(nc::NewFile& file, const Plan& plan,
                                       const Dims& d) {
  const std::array<TextAtt, 5> atts{
      {{.name = "standard_name", .value = "time"},
       {.name = "long_name", .value = "time"},
       {.name = "units", .value = sn::time_units},
       {.name = "calendar", .value = sn::calendar},
       {.name = "axis", .value = "T"}}};
  if (plan.layout == StationNcLayout::orthogonal) {
    const std::array<nc::DimInfo, 1> dims{d.sample};
    return as_io_error(file.define_var<double>(sn::time, dims, {}))
        .and_then([&] { return put_texts(file, sn::time, atts); });
  }
  const std::array<nc::DimInfo, 2> dims{d.station, d.sample};
  const std::array<TextAtt, 1> count{
      {{.name = "long_name",
        .value = "number of valid samples in this station time series"}}};
  const std::array<nc::DimInfo, 1> station{d.station};
  return in_order(
      [&] {
        return as_io_error(
            file.define_var<double>(sn::time, dims,
                                    {.fill = sn::fill_double,
                                     .deflate_level = sn::deflate_level,
                                     .chunks = sample_chunks(d)}));
      },
      [&] { return put_texts(file, sn::time, atts); },
      [&] {
        return as_io_error(
            file.define_var<std::int32_t>(sn::obs_count, station, {}));
      },
      [&] { return put_texts(file, sn::obs_count, count); });
}

std::expected<void, Error> define_status(nc::NewFile& file, const ColumnPlan& c,
                                         const nc::NcName& name,
                                         const Dims& d) {
  const std::array<nc::DimInfo, 2> dims{d.station, d.sample};
  const std::string long_name = c.long_name + " wet/dry status";
  const std::array<TextAtt, 2> names{
      {{.name = "standard_name", .value = "status_flag"},
       {.name = "long_name", .value = long_name}}};
  const std::array<TextAtt, 1> meanings{
      {{.name = "flag_meanings", .value = sn::flag_meanings}}};
  const std::array<std::int8_t, 2> flags{sn::status_dry, sn::status_wet};
  return in_order(
      [&] {
        return as_io_error(
            file.define_var<std::int8_t>(name, dims,
                                         {.fill = sn::fill_status,
                                          .deflate_level = sn::deflate_level,
                                          .chunks = sample_chunks(d)}));
      },
      [&] { return put_texts(file, name, names); },
      [&] { return as_io_error(file.put_att(name, "flag_values", flags)); },
      [&] { return put_texts(file, name, meanings); },
      [&] { return as_io_error(file.put_att(name, "valid_range", flags)); });
}

std::expected<void, Error> define_column(nc::NewFile& file, const ColumnPlan& c,
                                         const Dims& d) {
  const std::array<nc::DimInfo, 2> dims{d.station, d.sample};
  std::vector<TextAtt> atts{{.name = "coordinates", .value = sn::coordinates},
                            {.name = "grid_mapping", .value = "crs"}};
  if (c.standard_name) {
    atts.push_back({.name = "standard_name", .value = *c.standard_name});
  }
  atts.push_back({.name = "long_name", .value = c.long_name});
  if (c.units) {
    atts.push_back({.name = "units", .value = *c.units});
  }
  if (c.units_metadata) {
    atts.push_back({.name = "units_metadata", .value = *c.units_metadata});
  }
  if (c.datum) {
    atts.push_back({.name = "vertical_datum", .value = *c.datum});
  }
  if (c.status) {
    atts.push_back({.name = "ancillary_variables", .value = c.status->view()});
  }
  return in_order(
      [&] {
        return as_io_error(
            file.define_var<double>(c.name, dims,
                                    {.fill = sn::fill_double,
                                     .deflate_level = sn::deflate_level,
                                     .chunks = sample_chunks(d)}));
      },
      [&] { return put_texts(file, c.name, atts); },
      [&]() -> std::expected<void, Error> {
        if (not c.status) {
          return {};
        }
        return define_status(file, c, *c.status, d);
      });
}

std::expected<void, Error> define_globals(nc::NewFile& file,
                                          const StationNcWriteOptions& options,
                                          std::chrono::sys_seconds now) {
  const std::string created = std::format("{:%FT%TZ}", now);
  const std::string version =
      std::format("{}.{}", station_nc_version.major, station_nc_version.minor);
  const std::string history =
      std::format("{}: created by MetOceanViewer {} ({} {})", created,
                  core::version(), station_nc_format, version);
  std::vector<TextAtt> atts{{.name = "Conventions", .value = sn::conventions},
                            {.name = "featureType", .value = sn::feature_type},
                            {.name = "title", .value = options.title}};
  const auto optional = [&atts](nc::NcNameRef name,
                                const std::optional<std::string>& value) {
    if (value and not value->empty()) {  // empty is absent
      atts.push_back({.name = name, .value = *value});
    }
  };
  optional("institution", options.institution);
  optional("source", options.source);
  optional("references", options.references);
  optional("comment", options.comment);
  atts.push_back({.name = "history", .value = history});
  atts.push_back({.name = "date_created", .value = created});
  atts.push_back({.name = "metoceanviewer_format", .value = station_nc_format});
  atts.push_back({.name = "metoceanviewer_format_version", .value = version});
  return put_texts(file, nc::global, atts);
}

std::expected<void, Error> define_all(nc::NewFile& file, const Plan& plan,
                                      const Dims& d,
                                      const StationNcWriteOptions& options,
                                      std::chrono::sys_seconds now) {
  const std::array<TextAtt, 4> lat{
      {{.name = "standard_name", .value = "latitude"},
       {.name = "long_name", .value = "station latitude"},
       {.name = "units", .value = "degrees_north"},
       {.name = "axis", .value = "Y"}}};
  const std::array<TextAtt, 4> lon{
      {{.name = "standard_name", .value = "longitude"},
       {.name = "long_name", .value = "station longitude"},
       {.name = "units", .value = "degrees_east"},
       {.name = "axis", .value = "X"}}};
  auto done =
      in_order([&] { return define_station_vars(file, d); },
               [&] { return define_coordinate(file, sn::lat, d.station, lat); },
               [&] { return define_coordinate(file, sn::lon, d.station, lon); },
               [&] { return define_crs(file); },
               [&] { return define_time(file, plan, d); });
  for (const ColumnPlan& c : plan.columns) {
    done = done.and_then([&] { return define_column(file, c, d); });
  }
  return done.and_then([&] { return define_globals(file, options, now); })
      .and_then([&] { return as_io_error(file.end_define()); });
}

// ---- the data
// -----------------------------------------------------------------------

nc::Slab row_slab(std::size_t station, std::size_t n) {
  return {{.start = station, .count = 1}, {.start = 0, .count = n}};
}

std::vector<double> time_values(std::span<const core::Time> times) {
  std::vector<double> out(times.size());
  std::ranges::transform(times, out.begin(), [](core::Time t) {
    return static_cast<double>(t.time_since_epoch().count());
  });
  return out;
}

std::expected<void, Error> put_stations(nc::NewFile& file, const Plan& plan,
                                        const core::StationTable& table) {
  std::vector<double> lats;
  std::vector<double> lons;
  for (const StationIndex i : table.stations()) {
    lats.push_back(table.station(i).location.lat());
    lons.push_back(table.station(i).location.lon());
  }
  const nc::Slab all{{.start = 0, .count = table.size()}};
  return in_order(
      [&] { return as_io_error(file.put_char_rows(sn::station_id, plan.ids)); },
      [&] {
        return as_io_error(file.put_char_rows(sn::station_name, plan.names));
      },
      [&]() -> std::expected<void, Error> {
        if (not plan.providers) {
          return {};
        }
        return as_io_error(
            file.put_char_rows(sn::station_provider, *plan.providers));
      },
      [&] { return as_io_error(file.put(sn::lat, lats, all)); },
      [&] { return as_io_error(file.put(sn::lon, lons, all)); });
}

std::expected<void, Error> put_times(nc::NewFile& file, const Plan& plan,
                                     const core::StationTable& table) {
  if (plan.layout == StationNcLayout::orthogonal) {
    const auto times = time_values(table.times(StationIndex{0}));
    return as_io_error(
        file.put(sn::time, times, {{.start = 0, .count = times.size()}}));
  }
  std::vector<std::int32_t> counts;
  for (const StationIndex i : table.stations()) {
    const std::span<const core::Time> times = table.times(i);
    counts.push_back(static_cast<std::int32_t>(times.size()));
    if (times.empty()) {
      continue;  // the padding is left unwritten: it reads as fill
    }
    if (auto done = as_io_error(file.put(sn::time, time_values(times),
                                         row_slab(i.value(), times.size())));
        not done) {
      return done;
    }
  }
  return as_io_error(
      file.put(sn::obs_count, counts, {{.start = 0, .count = counts.size()}}));
}

std::int8_t status_of(const core::Sample& s) {
  if (s.is_dry()) {
    return sn::status_dry;
  }
  return s.is_value() ? sn::status_wet : sn::fill_status;
}

std::expected<void, Error> put_column(nc::NewFile& file, const ColumnPlan& c,
                                      const core::StationTable& table) {
  for (const StationIndex i : table.stations()) {
    const std::span<const core::Sample> samples = table.column(i, c.column);
    if (samples.empty()) {
      continue;
    }
    std::vector<double> values(samples.size());
    std::ranges::transform(
        samples, values.begin(),
        [&c](const core::Sample& s) { return stored(s, c.map); });
    const nc::Slab slab = row_slab(i.value(), samples.size());
    if (auto done = as_io_error(file.put(c.name, values, slab)); not done) {
      return done;
    }
    if (not c.status) {
      continue;
    }
    std::vector<std::int8_t> flags(samples.size());
    std::ranges::transform(samples, flags.begin(), status_of);
    if (auto done = as_io_error(file.put(*c.status, flags, slab)); not done) {
      return done;
    }
  }
  return {};
}

std::expected<void, Error> write_body(nc::NewFile& file, const Plan& plan,
                                      const core::StationTable& table,
                                      const StationNcWriteOptions& options,
                                      std::chrono::sys_seconds now) {
  auto done = define_dims(file, plan)
                  .and_then([&](const Dims& d) {
                    return define_all(file, plan, d, options, now);
                  })
                  .and_then([&] { return put_stations(file, plan, table); })
                  .and_then([&] { return put_times(file, plan, table); });
  for (const ColumnPlan& c : plan.columns) {
    done = done.and_then([&] { return put_column(file, c, table); });
  }
  return done;
}

}  // namespace

namespace detail {

std::expected<std::vector<Warning>, Error> validate_station_netcdf(
    const core::StationTable& table, const StationNcWriteOptions& options,
    const StationNcWriteLimits& limits) {
  return plan_write(table, options, limits).transform(warnings_of);
}

std::expected<std::vector<Warning>, Error> write_station_netcdf(
    const std::filesystem::path& path, const core::StationTable& table,
    const StationNcWriteOptions& options, std::chrono::sys_seconds now,
    const FaultInjector& fault) {
  auto plan = plan_write(table, options, {});
  if (not plan) {
    return std::unexpected{std::move(plan).error()};
  }
  auto written = nc::detail::write_netcdf_atomic_impl(
      path, ReadLimits{},
      [&](nc::NewFile& file) {
        return write_body(file, *plan, table, options, now);
      },
      fault);
  if (not written) {
    return std::unexpected{std::move(written).error()};
  }
  return warnings_of(*plan);
}

}  // namespace detail

std::expected<std::vector<Warning>, Error> validate_station_netcdf(
    const core::StationTable& table, const StationNcWriteOptions& options) {
  return detail::validate_station_netcdf(table, options, {});
}

std::expected<std::vector<Warning>, Error> write_station_netcdf(
    const std::filesystem::path& path, const core::StationTable& table,
    const StationNcWriteOptions& options, std::chrono::sys_seconds now) {
  return detail::write_station_netcdf(path, table, options, now, {});
}

}  // namespace mov::io
