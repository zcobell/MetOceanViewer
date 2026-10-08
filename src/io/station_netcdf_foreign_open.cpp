// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Opening a foreign CF discrete-sampling-geometry `timeSeries` file (docs/
// station-netcdf.md section 12, the "Foreign" column, and CF 9): which
// variable is the station id, the position, the time and the data, which of
// the five representations of CF 9.3 the samples are in, and everything a
// catalog holds. The samples are station_netcdf_foreign_read.cpp's.
//
// Variables are found by what CF says they are, never by name: `cf_role`,
// `standard_name`, `units`, `coordinates`, `sample_dimension` and
// `instance_dimension`. A standard name that is exactly a registry quantity's
// (and whose units convert to its canonical unit) gives that quantity;
// anything else is a generic quantity named after the variable, with a
// deterministic substitute when the name is not a token (decision F4).

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
#include "mov/core/detail/utf8.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/station.hpp"
#include "mov/core/units.hpp"
#include "mov/io/cf_time.hpp"
#include "mov/io/detail/checked_product.hpp"
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
#include "station_netcdf_format.hpp"
#include "station_netcdf_reader.hpp"

namespace mov::io::detail::station_nc {

namespace {

using Names = std::set<std::string, std::less<>>;

constexpr std::size_t max_token_bytes = 64;

// ---- what each variable says about itself -----------------------------------

struct Facts {
  nc::VarInfo var;
  std::optional<std::string> cf_role;
  std::optional<std::string> standard_name;
  std::optional<std::string> units;
  std::optional<std::string> axis;
  std::optional<std::string> sample_dimension;
  std::optional<std::string> instance_dimension;
  std::optional<std::string> coordinates;
  std::optional<std::string> ancillary_variables;
  std::optional<std::string> bounds;
  std::optional<std::string> grid_mapping;
};

std::expected<Facts, Error> facts_of(const nc::File& file, nc::VarInfo var) {
  auto texts =
      collect([&] { return text_of(file, var.name, "cf_role"); },
              [&] { return text_of(file, var.name, "standard_name"); },
              [&] { return text_of(file, var.name, "units"); },
              [&] { return text_of(file, var.name, "axis"); },
              [&] { return text_of(file, var.name, "sample_dimension"); },
              [&] { return text_of(file, var.name, "instance_dimension"); },
              [&] { return text_of(file, var.name, "coordinates"); },
              [&] { return text_of(file, var.name, "ancillary_variables"); },
              [&] { return text_of(file, var.name, "bounds"); },
              [&] { return text_of(file, var.name, "grid_mapping"); });
  if (not texts) {
    return std::unexpected{std::move(texts).error()};
  }
  auto& [role, standard, units, axis, sample, instance, coordinate_set,
         ancillary, bounds, mapping] = *texts;
  return Facts{.var = std::move(var),
               .cf_role = std::move(role),
               .standard_name = std::move(standard),
               .units = std::move(units),
               .axis = std::move(axis),
               .sample_dimension = std::move(sample),
               .instance_dimension = std::move(instance),
               .coordinates = std::move(coordinate_set),
               .ancillary_variables = std::move(ancillary),
               .bounds = std::move(bounds),
               .grid_mapping = std::move(mapping)};
}

bool says(const std::optional<std::string>& text, std::string_view value) {
  return text.has_value() and
         core::detail::equal_ignore_case(core::detail::trim(*text), value);
}

bool among(const std::optional<std::string>& text,
           std::span<const std::string_view> values) {
  return std::ranges::any_of(
      values, [&text](std::string_view v) { return says(text, v); });
}

constexpr std::array<std::string_view, 6> north_units{
    "degrees_north", "degree_north", "degree_N",
    "degrees_N",     "degreeN",      "degreesN"};
constexpr std::array<std::string_view, 6> east_units{"degrees_east",
                                                     "degree_east",
                                                     "degree_E",
                                                     "degrees_E",
                                                     "degree"
                                                     "E",
                                                     "degreesE"};

bool numeric(const nc::VarInfo& v) { return nc::sample_readable(v.type); }

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
  const bool numeric_or_int64 = numeric(f.var) or f.var.type == nc::Type::int64;
  if (not numeric_or_int64) {
    return false;
  }
  if (says(f.standard_name, "time") or says(f.axis, "T")) {
    return true;
  }
  return f.units.has_value() and parse_cf_time_units(*f.units).has_value();
}

std::vector<std::string> words(const std::optional<std::string>& text) {
  std::vector<std::string> out;
  if (text) {
    for (const std::string_view w : split_ws(*text)) {
      out.emplace_back(w);
    }
  }
  return out;
}

/// The names `coordinates`, `bounds` and `grid_mapping` attributes point at
/// (the extended grid mapping `crs: lat lon` contributes `crs`).
Names referenced_names(std::span<const Facts> all) {
  Names names;
  for (const Facts& f : all) {
    for (const auto* text : {&f.coordinates, &f.bounds}) {
      for (std::string& w : words(*text)) {
        names.insert(std::move(w));
      }
    }
    if (f.grid_mapping) {
      const std::vector<std::string> w = words(f.grid_mapping);
      if (not w.empty()) {
        std::string first = w.front();
        if (first.ends_with(':')) {
          first.pop_back();
        }
        names.insert(std::move(first));
      }
    }
  }
  return names;
}

Names coordinate_names(std::span<const Facts> all) {
  Names names;
  for (const Facts& f : all) {
    for (std::string& w : words(f.coordinates)) {
      names.insert(std::move(w));
    }
  }
  return names;
}

const Facts* pick(const std::vector<const Facts*>& candidates,
                  const Names& referenced) {
  for (const Facts* c : candidates) {
    if (referenced.contains(c->var.name.view())) {
      return c;
    }
  }
  return candidates.empty() ? nullptr : candidates.front();
}

bool over(const nc::VarInfo& v, std::initializer_list<int> dim_ids) {
  return std::ranges::equal(v.dims, dim_ids, {}, &nc::DimInfo::id);
}

bool uses(const nc::VarInfo& v, int dim_id) {
  return std::ranges::any_of(
      v.dims, [dim_id](const nc::DimInfo& d) { return d.id == dim_id; });
}

// ---- the station dimension --------------------------------------------------

std::expected<void, Error> id_type_ok(const nc::VarInfo& id) {
  const bool ok = id.type == nc::Type::char_ or id.type == nc::Type::string or
                  id.type == nc::Type::byte or id.type == nc::Type::short_ or
                  id.type == nc::Type::int_ or id.type == nc::Type::int64;
  if (not ok) {
    return invalid(FormatErrc::bad_encoding, subject_of(id.name.view()));
  }
  return {};
}

/// The station dimension an id variable implies (nullopt: one station, as the
/// id is a scalar string or one char row).
std::expected<std::optional<nc::DimInfo>, Error> dimension_of_id(
    const nc::VarInfo& id) {
  if (auto ok = id_type_ok(id); not ok) {
    return std::unexpected{std::move(ok).error()};
  }
  const std::size_t rank = id.dims.size();
  const std::size_t one_station_rank = id.type == nc::Type::char_ ? 1 : 0;
  if (rank == one_station_rank) {
    return std::optional<nc::DimInfo>{};
  }
  if (rank == one_station_rank + 1) {
    return std::optional<nc::DimInfo>{id.dims.front()};
  }
  return invalid(FormatErrc::dimension_mismatch, subject_of(id.name.view()));
}

// ---- the layout -------------------------------------------------------------

struct Located {
  CfDsgLayout layout;
  std::optional<nc::DimInfo> station;
  nc::DimInfo sample;
  std::optional<nc::VarInfo> row_size;
  std::optional<nc::VarInfo> index;
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

std::expected<nc::DimInfo, Error> same_station(std::optional<nc::DimInfo> known,
                                               nc::DimInfo found,
                                               const nc::VarInfo& by) {
  if (known and known->id != found.id) {
    return invalid(FormatErrc::dimension_mismatch, subject_of(by.name.view()));
  }
  return found;
}

/// `time(station, obs)` or `time(obs, station)` (CF 9.3.2).
std::expected<Located, Error> locate_incomplete(
    const nc::VarInfo& time, const std::optional<nc::DimInfo>& station) {
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
  return Located{.layout = CfDsgLayout::incomplete,
                 .station = station,
                 .sample = time.dims[station_first ? 1 : 0],
                 .row_size = std::nullopt,
                 .index = std::nullopt};
}

/// The ragged layouts (CF 9.3.3, 9.3.4) when the file has the variable that
/// makes the samples ragged; nullopt when it has neither.
std::expected<std::optional<Located>, Error> locate_ragged(
    const nc::File& file, std::span<const Facts> all, const nc::DimInfo& sample,
    const std::optional<nc::DimInfo>& station) {
  const Facts* counts = counting(all, sample);
  const Facts* indices = indexing(all, sample);
  if (counts != nullptr and indices != nullptr) {
    return invalid(FormatErrc::unsupported_layout,
                   subject_of(sample.name.view()));
  }
  if (counts != nullptr) {
    auto dim = same_station(station, counts->var.dims.front(), counts->var);
    if (not dim) {
      return std::unexpected{std::move(dim).error()};
    }
    return std::optional{Located{.layout = CfDsgLayout::contiguous_ragged,
                                 .station = *std::move(dim),
                                 .sample = sample,
                                 .row_size = counts->var,
                                 .index = std::nullopt}};
  }
  if (indices == nullptr) {
    return std::optional<Located>{};
  }
  auto named_dim = dimension_named(
      file, indices->instance_dimension.value_or(std::string{}));
  if (not named_dim) {
    return std::unexpected{std::move(named_dim).error()};
  }
  auto dim = same_station(station, *std::move(named_dim), indices->var);
  if (not dim) {
    return std::unexpected{std::move(dim).error()};
  }
  return std::optional{Located{.layout = CfDsgLayout::indexed_ragged,
                               .station = *std::move(dim),
                               .sample = sample,
                               .row_size = std::nullopt,
                               .index = indices->var}};
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
    return **std::move(ragged);
  }
  if (not station) {
    return Located{.layout = CfDsgLayout::single_station,
                   .station = std::nullopt,
                   .sample = sample,
                   .row_size = std::nullopt,
                   .index = std::nullopt};
  }
  if (station->id == sample.id) {
    return invalid(FormatErrc::unsupported_layout,
                   subject_of(time.name.view()));
  }
  return Located{.layout = CfDsgLayout::orthogonal,
                 .station = station,
                 .sample = sample,
                 .row_size = std::nullopt,
                 .index = std::nullopt};
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
      return locate_incomplete(time, station);
    default:
      return invalid(FormatErrc::unsupported_layout,
                     subject_of(time.name.view()));
  }
}

// ---- the data variables -----------------------------------------------------

std::expected<bool, Error> says_unsigned(const nc::File& file,
                                         const nc::VarInfo& v) {
  return text_of(file, v.name, "_Unsigned")
      .transform([](const std::optional<std::string>& text) {
        return says(text, "true");
      });
}

struct DataSearch {
  std::vector<nc::VarInfo> data;
  std::vector<Warning> warnings;
};

/// The id and length of the station dimension of a layout (one station when
/// it has none).
int station_id_of(const Located& l) {
  return l.station.transform([](const nc::DimInfo& d) { return d.id; })
      .value_or(-1);
}

std::size_t station_count_of(const Located& l) {
  return l.station.transform([](const nc::DimInfo& d) { return d.length; })
      .value_or(1);
}

/// Whether `v` has the dimensions of a data variable of this layout.
bool fits_layout(const nc::VarInfo& v, const Located& l) {
  switch (l.layout) {
    case CfDsgLayout::orthogonal:
    case CfDsgLayout::incomplete:
      return over(v, {station_id_of(l), l.sample.id}) or
             over(v, {l.sample.id, station_id_of(l)});
    case CfDsgLayout::contiguous_ragged:
    case CfDsgLayout::indexed_ragged:
    case CfDsgLayout::single_station:
      return over(v, {l.sample.id});
  }
  return false;
}

/// Whether `v` has anything to do with the stations or the samples.
bool concerns(const nc::VarInfo& v, const Located& l) {
  return uses(v, l.sample.id) or (l.station and uses(v, l.station->id));
}

/// What find_data makes of a variable.
enum class Verdict : std::uint8_t { ignored, skipped, candidate };

/// A variable that is a coordinate, a name, an instance variable or has
/// nothing to do with the samples is `ignored`; a variable that concerns the
/// samples but cannot be read as a series is `skipped` (with a warning).
std::expected<Verdict, Error> verdict_on(
    const nc::File& file, const Facts& f, const Located& l,
    const std::set<int, std::less<>>& structural_ids,
    const Names& coordinate_set, const Names& referenced) {
  const nc::VarInfo& v = f.var;
  if (structural_ids.contains(v.id) or coordinate_set.contains(v.name.view()) or
      referenced.contains(v.name.view()) or not concerns(v, l)) {
    return Verdict::ignored;
  }
  if (v.type == nc::Type::char_ or v.type == nc::Type::string) {
    return Verdict::ignored;  // names, ids: not a series
  }
  if (l.station and over(v, {l.station->id})) {
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

Warning skipped(const nc::VarInfo& v) {
  return {.code = WarningCode::skipped_variable,
          .subject = subject_of(v.name.view())};
}

std::expected<DataSearch, Error> find_data(
    const nc::File& file, std::span<const Facts> all, const Located& l,
    const std::set<int, std::less<>>& structural_ids,
    const Names& coordinate_set, const Names& referenced) {
  DataSearch out;
  std::vector<const Facts*> candidates;
  for (const Facts& f : all) {
    const auto verdict =
        verdict_on(file, f, l, structural_ids, coordinate_set, referenced);
    if (not verdict) {
      return std::unexpected{verdict.error()};
    }
    if (*verdict == Verdict::skipped) {
      out.warnings.push_back(skipped(f.var));
    } else if (*verdict == Verdict::candidate) {
      candidates.push_back(&f);
    }
  }
  // The targets of `ancillary_variables` (quality flags) are not series.
  Names ancillary;
  for (const Facts* c : candidates) {
    for (std::string& w : words(c->ancillary_variables)) {
      ancillary.insert(std::move(w));
    }
  }
  for (const Facts* c : candidates) {
    if (ancillary.contains(c->var.name.view())) {
      out.warnings.push_back(skipped(c->var));
    } else {
      out.data.push_back(c->var);
    }
  }
  return out;
}

// ---- the quantity of a data variable (decision F4) --------------------------

std::optional<core::Quantity> registry_for(std::string_view standard_name) {
  if (standard_name.empty()) {
    return std::nullopt;
  }
  constexpr auto last = static_cast<std::size_t>(core::Quantity::difference);
  for (std::size_t i = 0; i < last; ++i) {
    const auto q = static_cast<core::Quantity>(i);
    if (core::info(q).standard_name == standard_name) {
      return q;  // water_level before water_level_prediction
    }
  }
  return std::nullopt;
}

bool is_token_name(std::string_view name) {
  return core::GenericQuantity::parse({.token = name, .standard_name = ""})
             .has_value() and
         not is_reserved(name);
}

bool ascii_letter(char c) {
  return (c >= 'a' and c <= 'z') or (c >= 'A' and c <= 'Z');
}

bool token_char(char c) {
  return ascii_letter(c) or (c >= '0' and c <= '9') or c == '_';
}

/// A valid generic token made from `name`: every byte outside [A-Za-z0-9_]
/// becomes '_', a leading non-letter gets a 'v' in front, the length is cut to
/// 64 bytes, and a token that is a registry token, a name the format uses or
/// one already taken gets `_2`, `_3`, ... after it.
std::string mangled_token(std::string_view name, const Names& taken) {
  std::string token;
  for (const char c : name) {
    token.push_back(token_char(c) ? c : '_');
  }
  if (token.empty() or not ascii_letter(token.front())) {
    token.insert(token.begin(), 'v');
  }
  token.resize(std::min(token.size(), max_token_bytes));
  const auto free = [&taken](const std::string& t) {
    return not taken.contains(t) and is_token_name(t);
  };
  if (free(token)) {
    return token;
  }
  for (std::size_t n = 2;; ++n) {
    std::string candidate = token + "_" + std::to_string(n);
    if (free(candidate)) {
      return candidate;
    }
  }
}

/// The unit a file states, with the warning for a spelling core does not know.
Read<std::optional<core::Unit>> unit_of(
    const std::optional<std::string>& text) {
  Read<std::optional<core::Unit>> out{.value = std::nullopt, .warnings = {}};
  if (text) {
    out.value = core::parse_unit(*text);
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

/// The registry quantity a standard name and unit make, if the unit converts
/// to the canonical one and no earlier column has it. A standard name that is
/// a registry quantity's whose unit does not convert warns `unknown_quantity`.
std::optional<core::QuantityId> registry_quantity(
    std::string_view standard, const std::optional<core::Unit>& unit,
    Names& taken, std::vector<Warning>& warnings) {
  const auto q = registry_for(standard);
  if (not q) {
    return std::nullopt;
  }
  const std::optional<core::Unit> canonical = core::canonical_unit(*q);
  const bool convertible =
      unit and canonical and core::conversion(*unit, *canonical).has_value();
  if (not convertible) {
    warnings.push_back({.code = WarningCode::unknown_quantity,
                        .subject = subject_of(standard)});
    return std::nullopt;
  }
  if (taken.contains(core::token(*q))) {
    return std::nullopt;
  }
  taken.insert(std::string{core::token(*q)});
  return core::QuantityId{*q};
}

/// The generic quantity named after the variable: its name if that is a free
/// token, else a substitute (F4), with the standard name the file gives.
std::expected<core::QuantityId, Error> generic_quantity(
    std::string_view name, std::string_view standard, Names& taken,
    std::vector<Warning>& warnings) {
  const bool usable = is_token_name(name) and not taken.contains(name);
  const std::string token =
      usable ? std::string{name} : mangled_token(name, taken);
  if (not usable) {
    warnings.push_back(
        {.code = WarningCode::variable_renamed,
         .subject = subject_of(std::string{name} + " -> " + token)});
  }
  // Standard names that are no CF text (over 255 bytes) are dropped.
  auto generic = core::GenericQuantity::parse(
      {.token = token,
       .standard_name =
           standard.size() <= 255 ? standard : std::string_view{}});
  if (not generic) {
    return invalid(FormatErrc::invalid_variable_name, subject_of(token));
  }
  taken.insert(token);
  return core::QuantityId{*std::move(generic)};
}

/// The metadata of one data variable. `taken` holds the tokens of the earlier
/// columns.
std::expected<Read<core::SeriesMeta>, Error> meta_of(const nc::File& file,
                                                     const Facts& f,
                                                     Names& taken) {
  const std::string_view name = f.var.name.view();
  auto texts =
      collect([&] { return text_of(file, f.var.name, "long_name"); },
              [&] { return text_of(file, f.var.name, "vertical_datum"); });
  if (not texts) {
    return std::unexpected{std::move(texts).error()};
  }
  const auto& [long_name, datum] = *texts;
  const std::string standard{f.standard_name
                                 ? core::detail::trim(*f.standard_name)
                                 : std::string_view{}};
  Read<std::optional<core::Unit>> unit = unit_of(f.units);
  std::vector<Warning> warnings = std::move(unit.warnings);
  std::optional<core::QuantityId> quantity =
      registry_quantity(standard, unit.value, taken, warnings);
  if (not quantity) {
    auto generic = generic_quantity(name, standard, taken, warnings);
    if (not generic) {
      return std::unexpected{std::move(generic).error()};
    }
    quantity = *std::move(generic);
  }
  Read<core::SeriesMeta> meta = with_datum(
      core::SeriesMeta::make({.quantity = *std::move(quantity),
                              .label = long_name.value_or(std::string{name}),
                              .unit = std::move(unit.value)}),
      datum, name);
  append(warnings, std::move(meta.warnings));
  return Read<core::SeriesMeta>{.value = std::move(meta.value),
                                .warnings = std::move(warnings)};
}

// ---- stations ---------------------------------------------------------------

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

/// Integer ids as decimal text.
std::expected<std::vector<std::string>, Error> numeric_ids(
    const nc::File& file, const nc::VarInfo& var, const StopToken& stop) {
  auto values = file.read<std::int64_t>(var.name, nc::whole(var), stop);
  if (not values) {
    return std::unexpected{std::move(values).error()};
  }
  std::vector<std::string> out;
  out.reserve(values->size());
  for (const std::int64_t v : *values) {
    out.push_back(std::to_string(v));
  }
  return out;
}

struct StationTexts {
  std::vector<std::string> ids;
  std::vector<std::string> names;
  std::size_t replaced{0};
};

/// The strings (or decimal integers) of an id or name variable; empty when
/// the file has none.
std::expected<std::vector<std::string>, Error> text_column(
    const nc::File& file, const std::optional<nc::VarInfo>& var,
    std::size_t count, const StopToken& stop) {
  if (not var) {
    return std::vector<std::string>{};
  }
  const bool text =
      var->type == nc::Type::char_ or var->type == nc::Type::string;
  auto rows = text ? texts_of(file, *var, stop) : numeric_ids(file, *var, stop);
  if (rows and rows->size() != count) {
    return invalid(FormatErrc::dimension_mismatch,
                   subject_of(var->name.view()));
  }
  return rows;
}

std::expected<StationTexts, Error> station_texts(const nc::File& file,
                                                 const ForeignStructure& s,
                                                 std::size_t count,
                                                 const StopToken& stop) {
  auto parts = collect([&] { return text_column(file, s.id, count, stop); },
                       [&] { return text_column(file, s.name, count, stop); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  const auto& [id_rows, name_rows] = *parts;
  StationTexts out;
  for (std::size_t i = 0; i < count; ++i) {
    Cleaned id = s.id ? cleaned_text(id_rows[i])
                      : Cleaned{.text = std::to_string(i), .replaced = false};
    Cleaned name = s.name ? cleaned_text(name_rows[i])
                          : Cleaned{.text = "", .replaced = false};
    out.replaced += (id.replaced ? 1U : 0U) + (name.replaced ? 1U : 0U);
    if (id.text.empty()) {
      return invalid(FormatErrc::no_station_id,
                     s.id ? subject_of(s.id->name.view()) : std::string{}, i);
    }
    if (name.text.empty()) {
      out.names.push_back(id.text);
    } else {
      out.names.push_back(std::move(name.text));
    }
    out.ids.push_back(std::move(id.text));
  }
  return out;
}

/// The CRS of the file's positions: the grid mapping of the first data
/// variable that names one. A mapping this reader cannot use, or none, leaves
/// WGS 84 with a warning (the positions are geographic by their units).
std::expected<Read<core::Epsg>, Error> crs_of_file(
    const nc::File& file, std::span<const Facts> all,
    std::span<const nc::VarInfo> data) {
  std::optional<std::string> mapping;
  for (const Facts& f : all) {
    if (f.grid_mapping and not mapping and
        std::ranges::any_of(
            data, [&f](const nc::VarInfo& d) { return d.id == f.var.id; })) {
      const std::vector<std::string> w = words(f.grid_mapping);
      if (not w.empty()) {
        std::string first = w.front();
        if (first.ends_with(':')) {
          first.pop_back();
        }
        mapping = std::move(first);
      }
    }
  }
  const auto assumed = [](const std::string& subject) {
    return Read<core::Epsg>{.value = core::Epsg::wgs84(),
                            .warnings = {{.code = WarningCode::crs_assumed,
                                          .subject = subject_of(subject)}}};
  };
  if (not mapping) {
    return assumed("EPSG:4326");
  }
  const auto found = std::ranges::find_if(
      all, [&](const Facts& f) { return f.var.name.view() == *mapping; });
  if (found == all.end()) {
    return assumed(*mapping);
  }
  auto mapped = crs_of_mapping(file, found->var);
  if (mapped) {
    return mapped;
  }
  const auto* format = std::get_if<FormatError>(&mapped.error());
  if (format != nullptr and format->code == FormatErrc::unsupported_crs) {
    return assumed(*mapping);
  }
  return std::unexpected{std::move(mapped).error()};
}

std::expected<std::optional<Projector>, Error> projector_of(
    core::Epsg epsg, std::vector<Warning>& warnings) {
  if (epsg == core::Epsg::wgs84()) {
    return std::optional<Projector>{};
  }
  auto projector = Projector::make(epsg);
  if (not projector) {
    return fail(to_format_error(projector.error(), std::nullopt));
  }
  if (projector->kind() != CrsKind::geographic) {
    // lat and lon are degrees by their units whatever the grid is.
    warnings.push_back({.code = WarningCode::crs_assumed,
                        .subject = "EPSG:" + std::to_string(epsg.code())});
    return std::optional<Projector>{};
  }
  return std::optional<Projector>{*std::move(projector)};
}

std::expected<Read<std::vector<core::FileStation>>, Error> stations_of(
    const nc::File& file, const ForeignStructure& s, std::size_t count,
    core::Epsg epsg, const StopToken& stop) {
  Read<std::vector<core::FileStation>> out{.value = {}, .warnings = {}};
  auto parts = collect([&] { return station_texts(file, s, count, stop); },
                       [&] { return coordinate(file, s.lat, stop); },
                       [&] { return coordinate(file, s.lon, stop); },
                       [&] { return projector_of(epsg, out.warnings); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  auto& [texts, lats, lons, projector] = *parts;
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

// ---- the samples of each station --------------------------------------------

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
  std::vector<std::size_t> offsets;
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
  out.offsets.reserve(raw->values.size());
  std::size_t total = 0;
  for (std::size_t i = 0; i < raw->values.size(); ++i) {
    const std::int64_t n = raw->values[i];
    if (n < 0 or raw->masked[i] or
        std::cmp_greater(n, samples - std::min(total, samples))) {
      return invalid(FormatErrc::bad_row_size, subject_of(row_size.name.view()),
                     i);
    }
    out.offsets.push_back(total);
    out.counts.push_back(static_cast<std::size_t>(n));
    total += static_cast<std::size_t>(n);
  }
  if (total != samples) {
    return invalid(FormatErrc::bad_row_size, subject_of(row_size.name.view()));
  }
  return out;
}

/// Indexed ragged: the station of every sample, and each station's count.
struct Indexed {
  std::vector<std::size_t> index;
  std::vector<std::size_t> counts;
};

std::expected<Indexed, Error> indexed_of(const nc::File& file,
                                         const nc::VarInfo& index,
                                         std::size_t stations,
                                         const StopToken& stop) {
  auto raw = read_ints(file, index, stop);
  if (not raw) {
    return std::unexpected{std::move(raw).error()};
  }
  Indexed out;
  out.index.reserve(raw->values.size());
  out.counts.assign(stations, 0);
  for (std::size_t o = 0; o < raw->values.size(); ++o) {
    const std::int64_t s = raw->values[o];
    if (s < 0 or raw->masked[o] or std::cmp_greater_equal(s, stations)) {
      return invalid(FormatErrc::bad_ragged_index,
                     subject_of(index.name.view()), std::nullopt, o);
    }
    out.index.push_back(static_cast<std::size_t>(s));
    ++out.counts[static_cast<std::size_t>(s)];
  }
  return out;
}

/// Incomplete layout without `obs_count`: the number of leading non-missing
/// times of each station; a time after a missing one is padding_not_missing.
template <nc::Numeric T>
std::expected<std::vector<std::size_t>, Error> leading_counts(
    const nc::File& file, const nc::VarInfo& time, const Located& l,
    const StopToken& stop) {
  auto mask = file.masking<T>(time.name);
  if (not mask) {
    return fail(std::move(mask).error());
  }
  const std::size_t stations = station_count_of(l);
  const std::size_t length = l.sample.length;
  const bool station_major = time.dims.front().id == station_id_of(l);
  std::vector<std::size_t> counts(stations, 0);
  std::vector<bool> ended(stations, false);
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
        for (std::size_t r = 0; r < outer.count; ++r) {
          const std::size_t row = outer.start + r;
          const std::size_t width = station_major ? length : stations;
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
    const nc::File& file, const nc::VarInfo& obs_count, const Located& l,
    const StopToken& stop) {
  auto raw =
      file.read<std::int64_t>(obs_count.name, nc::whole(obs_count), stop);
  if (not raw) {
    return std::unexpected{std::move(raw).error()};
  }
  std::vector<std::size_t> counts;
  counts.reserve(raw->size());
  for (std::size_t i = 0; i < raw->size(); ++i) {
    const std::int64_t n = (*raw)[i];
    if (n < 0 or std::cmp_greater(n, l.sample.length)) {
      return invalid(FormatErrc::bad_obs_count,
                     subject_of(obs_count.name.view()), i);
    }
    counts.push_back(static_cast<std::size_t>(n));
  }
  return counts;
}

std::expected<std::vector<std::size_t>, Error> incomplete_counts(
    const nc::File& file, const nc::VarInfo& time, const Located& l,
    const std::optional<nc::VarInfo>& obs_count, const StopToken& stop) {
  if (obs_count) {
    return counts_from_obs_count(file, *obs_count, l, stop);
  }
  if (auto fits = check_result_size(file, time.name.view(), station_count_of(l),
                                    l.sample.length, 1);
      not fits) {
    return std::unexpected{std::move(fits).error()};
  }
  std::vector<std::size_t> counts;
  auto done = dispatch_role_numeric(
      RowRole::times, file, time, [&]<class T>() -> std::expected<void, Error> {
        auto leading = leading_counts<T>(file, time, l, stop);
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

// ---- identification ---------------------------------------------------------

std::expected<void, Error> validate_masking(const nc::File& file,
                                            const nc::VarInfo& var) {
  return dispatch_model_numeric(file, var,
                                [&]<class T>() -> std::expected<void, Error> {
                                  auto mask = file.masking<T>(var.name);
                                  if (not mask) {
                                    return fail(std::move(mask).error());
                                  }
                                  return {};
                                });
}

/// The station dimension, and whether the file has said what it is: the id
/// variable decides, else the position variables do.
struct StationAxis {
  bool known;
  std::optional<nc::DimInfo> dim;  // nullopt: one station
};

/// The one variable whose `cf_role` is timeseries_id (CF 9.5), or null.
std::expected<const Facts*, Error> find_id(std::span<const Facts> all) {
  const Facts* found = nullptr;
  for (const Facts& f : all) {
    if (not says(f.cf_role, "timeseries_id")) {
      continue;
    }
    if (found != nullptr) {
      return invalid(FormatErrc::no_station_id, "cf_role");
    }
    found = &f;
  }
  return found;
}

std::expected<StationAxis, Error> axis_of_id(const Facts* id) {
  if (id == nullptr) {
    return StationAxis{.known = false, .dim = std::nullopt};
  }
  return dimension_of_id(id->var).transform([](std::optional<nc::DimInfo> dim) {
    return StationAxis{.known = true, .dim = std::move(dim)};
  });
}

/// The candidates for latitude or longitude: numeric, scalar or over the
/// station dimension once that is known.
std::vector<const Facts*> positions(std::span<const Facts> all,
                                    bool (*test)(const Facts&),
                                    const StationAxis& axis) {
  std::vector<const Facts*> out;
  for (const Facts& f : all) {
    const std::size_t rank = f.var.dims.size();
    if (not test(f) or rank > 1) {
      continue;
    }
    const bool fits =
        not axis.known or (axis.dim ? over(f.var, {axis.dim->id}) : rank == 0);
    if (fits) {
      out.push_back(&f);
    }
  }
  return out;
}

struct Roles {
  const Facts* id;
  const Facts* lat;
  const Facts* lon;
  const Facts* time;
  std::optional<nc::DimInfo> station;
};

/// Latitude and longitude, and with them the station dimension when the id
/// did not give it.
std::expected<Roles, Error> find_positions(std::span<const Facts> all,
                                           const Facts* id,
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
  std::optional<nc::DimInfo> station = axis.dim;
  if (not axis.known) {
    const bool same_shape =
        lat->var.dims.size() == lon->var.dims.size() and
        (lat->var.dims.empty() or lat->var.dims[0].id == lon->var.dims[0].id);
    if (not same_shape) {
      return invalid(FormatErrc::dimension_mismatch,
                     subject_of(lon->var.name.view()));
    }
    if (not lat->var.dims.empty()) {
      station = lat->var.dims.front();
    }
  }
  return Roles{.id = id,
               .lat = lat,
               .lon = lon,
               .time = nullptr,
               .station = std::move(station)};
}

const Facts* find_time(std::span<const Facts> all,
                       const Names& coordinate_set) {
  Names bounds;
  for (const Facts& f : all) {
    for (std::string& w : words(f.bounds)) {
      bounds.insert(std::move(w));
    }
  }
  std::vector<const Facts*> times;
  for (const Facts& f : all) {
    if (is_time(f) and not bounds.contains(f.var.name.view())) {
      times.push_back(&f);
    }
  }
  return pick(times, coordinate_set);
}

struct Identified {
  Roles roles;
  Located located;
};

std::expected<Identified, Error> identify(const nc::File& file,
                                          std::span<const Facts> all,
                                          const Names& coordinate_set) {
  auto id = find_id(all);
  if (not id) {
    return std::unexpected{std::move(id).error()};
  }
  auto axis = axis_of_id(*id);
  if (not axis) {
    return std::unexpected{std::move(axis).error()};
  }
  auto roles = find_positions(all, *id, *axis, coordinate_set);
  if (not roles) {
    return std::unexpected{std::move(roles).error()};
  }
  roles->time = find_time(all, coordinate_set);
  if (roles->time == nullptr) {
    return invalid(FormatErrc::missing_variable, "time");
  }
  auto located = locate(file, all, roles->time->var, roles->station);
  if (not located) {
    return std::unexpected{std::move(located).error()};
  }
  // lat and lon are over the stations (or scalar for one station).
  for (const Facts* c : {roles->lat, roles->lon}) {
    const bool fits = located->station ? over(c->var, {located->station->id})
                                       : c->var.dims.empty();
    if (not fits) {
      return invalid(FormatErrc::dimension_mismatch,
                     subject_of(c->var.name.view()));
    }
  }
  return Identified{.roles = *std::move(roles), .located = *std::move(located)};
}

/// The structure without its data variables, and the ids of the variables that
/// are not data.
struct Skeleton {
  ForeignStructure s;
  std::set<int, std::less<>> structural;
};

Skeleton skeleton_of(std::span<const Facts> all, const Identified& found) {
  const Roles& r = found.roles;
  const Located& l = found.located;
  Skeleton out{.s = {.layout = l.layout,
                     .station = l.station,
                     .sample = l.sample,
                     .lat = r.lat->var,
                     .lon = r.lon->var,
                     .time = r.time->var,
                     .id = std::nullopt,
                     .name = std::nullopt,
                     .row_size = l.row_size,
                     .index = l.index,
                     .obs_count = std::nullopt,
                     .data = {}},
               .structural = {r.time->var.id, r.lat->var.id, r.lon->var.id}};
  if (r.id != nullptr) {
    out.s.id = r.id->var;
    out.structural.insert(r.id->var.id);
  }
  for (const Facts& f : all) {
    const bool text =
        f.var.type == nc::Type::char_ or f.var.type == nc::Type::string;
    if (says(f.standard_name, "platform_name") and text and not out.s.name) {
      out.s.name = f.var;
      out.structural.insert(f.var.id);
    }
    if (f.var.name.view() == "obs_count" and
        l.layout == CfDsgLayout::incomplete and l.station and
        over(f.var, {l.station->id})) {
      out.s.obs_count = f.var;
      out.structural.insert(f.var.id);
    }
  }
  for (const auto* helper : {&l.row_size, &l.index}) {
    if (*helper) {
      out.structural.insert((*helper)->id);
    }
  }
  return out;
}

/// The data variables, with their masking attributes checked.
std::expected<DataSearch, Error> data_of(const nc::File& file,
                                         std::span<const Facts> all,
                                         const Skeleton& k,
                                         const Names& coordinate_set) {
  const Located l{.layout = k.s.layout,
                  .station = k.s.station,
                  .sample = k.s.sample,
                  .row_size = k.s.row_size,
                  .index = k.s.index};
  auto search = find_data(file, all, l, k.structural, coordinate_set,
                          referenced_names(all));
  if (not search) {
    return std::unexpected{std::move(search).error()};
  }
  if (search->data.empty()) {
    return invalid(FormatErrc::no_data_variables, "");
  }
  for (const nc::VarInfo& d : search->data) {
    if (auto ok = validate_masking(file, d); not ok) {
      return std::unexpected{std::move(ok).error()};
    }
  }
  return search;
}

/// The samples of each station, by layout.
struct Counted {
  std::vector<std::size_t> counts;
  std::vector<std::size_t> offsets;
  std::vector<std::size_t> index;
};

std::expected<Counted, Error> count_samples(const nc::File& file,
                                            const ForeignStructure& s,
                                            const StopToken& stop) {
  const std::size_t stations = s.station ? s.station->length : 1;
  const std::size_t samples = s.sample.length;
  Counted out;
  switch (s.layout) {
    case CfDsgLayout::orthogonal:
    case CfDsgLayout::single_station:
      out.counts.assign(stations, samples);
      return out;
    case CfDsgLayout::incomplete: {
      const Located l{.layout = s.layout,
                      .station = s.station,
                      .sample = s.sample,
                      .row_size = std::nullopt,
                      .index = std::nullopt};
      auto counts = incomplete_counts(file, s.time, l, s.obs_count, stop);
      if (not counts) {
        return std::unexpected{std::move(counts).error()};
      }
      out.counts = *std::move(counts);
      return out;
    }
    case CfDsgLayout::contiguous_ragged: {
      if (not s.row_size) {
        return invalid(FormatErrc::missing_variable, "rowSize");
      }
      auto runs = runs_of(file, *s.row_size, samples, stop);
      if (not runs) {
        return std::unexpected{std::move(runs).error()};
      }
      out.counts = std::move(runs->counts);
      out.offsets = std::move(runs->offsets);
      return out;
    }
    case CfDsgLayout::indexed_ragged: {
      if (not s.index) {
        return invalid(FormatErrc::missing_variable, "index");
      }
      auto indexed = indexed_of(file, *s.index, stations, stop);
      if (not indexed) {
        return std::unexpected{std::move(indexed).error()};
      }
      out.counts = std::move(indexed->counts);
      out.index = std::move(indexed->index);
      return out;
    }
  }
  return out;
}

std::expected<Read<std::vector<core::SeriesMeta>>, Error> schema_of(
    const nc::File& file, std::span<const Facts> all,
    std::span<const nc::VarInfo> data) {
  Read<std::vector<core::SeriesMeta>> out{.value = {}, .warnings = {}};
  Names taken;
  for (const nc::VarInfo& d : data) {
    const auto facts = std::ranges::find_if(
        all, [&d](const Facts& f) { return f.var.id == d.id; });
    auto meta = meta_of(file, *facts, taken);
    if (not meta) {
      return std::unexpected{std::move(meta).error()};
    }
    out.value.push_back(std::move(meta->value));
    append(out.warnings, std::move(meta->warnings));
  }
  return out;
}

std::expected<std::vector<Facts>, Error> all_facts(const nc::File& file) {
  auto vars = file.variables();
  if (not vars) {
    return fail(std::move(vars).error());
  }
  std::vector<Facts> all;
  all.reserve(vars->size());
  for (nc::VarInfo& v : *vars) {
    auto facts = facts_of(file, std::move(v));
    if (not facts) {
      return std::unexpected{std::move(facts).error()};
    }
    all.push_back(*std::move(facts));
  }
  return all;
}

}  // namespace

std::expected<Read<ForeignOpened>, Error> open_foreign(const nc::File& file,
                                                       const StopToken& stop) {
  auto all = all_facts(file);
  if (not all) {
    return std::unexpected{std::move(all).error()};
  }
  const Names coordinate_set = coordinate_names(*all);
  auto found = identify(file, *all, coordinate_set);
  if (not found) {
    return std::unexpected{std::move(found).error()};
  }
  if (found->located.station) {
    if (auto count = check_station_count(file, *found->located.station);
        not count) {
      return std::unexpected{std::move(count).error()};
    }
  }
  Skeleton skeleton = skeleton_of(*all, *found);
  auto data = data_of(file, *all, skeleton, coordinate_set);
  if (not data) {
    return std::unexpected{std::move(data).error()};
  }
  ForeignStructure s = std::move(skeleton.s);
  s.data = std::move(data->data);
  const std::size_t stations = s.station ? s.station->length : 1;
  auto counted = count_samples(file, s, stop);
  if (not counted) {
    return std::unexpected{std::move(counted).error()};
  }
  if (counted->counts.size() != stations) {
    return invalid(FormatErrc::dimension_mismatch,
                   subject_of(s.time.name.view()));
  }
  auto parts = collect_read([&] { return crs_of_file(file, *all, s.data); },
                            [&] { return schema_of(file, *all, s.data); },
                            [&] {
                              return text_of(file, nc::global, "Conventions")
                                  .transform(pure<std::optional<std::string>>);
                            });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  auto& [epsg, schema, cf_text] = parts->value;
  auto station_list = stations_of(file, s, stations, epsg, stop);
  if (not station_list) {
    return std::unexpected{std::move(station_list).error()};
  }
  const auto cf = parse_cf_conventions(cf_text.value_or(""));
  StationNcCatalog catalog{
      .origin = ForeignCfOrigin{.layout = s.layout,
                                .version = cf.value_or(
                                    CfVersion{.major = 1, .minor = 6})},
      .stations = {},
      .schema = std::move(schema)};
  catalog.stations.reserve(stations);
  for (std::size_t i = 0; i < stations; ++i) {
    catalog.stations.push_back({.station = std::move(station_list->value[i]),
                                .samples = counted->counts[i]});
  }
  std::vector<Warning> warnings{{.code = WarningCode::foreign_cf,
                                 .subject = subject_of(cf_text.value_or(""))}};
  append(warnings, std::move(parts->warnings));
  append(warnings, std::move(station_list->warnings));
  append(warnings, std::move(data->warnings));
  return Read<ForeignOpened>{.value = {.s = std::move(s),
                                       .counts = std::move(counted->counts),
                                       .offsets = std::move(counted->offsets),
                                       .index = std::move(counted->index),
                                       .catalog = std::move(catalog)},
                             .warnings = std::move(warnings)};
}

}  // namespace mov::io::detail::station_nc
