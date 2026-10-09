// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The schema of a foreign CF file (docs/station-netcdf.md 12 "Foreign", owner
// decision 30): the quantity, label, unit and datum of each data variable, and
// what its quality-flag variables say about its samples.
//
// A standard name that is exactly a registry quantity's (and whose units
// convert to its canonical unit) gives that quantity; anything else is a
// generic quantity named after the variable, with a deterministic substitute
// when the name cannot be a column (decision F4). Tokens are made in two passes
// so that a substitute never takes the name a later variable has itself.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "model_netcdf.hpp"
#include "mov/core/ascii.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/units.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read.hpp"
#include "mov/io/warning.hpp"
#include "station_netcdf_foreign_facts.hpp"
#include "station_netcdf_format.hpp"
#include "station_netcdf_shared.hpp"

namespace mov::io::detail::station_nc {

namespace {

constexpr std::size_t max_token_bytes = 64;
constexpr std::size_t max_standard_name_bytes = 255;

// ---- the texts of a data variable
// --------------------------------------------

/// What a data variable says about itself, read once.
struct Raw {
  const Facts* facts;
  std::string standard;
  std::optional<std::string> long_name;
  /// The text that names the variable's datum: `vertical_datum`, else
  /// `geopotential_datum_name` of the variable, else of its grid mapping.
  std::optional<std::string> datum;
  Read<std::optional<core::Unit>> unit;
};

/// `geopotential_datum_name` of the grid mapping variable `f` points at.
std::expected<std::optional<std::string>, Error> mapping_datum(
    const nc::File& file, std::span<const Facts> all, const Facts& f) {
  const auto name = mapping_name(f);
  const Facts* mapping = name ? facts_named(all, *name) : nullptr;
  if (mapping == nullptr) {
    return std::optional<std::string>{};
  }
  return optional_text(file, mapping->var.name, "geopotential_datum_name");
}

std::expected<Raw, Error> raw_of(const nc::File& file,
                                 std::span<const Facts> all, const Facts& f) {
  auto texts = collect(
      [&] { return optional_text(file, f.var.name, "long_name"); },
      [&] { return optional_text(file, f.var.name, "vertical_datum"); },
      [&] {
        return optional_text(file, f.var.name, "geopotential_datum_name");
      },
      [&] { return mapping_datum(file, all, f); });
  if (not texts) {
    return std::unexpected{std::move(texts).error()};
  }
  auto& [long_name, vertical, geopotential, mapped] = *texts;
  std::optional<std::string> datum =
      vertical ? std::move(vertical)
               : (geopotential ? std::move(geopotential) : std::move(mapped));
  return Raw{.facts = &f,
             .standard = std::string{f.standard_name
                                         ? core::ascii::trim(*f.standard_name)
                                         : std::string_view{}},
             .long_name = std::move(long_name),
             .datum = std::move(datum),
             .unit = parsed_unit(f.units)};
}

// ---- the quantity (decision F4, owner decision 30.4)
// -------------------------

/// The name or long_name of the variable says its values are predicted.
bool hints_prediction(const Raw& r) {
  constexpr std::array<std::string_view, 4> hints{"predict", "tide",
                                                  "astronomical", "harmonic"};
  const std::string text = to_lower_ascii(r.facts->var.name.view()) + ' ' +
                           to_lower_ascii(r.long_name.value_or(""));
  return std::ranges::any_of(hints, [&text](std::string_view hint) {
    return text.find(hint) != std::string::npos;
  });
}

/// The registry quantity of the variable, if its standard name is one's, its
/// unit converts to the canonical one and the column is free. Of the two
/// quantities that share the water level standard name, the hints of the
/// variable's name pick the prediction; a variable that finds its quantity
/// taken, or whose unit does not fit, warns `unknown_quantity` and stays
/// generic.
std::optional<core::Quantity> registry_choice(const Raw& r,
                                              const TokenSet& taken,
                                              std::vector<Warning>& warnings) {
  const auto first = core::quantity_for_standard_name(r.standard);
  if (not first) {
    return std::nullopt;
  }
  const auto second = core::quantity_for_standard_name(r.standard, first);
  const core::Quantity q = second and hints_prediction(r) ? *second : *first;
  const std::optional<core::Unit> canonical = core::canonical_unit(q);
  const bool convertible = r.unit.value and canonical and
                           core::conversion(*r.unit.value, *canonical);
  if (not convertible or not writable_token(core::token(q), taken)) {
    warnings.push_back({.code = WarningCode::unknown_quantity,
                        .subject = subject_of(r.standard)});
    return std::nullopt;
  }
  return q;
}

bool token_char(char c) { return core::ascii::is_alnum(c) or c == '_'; }

/// Whether `token` can be the token of a generic column next to `taken`.
bool usable_token(std::string_view token, const TokenSet& taken) {
  return core::GenericQuantity::parse({.token = token, .standard_name = ""})
             .has_value() and
         writable_token(token, taken);
}

/// A usable token made from `name`: every byte outside [A-Za-z0-9_] becomes
/// '_', a leading non-letter gets a 'v' in front, the length is cut to 64
/// bytes, and a token that is a registry token, a name the format uses or one
/// already taken gets `_2`, `_3`, ... after it.
std::string mangled_token(std::string_view name, const TokenSet& taken) {
  std::string token;
  for (const char c : name) {
    token.push_back(token_char(c) ? c : '_');
  }
  if (token.empty() or not core::ascii::is_alpha(token.front())) {
    token.insert(token.begin(), 'v');
  }
  token.resize(std::min(token.size(), max_token_bytes));
  if (usable_token(token, taken)) {
    return token;
  }
  for (std::size_t n = 2;; ++n) {
    std::string candidate = token + "_" + std::to_string(n);
    if (usable_token(candidate, taken)) {
      return candidate;
    }
  }
}

std::expected<core::QuantityId, Error> generic_quantity(
    const std::string& token, std::string_view standard) {
  // Standard names that are no CF text (over 255 bytes) are dropped.
  auto generic = core::GenericQuantity::parse(
      {.token = token,
       .standard_name = standard.size() <= max_standard_name_bytes
                            ? standard
                            : std::string_view{}});
  if (not generic) {
    return fail(
        format_error(FormatErrc::invalid_variable_name, subject_of(token)));
  }
  return core::QuantityId{*std::move(generic)};
}

/// The quantity of each data variable. Pass one: registry quantities and
/// names that are columns in their own right claim their tokens, in file
/// order. Pass two: the others get substitutes (`variable_renamed`).
std::expected<std::vector<core::QuantityId>, Error> quantities_of(
    std::span<const Raw> raws, std::vector<Warning>& warnings) {
  std::vector<std::optional<core::QuantityId>> chosen(raws.size());
  TokenSet taken;
  for (std::size_t k = 0; k < raws.size(); ++k) {
    const Raw& r = raws[k];
    const std::string_view name = r.facts->var.name.view();
    if (const auto registry = registry_choice(r, taken, warnings)) {
      chosen[k] = *registry;
      taken.insert(std::string{core::token(*registry)});
    } else if (usable_token(name, taken)) {
      auto generic = generic_quantity(std::string{name}, r.standard);
      if (not generic) {
        return std::unexpected{std::move(generic).error()};
      }
      chosen[k] = *std::move(generic);
      taken.insert(std::string{name});
    }
  }
  std::vector<core::QuantityId> out;
  out.reserve(raws.size());
  for (std::size_t k = 0; k < raws.size(); ++k) {
    std::optional<core::QuantityId>& slot = chosen[k];
    if (not slot) {
      const Raw& r = raws[k];
      const std::string_view name = r.facts->var.name.view();
      const std::string token = mangled_token(name, taken);
      auto generic = generic_quantity(token, r.standard);
      if (not generic) {
        return std::unexpected{std::move(generic).error()};
      }
      warnings.push_back(
          {.code = WarningCode::variable_renamed, .subject = subject_of(name)});
      taken.insert(token);
      slot = *std::move(generic);
    }
    out.push_back(*std::move(slot));
  }
  return out;
}

// ---- quality flags (owner decision 30.1)
// -------------------------------------

/// The values of the IOOS QARTOD scheme (1 pass, 2 not evaluated, 3 suspect or
/// of high interest, 4 fail, 9 missing data), for a flag variable that gives
/// `flag_values` but no `flag_meanings`.
constexpr std::array<std::int64_t, 5> qartod_values{1, 2, 3, 4, 9};
constexpr std::array<std::int64_t, 2> qartod_bad{4, 9};
constexpr std::int64_t qartod_suspect = 3;

bool mentions(std::string_view meaning, std::string_view word) {
  return meaning.find(word) != std::string_view::npos;
}

/// What the meaning of a flag value says: bad (fail, missing), suspect or
/// neither.
enum class FlagClass : std::uint8_t { fine, suspect, bad };

FlagClass classify_meaning(std::string_view meaning) {
  const std::string text = to_lower_ascii(meaning);
  if (mentions(text, "bad") or mentions(text, "fail") or
      mentions(text, "missing")) {
    return FlagClass::bad;
  }
  return mentions(text, "suspect") ? FlagClass::suspect : FlagClass::fine;
}

/// The rules of a flag variable by its `flag_meanings` (one per `flag_values`).
QualityRules rules_by_meaning(const nc::VarInfo& var,
                              const std::vector<std::int64_t>& values,
                              const std::vector<std::string>& meanings) {
  QualityRules rules{.var = var, .bad = {}, .suspect = {}};
  for (std::size_t i = 0; i < values.size(); ++i) {
    switch (classify_meaning(meanings[i])) {
      case FlagClass::bad:
        rules.bad.push_back(values[i]);
        break;
      case FlagClass::suspect:
        rules.suspect.push_back(values[i]);
        break;
      case FlagClass::fine:
        break;
    }
  }
  return rules;
}

QualityRules qartod_rules(const nc::VarInfo& var) {
  return {.var = var,
          .bad = {qartod_bad.begin(), qartod_bad.end()},
          .suspect = {qartod_suspect}};
}

/// The rules of a quality variable, or nullopt when its scheme is not one this
/// reader knows: `flag_values` with a `flag_meanings` of the same length
/// (classified by meaning), or `flag_values` of QARTOD alone.
std::expected<std::optional<QualityRules>, Error> quality_rules(
    const nc::File& file, const nc::VarInfo& var) {
  auto parts = collect(
      [&] {
        const auto type = file.att_type(var.name, "flag_values");
        if (not type) {
          return std::expected<std::optional<std::vector<std::int64_t>>, Error>{
              fail(type.error())};
        }
        // A flag_values of floats or text is no scheme here, not a fault.
        if (*type and not is_signed_integer(**type)) {
          return std::expected<std::optional<std::vector<std::int64_t>>, Error>{
              std::nullopt};
        }
        return int_att(file, var.name, "flag_values", var.name.view());
      },
      [&] { return optional_text(file, var.name, "flag_meanings"); });
  if (not parts) {
    return std::unexpected{std::move(parts).error()};
  }
  const auto& [values, meanings_text] = *parts;
  if (not values or values->empty()) {
    return std::optional<QualityRules>{};
  }
  if (meanings_text) {
    const std::vector<std::string> meanings = words(meanings_text);
    if (meanings.size() != values->size()) {
      return std::optional<QualityRules>{};
    }
    return std::optional{rules_by_meaning(var, *values, meanings)};
  }
  const bool qartod = std::ranges::all_of(*values, [](std::int64_t v) {
    return std::ranges::find(qartod_values, v) != qartod_values.end();
  });
  return qartod ? std::optional{qartod_rules(var)} : std::nullopt;
}

/// The rules of each data variable's quality variables. A variable of an
/// unknown scheme is ignored with one `quality_flags_ignored` warning (however
/// many data variables name it); a known scheme that masks and flags nothing
/// is left out.
std::expected<std::vector<std::vector<QualityRules>>, Error> all_quality_rules(
    const nc::File& file, std::span<const DataFacts> data,
    std::vector<Warning>& warnings) {
  std::vector<std::vector<QualityRules>> out(data.size());
  Names warned;
  for (std::size_t k = 0; k < data.size(); ++k) {
    for (const Facts* q : data[k].quality) {
      auto rules = quality_rules(file, q->var);
      if (not rules) {
        return std::unexpected{std::move(rules).error()};
      }
      if (not *rules) {
        if (warned.insert(std::string{q->var.name.view()}).second) {
          warnings.push_back({.code = WarningCode::quality_flags_ignored,
                              .subject = subject_of(q->var.name.view())});
        }
      } else if (not(*rules)->bad.empty() or not(*rules)->suspect.empty()) {
        out[k].push_back(**std::move(rules));
      }
    }
  }
  return out;
}

}  // namespace

std::expected<Read<ForeignSchema>, Error> foreign_schema(
    const nc::File& file, std::span<const Facts> all,
    std::span<const DataFacts> data) {
  std::vector<Raw> raws;
  raws.reserve(data.size());
  for (const DataFacts& d : data) {
    auto raw = raw_of(file, all, *d.facts);
    if (not raw) {
      return std::unexpected{std::move(raw).error()};
    }
    raws.push_back(*std::move(raw));
  }
  Read<ForeignSchema> out{.value = {}, .warnings = {}};
  auto quantities = quantities_of(raws, out.warnings);
  if (not quantities) {
    return std::unexpected{std::move(quantities).error()};
  }
  out.value.meta.reserve(raws.size());
  for (std::size_t k = 0; k < raws.size(); ++k) {
    Raw& r = raws[k];
    append(out.warnings, std::move(r.unit.warnings));
    const std::string_view name = r.facts->var.name.view();
    Read<core::SeriesMeta> meta =
        with_datum(core::SeriesMeta::make(
                       {.quantity = std::move((*quantities)[k]),
                        .label = r.long_name.value_or(std::string{name}),
                        .unit = std::move(r.unit.value)}),
                   r.datum.transform(datum_text), name);
    append(out.warnings, std::move(meta.warnings));
    out.value.meta.push_back(std::move(meta.value));
  }
  auto quality = all_quality_rules(file, data, out.warnings);
  if (not quality) {
    return std::unexpected{std::move(quality).error()};
  }
  out.value.quality = *std::move(quality);
  return out;
}

}  // namespace mov::io::detail::station_nc
