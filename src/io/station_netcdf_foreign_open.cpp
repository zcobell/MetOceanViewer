// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Opening a foreign CF discrete-sampling-geometry `timeSeries` file (docs/
// station-netcdf.md section 12, the "Foreign" column, and CF 9): which
// variable is the station id, the position, the time and the data, which of
// the five representations of CF 9.3 the samples are in, and everything a
// catalog holds. The schema (quantities, units, datums, quality flags) is
// station_netcdf_foreign_schema.cpp's, the samples
// station_netcdf_foreign_read.cpp's.
//
// Variables are found by what CF says they are: `cf_role`, `standard_name`,
// `units`, `axis`, `coordinates`, `sample_dimension` and `instance_dimension`.
// Three conventions of the wild are found by name where CF has no attribute:
// a station name variable called `station_name` (when none has the standard
// name `platform_name`), the `obs_count` helper of the incomplete layout, and
// the variable names themselves, which become the tokens of generic quantities.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "model_netcdf.hpp"
#include "mov/core/detail/ascii.hpp"
#include "mov/core/detail/overloaded.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/station.hpp"
#include "mov/io/cf_time.hpp"
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
#include "station_netcdf_foreign_facts.hpp"
#include "station_netcdf_format.hpp"
#include "station_netcdf_reader.hpp"

namespace mov::io::detail::station_nc {

CfDsgLayout layout_of(const ForeignSampling& sampling) {
  return std::visit(
      core::detail::Overloaded{
          [](const Orthogonal&) { return CfDsgLayout::orthogonal; },
          [](const Incomplete&) { return CfDsgLayout::incomplete; },
          [](const ContiguousRagged&) {
            return CfDsgLayout::contiguous_ragged;
          },
          [](const IndexedRagged&) { return CfDsgLayout::indexed_ragged; },
          [](const SingleStation&) { return CfDsgLayout::single_station; }},
      sampling);
}

std::optional<nc::DimInfo> station_dim_of(const ForeignSampling& sampling) {
  return std::visit(core::detail::Overloaded{
                        [](const SingleStation&) -> std::optional<nc::DimInfo> {
                          return std::nullopt;
                        },
                        [](const auto& layout) -> std::optional<nc::DimInfo> {
                          return layout.station;
                        }},
                    sampling);
}

namespace {

using core::detail::Overloaded;

// ---- what each variable is
// ---------------------------------------------------

bool among(const std::optional<std::string>& text,
           std::span<const std::string_view> values) {
  return std::ranges::any_of(
      values, [&text](std::string_view v) { return says(text, v); });
}

constexpr std::array<std::string_view, 6> north_units{
    "degrees_north", "degree_north", "degree_N",
    "degrees_N",     "degreeN",      "degreesN"};
constexpr std::array<std::string_view, 6> east_units{
    "degrees_east", "degree_east", "degree_E",
    "degrees_E",    "degreeE",     "degreesE"};

bool numeric(const nc::VarInfo& v) { return nc::sample_readable(v.type); }

bool is_text(const nc::VarInfo& v) {
  return v.type == nc::Type::char_ or v.type == nc::Type::string;
}

bool is_latitude(const Facts& f) {
  return numeric(f.var) and
         (says(f.standard_name, "latitude") or among(f.units, north_units));
}

bool is_longitude(const Facts& f) {
  return numeric(f.var) and
         (says(f.standard_name, "longitude") or among(f.units, east_units));
}

/// A time variable: a CF time `units` ("<unit> since ..."), or the standard
/// name `time` or `axis` T.
bool is_time(const Facts& f) {
  const bool readable = numeric(f.var) or f.var.type == nc::Type::int64;
  if (not readable) {
    return false;
  }
  if (says(f.standard_name, "time") or says(f.axis, "T")) {
    return true;
  }
  return f.units.has_value() and parse_cf_time_units(*f.units).has_value();
}

bool over(const nc::VarInfo& v, std::initializer_list<int> dim_ids) {
  return std::ranges::equal(v.dims, dim_ids, {}, &nc::DimInfo::id);
}

bool uses(const nc::VarInfo& v, int dim_id) {
  return std::ranges::any_of(
      v.dims, [dim_id](const nc::DimInfo& d) { return d.id == dim_id; });
}

/// The names the `coordinates` attributes of the file list.
Names coordinate_names(std::span<const Facts> all) {
  Names names;
  for (const Facts& f : all) {
    for (std::string& w : words(f.coordinates)) {
      names.insert(std::move(w));
    }
  }
  return names;
}

/// The names `coordinates`, `bounds` and `grid_mapping` attributes point at.
Names referenced_names(std::span<const Facts> all) {
  Names names;
  for (const Facts& f : all) {
    for (const auto* text : {&f.coordinates, &f.bounds}) {
      for (std::string& w : words(*text)) {
        names.insert(std::move(w));
      }
    }
    if (auto mapping = mapping_name(f)) {
      names.insert(*std::move(mapping));
    }
  }
  return names;
}

/// The candidates of a position role: the one in the `coordinates` of some
/// variable, else the first.
const Facts* pick(const std::vector<const Facts*>& candidates,
                  const Names& referenced) {
  for (const Facts* c : candidates) {
    if (referenced.contains(c->var.name.view())) {
      return c;
    }
  }
  return candidates.empty() ? nullptr : candidates.front();
}

/// `a, b`: the names of two variables that compete for one role.
std::string pair_subject(const nc::VarInfo& a, const nc::VarInfo& b) {
  return subject_of(std::string{a.name.view()} + ", " +
                    std::string{b.name.view()});
}

// ---- the station dimension
// ---------------------------------------------------

/// What the id variable says about the stations: nothing yet (the file has
/// none), one station (a scalar string, one char row), or the dimension.
struct Undecided {};
struct OneStation {};
using StationAxis = std::variant<Undecided, OneStation, nc::DimInfo>;

std::expected<void, Error> id_type_ok(const nc::VarInfo& id) {
  const bool ok = is_text(id) or id.type == nc::Type::byte or
                  id.type == nc::Type::short_ or id.type == nc::Type::int_ or
                  id.type == nc::Type::int64;
  if (not ok) {
    return invalid(FormatErrc::bad_encoding, subject_of(id.name.view()));
  }
  return {};
}

/// The station dimension an id variable implies.
std::expected<StationAxis, Error> axis_of_id(const nc::VarInfo& id) {
  if (auto ok = id_type_ok(id); not ok) {
    return std::unexpected{std::move(ok).error()};
  }
  const std::size_t rank = id.dims.size();
  const std::size_t one_station_rank = id.type == nc::Type::char_ ? 1 : 0;
  if (rank == one_station_rank) {
    return StationAxis{OneStation{}};
  }
  if (rank == one_station_rank + 1) {
    return StationAxis{id.dims.front()};
  }
  return invalid(FormatErrc::dimension_mismatch, subject_of(id.name.view()));
}

/// The one variable whose `cf_role` is timeseries_id (CF 9.5), or null. Two
/// are `ambiguous_station_id`, naming both.
std::expected<const Facts*, Error> find_id(std::span<const Facts> all) {
  const Facts* found = nullptr;
  for (const Facts& f : all) {
    if (not says(f.cf_role, "timeseries_id")) {
      continue;
    }
    if (found != nullptr) {
      return invalid(FormatErrc::ambiguous_station_id,
                     pair_subject(found->var, f.var));
    }
    found = &f;
  }
  return found;
}

bool fits_axis(const nc::VarInfo& v, const StationAxis& axis) {
  return std::visit(
      Overloaded{[](Undecided) { return true; },
                 [&v](OneStation) { return v.dims.empty(); },
                 [&v](const nc::DimInfo& d) { return over(v, {d.id}); }},
      axis);
}

/// The candidates for latitude or longitude: numeric, scalar or over the
/// station dimension once that is known.
std::vector<const Facts*> positions(std::span<const Facts> all,
                                    bool (*test)(const Facts&),
                                    const StationAxis& axis) {
  std::vector<const Facts*> out;
  for (const Facts& f : all) {
    if (test(f) and f.var.dims.size() <= 1 and fits_axis(f.var, axis)) {
      out.push_back(&f);
    }
  }
  return out;
}

/// Latitude, longitude and the station dimension (nullopt: one station).
struct Positions {
  const Facts* lat;
  const Facts* lon;
  std::optional<nc::DimInfo> station;
};

std::expected<Positions, Error> find_positions(std::span<const Facts> all,
                                               const StationAxis& axis,
                                               const Names& coordinate_set) {
  const Facts* lat = pick(positions(all, is_latitude, axis), coordinate_set);
  const Facts* lon = pick(positions(all, is_longitude, axis), coordinate_set);
  if (lat == nullptr) {
    return invalid(FormatErrc::missing_variable, "latitude");
  }
  if (lon == nullptr) {
    return invalid(FormatErrc::missing_variable, "longitude");
  }
  if (const auto* dim = std::get_if<nc::DimInfo>(&axis)) {
    return Positions{.lat = lat, .lon = lon, .station = *dim};
  }
  if (std::holds_alternative<OneStation>(axis)) {
    return Positions{.lat = lat, .lon = lon, .station = std::nullopt};
  }
  // No id variable: the positions say whether there are several stations.
  const bool same_shape =
      lat->var.dims.size() == lon->var.dims.size() and
      (lat->var.dims.empty() or lat->var.dims[0].id == lon->var.dims[0].id);
  if (not same_shape) {
    return invalid(FormatErrc::dimension_mismatch,
                   subject_of(lon->var.name.view()));
  }
  return Positions{.lat = lat,
                   .lon = lon,
                   .station = lat->var.dims.empty()
                                  ? std::nullopt
                                  : std::optional{lat->var.dims.front()}};
}

// ---- the time variable
// -------------------------------------------------------

/// How firmly a variable is the time of the file, best first (CF 4.4): it is
/// the coordinate variable of its dimension; some variable lists it in
/// `coordinates`; it has the standard name `time` or axis T; or only its units
/// are a time's.
enum class TimeRank : std::uint8_t {
  coordinate_variable,
  listed,
  labelled,
  units_only,
};

TimeRank rank_of(const Facts& f, const Names& coordinate_set) {
  if (f.var.dims.size() == 1 and
      f.var.dims[0].name.view() == f.var.name.view()) {
    return TimeRank::coordinate_variable;
  }
  if (coordinate_set.contains(f.var.name.view())) {
    return TimeRank::listed;
  }
  if (says(f.standard_name, "time") or says(f.axis, "T")) {
    return TimeRank::labelled;
  }
  return TimeRank::units_only;
}

/// Whether the shape of `v` can be that of a time variable: a scalar is a
/// deployment time, a vector over the stations an instance variable.
bool shaped_like_time(const nc::VarInfo& v,
                      const std::optional<nc::DimInfo>& station) {
  if (v.dims.empty()) {
    return false;
  }
  return not station or v.dims.size() != 1 or v.dims[0].id != station->id;
}

/// The time variable: of the variables that can be one, the best ranked; two of
/// the same rank are `unsupported_layout` naming both.
std::expected<const Facts*, Error> find_time(
    std::span<const Facts> all, const Names& coordinate_set,
    const std::optional<nc::DimInfo>& station) {
  Names bounds;
  for (const Facts& f : all) {
    for (std::string& w : words(f.bounds)) {
      bounds.insert(std::move(w));
    }
  }
  const Facts* best = nullptr;
  const Facts* rival = nullptr;
  for (const Facts& f : all) {
    if (not is_time(f) or bounds.contains(f.var.name.view()) or
        not shaped_like_time(f.var, station)) {
      continue;
    }
    if (best == nullptr or
        rank_of(f, coordinate_set) < rank_of(*best, coordinate_set)) {
      best = &f;
      rival = nullptr;
    } else if (rank_of(f, coordinate_set) == rank_of(*best, coordinate_set) and
               rival == nullptr) {
      rival = &f;
    }
  }
  if (best == nullptr) {
    return invalid(FormatErrc::missing_variable, "time");
  }
  if (rival != nullptr) {
    return invalid(FormatErrc::unsupported_layout,
                   pair_subject(best->var, rival->var));
  }
  return best;
}

// ---- the layout
// --------------------------------------------------------------

struct Located {
  ForeignSampling sampling;
  nc::DimInfo sample;
};

/// The variable with `sample_dimension` = `dim` (CF 9.3.3).
const Facts* counting(std::span<const Facts> all, const nc::DimInfo& dim) {
  for (const Facts& f : all) {
    if (says(f.sample_dimension, dim.name.view()) and f.var.dims.size() == 1) {
      return &f;
    }
  }
  return nullptr;
}

/// The variable with an `instance_dimension` over `dim` (CF 9.3.4).
const Facts* indexing(std::span<const Facts> all, const nc::DimInfo& dim) {
  for (const Facts& f : all) {
    if (f.instance_dimension.has_value() and over(f.var, {dim.id})) {
      return &f;
    }
  }
  return nullptr;
}

std::expected<nc::DimInfo, Error> dimension_named(const nc::File& file,
                                                  std::string_view name) {
  const auto ref = nc::NcName::make(name);
  if (not ref) {
    return invalid(FormatErrc::missing_dimension, subject_of(name));
  }
  return require_dim(file, *ref);
}

std::expected<nc::DimInfo, Error> same_station(
    const std::optional<nc::DimInfo>& known, nc::DimInfo found,
    const nc::VarInfo& by) {
  if (known and known->id != found.id) {
    return invalid(FormatErrc::dimension_mismatch, subject_of(by.name.view()));
  }
  return found;
}

/// `time(station, obs)` or `time(obs, station)` (CF 9.3.2).
std::expected<Located, Error> locate_incomplete(
    std::span<const Facts> all, const nc::VarInfo& time,
    const std::optional<nc::DimInfo>& station) {
  if (not station) {
    return invalid(FormatErrc::unsupported_layout,
                   subject_of(time.name.view()));
  }
  const bool station_first = time.dims[0].id == station->id;
  const bool station_second = time.dims[1].id == station->id;
  if (station_first == station_second) {
    return invalid(FormatErrc::unsupported_layout,
                   subject_of(time.name.view()));
  }
  std::optional<nc::VarInfo> obs_count;
  for (const Facts& f : all) {
    if (f.var.name.view() == "obs_count" and over(f.var, {station->id})) {
      obs_count = f.var;
    }
  }
  return Located{
      .sampling = Incomplete{.station = *station, .obs_count = obs_count},
      .sample = time.dims[station_first ? 1 : 0]};
}

/// The ragged layouts (CF 9.3.3, 9.3.4) when the file has the variable that
/// makes the samples ragged; nullopt when it has neither.
std::expected<std::optional<ForeignSampling>, Error> locate_ragged(
    const nc::File& file, std::span<const Facts> all, const nc::DimInfo& sample,
    const std::optional<nc::DimInfo>& station) {
  const Facts* counts = counting(all, sample);
  const Facts* indices = indexing(all, sample);
  if (counts != nullptr and indices != nullptr) {
    return invalid(FormatErrc::unsupported_layout,
                   subject_of(sample.name.view()));
  }
  if (counts != nullptr) {
    return same_station(station, counts->var.dims.front(), counts->var)
        .transform([&](nc::DimInfo dim) {
          return std::optional<ForeignSampling>{ContiguousRagged{
              .station = std::move(dim), .row_size = counts->var}};
        });
  }
  if (indices == nullptr) {
    return std::optional<ForeignSampling>{};
  }
  return dimension_named(file, indices->instance_dimension.value_or(""))
      .and_then([&](nc::DimInfo named_dim) {
        return same_station(station, std::move(named_dim), indices->var);
      })
      .transform([&](nc::DimInfo dim) {
        return std::optional<ForeignSampling>{
            IndexedRagged{.station = std::move(dim), .index = indices->var}};
      });
}

/// `time(time)`: orthogonal, ragged, or a single station.
std::expected<Located, Error> locate_coordinate(
    const nc::File& file, std::span<const Facts> all, const nc::VarInfo& time,
    const std::optional<nc::DimInfo>& station) {
  const nc::DimInfo sample = time.dims.front();
  auto ragged = locate_ragged(file, all, sample, station);
  if (not ragged) {
    return std::unexpected{std::move(ragged).error()};
  }
  if (*ragged) {
    return Located{.sampling = **std::move(ragged), .sample = sample};
  }
  if (not station) {
    return Located{.sampling = SingleStation{}, .sample = sample};
  }
  if (station->id == sample.id) {
    return invalid(FormatErrc::unsupported_layout,
                   subject_of(time.name.view()));
  }
  return Located{.sampling = Orthogonal{.station = *station}, .sample = sample};
}

/// How the time variable lays out the samples (CF 9.3); `station` is the
/// station dimension, nullopt for one station.
std::expected<Located, Error> locate(
    const nc::File& file, std::span<const Facts> all, const nc::VarInfo& time,
    const std::optional<nc::DimInfo>& station) {
  switch (time.dims.size()) {
    case 1:
      return locate_coordinate(file, all, time, station);
    case 2:
      return locate_incomplete(all, time, station);
    default:
      return invalid(FormatErrc::unsupported_layout,
                     subject_of(time.name.view()));
  }
}

// ---- the roles
// ---------------------------------------------------------------

/// The variables of the file that have a role besides data, found once.
struct Roles {
  const Facts* id;  // null: the file has no cf_role variable
  const Facts* lat;
  const Facts* lon;
  const Facts* time;
  Located located;
};

/// Whether a text variable of this shape names the stations of `station`
/// (nullopt: a single station).
bool text_fits(const nc::VarInfo& v,
               const std::optional<nc::DimInfo>& station) {
  const std::size_t one_station_rank = v.type == nc::Type::char_ ? 1 : 0;
  if (not station) {
    return v.dims.size() == one_station_rank;
  }
  return v.dims.size() == one_station_rank + 1 and
         v.dims.front().id == station->id;
}

std::expected<Roles, Error> identify(const nc::File& file,
                                     std::span<const Facts> all,
                                     const Names& coordinate_set) {
  auto id = find_id(all);
  if (not id) {
    return std::unexpected{std::move(id).error()};
  }
  auto axis = *id != nullptr ? axis_of_id((*id)->var)
                             : std::expected<StationAxis, Error>{Undecided{}};
  if (not axis) {
    return std::unexpected{std::move(axis).error()};
  }
  auto where = find_positions(all, *axis, coordinate_set);
  if (not where) {
    return std::unexpected{std::move(where).error()};
  }
  auto time = find_time(all, coordinate_set, where->station);
  if (not time) {
    return std::unexpected{std::move(time).error()};
  }
  auto located = locate(file, all, (*time)->var, where->station);
  if (not located) {
    return std::unexpected{std::move(located).error()};
  }
  // lat and lon are over the stations (or scalar for one station).
  const std::optional<nc::DimInfo> station = station_dim_of(located->sampling);
  for (const Facts* c : {where->lat, where->lon}) {
    const bool fits =
        station ? over(c->var, {station->id}) : c->var.dims.empty();
    if (not fits) {
      return invalid(FormatErrc::dimension_mismatch,
                     subject_of(c->var.name.view()));
    }
  }
  return Roles{.id = *id,
               .lat = where->lat,
               .lon = where->lon,
               .time = *time,
               .located = *std::move(located)};
}

// ---- the data variables
// ------------------------------------------------------

/// Whether `v` has the dimensions of a data variable of this layout.
bool fits_layout(const nc::VarInfo& v, const Located& l) {
  // A matrix layout has the station and sample dimensions in either order.
  const auto matrix = [&v, &l](const nc::DimInfo& station) {
    return over(v, {station.id, l.sample.id}) or
           over(v, {l.sample.id, station.id});
  };
  if (const auto* orthogonal = std::get_if<Orthogonal>(&l.sampling)) {
    return matrix(orthogonal->station);
  }
  if (const auto* incomplete = std::get_if<Incomplete>(&l.sampling)) {
    return matrix(incomplete->station);
  }
  return over(v, {l.sample.id});
}

/// Whether `v` has anything to do with the stations or the samples.
bool concerns(const nc::VarInfo& v, const Located& l) {
  const std::optional<nc::DimInfo> station = station_dim_of(l.sampling);
  return uses(v, l.sample.id) or (station and uses(v, station->id));
}

std::expected<bool, Error> says_unsigned(const nc::File& file,
                                         const nc::VarInfo& v) {
  return text_of(file, v.name, "_Unsigned")
      .transform([](const std::optional<std::string>& text) {
        return says(text, "true");
      });
}

/// A text variable that is called the station name, by standard name or by
/// the name `station_name`.
bool claims_name(const Facts& f) {
  return is_text(f.var) and (says(f.standard_name, "platform_name") or
                             f.var.name.view() == "station_name");
}

/// The variable that names the stations: of the text variables over the
/// station dimension (or one string for a single station), the one with the
/// standard name `platform_name`, else the one called `station_name`.
const Facts* find_name(std::span<const Facts> all, const Facts* id,
                       const std::optional<nc::DimInfo>& station) {
  const auto fitting = [&](const Facts& f) {
    return &f != id and claims_name(f) and text_fits(f.var, station);
  };
  const auto standard = std::ranges::find_if(all, [&](const Facts& f) {
    return fitting(f) and says(f.standard_name, "platform_name");
  });
  if (standard != all.end()) {
    return &*standard;
  }
  const auto named_one = std::ranges::find_if(all, fitting);
  return named_one == all.end() ? nullptr : &*named_one;
}

/// What the search for data variables makes of a variable.
enum class Verdict : std::uint8_t { ignored, skipped, candidate };

struct Excluded {
  std::set<int> structural;  // ids of the variables with another role
  Names coordinates;         // the names `coordinates` lists
  Names referenced;          // ... and `bounds`, `grid_mapping`
};

/// A variable that is a coordinate, a name, an instance variable or has
/// nothing to do with the samples is `ignored`; one that concerns the samples
/// but cannot be read as a series, or a name variable that is not used, is
/// `skipped` (with a warning).
std::expected<Verdict, Error> verdict_on(const nc::File& file, const Facts& f,
                                         const Located& l,
                                         const Excluded& excluded) {
  const nc::VarInfo& v = f.var;
  if (excluded.structural.contains(v.id) or
      excluded.coordinates.contains(v.name.view()) or
      excluded.referenced.contains(v.name.view())) {
    return Verdict::ignored;
  }
  if (claims_name(f)) {
    return Verdict::skipped;  // not the name variable: wrong shape, or a second
  }
  if (not concerns(v, l) or is_text(v)) {
    return Verdict::ignored;  // names, ids: not a series
  }
  const std::optional<nc::DimInfo> station = station_dim_of(l.sampling);
  if (station and over(v, {station->id})) {
    return Verdict::ignored;  // an instance variable (an altitude, a count)
  }
  const auto unsigned_flag = says_unsigned(file, v);
  if (not unsigned_flag) {
    return std::unexpected{unsigned_flag.error()};
  }
  return fits_layout(v, l) and numeric(v) and not *unsigned_flag
             ? Verdict::candidate
             : Verdict::skipped;
}

Warning skipped(const std::string& subject) {
  return {.code = WarningCode::skipped_variable,
          .subject = subject_of(subject)};
}

/// A variable `ancillary_variables` links to a series and that is whole
/// numbers of the series' shape: its values are quality flags.
bool flag_shaped(const nc::VarInfo& v) {
  return v.type == nc::Type::byte or v.type == nc::Type::short_ or
         v.type == nc::Type::int_;
}

/// nullopt when the masking attributes of `var` can be read; else the subject
/// (`var:attribute`) of the fault.
std::expected<std::optional<std::string>, Error> masking_fault(
    const nc::File& file, const nc::VarInfo& var) {
  auto checked = dispatch_model_numeric(
      file, var, [&]<class T>() -> std::expected<void, Error> {
        auto mask = file.masking<T>(var.name);
        if (not mask) {
          return fail(std::move(mask).error());
        }
        return {};
      });
  if (checked) {
    return std::optional<std::string>{};
  }
  const auto* fault = std::get_if<NcError>(&checked.error());
  if (fault == nullptr) {
    return std::unexpected{std::move(checked).error()};
  }
  return std::optional{subject_of(fault->object)};
}

/// The data variables and, per data variable, its quality-flag variables.
struct DataSearch {
  std::vector<DataFacts> data;
  std::vector<Warning> warnings;
};

/// Splits the candidates into data variables and the quality variables their
/// `ancillary_variables` name; an ancillary variable that is no flag is not a
/// series (skipped).
DataSearch split_ancillary(std::span<const Facts* const> candidates,
                           std::vector<Warning> warnings) {
  Names ancillary;
  for (const Facts* c : candidates) {
    for (std::string& w : words(c->ancillary_variables)) {
      ancillary.insert(std::move(w));
    }
  }
  DataSearch out{.data = {}, .warnings = std::move(warnings)};
  Names flags;
  for (const Facts* c : candidates) {
    if (not ancillary.contains(c->var.name.view())) {
      out.data.push_back({.facts = c, .quality = {}});
    } else if (flag_shaped(c->var)) {
      flags.insert(std::string{c->var.name.view()});
    } else {
      out.warnings.push_back(skipped(std::string{c->var.name.view()}));
    }
  }
  for (DataFacts& d : out.data) {
    for (const std::string& w : words(d.facts->ancillary_variables)) {
      if (flags.contains(w)) {
        const auto flag = std::ranges::find_if(
            candidates,
            [&w](const Facts* c) { return c->var.name.view() == w; });
        d.quality.push_back(*flag);
      }
    }
  }
  return out;
}

/// A variable whose masking attributes cannot be read is skipped, with the
/// attribute as the subject; quality variables with such a fault are dropped
/// from the series they flag.
std::expected<void, Error> drop_unmaskable(const nc::File& file,
                                           DataSearch& search) {
  std::vector<DataFacts> kept;
  for (DataFacts& d : search.data) {
    auto fault = masking_fault(file, d.facts->var);
    if (not fault) {
      return std::unexpected{std::move(fault).error()};
    }
    if (*fault) {
      search.warnings.push_back(skipped(**fault));
      continue;
    }
    std::vector<const Facts*> flags;
    for (const Facts* q : d.quality) {
      auto flag_fault = masking_fault(file, q->var);
      if (not flag_fault) {
        return std::unexpected{std::move(flag_fault).error()};
      }
      if (*flag_fault) {
        search.warnings.push_back(skipped(**flag_fault));
      } else {
        flags.push_back(q);
      }
    }
    d.quality = std::move(flags);
    kept.push_back(std::move(d));
  }
  search.data = std::move(kept);
  return {};
}

std::expected<DataSearch, Error> find_data(const nc::File& file,
                                           std::span<const Facts> all,
                                           const Roles& roles,
                                           const Facts* name,
                                           const Names& coordinate_set) {
  Excluded excluded{
      .structural = {roles.time->var.id, roles.lat->var.id, roles.lon->var.id},
      .coordinates = coordinate_set,
      .referenced = referenced_names(all)};
  if (roles.id != nullptr) {
    excluded.structural.insert(roles.id->var.id);
  }
  if (name != nullptr) {
    excluded.structural.insert(name->var.id);
  }
  std::visit(Overloaded{[&excluded](const Incomplete& l) {
                          if (l.obs_count) {
                            excluded.structural.insert(l.obs_count->id);
                          }
                        },
                        [&excluded](const ContiguousRagged& l) {
                          excluded.structural.insert(l.row_size.id);
                        },
                        [&excluded](const IndexedRagged& l) {
                          excluded.structural.insert(l.index.id);
                        },
                        [](const auto&) {}},
             roles.located.sampling);
  std::vector<const Facts*> candidates;
  std::vector<Warning> warnings;
  for (const Facts& f : all) {
    const auto verdict = verdict_on(file, f, roles.located, excluded);
    if (not verdict) {
      return std::unexpected{verdict.error()};
    }
    if (*verdict == Verdict::skipped) {
      warnings.push_back(skipped(std::string{f.var.name.view()}));
    } else if (*verdict == Verdict::candidate) {
      candidates.push_back(&f);
    }
  }
  DataSearch search = split_ancillary(candidates, std::move(warnings));
  if (auto ok = drop_unmaskable(file, search); not ok) {
    return std::unexpected{std::move(ok).error()};
  }
  if (search.data.empty()) {
    return invalid(FormatErrc::no_data_variables, "");
  }
  return search;
}

// ---- the samples of each station
// ---------------------------------------------

/// An integer variable as int64 values, with the ones its own attributes mask
/// (a _FillValue or missing_value) flagged.
struct Ints {
  std::vector<std::int64_t> values;
  std::vector<bool> masked;
};

std::expected<Ints, Error> read_ints(const nc::File& file,
                                     const nc::VarInfo& var,
                                     const StopToken& stop) {
  auto raw = file.read<std::int64_t>(var.name, nc::whole(var), stop);
  if (not raw) {
    return std::unexpected{std::move(raw).error()};
  }
  Ints out{.values = *std::move(raw), .masked = {}};
  out.masked.assign(out.values.size(), false);
  auto done = nc::dispatch_numeric(
      var.type,
      [&]<class T>() -> std::expected<void, Error> {
        auto mask = file.masking<T>(var.name);
        if (not mask) {
          return fail(std::move(mask).error());
        }
        for (std::size_t i = 0; i < out.values.size(); ++i) {
          // Every value was read from a T: the cast is exact.
          out.masked[i] = mask->masks(static_cast<T>(out.values[i]));
        }
        return {};
      },
      [&]() -> std::expected<void, Error> {
        return fail(nc_fault(file, WrapperFault::type_mismatch, NcOp::get_var,
                             var.name.view()));
      });
  if (not done) {
    return std::unexpected{std::move(done).error()};
  }
  return out;
}

/// Contiguous ragged: the count of each station, and where its samples start.
struct Runs {
  std::vector<std::size_t> counts;
  std::vector<std::size_t> starts;
};

std::expected<Runs, Error> runs_of(const nc::File& file,
                                   const nc::VarInfo& row_size,
                                   std::size_t samples, const StopToken& stop) {
  auto raw = read_ints(file, row_size, stop);
  if (not raw) {
    return std::unexpected{std::move(raw).error()};
  }
  Runs out;
  out.counts.reserve(raw->values.size());
  out.starts.reserve(raw->values.size());
  std::size_t total = 0;
  for (std::size_t i = 0; i < raw->values.size(); ++i) {
    const std::int64_t n = raw->values[i];
    if (n < 0 or raw->masked[i] or
        std::cmp_greater(n, samples - std::min(total, samples))) {
      return invalid(FormatErrc::bad_row_size, subject_of(row_size.name.view()),
                     i);
    }
    out.starts.push_back(total);
    out.counts.push_back(static_cast<std::size_t>(n));
    total += static_cast<std::size_t>(n);
  }
  if (total != samples) {
    return invalid(FormatErrc::bad_row_size, subject_of(row_size.name.view()));
  }
  return out;
}

/// Indexed ragged: the station of every sample, and each station's count.
struct Owners {
  std::vector<std::size_t> station_of_sample;
  std::vector<std::size_t> counts;
};

std::expected<Owners, Error> owners_of(const nc::File& file,
                                       const nc::VarInfo& index,
                                       std::size_t stations,
                                       const StopToken& stop) {
  auto raw = read_ints(file, index, stop);
  if (not raw) {
    return std::unexpected{std::move(raw).error()};
  }
  Owners out;
  out.station_of_sample.reserve(raw->values.size());
  out.counts.assign(stations, 0);
  for (std::size_t o = 0; o < raw->values.size(); ++o) {
    const std::int64_t s = raw->values[o];
    if (s < 0 or raw->masked[o] or std::cmp_greater_equal(s, stations)) {
      return invalid(FormatErrc::bad_ragged_index,
                     subject_of(index.name.view()), std::nullopt, o);
    }
    out.station_of_sample.push_back(static_cast<std::size_t>(s));
    ++out.counts[static_cast<std::size_t>(s)];
  }
  return out;
}

/// Incomplete layout without `obs_count`: the number of leading non-missing
/// times of each station; a time after a missing one is padding_not_missing.
template <nc::Numeric T>
std::expected<std::vector<std::size_t>, Error> leading_counts(
    const nc::File& file, const nc::VarInfo& time, const nc::DimInfo& station,
    std::size_t length, const StopToken& stop) {
  auto mask = file.masking<T>(time.name);
  if (not mask) {
    return fail(std::move(mask).error());
  }
  const bool station_major = time.dims.front().id == station.id;
  std::vector<std::size_t> counts(station.length, 0);
  std::vector<bool> ended(station.length, false);
  const auto step = [&](std::size_t s, std::size_t j,
                        T x) -> std::expected<void, Error> {
    if (mask->apply(x).is_missing()) {
      ended[s] = true;
    } else if (ended[s]) {
      return invalid(FormatErrc::padding_not_missing,
                     subject_of(time.name.view()), s, j);
    } else {
      ++counts[s];
    }
    return {};
  };
  auto done = file.read_blocks<T>(
      time.name, nc::whole(time),
      [&](std::span<const T> block,
          nc::DimRange outer) -> std::expected<void, Error> {
        const std::size_t width = station_major ? length : station.length;
        for (std::size_t r = 0; r < outer.count; ++r) {
          const std::size_t row = outer.start + r;
          for (std::size_t c = 0; c < width; ++c) {
            const std::size_t s = station_major ? row : c;
            const std::size_t j = station_major ? c : row;
            if (auto ok = step(s, j, block[(r * width) + c]); not ok) {
              return ok;
            }
          }
        }
        return {};
      },
      stop);
  if (not done) {
    return std::unexpected{std::move(done).error()};
  }
  return counts;
}

std::expected<std::vector<std::size_t>, Error> counts_from_obs_count(
    const nc::File& file, const nc::VarInfo& obs_count, std::size_t length,
    const StopToken& stop) {
  auto raw = read_ints(file, obs_count, stop);
  if (not raw) {
    return std::unexpected{std::move(raw).error()};
  }
  std::vector<std::size_t> counts;
  counts.reserve(raw->values.size());
  for (std::size_t i = 0; i < raw->values.size(); ++i) {
    const std::int64_t n = raw->values[i];
    if (n < 0 or raw->masked[i] or std::cmp_greater(n, length)) {
      return invalid(FormatErrc::bad_obs_count,
                     subject_of(obs_count.name.view()), i);
    }
    counts.push_back(static_cast<std::size_t>(n));
  }
  return counts;
}

std::expected<std::vector<std::size_t>, Error> incomplete_counts(
    const nc::File& file, const Incomplete& layout, const nc::VarInfo& time,
    std::size_t length, const StopToken& stop) {
  if (layout.obs_count) {
    return counts_from_obs_count(file, *layout.obs_count, length, stop);
  }
  if (auto fits = check_result_size(file, time.name.view(),
                                    layout.station.length, length, 1);
      not fits) {
    return std::unexpected{std::move(fits).error()};
  }
  std::vector<std::size_t> counts;
  auto done = dispatch_role_numeric(
      RowRole::times, file, time, [&]<class T>() -> std::expected<void, Error> {
        auto leading =
            leading_counts<T>(file, time, layout.station, length, stop);
        if (not leading) {
          return std::unexpected{std::move(leading).error()};
        }
        counts = *std::move(leading);
        return {};
      });
  if (not done) {
    return std::unexpected{std::move(done).error()};
  }
  return counts;
}

/// The samples of each station and where they are.
struct Counted {
  std::vector<std::size_t> counts;
  Placement placement;
};

std::expected<Counted, Error> count_samples(const nc::File& file,
                                            const Located& l,
                                            const nc::VarInfo& time,
                                            const StopToken& stop) {
  const std::size_t samples = l.sample.length;
  return std::visit(
      Overloaded{
          [&](const Orthogonal& layout) -> std::expected<Counted, Error> {
            return Counted{.counts = std::vector<std::size_t>(
                               layout.station.length, samples),
                           .placement = Matrix{.station = layout.station}};
          },
          [&](const SingleStation&) -> std::expected<Counted, Error> {
            return Counted{.counts = {samples},
                           .placement = RunStarts{.starts = {0}}};
          },
          [&](const Incomplete& layout) -> std::expected<Counted, Error> {
            return incomplete_counts(file, layout, time, samples, stop)
                .transform([&layout](std::vector<std::size_t> counts) {
                  return Counted{
                      .counts = std::move(counts),
                      .placement = Matrix{.station = layout.station}};
                });
          },
          [&](const ContiguousRagged& layout) -> std::expected<Counted, Error> {
            return runs_of(file, layout.row_size, samples, stop)
                .transform([](Runs runs) {
                  return Counted{
                      .counts = std::move(runs.counts),
                      .placement = RunStarts{.starts = std::move(runs.starts)}};
                });
          },
          [&](const IndexedRagged& layout) -> std::expected<Counted, Error> {
            return owners_of(file, layout.index, layout.station.length, stop)
                .transform([](Owners owners) {
                  return Counted{
                      .counts = std::move(owners.counts),
                      .placement = SampleOwners{.station_of_sample = std::move(
                                                    owners.station_of_sample)}};
                });
          }},
      l.sampling);
}

// ---- stations
// ----------------------------------------------------------------

struct Cleaned {
  std::string text;
  bool replaced;
};

Cleaned cleaned_text(std::string_view raw) {
  const std::string_view trimmed_text = core::detail::trim(cut_at_nul(raw));
  auto cleaned = replace_invalid_utf8(trimmed_text);
  return {.text = std::string{cleaned.text.view()},
          .replaced = cleaned.replaced};
}

/// The strings of a char or NC_STRING variable, one per station (a scalar or
/// a single char row is one).
std::expected<std::vector<std::string>, Error> texts_of(const nc::File& file,
                                                        const nc::VarInfo& var,
                                                        const StopToken& stop) {
  if (var.type == nc::Type::string) {
    return file.read_strings(var.name, stop);
  }
  return file.read_char_rows(var.name, stop);
}

/// Integer ids as decimal text; a value its own attributes mask is "", which
/// is no id.
std::expected<std::vector<std::string>, Error> numeric_ids(
    const nc::File& file, const nc::VarInfo& var, const StopToken& stop) {
  auto ints = read_ints(file, var, stop);
  if (not ints) {
    return std::unexpected{std::move(ints).error()};
  }
  std::vector<std::string> out;
  out.reserve(ints->values.size());
  for (std::size_t i = 0; i < ints->values.size(); ++i) {
    out.push_back(ints->masked[i] ? std::string{}
                                  : std::to_string(ints->values[i]));
  }
  return out;
}

/// The strings (or decimal integers) of an id or name variable, one per
/// station; nullopt when the file has none.
std::expected<std::optional<std::vector<std::string>>, Error> text_column(
    const nc::File& file, const std::optional<nc::VarInfo>& var,
    std::size_t count, const StopToken& stop) {
  using Column = std::optional<std::vector<std::string>>;
  if (not var) {
    return Column{};
  }
  auto rows = is_text(*var) ? texts_of(file, *var, stop)
                            : numeric_ids(file, *var, stop);
  if (not rows) {
    return std::unexpected{std::move(rows).error()};
  }
  if (rows->size() != count) {
    return invalid(FormatErrc::dimension_mismatch,
                   subject_of(var->name.view()));
  }
  return Column{*std::move(rows)};
}

/// The ids and names of the stations. A station without an id (an empty
/// string, a NULL one, a masked integer, or no id variable) is its index in
/// decimal; one without a name has its id for a name.
struct StationTexts {
  std::vector<std::string> ids;
  std::vector<std::string> names;
  std::size_t replaced{0};
  std::size_t id_substituted{0};
};

std::expected<StationTexts, Error> station_texts(
    const nc::File& file, const std::optional<nc::VarInfo>& id_var,
    const std::optional<nc::VarInfo>& name_var, std::size_t count,
    const StopToken& stop) {
  auto parts =
      collect([&] { return text_column(file, id_var, count, stop); },
              [&] { return text_column(file, name_var, count, stop); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  const auto& [id_rows, name_rows] = *parts;
  StationTexts out;
  out.ids.reserve(count);
  out.names.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    Cleaned id = id_rows ? cleaned_text((*id_rows)[i]) : Cleaned{};
    const Cleaned name = name_rows ? cleaned_text((*name_rows)[i]) : Cleaned{};
    out.replaced += (id.replaced ? 1U : 0U) + (name.replaced ? 1U : 0U);
    if (id.text.empty()) {
      id.text = std::to_string(i);
      out.id_substituted += id_rows ? 1U : 0U;
    }
    out.names.push_back(name.text.empty() ? id.text : name.text);
    out.ids.push_back(std::move(id.text));
  }
  return out;
}

/// The CRS of the file's positions: the grid mapping of the first data
/// variable that names one. A mapping this reader cannot use, or none, leaves
/// WGS 84 with a warning (the positions are geographic by their units).
std::expected<Read<core::Epsg>, Error> crs_of_file(
    const nc::File& file, std::span<const Facts> all,
    std::span<const DataFacts> data) {
  const auto assumed = [](std::string_view subject) {
    return Read<core::Epsg>{.value = core::Epsg::wgs84(),
                            .warnings = {{.code = WarningCode::crs_assumed,
                                          .subject = subject_of(subject)}}};
  };
  std::optional<std::string> mapping_text;
  for (const DataFacts& d : data) {
    mapping_text = mapping_name(*d.facts);
    if (mapping_text) {
      break;
    }
  }
  if (not mapping_text) {
    return assumed("EPSG:4326");
  }
  const std::string& name = *mapping_text;
  const Facts* mapping = facts_named(all, name);
  if (mapping == nullptr) {
    return assumed(name);
  }
  auto mapped = crs_of_mapping(file, mapping->var);
  if (mapped) {
    return mapped;
  }
  const auto* format = std::get_if<FormatError>(&mapped.error());
  if (format != nullptr and format->code == FormatErrc::unsupported_crs) {
    return assumed(name);
  }
  return std::unexpected{std::move(mapped).error()};
}

/// What turns the file's lon, lat into Locations: nothing for WGS 84 (or a
/// CRS that is not geographic, with a warning: the degrees are what the units
/// say), a Projector for another geographic CRS.
std::expected<Read<std::optional<Projector>>, Error> projector_of(
    core::Epsg epsg) {
  using Result = Read<std::optional<Projector>>;
  if (epsg == core::Epsg::wgs84()) {
    return Result{.value = std::nullopt, .warnings = {}};
  }
  auto projector = Projector::make(epsg);
  if (not projector) {
    return fail(to_format_error(projector.error(), std::nullopt));
  }
  if (projector->kind() != CrsKind::geographic) {
    return Result{
        .value = std::nullopt,
        .warnings = {{.code = WarningCode::crs_assumed,
                      .subject = "EPSG:" + std::to_string(epsg.code())}}};
  }
  return Result{.value = std::optional<Projector>{*std::move(projector)},
                .warnings = {}};
}

std::expected<Read<std::vector<core::FileStation>>, Error> stations_of(
    const nc::File& file, const ForeignStructure& s, std::size_t count,
    core::Epsg epsg, const StopToken& stop) {
  auto parts = collect_read(
      [&] {
        return station_texts(file, s.id, s.name, count, stop)
            .transform(pure<StationTexts>);
      },
      [&] {
        return coordinate(file, s.lat, stop)
            .transform(pure<std::vector<double>>);
      },
      [&] {
        return coordinate(file, s.lon, stop)
            .transform(pure<std::vector<double>>);
      },
      [&] { return projector_of(epsg); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  auto& [texts, lats, lons, projector] = parts->value;
  Read<std::vector<core::FileStation>> out{
      .value = {}, .warnings = std::move(parts->warnings)};
  if (lats.size() != count or lons.size() != count) {
    return invalid(FormatErrc::dimension_mismatch,
                   subject_of(s.lat.name.view()));
  }
  const UniqueIds unique = uniquify_ids(texts.ids);
  out.value.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    auto position = place(lats[i], lons[i], projector, i);
    if (not position) {
      return std::unexpected{std::move(position).error()};
    }
    auto key = core::StationKey::make(unique.ids[i]);
    auto text = core::StationText::make(texts.names[i]);
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
  append_if_counted(
      out.warnings,
      {.code = WarningCode::station_id_substituted,
       .subject = s.id ? subject_of(s.id->name.view()) : std::string{},
       .count = texts.id_substituted});
  append_if_counted(out.warnings, {.code = WarningCode::invalid_utf8_replaced,
                                   .subject = {},
                                   .count = texts.replaced});
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

}  // namespace

std::expected<Read<ForeignOpened>, Error> open_foreign(const nc::File& file,
                                                       CfVersion version,
                                                       const StopToken& stop) {
  auto all = all_facts(file);
  if (not all) {
    return std::unexpected{std::move(all).error()};
  }
  const Names coordinate_set = coordinate_names(*all);
  auto roles = identify(file, *all, coordinate_set);
  if (not roles) {
    return std::unexpected{std::move(roles).error()};
  }
  const std::optional<nc::DimInfo> station =
      station_dim_of(roles->located.sampling);
  if (station) {
    if (auto count = check_station_count(file, *station); not count) {
      return std::unexpected{std::move(count).error()};
    }
  }
  const Facts* name = find_name(*all, roles->id, station);
  auto search = find_data(file, *all, *roles, name, coordinate_set);
  if (not search) {
    return std::unexpected{std::move(search).error()};
  }
  const std::size_t stations = station ? station->length : 1;
  auto counted = count_samples(file, roles->located, roles->time->var, stop);
  if (not counted) {
    return std::unexpected{std::move(counted).error()};
  }
  if (counted->counts.size() != stations) {
    return invalid(FormatErrc::dimension_mismatch,
                   subject_of(roles->time->var.name.view()));
  }
  auto parts =
      collect([&] { return crs_of_file(file, *all, search->data); },
              [&] { return foreign_schema(file, *all, search->data); },
              [&] { return text_of(file, nc::global, "Conventions"); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  auto& [crs_read, schema_read, conventions_text] = *parts;
  ForeignSchema& schema = schema_read.value;

  ForeignStructure s{
      .sampling = roles->located.sampling,
      .sample = roles->located.sample,
      .lat = roles->lat->var,
      .lon = roles->lon->var,
      .time = roles->time->var,
      .id = roles->id != nullptr ? std::optional{roles->id->var} : std::nullopt,
      .name = name != nullptr ? std::optional{name->var} : std::nullopt,
      .data = {}};
  s.data.reserve(search->data.size());
  for (std::size_t k = 0; k < search->data.size(); ++k) {
    s.data.push_back({.var = search->data[k].facts->var,
                      .quality = std::move(schema.quality[k])});
  }
  auto station_list = stations_of(file, s, stations, crs_read.value, stop);
  if (not station_list) {
    return std::unexpected{std::move(station_list).error()};
  }
  StationNcCatalog catalog{
      .origin =
          ForeignCfOrigin{.layout = layout_of(s.sampling), .version = version},
      .stations = {},
      .schema = std::move(schema.meta)};
  catalog.stations.reserve(stations);
  for (std::size_t i = 0; i < stations; ++i) {
    catalog.stations.push_back({.station = std::move(station_list->value[i]),
                                .samples = counted->counts[i]});
  }
  // The order of SN 12: the file is foreign, the CRS of the positions, the
  // stations, the variables skipped, the schema.
  std::vector<Warning> warnings{
      {.code = WarningCode::foreign_cf,
       .subject = subject_of(conventions_text.value_or(""))}};
  append(warnings, std::move(crs_read.warnings));
  append(warnings, std::move(station_list->warnings));
  append(warnings, std::move(search->warnings));
  append(warnings, std::move(schema_read.warnings));
  return Read<ForeignOpened>{
      .value = {.s = std::move(s),
                .counts = std::move(counted->counts),
                .placement = std::move(counted->placement),
                .catalog = std::move(catalog)},
      .warnings = std::move(warnings)};
}

}  // namespace mov::io::detail::station_nc
