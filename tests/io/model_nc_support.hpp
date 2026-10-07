// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Helpers shared by the ADCIRC netCDF and D-Flow FM tests.

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <format>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "adcirc_test_support.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station_table.hpp"
#include "mov/io/error.hpp"
#include "mov/io/projection.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/test/scratch_dir.hpp"
#include "model_fixtures.hpp"

namespace mov::test {

/// The projection database, for the tests that project.
inline void configure_projection_database() {
#if defined(MOV_TEST_PROJ_DATA_DIR)
  io::set_projection_data_dir(MOV_TEST_PROJ_DATA_DIR);
#endif
}

/// A file of the legacy tree (MetOceanViewer/function_tests/...), or nullopt
/// when the tree is not there: the [legacy] tests skip then.
[[nodiscard]] inline std::optional<std::filesystem::path> legacy_file(
    std::string_view relative) {
#if defined(MOV_TEST_REPO_DIR)
  const std::filesystem::path path =
      std::filesystem::path{MOV_TEST_REPO_DIR} / "MetOceanViewer" /
      "function_tests" / relative;
  if (std::filesystem::is_regular_file(path)) {
    return path;
  }
#endif
  static_cast<void>(relative);
  return std::nullopt;
}

/// The samples of column `k` of station `i`, copied.
[[nodiscard]] inline std::vector<core::Sample> samples_of(
    const core::StationTable& table, std::size_t i, std::size_t k) {
  const std::span<const core::Sample> column =
      table.column(core::StationIndex{i}, core::ColumnIndex{k});
  return {column.begin(), column.end()};
}

/// The number in `s`; the test fails if it is Dry or Missing.
[[nodiscard]] inline double number(const core::Sample& s) {
  const std::optional<double> v = s.value();
  REQUIRE(v.has_value());
  return v.value_or(0.0);
}

/// The NcError inside an io::Error, or nullptr.
[[nodiscard]] inline const io::NcError* nc_error_in(const io::Error& error) {
  return std::get_if<io::NcError>(&error);
}

/// A short text for a failure message of a test (the real describe() is for
/// the edge of the application).
[[nodiscard]] inline std::string what(const io::Error& error) {
  if (const auto* format = std::get_if<io::FormatError>(&error)) {
    return std::format("FormatError code {} subject '{}' station {} index {}",
                       static_cast<int>(format->code), format->subject,
                       format->station.value_or(9999),
                       format->index.value_or(9999));
  }
  if (const auto* nc = std::get_if<io::NcError>(&error)) {
    return std::format("NcError op {} object '{}' file {}",
                       static_cast<int>(nc->op), nc->object,
                       nc->file.string());
  }
  if (const auto* parse = std::get_if<io::ParseError>(&error)) {
    return std::format("ParseError code {} context '{}'",
                       static_cast<int>(parse->code()), parse->context());
  }
  return std::format("error alternative {}", error.index());
}

/// The path of file `name` in `dir`.
[[nodiscard]] inline std::filesystem::path in(const ScratchDir& dir,
                                              std::string_view name) {
  return dir / name;
}

}  // namespace mov::test
