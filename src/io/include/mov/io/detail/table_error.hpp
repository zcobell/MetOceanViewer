// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <optional>
#include <variant>

#include "mov/core/detail/overloaded.hpp"
#include "mov/core/station_table.hpp"
#include "mov/io/error.hpp"

namespace mov::io::detail {

/// The FormatError for a StationTable::make failure (design section 5.0):
/// the station and index are kept. A reader that builds the table from parts
/// it has already checked cannot reach most of these; the mapping exists so
/// that none is silently lost.
[[nodiscard]] inline FormatError to_format_error(const core::TableError& e) {
  const auto error = [](FormatErrc code, std::optional<std::size_t> index) {
    return FormatError{
        .code = code, .subject = {}, .station = std::nullopt, .index = index};
  };
  const auto schema = [&error](const core::SchemaError& s) {
    return error(s.code == core::SchemaErrc::duplicate_quantity
                     ? FormatErrc::duplicate_quantity
                     : FormatErrc::dimension_mismatch,
                 s.column.value());
  };
  const auto station = [&error](const core::StationError& s) {
    FormatError mapped = std::visit(
        core::detail::Overloaded{
            [&error](core::DuplicateStationId) {
              return error(FormatErrc::duplicate_station_id, std::nullopt);
            },
            [&error](core::TimeOutOfRange t) {
              return error(FormatErrc::time_out_of_range, t.index);
            },
            [&error](core::TimeNotIncreasing t) {
              return error(FormatErrc::time_not_increasing, t.index);
            },
            [&error](core::ColumnLengthMismatch c) {
              return error(FormatErrc::dimension_mismatch, c.column.value());
            },
            // AxisOutOfRange and SchemaMismatch: the parts do not fit.
            [&error](auto) {
              return error(FormatErrc::dimension_mismatch, std::nullopt);
            }},
        s.fault);
    mapped.station = s.station.value();
    return mapped;
  };
  return std::visit(core::detail::Overloaded{schema, station}, e);
}

}  // namespace mov::io::detail
