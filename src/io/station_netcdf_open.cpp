// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Opening a v5 station netCDF file (docs/station-netcdf.md section 12, the v5
// column): the header (format, version, conventions), the structure (which
// variable is what, the layout, the wet/dry status variables), the CRS, the
// stations with their sample counts, and the schema. Everything a catalog
// holds; the samples are station_netcdf_samples.cpp's.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
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
#include "mov/core/detail/utf8.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/station.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/table_error.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/projection.hpp"
#include "mov/io/read.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "station_netcdf_format.hpp"
#include "station_netcdf_reader.hpp"

namespace mov::io::detail::station_nc {

std::unexpected<Error> invalid(FormatErrc code, std::string subject,
                               std::optional<std::size_t> station,
                               std::optional<std::size_t> index) {
  return fail(format_error(code, std::move(subject), station, index));
}

std::string subject_of(std::string_view text) {
  return std::string{truncate_utf8(text, ParseError::max_context_bytes)};
}

namespace {

namespace sn = ::mov::io::detail::station_nc;

using Vars = std::vector<nc::VarInfo>;

/// A text attribute of `on`, cut at its first NUL; nullopt when absent or not
/// text.
std::expected<std::optional<std::string>, Error> text_of(const nc::File& file,
                                                         nc::AttTarget on,
                                                         nc::NcNameRef att) {
  return optional_text(file, on, att)
      .transform([](std::optional<std::string> text) {
        return text.transform(
            [](const std::string& t) { return std::string{cut_at_nul(t)}; });
      });
}

// ---- the header (SN 12.1, 13)
// ----------------------------------------------------

std::expected<void, Error> check_format(const nc::File& file) {
  return text_of(file, nc::global, "metoceanviewer_format")
      .and_then([](const std::optional<std::string>& text)
                    -> std::expected<void, Error> {
        if (text != station_nc_format) {
          return invalid(FormatErrc::not_this_format, ":metoceanviewer_format");
        }
        return {};
      });
}

std::expected<Read<StationNcVersion>, Error> read_version(
    const nc::File& file) {
  auto text = text_of(file, nc::global, "metoceanviewer_format_version");
  if (not text) {
    return std::unexpected{std::move(text).error()};
  }
  if (not *text) {
    return invalid(FormatErrc::bad_version, ":metoceanviewer_format_version");
  }
  const std::string version = subject_of(**text);
  const auto parsed = parse_station_nc_version(**text);
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

/// `Conventions` names CF 1.6 or a later 1.x (CF 2 would be another
/// convention: refused, N4).
std::expected<void, Error> check_conventions(const nc::File& file) {
  auto conventions = text_of(file, nc::global, "Conventions");
  if (not conventions) {
    return std::unexpected{std::move(conventions).error()};
  }
  if (not *conventions) {
    return invalid(FormatErrc::missing_attribute, ":Conventions");
  }
  const auto cf = parse_cf_conventions(**conventions);
  if (not cf or cf->major != 1 or cf->minor < 6) {
    return invalid(FormatErrc::unsupported_version, ":Conventions");
  }
  return {};
}

std::expected<void, Error> check_feature_type(const nc::File& file) {
  auto feature = text_of(file, nc::global, "featureType");
  if (not feature) {
    return std::unexpected{std::move(feature).error()};
  }
  if (not *feature) {
    return invalid(FormatErrc::missing_attribute, ":featureType");
  }
  if (not core::detail::equal_ignore_case(core::detail::trim(**feature),
                                          sn::feature_type)) {
    return invalid(FormatErrc::unsupported_layout, ":featureType");
  }
  return {};
}

std::expected<Read<StationNcVersion>, Error> read_header(const nc::File& file) {
  return check_format(file)
      .and_then([&] { return read_version(file); })
      .and_then([&](Read<StationNcVersion> version) {
        return check_conventions(file)
            .and_then([&] { return check_feature_type(file); })
            .transform([&] { return std::move(version); });
      });
}

// ---- the structure (SN 12.2, 12.3)
// -------------------------------------------------

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

/// A v5 file has no unlimited dimension (SN 3); a dimension no variable uses
/// does not matter.
std::expected<void, Error> check_fixed(const Vars& vars) {
  for (const nc::VarInfo& v : vars) {
    const auto unlimited = std::ranges::find_if(
        v.dims, [](const nc::DimInfo& d) { return d.unlimited; });
    if (unlimited != v.dims.end()) {
      return invalid(FormatErrc::unsupported_layout,
                     std::string{unlimited->name.view()});
    }
  }
  return {};
}

/// The one variable whose `cf_role` is timeseries_id (SN 12.2).
std::expected<nc::VarInfo, Error> find_station_id(const nc::File& file,
                                                  const Vars& vars,
                                                  const nc::DimInfo& station) {
  std::vector<const nc::VarInfo*> ids;
  for (const nc::VarInfo& v : vars) {
    auto role = text_of(file, v.name, "cf_role");
    if (not role) {
      return std::unexpected{std::move(role).error()};
    }
    if (role->transform(core::detail::trim) == "timeseries_id") {
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
    return std::unexpected{std::move(count).error()};
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
  return text_of(file, var.name, "ancillary_variables")
      .transform([](const std::optional<std::string>& text) {
        std::vector<std::string> names;
        if (text) {
          for (const std::string_view word : split_ws(*text)) {
            names.emplace_back(word);
          }
        }
        return names;
      });
}

/// A numeric attribute that must be int8 values: nullopt when absent or of
/// another type (the caller refuses it), other errors propagated.
std::expected<std::optional<std::vector<std::int8_t>>, Error> byte_att(
    const nc::File& file, nc::NcNameRef var, nc::NcNameRef att) {
  auto values = file.numeric_att<std::int8_t>(var, att);
  if (values) {
    return *std::move(values);
  }
  const auto* fault = std::get_if<WrapperFault>(&values.error().status);
  if (fault != nullptr and (*fault == WrapperFault::type_mismatch or
                            *fault == WrapperFault::count_mismatch)) {
    return std::optional<std::vector<std::int8_t>>{};
  }
  return fail(std::move(values).error());
}

/// The wet/dry status variable of SN 8.2, which a v5 file must write
/// exactly: byte, `flag_values` 0 1, `flag_meanings` "dry wet", a _FillValue
/// (if any) that is no flag. Anything else is `bad_flag`.
std::expected<void, Error> check_status(const nc::File& file,
                                        const nc::VarInfo& status) {
  const std::string subject{status.name.view()};
  if (status.type != nc::Type::byte) {
    return invalid(FormatErrc::bad_flag, subject);
  }
  auto meanings = text_of(file, status.name, "flag_meanings");
  auto values = byte_att(file, status.name, "flag_values");
  auto fill = byte_att(file, status.name, "_FillValue");
  for (const auto* r : {&values, &fill}) {
    if (not *r) {
      return std::unexpected{r->error()};
    }
  }
  if (not meanings) {
    return std::unexpected{std::move(meanings).error()};
  }
  const bool meanings_ok =
      *meanings and
      split_ws(**meanings) == std::vector<std::string_view>{"dry", "wet"};
  const bool values_ok =
      *values == std::vector<std::int8_t>{sn::status_dry, sn::status_wet};
  const bool fill_ok =
      fill->value_or(std::vector<std::int8_t>{}).size() <= 1 and
      std::ranges::none_of(fill->value_or(std::vector<std::int8_t>{}),
                           [](std::int8_t f) {
                             return f == sn::status_dry or f == sn::status_wet;
                           });
  if (not meanings_ok or not values_ok or not fill_ok) {
    return invalid(FormatErrc::bad_flag, subject);
  }
  return {};
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
      return std::unexpected{std::move(names).error()};
    }
    for (const std::string& name : *names) {
      const auto target = named(vars, name);
      if (not target) {
        continue;  // a target that is not there is no structure
      }
      if (not std::ranges::equal(target->dims, c.dims, {}, &nc::DimInfo::id,
                                 &nc::DimInfo::id)) {
        return invalid(FormatErrc::bad_ancillary, subject_of(name));
      }
      targets.insert(name);
    }
  }
  return targets;
}

/// The wet/dry status of `var`: the ancillary target named `<var>_status`,
/// checked. Other ancillary variables (a newer minor version's) are ignored.
std::expected<std::optional<nc::VarInfo>, Error> status_of(
    const nc::File& file, const Vars& vars, const nc::VarInfo& var) {
  auto names = ancillary_names(file, var);
  if (not names) {
    return std::unexpected{std::move(names).error()};
  }
  const std::string expected =
      std::string{var.name.view()} + std::string{sn::status_suffix};
  if (std::ranges::find(*names, expected) == names->end()) {
    return std::optional<nc::VarInfo>{};
  }
  auto status = named(vars, expected);
  if (not status) {
    return std::optional<nc::VarInfo>{};
  }
  return check_status(file, *status).transform([&] { return status; });
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
      return invalid(FormatErrc::dimension_mismatch, subject_of(v.name.view()));
    }
    candidates.push_back(v);
  }
  auto targets = ancillary_targets(file, vars, candidates);
  if (not targets) {
    return std::unexpected{std::move(targets).error()};
  }
  std::vector<DataVar> data;
  for (nc::VarInfo& v : candidates) {
    if (targets->contains(v.name.view())) {
      continue;
    }
    auto status = status_of(file, vars, v);
    if (not status) {
      return std::unexpected{std::move(status).error()};
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
  auto parts = collect(
      [&] {
        return require_named(vars, sn::station_name.view())
            .and_then(
                [&](nc::VarInfo v) { return text_var(std::move(v), station); });
      },
      [&] {
        return require_named(vars, sn::lat.view()).and_then([&](nc::VarInfo v) {
          return instance_var(std::move(v), station);
        });
      },
      [&] {
        return require_named(vars, sn::lon.view()).and_then([&](nc::VarInfo v) {
          return instance_var(std::move(v), station);
        });
      },
      [&]() -> std::expected<std::optional<nc::VarInfo>, Error> {
        auto provider = named(vars, sn::station_provider.view());
        if (not provider) {
          return std::nullopt;
        }
        return text_var(*std::move(provider), station);
      });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  auto& [name, lat, lon, provider] = *parts;
  return Instances{.name = std::move(name),
                   .lat = std::move(lat),
                   .lon = std::move(lon),
                   .provider = std::move(provider)};
}

std::expected<Structure, Error> read_structure(const nc::File& file) {
  auto base =
      collect([&] { return detail::require_dim(file, sn::station_dim); },
              [&] { return file.variables().transform_error(lift<Error>); });
  if (not base) {
    return std::unexpected{std::move(base).error()};
  }
  auto& [station, vars] = *base;
  auto parts = collect(
      [&] {
        return check_station_count(file, station).transform([] { return 0; });
      },
      [&] { return check_fixed(vars).transform([] { return 0; }); },
      [&] { return find_station_id(file, vars, station); },
      [&] { return find_instances(vars, station); },
      [&] {
        return require_named(vars, sn::time.view())
            .and_then([&](nc::VarInfo t) {
              return timing_of(vars, std::move(t), station);
            });
      });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  auto& [count, fixed, id, instances, timing] = *parts;
  static_cast<void>(count);
  static_cast<void>(fixed);
  auto data = find_data(file, vars, station, timing);
  if (not data) {
    return std::unexpected{std::move(data).error()};
  }
  return Structure{.vars = std::move(vars),
                   .station = std::move(station),
                   .id = std::move(id),
                   .instances = std::move(instances),
                   .timing = std::move(timing),
                   .data = *std::move(data)};
}

// ---- the CRS (SN 10.1)
// ------------------------------------------------------------

/// The grid_mapping variables the data variables name, in order, distinct.
std::expected<std::vector<std::string>, Error> grid_mappings(
    const nc::File& file, std::span<const DataVar> data) {
  std::vector<std::string> names;
  for (const DataVar& d : data) {
    auto text = text_of(file, d.var.name, "grid_mapping");
    if (not text) {
      return std::unexpected{std::move(text).error()};
    }
    if (*text) {
      std::string name{core::detail::trim(**text)};
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
  int code = 0;
  for (const char c : digits) {
    code = (code * 10) + (c - '0');
  }
  const auto epsg = core::Epsg::make(code);
  return epsg ? std::optional{*epsg} : std::nullopt;
}

/// A one-value double attribute: nullopt when absent.
std::expected<std::optional<double>, Error> double_att(const nc::File& file,
                                                       nc::NcNameRef var,
                                                       nc::NcNameRef att) {
  auto values = file.numeric_att<double>(var, att);
  if (not values) {
    return fail(std::move(values).error());
  }
  if (*values and (*values)->size() != 1) {
    return fail(nc_fault(file, WrapperFault::count_mismatch, NcOp::get_att,
                         att.view()));
  }
  return values->transform([](const std::vector<double>& v) { return v[0]; });
}

/// The CRS of a grid mapping variable without `epsg_code`:
/// latitude_longitude on the WGS 84 ellipsoid; without ellipsoid parameters,
/// assumed to be WGS 84 (warning).
std::expected<Read<core::Epsg>, Error> crs_from_parameters(
    const nc::File& file, const nc::VarInfo& var) {
  auto parts =
      collect([&] { return text_of(file, var.name, "grid_mapping_name"); },
              [&] { return double_att(file, var.name, "semi_major_axis"); },
              [&] { return double_att(file, var.name, "inverse_flattening"); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  const auto& [mapping, axis, flattening] = *parts;
  const std::string subject{var.name.view()};
  if (mapping.transform(core::detail::trim) != "latitude_longitude" or
      axis.value_or(sn::wgs84_semi_major_axis) != sn::wgs84_semi_major_axis or
      flattening.value_or(sn::wgs84_inverse_flattening) !=
          sn::wgs84_inverse_flattening) {
    return invalid(FormatErrc::unsupported_crs, subject);
  }
  Read<core::Epsg> out{.value = core::Epsg::wgs84(), .warnings = {}};
  if (not axis or not flattening) {
    out.warnings.push_back(
        {.code = WarningCode::crs_assumed, .subject = subject});
  }
  return out;
}

/// The grid mapping variable's CRS: `epsg_code`, else its parameters. A
/// prime meridian other than Greenwich is refused either way.
std::expected<Read<core::Epsg>, Error> crs_of_mapping(const nc::File& file,
                                                      const nc::VarInfo& var) {
  auto parts = collect([&] { return text_of(file, var.name, "epsg_code"); },
                       [&] {
                         return double_att(file, var.name,
                                           "longitude_of_prime_meridian");
                       });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  const auto& [code, meridian] = *parts;
  if (meridian.value_or(0.0) != 0.0) {
    return invalid(FormatErrc::unsupported_crs, std::string{var.name.view()});
  }
  if (not code) {
    return crs_from_parameters(file, var);
  }
  const auto epsg = parse_epsg(core::detail::trim(*code));
  if (not epsg) {
    return invalid(FormatErrc::unsupported_crs, std::string{var.name.view()});
  }
  return Read<core::Epsg>{.value = *epsg, .warnings = {}};
}

std::expected<Read<core::Epsg>, Error> crs_of(const nc::File& file,
                                              const Structure& s) {
  auto names = grid_mappings(file, s.data);
  if (not names) {
    return std::unexpected{std::move(names).error()};
  }
  if (names->empty()) {
    return Read<core::Epsg>{.value = core::Epsg::wgs84(),
                            .warnings = {{.code = WarningCode::crs_assumed,
                                          .subject = "EPSG:4326"}}};
  }
  if (names->size() > 1) {
    return invalid(FormatErrc::unsupported_crs, subject_of(names->at(1)));
  }
  const auto var = named(s.vars, names->front());
  if (not var) {
    return invalid(FormatErrc::unsupported_crs, subject_of(names->front()));
  }
  return crs_of_mapping(file, *var);
}

/// What turns the file's lon/lat into Locations: nothing for EPSG:4326, a
/// Projector for another geographic CRS. A projected CRS is refused (SN 10.1).
std::expected<std::optional<Projector>, Error> projector_for(core::Epsg epsg) {
  if (epsg == core::Epsg::wgs84()) {
    return std::optional<Projector>{};
  }
  auto projector = Projector::make(epsg);
  if (not projector) {
    return fail(to_format_error(projector.error(), std::nullopt));
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
    return std::unexpected{std::move(rows).error()};
  }
  const std::string subject{var.name.view()};
  std::vector<core::StationKey> ids;
  ids.reserve(rows->size());
  std::set<std::string, std::less<>> seen;
  for (std::size_t i = 0; i < rows->size(); ++i) {
    std::string text = trimmed(std::move((*rows)[i]));
    if (seen.contains(text)) {
      return invalid(FormatErrc::duplicate_station_id, subject_of(text), i);
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
    return std::unexpected{std::move(rows).error()};
  }
  std::vector<core::StationText> names;
  names.reserve(rows->size());
  for (std::size_t i = 0; i < rows->size(); ++i) {
    auto text = core::StationText::make(trimmed(std::move((*rows)[i])));
    if (not text) {
      return invalid(FormatErrc::bad_encoding, std::string{var.name.view()}, i);
    }
    names.push_back(*std::move(text));
  }
  return names;
}

/// The source of each station: UTF-8 without NUL (else bad_encoding); an
/// unknown token is none, with a warning (one per token, counting its
/// stations).
std::expected<Read<std::vector<std::optional<core::DataSource>>>, Error>
station_sources(const nc::File& file, const Structure& s,
                const StopToken& stop) {
  using Sources = std::vector<std::optional<core::DataSource>>;
  Read<Sources> out{.value = Sources(s.station.length), .warnings = {}};
  if (not s.instances.provider) {
    return out;
  }
  const nc::VarInfo& var = *s.instances.provider;
  auto rows = file.read_char_rows(var.name, stop);
  if (not rows) {
    return std::unexpected{std::move(rows).error()};
  }
  for (std::size_t i = 0; i < rows->size(); ++i) {
    const std::string token = trimmed(std::move((*rows)[i]));
    if (token.find('\0') != std::string::npos or
        not core::detail::is_valid_utf8(token)) {
      return invalid(FormatErrc::bad_encoding, std::string{var.name.view()}, i);
    }
    out.value[i] = core::parse_data_source(token);
    if (out.value[i] or token.empty()) {
      continue;
    }
    const std::string subject = subject_of(token);
    const auto known =
        std::ranges::find(out.warnings, subject, &Warning::subject);
    if (known != out.warnings.end()) {
      ++known->count;
    } else {
      out.warnings.push_back(
          {.code = WarningCode::unknown_provider, .subject = subject});
    }
  }
  return out;
}

std::expected<std::vector<double>, Error> coordinate(const nc::File& file,
                                                     const nc::VarInfo& var,
                                                     const StopToken& stop) {
  auto samples = file.read_samples(var.name, nc::whole(var), stop);
  if (not samples) {
    return std::unexpected{std::move(samples).error()};
  }
  std::vector<double> values;
  values.reserve(samples->size());
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
  const auto where = projector->to_location({.x = lon, .y = lat});
  if (not native or not where) {
    return invalid(FormatErrc::bad_coordinates, "lon, lat", i);
  }
  return Position{.location = *where, .native = *native};
}

std::expected<Read<std::vector<core::FileStation>>, Error> read_stations(
    const nc::File& file, const Structure& s, core::Epsg epsg,
    const StopToken& stop) {
  auto parts =
      collect([&] { return station_ids(file, s.id, stop); },
              [&] { return station_names(file, s.instances.name, stop); },
              [&] { return coordinate(file, s.instances.lat, stop); },
              [&] { return coordinate(file, s.instances.lon, stop); },
              [&] { return station_sources(file, s, stop); },
              [&] { return projector_for(epsg); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  auto& [ids, names, lats, lons, sources, projector] = *parts;
  Read<std::vector<core::FileStation>> out{
      .value = {}, .warnings = std::move(sources.warnings)};
  out.value.reserve(ids.size());
  for (std::size_t i = 0; i < ids.size(); ++i) {
    auto position = place(lats[i], lons[i], projector, i);
    if (not position) {
      return std::unexpected{std::move(position).error()};
    }
    out.value.push_back({.id = std::move(ids[i]),
                         .name = std::move(names[i]),
                         .location = position->location,
                         .native = position->native,
                         .source = sources.value[i]});
  }
  if (projector) {
    if (auto w = projector->approximation_warning()) {
      out.warnings.push_back(*std::move(w));
    }
  }
  return out;
}

// ---- the schema (SN 4.3, 6, 10.2)
// ---------------------------------------------------

/// The quantity a v5 variable name stands for (SN 6: the name is the token):
/// a registry token, or a generic token that is a CF name the format does not
/// use itself (as the writer refuses others). Anything else is
/// invalid_variable_name.
std::expected<core::QuantityId, Error> quantity_of(
    std::string_view token, const std::optional<std::string>& standard_name) {
  if (const auto registry = core::parse_quantity_token(token)) {
    return *registry;
  }
  auto generic = core::GenericQuantity::parse(
      {.token = token, .standard_name = standard_name.value_or("")});
  if (not generic or sn::is_reserved(token)) {
    return invalid(FormatErrc::invalid_variable_name, subject_of(token));
  }
  return *std::move(generic);
}

/// The unit of `units`: a registry quantity other than `difference` needs one
/// that converts to its canonical unit (noncanonical_unit); an OtherUnit core
/// does not know warns.
std::expected<Read<std::optional<core::Unit>>, Error> unit_of(
    const core::QuantityId& q, const std::optional<std::string>& text) {
  Read<std::optional<core::Unit>> out{
      .value = text ? core::parse_unit(*text) : std::nullopt, .warnings = {}};
  const auto* registry = std::get_if<core::Quantity>(&q);
  if (const auto canonical = registry != nullptr
                                 ? core::canonical_unit(*registry)
                                 : std::nullopt) {
    if (not out.value or not core::conversion(*out.value, *canonical)) {
      return invalid(FormatErrc::noncanonical_unit,
                     std::string{core::token(q)});
    }
  }
  if (out.value) {
    const auto* other = std::get_if<core::OtherUnit>(&*out.value);
    if (other != nullptr and not core::is_canonical_other(*other)) {
      out.warnings.push_back({.code = WarningCode::unrecognized_unit,
                              .subject = subject_of(other->symbol())});
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
                            .subject = subject_of(datum.error().text)});
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

std::expected<Read<core::SeriesMeta>, Error> meta_of(const nc::File& file,
                                                     const nc::VarInfo& var) {
  const std::string_view token = var.name.view();
  auto texts =
      collect([&] { return text_of(file, var.name, "standard_name"); },
              [&] { return text_of(file, var.name, "long_name"); },
              [&] { return text_of(file, var.name, "units"); },
              [&] { return text_of(file, var.name, "vertical_datum"); });
  if (not texts) {
    return std::unexpected{std::move(texts).error()};
  }
  const auto& [standard_name, long_name, units, datum] = *texts;
  auto quantity = quantity_of(token, standard_name);
  if (not quantity) {
    return std::unexpected{std::move(quantity).error()};
  }
  auto unit = unit_of(*quantity, units);
  if (not unit) {
    return std::unexpected{std::move(unit).error()};
  }
  Read<core::SeriesMeta> meta = with_datum(
      core::SeriesMeta::make({.quantity = *std::move(quantity),
                              .label = long_name.value_or(std::string{token}),
                              .unit = std::move(unit->value)}),
      datum, token);
  meta.warnings =
      concatenated(std::move(unit->warnings), std::move(meta.warnings));
  return meta;
}

std::expected<Read<std::vector<core::SeriesMeta>>, Error> read_schema(
    const nc::File& file, std::span<const DataVar> data) {
  Read<std::vector<core::SeriesMeta>> out{.value = {}, .warnings = {}};
  out.value.reserve(data.size());
  std::set<std::string, std::less<>> tokens;
  for (const DataVar& d : data) {
    auto meta = meta_of(file, d.var);
    if (not meta) {
      return std::unexpected{std::move(meta).error()};
    }
    const std::string token{core::token(meta->value.quantity())};
    if (not tokens.insert(token).second) {
      return invalid(FormatErrc::duplicate_quantity, subject_of(token));
    }
    out.value.push_back(std::move(meta->value));
    append(out.warnings, std::move(meta->warnings));
  }
  return out;
}

// ---- the sample counts
// ----------------------------------------------------------------

/// The number of samples of each station: obs_count (SN 12.4) or the length
/// of `time`.
std::expected<std::vector<std::size_t>, Error> sample_counts(
    const nc::File& file, const Structure& s, const StopToken& stop) {
  if (not s.timing.obs_count) {
    return std::vector<std::size_t>(s.station.length, s.timing.sample.length);
  }
  const nc::VarInfo& var = *s.timing.obs_count;
  auto raw = file.read<std::int64_t>(var.name, nc::whole(var), stop);
  if (not raw) {
    return std::unexpected{std::move(raw).error()};
  }
  std::vector<std::size_t> counts;
  counts.reserve(raw->size());
  for (std::size_t i = 0; i < raw->size(); ++i) {
    const std::int64_t n = (*raw)[i];
    if (n < 0 or std::cmp_greater(n, s.timing.sample.length)) {
      return invalid(FormatErrc::bad_obs_count, std::string{var.name.view()},
                     i);
    }
    counts.push_back(static_cast<std::size_t>(n));
  }
  return counts;
}

}  // namespace

std::expected<Read<Opened>, Error> open_v5(const nc::File& file,
                                           const StopToken& stop) {
  auto header = read_header(file);
  if (not header) {
    return std::unexpected{std::move(header).error()};
  }
  auto structure = read_structure(file);
  if (not structure) {
    return std::unexpected{std::move(structure).error()};
  }
  const Structure& s = *structure;
  auto parts = collect_read(
      [&] {
        return crs_of(file, s).and_then([&](Read<core::Epsg> epsg) {
          return read_stations(file, s, epsg.value, stop)
              .transform([&](Read<std::vector<core::FileStation>> stations) {
                stations.warnings = concatenated(std::move(epsg.warnings),
                                                 std::move(stations.warnings));
                return stations;
              });
        });
      },
      [&] { return read_schema(file, s.data); },
      [&] {
        return sample_counts(file, s, stop)
            .transform(pure<std::vector<std::size_t>>);
      });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  auto& [stations, schema, counts] = parts->value;
  const V5Origin origin{.version = header->value, .layout = s.timing.layout};
  StationNcCatalog catalog{
      .origin = origin, .stations = {}, .schema = std::move(schema)};
  catalog.stations.reserve(stations.size());
  for (std::size_t i = 0; i < stations.size(); ++i) {
    catalog.stations.push_back(
        {.station = std::move(stations[i]), .samples = counts[i]});
  }
  return Read<Opened>{.value = {.structure = *std::move(structure),
                                .origin = origin,
                                .catalog = std::move(catalog)},
                      .warnings = concatenated(std::move(header->warnings),
                                               std::move(parts->warnings))};
}

}  // namespace mov::io::detail::station_nc
