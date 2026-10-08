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
  std::string standard_name;         // empty: none
  std::string long_name;
  std::optional<std::string> units;
  std::optional<std::string_view> units_metadata;
  std::optional<std::string_view> datum;
};

/// Everything the body writes, decided before the file exists.
struct Plan {
  StationNcLayout layout;
  std::size_t samples;  // the length of `time` (orthogonal) or `obs`
  std::vector<std::string> ids;
  std::vector<std::string> names;
  std::optional<std::vector<std::string>> providers;
  std::vector<ColumnPlan> columns;
};

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

bool is_reserved(std::string_view token) {
  return std::ranges::find(sn::reserved_names, token) !=
         sn::reserved_names.end();
}

/// The variable name of a column: its token, which a generic quantity may
/// not take from the format.
std::expected<nc::NcName, Error> variable_name(const core::QuantityId& q) {
  const std::string_view token = core::token(q);
  auto name = nc::NcName::make(token);
  if (is_reserved(token) or not name) {
    return refuse(FormatErrc::invalid_variable_name, std::string{token});
  }
  return *std::move(name);
}

/// What a pass over a column's samples finds.
struct ColumnFacts {
  bool any_dry{false};
  std::size_t reads_as_fill{0};  // values written as the _FillValue
};

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

/// CF 1.11 3.1.2: temperatures say whether they are on the scale or a
/// difference.
std::optional<std::string_view> units_metadata(const Target& target,
                                               const core::QuantityId& q) {
  if (not target.unit or not core::is_temperature(*target.unit)) {
    return std::nullopt;
  }
  const bool difference = q == core::QuantityId{core::Quantity::difference};
  return difference ? "temperature: difference" : "temperature: on_scale";
}

std::string long_name_of(const core::SeriesMeta& meta) {
  if (not meta.label().empty()) {
    return std::string{meta.label()};
  }
  const auto* registry = std::get_if<core::Quantity>(&meta.quantity());
  return std::string{registry != nullptr ? core::info(*registry).long_name
                                         : core::token(meta.quantity())};
}

std::string standard_name_of(const core::QuantityId& q) {
  if (const auto* registry = std::get_if<core::Quantity>(&q)) {
    return std::string{core::info(*registry).standard_name};
  }
  return std::string{std::get<core::GenericQuantity>(q).standard_name()};
}

std::expected<ColumnPlan, Error> plan_column(const core::StationTable& table,
                                             ColumnIndex k,
                                             std::vector<Warning>& warnings) {
  const core::SeriesMeta& meta = table.schema()[k.value()];
  auto name = variable_name(meta.quantity());
  if (not name) {
    return std::unexpected{std::move(name.error())};
  }
  auto target = target_unit(meta);
  if (not target) {
    return std::unexpected{std::move(target.error())};
  }
  const ColumnFacts facts = facts_of(table, k, target->map);
  ColumnPlan plan{
      .column = k,
      .name = *std::move(name),
      .status = std::nullopt,
      .map = target->map,
      .standard_name = standard_name_of(meta.quantity()),
      .long_name = long_name_of(meta),
      .units = target->unit
                   ? std::optional{std::string{core::udunits(*target->unit)}}
                   : std::nullopt,
      .units_metadata = std::nullopt,
      .datum = meta.datum() ? std::optional{core::to_string(*meta.datum())}
                            : std::nullopt};
  plan.units_metadata = units_metadata(*target, meta.quantity());
  if (plan.units and plan.units->empty()) {
    plan.units.reset();  // an OtherUnit with no symbol says nothing
  }
  if (facts.any_dry) {
    auto status = nc::NcName::make(std::string{plan.name.view()} +
                                   std::string{sn::status_suffix});
    if (not status) {
      return refuse(FormatErrc::invalid_variable_name,
                    std::string{plan.name.view()});
    }
    plan.status = *std::move(status);
  }
  if (target->map) {
    warnings.push_back({.code = WarningCode::unit_converted,
                        .subject = std::string{plan.name.view()}});
  }
  append_if_counted(warnings, {.code = WarningCode::value_reads_as_missing,
                               .subject = std::string{plan.name.view()},
                               .count = facts.reads_as_fill});
  return plan;
}

/// No column may be named like another column's status variable.
std::expected<void, Error> check_status_names(
    std::span<const ColumnPlan> columns) {
  for (const ColumnPlan& c : columns) {
    if (not c.status) {
      continue;
    }
    const auto clash = std::ranges::find_if(columns, [&](const ColumnPlan& d) {
      return d.name == c.status->view();
    });
    if (clash != columns.end()) {
      return refuse(FormatErrc::invalid_variable_name,
                    std::string{clash->name.view()});
    }
  }
  return {};
}

/// The station texts; an empty name becomes "Station <id>".
void plan_stations(const core::StationTable& table, Plan& plan,
                   std::vector<Warning>& warnings) {
  std::size_t substituted = 0;
  std::size_t native = 0;
  bool any_source = false;
  std::vector<std::string> providers;
  for (const StationIndex i : table.stations()) {
    const core::FileStation& s = table.station(i);
    plan.ids.emplace_back(s.id.view());
    if (s.name.empty()) {
      plan.names.push_back(std::format("Station {}", s.id.view()));
      ++substituted;
    } else {
      plan.names.emplace_back(s.name.view());
    }
    native += s.native ? std::size_t{1} : std::size_t{0};
    any_source = any_source or s.source.has_value();
    providers.emplace_back(s.source ? core::to_token(*s.source) : "");
  }
  if (any_source) {
    plan.providers = std::move(providers);
  }
  append_if_counted(warnings, {.code = WarningCode::station_name_substituted,
                               .count = substituted});
  append_if_counted(warnings, {.code = WarningCode::native_position_dropped,
                               .count = native});
}

std::size_t sample_length(const core::StationTable& table,
                          StationNcLayout layout) {
  if (layout == StationNcLayout::orthogonal) {
    return table.times(StationIndex{0}).size();
  }
  std::size_t longest = 0;
  for (const StationIndex i : table.stations()) {
    longest = std::max(longest, table.times(i).size());
  }
  return longest;
}

std::expected<Read<Plan>, Error> plan_write(const core::StationTable& table) {
  if (table.size() == 0) {
    return refuse(FormatErrc::empty_collection, "station");
  }
  if (table.schema().empty() or table.total_samples() == 0) {
    return refuse(FormatErrc::empty_collection,
                  table.schema().empty() ? "schema" : "samples");
  }
  const StationNcLayout layout = choose_layout(table);
  Read<Plan> plan{.value = {.layout = layout,
                            .samples = sample_length(table, layout),
                            .ids = {},
                            .names = {},
                            .providers = std::nullopt,
                            .columns = {}},
                  .warnings = {}};
  std::vector<Warning> later;  // value_reads_as_missing comes last
  for (std::size_t k = 0; k < table.schema().size(); ++k) {
    std::vector<Warning> column_warnings;
    auto column = plan_column(table, ColumnIndex{k}, column_warnings);
    if (not column) {
      return std::unexpected{std::move(column.error())};
    }
    for (Warning& w : column_warnings) {
      (w.code == WarningCode::unit_converted ? plan.warnings : later)
          .push_back(std::move(w));
    }
    plan.value.columns.push_back(*std::move(column));
  }
  if (auto names = check_status_names(plan.value.columns); not names) {
    return std::unexpected{std::move(names.error())};
  }
  plan_stations(table, plan.value, plan.warnings);
  plan.warnings.insert(plan.warnings.end(), later.begin(), later.end());
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
std::expected<void, Error> lifted(std::expected<T, NcError> r) {
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
  auto station = dim(sn::station_dim, plan.ids.size());
  auto id_len = station.and_then(
      [&](const auto&) { return dim(sn::id_len_dim, longest(plan.ids)); });
  auto name_len = id_len.and_then(
      [&](const auto&) { return dim(sn::name_len_dim, longest(plan.names)); });
  if (not name_len) {
    return std::unexpected{std::move(name_len.error())};
  }
  std::optional<nc::DimInfo> provider_len;
  if (plan.providers) {
    auto d = dim(sn::provider_len_dim, longest(*plan.providers));
    if (not d) {
      return std::unexpected{std::move(d.error())};
    }
    provider_len = *std::move(d);
  }
  auto sample = dim(
      plan.layout == StationNcLayout::orthogonal ? sn::time_dim : sn::obs_dim,
      plan.samples);
  if (not sample) {
    return std::unexpected{std::move(sample.error())};
  }
  return Dims{.station = *std::move(station),
              .id_len = *std::move(id_len),
              .name_len = *std::move(name_len),
              .provider_len = std::move(provider_len),
              .sample = *std::move(sample)};
}

std::expected<void, Error> define_text_var(nc::NewFile& file,
                                           nc::NcNameRef name,
                                           const nc::DimInfo& station,
                                           const nc::DimInfo& length,
                                           std::span<const TextAtt> atts) {
  const std::array<nc::DimInfo, 2> dims{station, length};
  return lifted(file.define_char_var(name, dims)).and_then([&] {
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
  return lifted(file.define_var<double>(name, dims, {})).and_then([&] {
    return put_texts(file, name, atts);
  });
}

std::expected<void, Error> define_crs(nc::NewFile& file) {
  const auto number = [&file](nc::NcNameRef att, double value) {
    return [&file, att, value] {
      return lifted(file.put_att(sn::crs, att, std::array<double, 1>{value}));
    };
  };
  const std::array<TextAtt, 1> mapping{
      {{.name = "grid_mapping_name", .value = "latitude_longitude"}}};
  const std::array<TextAtt, 2> wkt{
      {{.name = "crs_wkt", .value = sn::wgs84_wkt},
       {.name = "epsg_code", .value = sn::epsg_4326}}};
  return in_order(
      [&] { return lifted(file.define_var<std::int32_t>(sn::crs, {}, {})); },
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
    return lifted(file.define_var<double>(sn::time, dims, {})).and_then([&] {
      return put_texts(file, sn::time, atts);
    });
  }
  const std::array<nc::DimInfo, 2> dims{d.station, d.sample};
  const std::array<TextAtt, 1> count{
      {{.name = "long_name",
        .value = "number of valid samples in this station time series"}}};
  const std::array<nc::DimInfo, 1> station{d.station};
  return in_order(
      [&] {
        return lifted(
            file.define_var<double>(sn::time, dims,
                                    {.fill = sn::fill_double,
                                     .deflate_level = sn::deflate_level,
                                     .chunks = sample_chunks(d)}));
      },
      [&] { return put_texts(file, sn::time, atts); },
      [&] {
        return lifted(
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
        return lifted(
            file.define_var<std::int8_t>(name, dims,
                                         {.fill = sn::fill_status,
                                          .deflate_level = sn::deflate_level,
                                          .chunks = sample_chunks(d)}));
      },
      [&] { return put_texts(file, name, names); },
      [&] { return lifted(file.put_att(name, "flag_values", flags)); },
      [&] { return put_texts(file, name, meanings); },
      [&] { return lifted(file.put_att(name, "valid_range", flags)); });
}

std::expected<void, Error> define_column(nc::NewFile& file, const ColumnPlan& c,
                                         const Dims& d) {
  const std::array<nc::DimInfo, 2> dims{d.station, d.sample};
  std::vector<TextAtt> atts{{.name = "coordinates", .value = sn::coordinates},
                            {.name = "grid_mapping", .value = "crs"}};
  if (not c.standard_name.empty()) {
    atts.push_back({.name = "standard_name", .value = c.standard_name});
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
        return lifted(
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
                                          core::Time now) {
  const std::string created =
      std::format("{:%FT%TZ}", std::chrono::floor<std::chrono::seconds>(now));
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
    if (value) {
      atts.push_back({name, *value});
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
                                      core::Time now) {
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
      .and_then([&] { return lifted(file.end_define()); });
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
      [&] { return lifted(file.put_char_rows(sn::station_id, plan.ids)); },
      [&] { return lifted(file.put_char_rows(sn::station_name, plan.names)); },
      [&]() -> std::expected<void, Error> {
        if (not plan.providers) {
          return {};
        }
        return lifted(
            file.put_char_rows(sn::station_provider, *plan.providers));
      },
      [&] { return lifted(file.put(sn::lat, lats, all)); },
      [&] { return lifted(file.put(sn::lon, lons, all)); });
}

std::expected<void, Error> put_times(nc::NewFile& file, const Plan& plan,
                                     const core::StationTable& table) {
  if (plan.layout == StationNcLayout::orthogonal) {
    const auto times = time_values(table.times(StationIndex{0}));
    return lifted(
        file.put(sn::time, times, {{.start = 0, .count = times.size()}}));
  }
  std::vector<std::int32_t> counts;
  for (const StationIndex i : table.stations()) {
    const std::span<const core::Time> times = table.times(i);
    counts.push_back(static_cast<std::int32_t>(times.size()));
    if (times.empty()) {
      continue;  // the padding is left unwritten: it reads as fill
    }
    if (auto done = lifted(file.put(sn::time, time_values(times),
                                    row_slab(i.value(), times.size())));
        not done) {
      return done;
    }
  }
  return lifted(
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
    if (auto done = lifted(file.put(c.name, values, slab)); not done) {
      return done;
    }
    if (not c.status) {
      continue;
    }
    std::vector<std::int8_t> flags(samples.size());
    std::ranges::transform(samples, flags.begin(), status_of);
    if (auto done = lifted(file.put(*c.status, flags, slab)); not done) {
      return done;
    }
  }
  return {};
}

std::expected<void, Error> write_body(nc::NewFile& file, const Plan& plan,
                                      const core::StationTable& table,
                                      const StationNcWriteOptions& options,
                                      core::Time now) {
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

std::expected<Read<StationNcLayout>, Error> write_station_netcdf(
    const std::filesystem::path& path, const core::StationTable& table,
    const StationNcWriteOptions& options, core::Time now,
    const FaultInjector& fault) {
  auto plan = plan_write(table);
  if (not plan) {
    return std::unexpected{std::move(plan.error())};
  }
  auto written = nc::detail::write_netcdf_atomic_impl(
      path, ReadLimits{},
      [&](nc::NewFile& file) {
        return write_body(file, plan->value, table, options, now);
      },
      fault);
  if (not written) {
    return std::unexpected{std::move(written.error())};
  }
  return Read<StationNcLayout>{.value = plan->value.layout,
                               .warnings = std::move(plan->warnings)};
}

}  // namespace detail

std::expected<Read<StationNcLayout>, Error> write_station_netcdf(
    const std::filesystem::path& path, const core::StationTable& table,
    const StationNcWriteOptions& options, core::Time now) {
  return detail::write_station_netcdf(path, table, options, now, {});
}

}  // namespace mov::io
