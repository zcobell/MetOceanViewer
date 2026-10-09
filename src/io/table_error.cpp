// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/detail/table_error.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "mov/core/overloaded.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/io/error.hpp"
#include "mov/io/projection.hpp"

namespace mov::io::detail {

namespace {

FormatError error_of(FormatErrc code, std::string subject = {},
                     std::optional<std::size_t> station = std::nullopt,
                     std::optional<std::size_t> index = std::nullopt) {
  return FormatError{.code = code,
                     .subject = std::move(subject),
                     .station = station,
                     .index = index};
}

FormatError from_schema(const core::SchemaError& s) {
  switch (s.code) {
    case core::SchemaErrc::duplicate_quantity:
      return error_of(FormatErrc::duplicate_quantity, {}, std::nullopt,
                      s.column.value());
    case core::SchemaErrc::station_count_mismatch:
      return error_of(FormatErrc::dimension_mismatch, {}, std::nullopt,
                      s.column.value());
  }
  return error_of(FormatErrc::dimension_mismatch);
}

FormatError from_station(const core::StationError& s) {
  const std::size_t station = s.station.value();
  return std::visit(
      core::Overloaded{
          [station](core::DuplicateStationId) {
            return error_of(FormatErrc::duplicate_station_id, {}, station);
          },
          [station](core::AxisOutOfRange) {
            return error_of(FormatErrc::dimension_mismatch, {}, station);
          },
          [station](core::TimeOutOfRange t) {
            return error_of(FormatErrc::time_out_of_range, {}, station,
                            t.index);
          },
          [station](core::TimeNotIncreasing t) {
            return error_of(FormatErrc::time_not_increasing, {}, station,
                            t.index);
          },
          [station](core::ColumnLengthMismatch c) {
            return error_of(FormatErrc::dimension_mismatch, {}, station,
                            c.column.value());
          },
          [station](core::SchemaMismatch) {
            return error_of(FormatErrc::dimension_mismatch, {}, station);
          }},
      s.fault);
}

std::string crs_subject(core::Epsg crs) {
  return "EPSG:" + std::to_string(crs.code());
}

}  // namespace

FormatError to_format_error(const core::TableError& e) {
  return std::visit(
      core::Overloaded{
          [](const core::SchemaError& s) { return from_schema(s); },
          [](const core::StationError& s) { return from_station(s); }},
      e);
}

FormatError to_format_error(core::StationKeyError e, std::size_t station) {
  switch (e) {
    case core::StationKeyError::empty:
      return error_of(FormatErrc::no_station_id, {}, station);
    case core::StationKeyError::embedded_nul:
    case core::StationKeyError::invalid_utf8:
      return error_of(FormatErrc::bad_encoding, {}, station);
  }
  return error_of(FormatErrc::bad_encoding, {}, station);
}

FormatError to_format_error(core::StationTextError e, std::size_t station) {
  switch (e) {
    case core::StationTextError::embedded_nul:
    case core::StationTextError::invalid_utf8:
      return error_of(FormatErrc::bad_encoding, {}, station);
  }
  return error_of(FormatErrc::bad_encoding, {}, station);
}

FormatError to_format_error(const ProjectionError& e,
                            std::optional<std::size_t> station) {
  switch (e.code) {
    case ProjectionErrc::unknown_crs:
      return error_of(FormatErrc::unsupported_crs, crs_subject(e.crs), station);
    case ProjectionErrc::database_unavailable:
      return error_of(FormatErrc::projection_unavailable, crs_subject(e.crs),
                      station);
    case ProjectionErrc::transform_failed:
      return error_of(FormatErrc::bad_coordinates, crs_subject(e.crs), station);
  }
  return error_of(FormatErrc::bad_coordinates, crs_subject(e.crs), station);
}

}  // namespace mov::io::detail
