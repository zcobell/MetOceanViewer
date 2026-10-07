// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <optional>

#include "mov/core/station_table.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/detail/table_error.hpp"
#include "mov/io/error.hpp"

namespace {

using mov::core::ColumnIndex;
using mov::core::SchemaErrc;
using mov::core::StationIndex;
using mov::core::TableError;
using mov::io::FormatErrc;
using mov::io::FormatError;
using mov::io::detail::to_format_error;

FormatError expected(FormatErrc code, std::optional<std::size_t> station,
                     std::optional<std::size_t> index) {
  return FormatError{
      .code = code, .subject = {}, .station = station, .index = index};
}

TableError schema(SchemaErrc code, std::size_t column) {
  return mov::core::SchemaError{.code = code, .column = ColumnIndex{column}};
}

TableError station(std::size_t i, const mov::core::StationFault& fault) {
  return mov::core::StationError{.station = StationIndex{i}, .fault = fault};
}

}  // namespace

TEST_CASE("a StationTable error maps to the FormatError of the same meaning",
          "[io][detail][table_error]") {
  CHECK(to_format_error(schema(SchemaErrc::duplicate_quantity, 2)) ==
        expected(FormatErrc::duplicate_quantity, std::nullopt, 2));
  CHECK(to_format_error(schema(SchemaErrc::station_count_mismatch, 1)) ==
        expected(FormatErrc::dimension_mismatch, std::nullopt, 1));

  CHECK(to_format_error(station(3, mov::core::DuplicateStationId{})) ==
        expected(FormatErrc::duplicate_station_id, 3, std::nullopt));
  CHECK(to_format_error(station(4, mov::core::TimeOutOfRange{.index = 9})) ==
        expected(FormatErrc::time_out_of_range, 4, 9));
  CHECK(to_format_error(station(5, mov::core::TimeNotIncreasing{.index = 7})) ==
        expected(FormatErrc::time_not_increasing, 5, 7));
  CHECK(to_format_error(station(
            6, mov::core::ColumnLengthMismatch{.column = ColumnIndex{1}})) ==
        expected(FormatErrc::dimension_mismatch, 6, 1));
  CHECK(to_format_error(station(7, mov::core::AxisOutOfRange{})) ==
        expected(FormatErrc::dimension_mismatch, 7, std::nullopt));
  CHECK(to_format_error(station(8, mov::core::SchemaMismatch{})) ==
        expected(FormatErrc::dimension_mismatch, 8, std::nullopt));
}
