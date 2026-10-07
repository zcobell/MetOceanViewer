// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Builders for the StationTables the IMEDS and CSV tests write, and readers
// of what the IMEDS parser returns.

#pragma once

#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"

namespace mov::test {

/// UTC date and time as a core::Time.
[[nodiscard]] inline core::Time utc(int year, unsigned month, unsigned day,
                                    int hour = 0, int minute = 0,
                                    int second = 0, int millisecond = 0) {
  namespace chrono = std::chrono;
  return core::Time{chrono::sys_days{chrono::year{year} / chrono::month{month} /
                                     chrono::day{day}}} +
         chrono::hours{hour} + chrono::minutes{minute} +
         chrono::seconds{second} + chrono::milliseconds{millisecond};
}

/// A sample that is a value (a test input is always finite).
[[nodiscard]] inline core::Sample value(double v) {
  const std::optional<core::Sample> sample = core::Sample::of(v);
  REQUIRE(sample.has_value());
  return *sample;
}

[[nodiscard]] inline core::Location location(double lat, double lon) {
  const auto where = core::Location::make({.lat = lat, .lon = lon});
  REQUIRE(where.has_value());
  return *where;
}

/// One station of a test table.
struct StationSpec {
  std::string id;
  std::string name;
  core::Location where;
  std::vector<core::Time> times;
  std::vector<core::Sample> samples;
};

[[nodiscard]] inline core::FileStation file_station(const StationSpec& s) {
  auto id = core::StationKey::make(s.id);
  auto name = core::StationText::make(s.name);
  REQUIRE(id.has_value());
  REQUIRE(name.has_value());
  return {.id = *std::move(id),
          .name = *std::move(name),
          .location = s.where,
          .native = std::nullopt,
          .source = core::DataSource::user};
}

/// A table with the one column `meta` and a station per spec, each on its own
/// axis.
[[nodiscard]] inline core::StationTable one_column_table(
    const core::SeriesMeta& meta, const std::vector<StationSpec>& specs) {
  std::vector<core::TimeAxis> axes;
  std::vector<core::Column> column;
  std::vector<core::StationRow> rows;
  for (const StationSpec& s : specs) {
    rows.push_back({.station = file_station(s), .axis = axes.size()});
    axes.push_back(s.times);
    column.push_back(s.samples);
  }
  std::vector<core::Variable> variables;
  variables.push_back({.meta = meta, .per_station = std::move(column)});
  auto table = core::StationTable::make(std::move(variables), std::move(axes),
                                        std::move(rows));
  REQUIRE(table.has_value());
  return *std::move(table);
}

/// The samples of column `k` of station `i`, as numbers (nullopt for a
/// non-value).
[[nodiscard]] inline std::vector<std::optional<double>> numbers(
    const core::StationTable& t, std::size_t i, std::size_t k = 0) {
  std::vector<std::optional<double>> out;
  for (const core::Sample& s :
       t.column(core::StationIndex{i}, core::ColumnIndex{k})) {
    out.push_back(s.value());
  }
  return out;
}

}  // namespace mov::test
