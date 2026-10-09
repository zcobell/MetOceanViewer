// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// What a foreign CF file says about each of its variables (the attributes CF
// reads a discrete-sampling-geometry file by), and the pieces of the foreign
// reader that work from them: the schema of the data variables
// (station_netcdf_foreign_schema.cpp) and the quality-flag rules. Private to
// src/io/.

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "mov/core/meta.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read.hpp"
#include "station_netcdf_dialects.hpp"
#include "station_netcdf_shared.hpp"

namespace mov::io::detail::station_nc {

using Names = std::set<std::string, std::less<>>;

/// The attributes of one variable that identify what it is.
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

/// The facts of every variable of the file, in file order.
[[nodiscard]] std::expected<std::vector<Facts>, Error> all_facts(
    const nc::File& file);

/// `text` is `value` (ignoring case and blanks around it).
[[nodiscard]] bool says(const std::optional<std::string>& text,
                        std::string_view value);

/// The blank-separated words of an attribute; none when it is absent.
[[nodiscard]] std::vector<std::string> words(
    const std::optional<std::string>& text);

/// The name a `grid_mapping` attribute points at: its first word, without the
/// `:` of the extended form (`crs: lat lon`); nullopt without one.
[[nodiscard]] std::optional<std::string> mapping_name(const Facts& facts);

/// The variable, among the facts, called `name`.
[[nodiscard]] const Facts* facts_named(std::span<const Facts> all,
                                       std::string_view name);

/// One data variable of a foreign file: the facts it was found by and the
/// quality-flag variables its `ancillary_variables` name.
struct DataFacts {
  const Facts* facts;
  std::vector<const Facts*> quality;
};

/// The metadata of each data variable (quantity, label, unit, datum), in the
/// order of `data`, and the quality-flag rules of the file's flag variables
/// (one entry per `DataFacts::quality` entry of every data variable, in the
/// same order); warnings about what could not be used.
///
/// Quantities: a registry quantity when the standard name is its own and the
/// unit converts to its canonical unit (of the two that share
/// `water_surface_height_above_reference_datum`, the one the variable's name
/// or long_name hints: predict, tide, astronomical, harmonic say prediction);
/// otherwise a generic one named after the variable. Names are made in two
/// passes: every variable whose name is a writable token (writable_token)
/// claims it first, then the others get substitutes (`variable_renamed`).
struct ForeignSchema {
  std::vector<core::SeriesMeta> meta;
  /// Per data variable, the rules of its quality variables.
  std::vector<std::vector<QualityRules>> quality;
};

[[nodiscard]] std::expected<Read<ForeignSchema>, Error> foreign_schema(
    const nc::File& file, std::span<const Facts> all,
    std::span<const DataFacts> data);

}  // namespace mov::io::detail::station_nc
