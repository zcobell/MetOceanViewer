// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The v5 station netCDF reader (docs/station-netcdf.md section 12, the v5
// column). In order: the header (format, version, conventions), the
// structure (which variable is what, the layout), the CRS, the stations and
// the schema; then, for a read, the times and samples of the selected
// stations, each row checked over its padding as it streams past.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <numeric>
#include <optional>
#include <ranges>
#include <set>
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
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/io/cf_time.hpp"
#include "mov/io/detail/station_groups.hpp"
#include "mov/io/detail/table_error.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/projection.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "station_netcdf_format.hpp"

namespace mov::io {

namespace {

namespace sn = detail::station_nc;
using detail::fail;
using detail::format_error;
using detail::optional_text;
using detail::SelectedStation;
using detail::StationGroup;

/// `code` about `subject`, as an io::Error result.
std::unexpected<Error> invalid(
    FormatErrc code, std::string subject,
    std::optional<std::size_t> station = std::nullopt,
    std::optional<std::size_t> index = std::nullopt) {
  return fail(format_error(code, std::move(subject), station, index));
}

// ---- the header (SN 12.1, 13)
// ----------------------------------------------------

std::expected<void, Error> check_format(const nc::File& file) {
  return optional_text(file, nc::global, "metoceanviewer_format")
      .and_then([](const std::optional<std::string>& text)
                    -> std::expected<void, Error> {
        if (not text or detail::cut_at_nul(*text) != station_nc_format) {
          return invalid(FormatErrc::not_this_format, ":metoceanviewer_format");
        }
        return {};
      });
}

std::expected<Read<StationNcVersion>, Error> read_version(
    const nc::File& file) {
  auto text = optional_text(file, nc::global, "metoceanviewer_format_version");
  if (not text) {
    return std::unexpected{std::move(text.error())};
  }
  if (not *text) {
    return invalid(FormatErrc::bad_version, ":metoceanviewer_format_version");
  }
  const std::string version{detail::cut_at_nul(**text)};
  const auto parsed = parse_station_nc_version(version);
  if (not parsed) {
    return invalid(FormatErrc::bad_version, version);
  }
  if (parsed->major != station_nc_version.major) {
    return invalid(FormatErrc::unsupported_version, version);
  }
  Read<StationNcVersion> out{.value = *parsed, .warnings = {}};
  if (parsed->minor > station_nc_version.minor) {
    out.warnings.push_back(
        {.code = WarningCode::minor_newer, .subject = version});
  }
  return out;
}

std::expected<void, Error> check_conventions(const nc::File& file) {
  auto conventions = optional_text(file, nc::global, "Conventions");
  if (not conventions) {
    return std::unexpected{std::move(conventions.error())};
  }
  if (not *conventions) {
    return invalid(FormatErrc::missing_attribute, ":Conventions");
  }
  if (not sn::has_cf_1_6_or_later(detail::cut_at_nul(**conventions))) {
    return invalid(FormatErrc::unsupported_version, ":Conventions");
  }
  auto feature = optional_text(file, nc::global, "featureType");
  if (not feature) {
    return std::unexpected{std::move(feature.error())};
  }
  if (not *feature) {
    return invalid(FormatErrc::missing_attribute, ":featureType");
  }
  if (not core::detail::equal_ignore_case(
          core::detail::trim(detail::cut_at_nul(**feature)),
          sn::feature_type)) {
    return invalid(FormatErrc::unsupported_layout, ":featureType");
  }
  return {};
}

std::expected<Read<StationNcVersion>, Error> read_header(const nc::File& file) {
  if (auto format = check_format(file); not format) {
    return std::unexpected{std::move(format.error())};
  }
  auto version = read_version(file);
  if (not version) {
    return version;
  }
  if (auto conventions = check_conventions(file); not conventions) {
    return std::unexpected{std::move(conventions.error())};
  }
  return version;
}

// ---- the structure (SN 12.2, 12.3)
// -------------------------------------------------

/// A data variable and its wet/dry status variable, if it has one.
struct DataVar {
  nc::VarInfo var;
  std::optional<nc::VarInfo> status;
};

using Vars = std::vector<nc::VarInfo>;

/// The time variable, the layout it gives, and obs_count (incomplete).
struct Timing {
  nc::VarInfo time;
  StationNcLayout layout;
  nc::DimInfo sample;  // `time` or `obs`
  std::optional<nc::VarInfo> obs_count;
};

/// The instance variables found by name (SN 4.2).
struct Instances {
  nc::VarInfo name;
  nc::VarInfo lat;
  nc::VarInfo lon;
  std::optional<nc::VarInfo> provider;
};

struct Structure {
  Vars vars;
  nc::DimInfo station;
  nc::VarInfo id;
  Instances instances;
  Timing timing;
  std::vector<DataVar> data;

  [[nodiscard]] StationNcLayout layout() const { return timing.layout; }
  [[nodiscard]] const nc::DimInfo& sample() const { return timing.sample; }
  [[nodiscard]] const nc::VarInfo& time() const { return timing.time; }
};

std::optional<nc::VarInfo> named(const Vars& vars, std::string_view name) {
  const auto it = std::ranges::find_if(
      vars, [name](const nc::VarInfo& v) { return v.name == name; });
  return it == vars.end() ? std::nullopt : std::optional{*it};
}

std::expected<nc::VarInfo, Error> require_named(const Vars& vars,
                                                std::string_view name) {
  auto var = named(vars, name);
  if (not var) {
    return invalid(FormatErrc::missing_variable, std::string{name});
  }
  return *std::move(var);
}

/// Whether `var` is over exactly these dimensions, in this order.
bool is_over(const nc::VarInfo& var, std::initializer_list<int> dims) {
  return std::ranges::equal(var.dims, dims, {}, &nc::DimInfo::id);
}

bool uses_dim(const nc::VarInfo& var, int dim) {
  return std::ranges::any_of(
      var.dims, [dim](const nc::DimInfo& d) { return d.id == dim; });
}

/// A char variable over (station, length).
std::expected<nc::VarInfo, Error> text_var(nc::VarInfo var,
                                           const nc::DimInfo& station) {
  if (var.type != nc::Type::char_) {
    return invalid(FormatErrc::bad_encoding, std::string{var.name.view()});
  }
  if (var.dims.size() != 2 or var.dims[0].id != station.id) {
    return invalid(FormatErrc::dimension_mismatch,
                   std::string{var.name.view()});
  }
  return var;
}

/// A variable over (station).
std::expected<nc::VarInfo, Error> instance_var(nc::VarInfo var,
                                               const nc::DimInfo& station) {
  if (not is_over(var, {station.id})) {
    return invalid(FormatErrc::dimension_mismatch,
                   std::string{var.name.view()});
  }
  return var;
}

/// The one variable whose `cf_role` is timeseries_id (SN 12.2).
std::expected<nc::VarInfo, Error> find_station_id(const nc::File& file,
                                                  const Vars& vars,
                                                  const nc::DimInfo& station) {
  std::vector<const nc::VarInfo*> ids;
  for (const nc::VarInfo& v : vars) {
    auto role = optional_text(file, v.name, "cf_role");
    if (not role) {
      return std::unexpected{std::move(role.error())};
    }
    if (*role and
        core::detail::trim(detail::cut_at_nul(**role)) == "timeseries_id") {
      ids.push_back(&v);
    }
  }
  if (ids.size() != 1) {
    return invalid(FormatErrc::no_station_id, "cf_role");
  }
  return text_var(*ids.front(), station);
}

/// L1: `time(time)`; L2: `time(station, obs)` with `obs_count(station)`.
std::expected<Timing, Error> timing_of(const Vars& vars, nc::VarInfo time,
                                       const nc::DimInfo& station) {
  if (time.dims.size() == 1 and time.dims[0].name == sn::time_dim.view()) {
    nc::DimInfo sample = time.dims[0];
    return Timing{.time = std::move(time),
                  .layout = StationNcLayout::orthogonal,
                  .sample = std::move(sample),
                  .obs_count = std::nullopt};
  }
  if (time.dims.size() != 2 or time.dims[0].id != station.id or
      time.dims[1].name != sn::obs_dim.view()) {
    return invalid(FormatErrc::unsupported_layout,
                   std::string{time.name.view()});
  }
  auto count =
      require_named(vars, sn::obs_count.view()).and_then([&](nc::VarInfo v) {
        return instance_var(std::move(v), station);
      });
  if (not count) {
    return std::unexpected{std::move(count.error())};
  }
  nc::DimInfo sample = time.dims[1];
  return Timing{.time = std::move(time),
                .layout = StationNcLayout::incomplete,
                .sample = std::move(sample),
                .obs_count = *std::move(count)};
}

/// The whitespace-separated names in `ancillary_variables` of `var`.
std::expected<std::vector<std::string>, Error> ancillary_names(
    const nc::File& file, const nc::VarInfo& var) {
  auto text = optional_text(file, var.name, "ancillary_variables");
  if (not text) {
    return std::unexpected{std::move(text.error())};
  }
  std::vector<std::string> names;
  if (*text) {
    for (const std::string_view word :
         detail::split_ws(detail::cut_at_nul(**text))) {
      names.emplace_back(word);
    }
  }
  return names;
}

/// A wet/dry status (SN 8.2): byte, flag_values 0 1, flag_meanings "dry wet".
std::expected<bool, Error> is_wet_dry(const nc::File& file,
                                      const nc::VarInfo& var) {
  if (var.type != nc::Type::byte) {
    return false;
  }
  auto meanings = optional_text(file, var.name, "flag_meanings");
  if (not meanings) {
    return std::unexpected{std::move(meanings.error())};
  }
  if (not *meanings or detail::split_ws(detail::cut_at_nul(**meanings)) !=
                           std::vector<std::string_view>{"dry", "wet"}) {
    return false;
  }
  const auto values = file.numeric_att<std::int8_t>(var.name, "flag_values");
  return values and *values and
         **values == std::vector<std::int8_t>{sn::status_dry, sn::status_wet};
}

/// The candidates' ancillary targets that exist: each must have its data
/// variable's dimensions (bad_ancillary). Returns the targets' names.
std::expected<std::set<std::string, std::less<>>, Error> ancillary_targets(
    const nc::File& file, const Vars& vars,
    std::span<const nc::VarInfo> candidates) {
  std::set<std::string, std::less<>> targets;
  for (const nc::VarInfo& c : candidates) {
    auto names = ancillary_names(file, c);
    if (not names) {
      return std::unexpected{std::move(names.error())};
    }
    for (const std::string& name : *names) {
      const auto target = named(vars, name);
      if (not target) {
        continue;  // a target that is not there is no structure
      }
      if (not std::ranges::equal(target->dims, c.dims, {}, &nc::DimInfo::id,
                                 &nc::DimInfo::id)) {
        return invalid(FormatErrc::bad_ancillary, name);
      }
      targets.insert(name);
    }
  }
  return targets;
}

/// The wet/dry status among the ancillary targets of `var`, if any.
std::expected<std::optional<nc::VarInfo>, Error> status_of(
    const nc::File& file, const Vars& vars, const nc::VarInfo& var) {
  auto names = ancillary_names(file, var);
  if (not names) {
    return std::unexpected{std::move(names.error())};
  }
  for (const std::string& name : *names) {
    auto target = named(vars, name);
    if (not target) {
      continue;
    }
    auto wet_dry = is_wet_dry(file, *target);
    if (not wet_dry) {
      return std::unexpected{std::move(wet_dry.error())};
    }
    if (*wet_dry) {
      return target;
    }
  }
  return std::optional<nc::VarInfo>{};
}

/// Every variable over the sample dimension must be over (station, sample);
/// those that are and are no ancillary target are the data variables.
std::expected<std::vector<DataVar>, Error> find_data(const nc::File& file,
                                                     const Vars& vars,
                                                     const nc::DimInfo& station,
                                                     const Timing& timing) {
  std::vector<nc::VarInfo> candidates;
  for (const nc::VarInfo& v : vars) {
    if (v.id == timing.time.id or not uses_dim(v, timing.sample.id)) {
      continue;
    }
    if (not is_over(v, {station.id, timing.sample.id})) {
      return invalid(FormatErrc::dimension_mismatch,
                     std::string{v.name.view()});
    }
    candidates.push_back(v);
  }
  auto targets = ancillary_targets(file, vars, candidates);
  if (not targets) {
    return std::unexpected{std::move(targets.error())};
  }
  std::vector<DataVar> data;
  for (nc::VarInfo& v : candidates) {
    if (targets->contains(v.name.view())) {
      continue;
    }
    auto status = status_of(file, vars, v);
    if (not status) {
      return std::unexpected{std::move(status.error())};
    }
    data.push_back({.var = std::move(v), .status = *std::move(status)});
  }
  if (data.empty()) {
    return invalid(FormatErrc::no_data_variables, "");
  }
  return data;
}

/// The named instance variables: station_name, lat, lon, station_provider.
std::expected<Instances, Error> find_instances(const Vars& vars,
                                               const nc::DimInfo& station) {
  auto name =
      require_named(vars, sn::station_name.view()).and_then([&](nc::VarInfo v) {
        return text_var(std::move(v), station);
      });
  auto lat = name.and_then([&](const auto&) {
    return require_named(vars, sn::lat.view()).and_then([&](nc::VarInfo v) {
      return instance_var(std::move(v), station);
    });
  });
  auto lon = lat.and_then([&](const auto&) {
    return require_named(vars, sn::lon.view()).and_then([&](nc::VarInfo v) {
      return instance_var(std::move(v), station);
    });
  });
  if (not lon) {
    return std::unexpected{std::move(lon.error())};
  }
  std::optional<nc::VarInfo> provider;
  if (auto found = named(vars, sn::station_provider.view())) {
    auto checked = text_var(*std::move(found), station);
    if (not checked) {
      return std::unexpected{std::move(checked.error())};
    }
    provider = *std::move(checked);
  }
  return Instances{.name = *std::move(name),
                   .lat = *std::move(lat),
                   .lon = *std::move(lon),
                   .provider = std::move(provider)};
}

std::expected<Structure, Error> read_structure(const nc::File& file) {
  auto station = detail::require_dim(file, sn::station_dim);
  if (not station) {
    return std::unexpected{std::move(station.error())};
  }
  if (auto count = detail::check_station_count(file, *station); not count) {
    return std::unexpected{std::move(count.error())};
  }
  auto vars = file.variables();
  if (not vars) {
    return fail(std::move(vars.error()));
  }
  auto id = find_station_id(file, *vars, *station);
  auto instances =
      id.and_then([&](const auto&) { return find_instances(*vars, *station); });
  auto timing = instances.and_then([&](const auto&) {
    return require_named(*vars, sn::time.view()).and_then([&](nc::VarInfo t) {
      return timing_of(*vars, std::move(t), *station);
    });
  });
  auto data = timing.and_then(
      [&](const Timing& t) { return find_data(file, *vars, *station, t); });
  if (not data) {
    return std::unexpected{std::move(data.error())};
  }
  return Structure{.vars = *std::move(vars),
                   .station = *std::move(station),
                   .id = *std::move(id),
                   .instances = *std::move(instances),
                   .timing = *std::move(timing),
                   .data = *std::move(data)};
}

// ---- the CRS (SN 10.1)
// ------------------------------------------------------------

/// The grid_mapping variables the data variables name, in order, distinct.
std::expected<std::vector<std::string>, Error> grid_mappings(
    const nc::File& file, std::span<const DataVar> data) {
  std::vector<std::string> names;
  for (const DataVar& d : data) {
    auto text = optional_text(file, d.var.name, "grid_mapping");
    if (not text) {
      return std::unexpected{std::move(text.error())};
    }
    if (*text) {
      std::string name{core::detail::trim(detail::cut_at_nul(**text))};
      if (std::ranges::find(names, name) == names.end()) {
        names.push_back(std::move(name));
      }
    }
  }
  return names;
}

/// "EPSG:<digits>" (at most 9 digits), positive.
std::optional<core::Epsg> parse_epsg(std::string_view text) {
  constexpr std::string_view prefix = "EPSG:";
  constexpr std::size_t max_digits = 9;
  if (not text.starts_with(prefix)) {
    return std::nullopt;
  }
  const std::string_view digits = text.substr(prefix.size());
  if (digits.empty() or digits.size() > max_digits or
      not std::ranges::all_of(digits,
                              [](char c) { return c >= '0' and c <= '9'; })) {
    return std::nullopt;
  }
  const int code =
      std::accumulate(digits.begin(), digits.end(), 0,
                      [](int n, char c) { return (n * 10) + (c - '0'); });
  const auto epsg = core::Epsg::make(code);
  return epsg ? std::optional{*epsg} : std::nullopt;
}

/// Equal to `value` when the attribute is there; absent is fine.
std::expected<bool, Error> double_att_is(const nc::File& file,
                                         nc::NcNameRef var, nc::NcNameRef att,
                                         double value) {
  auto values = file.numeric_att<double>(var, att);
  if (not values) {
    return fail(std::move(values.error()));
  }
  return not *values or **values == std::vector<double>{value};
}

/// The CRS of a grid mapping variable without `epsg_code`: latitude_longitude
/// on the WGS 84 ellipsoid.
std::expected<Read<core::Epsg>, Error> crs_from_parameters(
    const nc::File& file, const nc::VarInfo& var) {
  auto mapping = optional_text(file, var.name, "grid_mapping_name");
  if (not mapping) {
    return std::unexpected{std::move(mapping.error())};
  }
  const std::string subject{var.name.view()};
  if (not *mapping or core::detail::trim(detail::cut_at_nul(**mapping)) !=
                          "latitude_longitude") {
    return invalid(FormatErrc::unsupported_crs, subject);
  }
  auto axis = double_att_is(file, var.name, "semi_major_axis",
                            sn::wgs84_semi_major_axis);
  auto flattening = double_att_is(file, var.name, "inverse_flattening",
                                  sn::wgs84_inverse_flattening);
  for (auto* r : {&axis, &flattening}) {
    if (not *r) {
      return std::unexpected{std::move(r->error())};
    }
    if (not **r) {
      return invalid(FormatErrc::unsupported_crs, subject);
    }
  }
  return Read<core::Epsg>{.value = core::Epsg::wgs84(), .warnings = {}};
}

std::expected<Read<core::Epsg>, Error> crs_of(const nc::File& file,
                                              const Vars& vars,
                                              std::span<const DataVar> data) {
  auto names = grid_mappings(file, data);
  if (not names) {
    return std::unexpected{std::move(names.error())};
  }
  if (names->empty()) {
    return Read<core::Epsg>{.value = core::Epsg::wgs84(),
                            .warnings = {{.code = WarningCode::crs_assumed,
                                          .subject = "EPSG:4326"}}};
  }
  if (names->size() > 1) {
    return invalid(FormatErrc::unsupported_crs, names->at(1));
  }
  const auto var = named(vars, names->front());
  if (not var) {
    return invalid(FormatErrc::unsupported_crs, names->front());
  }
  auto code = optional_text(file, var->name, "epsg_code");
  if (not code) {
    return std::unexpected{std::move(code.error())};
  }
  if (not *code) {
    return crs_from_parameters(file, *var);
  }
  const auto epsg = parse_epsg(core::detail::trim(detail::cut_at_nul(**code)));
  if (not epsg) {
    return invalid(FormatErrc::unsupported_crs, std::string{var->name.view()});
  }
  return Read<core::Epsg>{.value = *epsg, .warnings = {}};
}

/// What turns the file's lon/lat into Locations: nothing for EPSG:4326, a
/// Projector for another geographic CRS. A projected CRS is refused (SN 10.1).
std::expected<std::optional<Projector>, Error> projector_for(core::Epsg epsg) {
  if (epsg == core::Epsg::wgs84()) {
    return std::optional<Projector>{};
  }
  auto projector = Projector::make(epsg);
  if (not projector) {
    return fail(detail::to_format_error(projector.error(), std::nullopt));
  }
  if (projector->kind() != CrsKind::geographic) {
    return invalid(FormatErrc::unsupported_crs,
                   "EPSG:" + std::to_string(epsg.code()));
  }
  return std::optional<Projector>{*std::move(projector)};
}

// ---- the stations (SN 12.4)
// ---------------------------------------------------------

/// The bytes of a char row without its trailing NUL padding.
std::string trimmed(std::string row) {
  while (not row.empty() and row.back() == '\0') {
    row.pop_back();
  }
  return row;
}

std::expected<std::vector<core::StationKey>, Error> station_ids(
    const nc::File& file, const nc::VarInfo& var, const StopToken& stop) {
  auto rows = file.read_char_rows(var.name, stop);
  if (not rows) {
    return std::unexpected{std::move(rows.error())};
  }
  const std::string subject{var.name.view()};
  std::vector<core::StationKey> ids;
  std::set<std::string, std::less<>> seen;
  for (std::size_t i = 0; i < rows->size(); ++i) {
    std::string text = trimmed(std::move((*rows)[i]));
    if (seen.contains(text)) {
      return invalid(FormatErrc::duplicate_station_id, text, i);
    }
    auto key = core::StationKey::make(text);
    if (not key) {
      return invalid(key.error() == core::StationKeyError::empty
                         ? FormatErrc::no_station_id
                         : FormatErrc::bad_encoding,
                     subject, i);
    }
    seen.insert(std::move(text));
    ids.push_back(*std::move(key));
  }
  return ids;
}

std::expected<std::vector<core::StationText>, Error> station_names(
    const nc::File& file, const nc::VarInfo& var, const StopToken& stop) {
  auto rows = file.read_char_rows(var.name, stop);
  if (not rows) {
    return std::unexpected{std::move(rows.error())};
  }
  std::vector<core::StationText> names;
  for (std::size_t i = 0; i < rows->size(); ++i) {
    auto text = core::StationText::make(trimmed(std::move((*rows)[i])));
    if (not text) {
      return invalid(FormatErrc::bad_encoding, std::string{var.name.view()}, i);
    }
    names.push_back(*std::move(text));
  }
  return names;
}

/// The source of each station; an unknown token is none, with a warning
/// (one per token, counting its stations).
std::expected<Read<std::vector<std::optional<core::DataSource>>>, Error>
station_sources(const nc::File& file, const Structure& s,
                const StopToken& stop) {
  Read<std::vector<std::optional<core::DataSource>>> out{
      .value = std::vector<std::optional<core::DataSource>>(s.station.length),
      .warnings = {}};
  if (not s.instances.provider) {
    return out;
  }
  auto rows = file.read_char_rows(s.instances.provider->name, stop);
  if (not rows) {
    return std::unexpected{std::move(rows.error())};
  }
  for (std::size_t i = 0; i < rows->size(); ++i) {
    const std::string token = trimmed(std::move((*rows)[i]));
    out.value[i] = core::parse_data_source(token);
    if (out.value[i] or token.empty()) {
      continue;
    }
    const auto known =
        std::ranges::find(out.warnings, token, &Warning::subject);
    if (known != out.warnings.end()) {
      ++known->count;
    } else {
      out.warnings.push_back(
          {.code = WarningCode::unknown_provider, .subject = token});
    }
  }
  return out;
}

std::expected<std::vector<double>, Error> coordinate(const nc::File& file,
                                                     const nc::VarInfo& var,
                                                     const StopToken& stop) {
  auto samples = file.read_samples(var.name, nc::whole(var), stop);
  if (not samples) {
    return std::unexpected{std::move(samples.error())};
  }
  std::vector<double> values;
  for (std::size_t i = 0; i < samples->size(); ++i) {
    const std::optional<double> x = (*samples)[i].value();
    if (not x) {
      return invalid(FormatErrc::bad_coordinates, std::string{var.name.view()},
                     i);
    }
    values.push_back(*x);
  }
  return values;
}

/// Where a station is: its WGS 84 Location and, when the file's CRS is
/// another geographic one, its point in that CRS.
struct Position {
  core::Location location;
  std::optional<core::NativePoint> native;
};

/// The position of station i from lon/lat in `projector`'s CRS, or WGS 84.
std::expected<Position, Error> place(double lat, double lon,
                                     std::optional<Projector>& projector,
                                     std::size_t i) {
  if (not projector) {
    const auto where = core::Location::make({.lat = lat, .lon = lon});
    if (not where) {
      return invalid(
          FormatErrc::bad_coordinates,
          where.error() == core::LocationError::longitude_out_of_range ? "lon"
                                                                       : "lat",
          i);
    }
    return Position{.location = *where, .native = std::nullopt};
  }
  const auto native =
      core::NativePoint::make({.x = lon, .y = lat}, projector->crs());
  if (not native) {
    return invalid(FormatErrc::bad_coordinates, "lat", i);
  }
  const auto where = projector->to_location({.x = lon, .y = lat});
  if (not where) {
    return invalid(FormatErrc::bad_coordinates, "lat", i);
  }
  return Position{.location = *where, .native = *native};
}

struct StationParts {
  std::vector<core::StationKey> ids;
  std::vector<core::StationText> names;
  std::vector<double> lats;
  std::vector<double> lons;
};

std::expected<StationParts, Error> station_parts(const nc::File& file,
                                                 const Structure& s,
                                                 const StopToken& stop) {
  auto ids = station_ids(file, s.id, stop);
  auto names = ids.and_then(
      [&](const auto&) { return station_names(file, s.instances.name, stop); });
  auto lats = names.and_then(
      [&](const auto&) { return coordinate(file, s.instances.lat, stop); });
  auto lons = lats.and_then(
      [&](const auto&) { return coordinate(file, s.instances.lon, stop); });
  if (not lons) {
    return std::unexpected{std::move(lons.error())};
  }
  return StationParts{.ids = *std::move(ids),
                      .names = *std::move(names),
                      .lats = *std::move(lats),
                      .lons = *std::move(lons)};
}

std::expected<Read<std::vector<core::FileStation>>, Error> read_stations(
    const nc::File& file, const Structure& s, core::Epsg crs,
    const StopToken& stop) {
  auto parts = station_parts(file, s, stop);
  if (not parts) {
    return std::unexpected{std::move(parts.error())};
  }
  auto sources = station_sources(file, s, stop);
  if (not sources) {
    return std::unexpected{std::move(sources.error())};
  }
  auto projector = projector_for(crs);
  if (not projector) {
    return std::unexpected{std::move(projector.error())};
  }
  Read<std::vector<core::FileStation>> out{.value = {},
                                           .warnings = sources->warnings};
  for (std::size_t i = 0; i < parts->ids.size(); ++i) {
    auto position = place(parts->lats[i], parts->lons[i], *projector, i);
    if (not position) {
      return std::unexpected{std::move(position.error())};
    }
    out.value.push_back({.id = std::move(parts->ids[i]),
                         .name = std::move(parts->names[i]),
                         .location = position->location,
                         .native = position->native,
                         .source = sources->value[i]});
  }
  if (*projector) {
    if (auto w = (*projector)->approximation_warning()) {
      out.warnings.push_back(*std::move(w));
    }
  }
  return out;
}

// ---- the schema (SN 4.3, 6, 10.2)
// ---------------------------------------------------

/// The quantity a variable name stands for (SN 6: the name is the token).
Read<core::QuantityId> quantity_of(std::string_view token,
                                   std::string_view standard_name) {
  if (const auto registry = core::parse_quantity_token(token)) {
    return {.value = *registry, .warnings = {}};
  }
  auto generic = core::GenericQuantity::parse(
      {.token = token, .standard_name = standard_name});
  const bool known = generic.has_value() and token == "value";
  Read<core::QuantityId> out{
      .value = generic ? core::QuantityId{*std::move(generic)}
                       : core::QuantityId{core::GenericQuantity::value()},
      .warnings = {}};
  if (not known) {
    out.warnings.push_back(
        {.code = WarningCode::unknown_quantity, .subject = std::string{token}});
  }
  return out;
}

/// The unit of `units`; an OtherUnit core does not know warns.
Read<std::optional<core::Unit>> unit_of(
    const std::optional<std::string>& text) {
  Read<std::optional<core::Unit>> out{
      .value = text ? core::parse_unit(*text) : std::nullopt, .warnings = {}};
  if (out.value) {
    const auto* other = std::get_if<core::OtherUnit>(&*out.value);
    if (other != nullptr and not core::is_canonical_other(*other)) {
      out.warnings.push_back({.code = WarningCode::unrecognized_unit,
                              .subject = std::string{other->symbol()}});
    }
  }
  return out;
}

/// `meta` with the datum of `vertical_datum`, when it has one it can carry.
Read<core::SeriesMeta> with_datum(core::SeriesMeta meta,
                                  const std::optional<std::string>& text,
                                  std::string_view variable) {
  Read<core::SeriesMeta> out{.value = std::move(meta), .warnings = {}};
  if (not text) {
    return out;
  }
  const auto datum = core::parse_vertical_datum(*text);
  if (not datum) {
    out.warnings.push_back({.code = WarningCode::datum_unknown,
                            .subject = std::string{datum.error().text}});
    return out;
  }
  if (not *datum) {
    return out;  // "none"
  }
  auto assumed = out.value.assume_datum(**datum);
  if (not assumed) {
    out.warnings.push_back(
        {.code = WarningCode::datum_unknown,
         .subject = std::string{variable} + ":vertical_datum"});
    return out;
  }
  out.value = *std::move(assumed);
  return out;
}

struct VarTexts {
  std::optional<std::string> standard_name;
  std::optional<std::string> long_name;
  std::optional<std::string> units;
  std::optional<std::string> datum;
};

std::expected<VarTexts, Error> texts_of(const nc::File& file,
                                        const nc::VarInfo& var) {
  const auto text = [&](nc::NcNameRef att) {
    return optional_text(file, var.name, att);
  };
  auto standard_name = text("standard_name");
  auto long_name = text("long_name");
  auto units = text("units");
  auto datum = text("vertical_datum");
  for (auto* t : {&standard_name, &long_name, &units, &datum}) {
    if (not *t) {
      return std::unexpected{std::move(t->error())};
    }
  }
  return VarTexts{.standard_name = *std::move(standard_name),
                  .long_name = *std::move(long_name),
                  .units = *std::move(units),
                  .datum = *std::move(datum)};
}

std::expected<Read<core::SeriesMeta>, Error> meta_of(const nc::File& file,
                                                     const nc::VarInfo& var) {
  auto texts = texts_of(file, var);
  if (not texts) {
    return std::unexpected{std::move(texts.error())};
  }
  const std::string_view token = var.name.view();
  Read<core::QuantityId> quantity =
      quantity_of(token, texts->standard_name.value_or(""));
  Read<std::optional<core::Unit>> unit = unit_of(texts->units);
  Read<core::SeriesMeta> meta =
      with_datum(core::SeriesMeta::make(
                     {.quantity = std::move(quantity.value),
                      .label = texts->long_name.value_or(std::string{token}),
                      .unit = std::move(unit.value)}),
                 texts->datum, token);
  meta.warnings.insert(meta.warnings.begin(), unit.warnings.begin(),
                       unit.warnings.end());
  meta.warnings.insert(meta.warnings.begin(), quantity.warnings.begin(),
                       quantity.warnings.end());
  return meta;
}

std::expected<Read<std::vector<core::SeriesMeta>>, Error> read_schema(
    const nc::File& file, std::span<const DataVar> data) {
  Read<std::vector<core::SeriesMeta>> out{.value = {}, .warnings = {}};
  for (const DataVar& d : data) {
    auto meta = meta_of(file, d.var);
    if (not meta) {
      return std::unexpected{std::move(meta.error())};
    }
    out.value.push_back(std::move(meta->value));
    detail::append(out.warnings, std::move(meta->warnings));
  }
  return out;
}

// ---- the catalog
// ---------------------------------------------------------------------

/// The number of samples of each station: obs_count (SN 12.4) or the length
/// of `time`.
std::expected<std::vector<std::size_t>, Error> sample_counts(
    const nc::File& file, const Structure& s, const StopToken& stop) {
  if (not s.timing.obs_count) {
    return std::vector<std::size_t>(s.station.length, s.sample().length);
  }
  auto raw = file.read<std::int64_t>(s.timing.obs_count->name,
                                     nc::whole(*s.timing.obs_count), stop);
  if (not raw) {
    return std::unexpected{std::move(raw.error())};
  }
  std::vector<std::size_t> counts;
  for (std::size_t i = 0; i < raw->size(); ++i) {
    const std::int64_t n = (*raw)[i];
    if (n < 0 or std::cmp_greater(n, s.sample().length)) {
      return invalid(FormatErrc::bad_obs_count,
                     std::string{s.timing.obs_count->name.view()}, i);
    }
    counts.push_back(static_cast<std::size_t>(n));
  }
  return counts;
}

/// Everything but the samples: what inspect returns and a read starts from.
struct Opened {
  Structure structure;
  StationNcCatalog catalog;
};

std::expected<Read<Opened>, Error> open_v5(const nc::File& file,
                                           const StopToken& stop) {
  auto header = read_header(file);
  if (not header) {
    return std::unexpected{std::move(header.error())};
  }
  auto structure = read_structure(file);
  if (not structure) {
    return std::unexpected{std::move(structure.error())};
  }
  auto crs = crs_of(file, structure->vars, structure->data);
  auto stations = crs.and_then([&](const Read<core::Epsg>& c) {
    return read_stations(file, *structure, c.value, stop);
  });
  auto schema = stations.and_then(
      [&](const auto&) { return read_schema(file, structure->data); });
  auto counts = schema.and_then(
      [&](const auto&) { return sample_counts(file, *structure, stop); });
  if (not counts) {
    return std::unexpected{std::move(counts.error())};
  }
  const StationNcLayout layout = structure->layout();
  Read<Opened> out{.value = {.structure = *std::move(structure),
                             .catalog = {.version = header->value,
                                         .layout = layout,
                                         .stations = std::move(stations->value),
                                         .sample_counts = *std::move(counts),
                                         .schema = std::move(schema->value)}},
                   .warnings = std::move(header->warnings)};
  detail::append(out.warnings, std::move(crs->warnings));
  detail::append(out.warnings, std::move(stations->warnings));
  detail::append(out.warnings, std::move(schema->warnings));
  return out;
}

// ---- the samples (SN 7, 8, 12.4)
// ------------------------------------------------------

/// Calls sink(member, row) for each selected station of `groups` with its row
/// of `var` (station, sample) as raw values of type T.
template <nc::Numeric T, class Sink>
std::expected<void, Error> raw_rows(const nc::File& file, nc::NcNameRef var,
                                    std::size_t length,
                                    std::span<const StationGroup> groups,
                                    const StopToken& stop, const Sink& sink) {
  for (const StationGroup& group : groups) {
    const nc::Slab slab{{.start = group.first, .count = group.width()},
                        {.start = 0, .count = length}};
    auto done = file.read_blocks<T>(
        var, slab,
        [&](std::span<const T> block,
            nc::DimRange outer) -> std::expected<void, Error> {
          for (const SelectedStation& m : group.members) {
            if (m.station < outer.start or
                m.station >= outer.start + outer.count) {
              continue;
            }
            const std::size_t row = m.station - outer.start;
            if (auto r = sink(m, block.subspan(row * length, length)); not r) {
              return r;
            }
          }
          return {};
        },
        stop);
    if (not done) {
      return done;
    }
  }
  return {};
}

/// raw_rows, masked in the variable's own type into Samples.
template <class Sink>
std::expected<void, Error> sample_rows(const nc::File& file,
                                       const nc::VarInfo& var,
                                       std::size_t length,
                                       std::span<const StationGroup> groups,
                                       const StopToken& stop,
                                       const Sink& sink) {
  return detail::dispatch_model_numeric(
      file, var, [&]<class T>() -> std::expected<void, Error> {
        auto mask = file.masking<T>(var.name);
        if (not mask) {
          return fail(std::move(mask.error()));
        }
        std::vector<core::Sample> row;
        return raw_rows<T>(
            file, var.name, length, groups, stop,
            [&](const SelectedStation& m, std::span<const T> raw) {
              row.resize(raw.size());
              std::ranges::transform(raw, row.begin(),
                                     [&](T x) { return mask->apply(x); });
              return sink(m, std::span<const core::Sample>{row});
            });
      });
}

/// What a read needs besides the file: the counts, the groups of each
/// variable and the stop token.
struct Rows {
  const Structure& s;
  std::span<const std::size_t> counts;
  std::span<const std::size_t> selected;
  const StopToken& stop;
};

std::expected<std::vector<StationGroup>, Error> groups_for(
    const nc::File& file, const nc::VarInfo& var, const Rows& rows) {
  return detail::plan_groups(file, var, rows.s.station.id, rows.selected,
                             std::nullopt);
}

/// The first element of `tail` that is not missing, as an error.
std::expected<void, Error> check_padding(std::span<const core::Sample> tail,
                                         std::size_t count,
                                         std::string_view var,
                                         std::size_t station) {
  const auto present = std::ranges::find_if(
      tail, [](const core::Sample& x) { return not x.is_missing(); });
  if (present != tail.end()) {
    return invalid(FormatErrc::padding_not_missing, std::string{var}, station,
                   count + static_cast<std::size_t>(present - tail.begin()));
  }
  return {};
}

/// One station's times: present, on the clock, strictly increasing.
std::expected<core::TimeAxis, Error> axis_of(std::span<const core::Sample> row,
                                             const CfClock& clock,
                                             std::string_view var,
                                             std::size_t station) {
  core::TimeAxis axis;
  axis.reserve(row.size());
  for (std::size_t j = 0; j < row.size(); ++j) {
    const std::optional<double> x = row[j].value();
    if (not x) {
      return invalid(FormatErrc::time_missing, std::string{var}, station, j);
    }
    const auto time = clock.at(*x);
    if (not time) {
      return invalid(FormatErrc::time_out_of_range, std::string{var}, station,
                     j);
    }
    if (not axis.empty() and not(axis.back() < *time)) {
      return invalid(FormatErrc::time_not_increasing, std::string{var}, station,
                     j);
    }
    axis.push_back(*time);
  }
  return axis;
}

std::expected<CfClock, Error> clock_of(const nc::File& file,
                                       const nc::VarInfo& time,
                                       std::vector<Warning>& warnings) {
  auto units = optional_text(file, time.name, "units");
  if (not units) {
    return std::unexpected{std::move(units.error())};
  }
  if (not *units) {
    return invalid(FormatErrc::missing_attribute,
                   std::string{time.name.view()} + ":units");
  }
  auto parsed = parse_cf_time_units(detail::cut_at_nul(**units));
  if (not parsed) {
    return fail(std::move(parsed.error()));
  }
  detail::append(warnings, std::move(parsed->warnings));
  return detail::read_calendar(file, time).and_then([&](CfCalendar calendar) {
    return detail::make_clock(parsed->value, calendar, time.name.view());
  });
}

/// The axes: one shared (orthogonal) or one per selected station.
std::expected<std::vector<core::TimeAxis>, Error> read_axes(
    const nc::File& file, const Rows& rows, const CfClock& clock) {
  const Structure& s = rows.s;
  if (s.layout() == StationNcLayout::orthogonal) {
    return detail::read_time_axis(file, s.time(), clock, rows.stop)
        .transform([](core::TimeAxis axis) {
          return std::vector<core::TimeAxis>{std::move(axis)};
        });
  }
  auto groups = groups_for(file, s.time(), rows);
  if (not groups) {
    return std::unexpected{std::move(groups.error())};
  }
  std::vector<core::TimeAxis> axes(rows.selected.size());
  auto done = sample_rows(
      file, s.time(), s.sample().length, *groups, rows.stop,
      [&](const SelectedStation& m,
          std::span<const core::Sample> row) -> std::expected<void, Error> {
        const std::size_t n = rows.counts[m.station];
        auto axis =
            axis_of(row.first(n), clock, s.time().name.view(), m.station);
        if (not axis) {
          return std::unexpected{std::move(axis.error())};
        }
        axes[m.position] = *std::move(axis);
        return check_padding(row.subspan(n), n, s.time().name.view(),
                             m.station);
      });
  if (not done) {
    return std::unexpected{std::move(done.error())};
  }
  return axes;
}

/// The samples of a data variable at each selected station (its count's
/// first), the padding checked.
std::expected<std::vector<core::Column>, Error> read_values(
    const nc::File& file, const nc::VarInfo& var, const Rows& rows) {
  auto groups = groups_for(file, var, rows);
  if (not groups) {
    return std::unexpected{std::move(groups.error())};
  }
  std::vector<core::Column> columns(rows.selected.size());
  auto done = sample_rows(
      file, var, rows.s.sample().length, *groups, rows.stop,
      [&](const SelectedStation& m, std::span<const core::Sample> row) {
        const std::size_t n = rows.counts[m.station];
        columns[m.position].assign(
            row.begin(), row.begin() + static_cast<std::ptrdiff_t>(n));
        return check_padding(row.subspan(n), n, var.name.view(), m.station);
      });
  if (not done) {
    return std::unexpected{std::move(done.error())};
  }
  return columns;
}

struct Flags {
  std::vector<std::vector<std::int8_t>> rows;
  std::optional<std::int8_t> fill;
};

/// The wet/dry flags at each selected station, raw (a flag outside
/// valid_range must be seen, not masked); the padding must be fill.
std::expected<Flags, Error> read_flags(const nc::File& file,
                                       const nc::VarInfo& var,
                                       const Rows& rows) {
  auto mask = file.masking<std::int8_t>(var.name);
  if (not mask) {
    return fail(std::move(mask.error()));
  }
  auto groups = groups_for(file, var, rows);
  if (not groups) {
    return std::unexpected{std::move(groups.error())};
  }
  Flags flags{
      .rows = std::vector<std::vector<std::int8_t>>(rows.selected.size()),
      .fill = mask->fill};
  auto done = raw_rows<std::int8_t>(
      file, var.name, rows.s.sample().length, *groups, rows.stop,
      [&](const SelectedStation& m,
          std::span<const std::int8_t> row) -> std::expected<void, Error> {
        const std::size_t n = rows.counts[m.station];
        const auto tail = row.subspan(n);
        const auto present = std::ranges::find_if(
            tail, [&](std::int8_t f) { return f != flags.fill; });
        if (present != tail.end()) {
          return invalid(FormatErrc::padding_not_missing,
                         std::string{var.name.view()}, m.station,
                         n + static_cast<std::size_t>(present - tail.begin()));
        }
        flags.rows[m.position].assign(
            row.begin(), row.begin() + static_cast<std::ptrdiff_t>(n));
        return {};
      });
  if (not done) {
    return std::unexpected{std::move(done.error())};
  }
  return flags;
}

/// The column of one station with its flags applied (SN 8.2).
std::expected<void, Error> apply_flags(core::Column& column,
                                       std::span<const std::int8_t> flags,
                                       std::optional<std::int8_t> fill,
                                       const nc::VarInfo& data,
                                       const nc::VarInfo& status,
                                       std::size_t station) {
  for (std::size_t j = 0; j < flags.size(); ++j) {
    const std::int8_t f = flags[j];
    if (f == fill) {
      continue;  // unclassified
    }
    if (f != sn::status_dry and f != sn::status_wet) {
      return invalid(FormatErrc::bad_flag, std::string{status.name.view()},
                     station, j);
    }
    if ((f == sn::status_dry) == column[j].is_value()) {
      return invalid(FormatErrc::wet_dry_inconsistent,
                     std::string{data.name.view()}, station, j);
    }
    if (f == sn::status_dry) {
      column[j] = core::Sample{core::Dry{}};
    }
  }
  return {};
}

std::expected<std::vector<core::Column>, Error> read_data(const nc::File& file,
                                                          const DataVar& d,
                                                          const Rows& rows) {
  auto columns = read_values(file, d.var, rows);
  if (not columns or not d.status) {
    return columns;
  }
  auto flags = read_flags(file, *d.status, rows);
  if (not flags) {
    return std::unexpected{std::move(flags.error())};
  }
  for (std::size_t p = 0; p < rows.selected.size(); ++p) {
    if (auto done = apply_flags((*columns)[p], flags->rows[p], flags->fill,
                                d.var, *d.status, rows.selected[p]);
        not done) {
      return std::unexpected{std::move(done.error())};
    }
  }
  return columns;
}

std::vector<std::size_t> selected_indices(const StationNcSelection& which,
                                          std::size_t count) {
  if (const auto* selection = std::get_if<core::StationSelection>(&which)) {
    return {selection->indices().begin(), selection->indices().end()};
  }
  std::vector<std::size_t> all(count);
  std::ranges::copy(std::views::iota(std::size_t{0}, count), all.begin());
  return all;
}

std::expected<void, Error> check_selection(const StationNcSelection& which,
                                           std::size_t count) {
  if (const auto* selection = std::get_if<core::StationSelection>(&which)) {
    return detail::require_selection(*selection, count);
  }
  return {};
}

/// The table of the selected stations.
std::expected<Read<core::StationTable>, Error> read_table(
    const nc::File& file, Opened& opened, std::span<const std::size_t> selected,
    const StopToken& stop) {
  const Structure& s = opened.structure;
  const Rows rows{.s = s,
                  .counts = opened.catalog.sample_counts,
                  .selected = selected,
                  .stop = stop};
  std::size_t samples = 0;
  for (const std::size_t i : selected) {
    samples += rows.counts[i];
  }
  if (auto size = detail::check_result_size(
          file, s.data.front().var.name.view(), 1, samples, s.data.size());
      not size) {
    return std::unexpected{std::move(size.error())};
  }
  std::vector<Warning> warnings;
  auto clock = clock_of(file, s.time(), warnings);
  auto axes = clock.and_then(
      [&](const CfClock& c) { return read_axes(file, rows, c); });
  if (not axes) {
    return std::unexpected{std::move(axes.error())};
  }
  std::vector<core::Variable> variables;
  for (std::size_t k = 0; k < s.data.size(); ++k) {
    auto columns = read_data(file, s.data[k], rows);
    if (not columns) {
      return std::unexpected{std::move(columns.error())};
    }
    variables.push_back(
        {.meta = opened.catalog.schema[k], .per_station = *std::move(columns)});
  }
  std::vector<core::StationRow> station_rows;
  station_rows.reserve(selected.size());
  for (std::size_t p = 0; p < selected.size(); ++p) {
    station_rows.push_back(
        {.station = opened.catalog.stations[selected[p]],
         .axis = s.layout() == StationNcLayout::orthogonal ? 0 : p});
  }
  auto table = core::StationTable::make(std::move(variables), *std::move(axes),
                                        std::move(station_rows));
  if (not table) {
    return fail(detail::to_format_error(table.error()));
  }
  return Read<core::StationTable>{.value = *std::move(table),
                                  .warnings = std::move(warnings)};
}

std::expected<Read<StationFile>, Error> read_v5(const nc::File& file,
                                                const StationNcSelection& which,
                                                const StopToken& stop) {
  auto opened = open_v5(file, stop);
  if (not opened) {
    return std::unexpected{std::move(opened.error())};
  }
  const std::size_t count = opened->value.catalog.stations.size();
  if (auto ok = check_selection(which, count); not ok) {
    return std::unexpected{std::move(ok.error())};
  }
  const std::vector<std::size_t> selected = selected_indices(which, count);
  auto table = read_table(file, opened->value, selected, stop);
  if (not table) {
    return std::unexpected{std::move(table.error())};
  }
  Read<StationFile> out{
      .value = V5StationFile{.version = opened->value.catalog.version,
                             .layout = opened->value.catalog.layout,
                             .table = std::move(table->value)},
      .warnings = std::move(opened->warnings)};
  detail::append(out.warnings, std::move(table->warnings));
  return out;
}

/// Opens `path`, runs `read` on it and closes it, on every path.
template <class F>
auto with_file(const std::filesystem::path& path, const ReadContext& ctx,
               F&& read) -> std::invoke_result_t<F, const nc::File&> {
  auto file = nc::File::open(path, ctx.limits);
  if (not file) {
    return fail(std::move(file.error()));
  }
  auto result = std::forward<F>(read)(std::as_const(*file));
  if (auto closed = std::move(*file).close(); not closed and result) {
    return fail(std::move(closed.error()));
  }
  return result;
}

}  // namespace

std::expected<Read<StationNcCatalog>, Error> inspect_station_netcdf(
    const std::filesystem::path& path, const ReadContext& ctx) {
  return with_file(
      path, ctx,
      [&](const nc::File& file)
          -> std::expected<Read<StationNcCatalog>, Error> {
        return open_v5(file, ctx.stop).transform([](Read<Opened> opened) {
          return Read<StationNcCatalog>{
              .value = std::move(opened.value.catalog),
              .warnings = std::move(opened.warnings)};
        });
      });
}

std::expected<Read<StationFile>, Error> read_station_netcdf(
    const std::filesystem::path& path, const StationNcSelection& which,
    const ReadContext& ctx) {
  return with_file(path, ctx, [&](const nc::File& file) {
    return read_v5(file, which, ctx.stop);
  });
}

}  // namespace mov::io
