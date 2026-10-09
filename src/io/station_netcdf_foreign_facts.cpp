// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "station_netcdf_foreign_facts.hpp"

#include <algorithm>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/ascii.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read.hpp"
#include "station_netcdf_reader.hpp"

namespace mov::io::detail::station_nc {

namespace {

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

}  // namespace

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

bool says(const std::optional<std::string>& text, std::string_view value) {
  return text.has_value() and
         core::ascii::equal_ignore_case(core::ascii::trim(*text), value);
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

std::optional<std::string> mapping_name(const Facts& facts) {
  std::vector<std::string> w = words(facts.grid_mapping);
  if (w.empty()) {
    return std::nullopt;
  }
  std::string first = std::move(w.front());
  if (first.ends_with(':')) {
    first.pop_back();
  }
  return first;
}

const Facts* facts_named(std::span<const Facts> all, std::string_view name) {
  const auto it = std::ranges::find_if(
      all, [name](const Facts& f) { return f.var.name.view() == name; });
  return it == all.end() ? nullptr : &*it;
}

}  // namespace mov::io::detail::station_nc
