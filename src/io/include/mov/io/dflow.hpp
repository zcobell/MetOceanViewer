// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/io/cf_time.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/name.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io {

// D-Flow FM history ("his") output (LF section 4): dimensions `time`,
// `stations` and `name_len`, and for a 3-D model `laydim` (layer centres) and
// `laydimw` (layer interfaces); variables `time(time)`, `station_name(stations,
// name_len)`, `station_x_coordinate(stations)`,
// `station_y_coordinate(stations)` and the data, `(time, stations)` or `(time,
// stations, laydim)`. Variables on `laydimw` are not offered. There is no real
// D-Flow file in the repository (plan decision 26): the format is read as the
// D-Flow FM manual describes it.
//
// Unlike ADCIRC, D-Flow has no dry sentinel (a dry station reports its bed
// level, which can be below -999 m): a value is missing only when the
// variable's own attributes say so (_FillValue, missing_value, valid range,
// the library default) or it is NaN.
//
// netCDF-C is not thread-safe: the caller serializes these functions with
// every other netCDF call. Each opens the file once and closes it before it
// returns. Blocking I/O: call them from a worker.

/// The variables a request derives from others, as the plot list of v4 offered
/// them. The token is v4's name for it (sessions stored it).
enum class DflowDerived : std::uint8_t {
  current_speed_2d,      // hypot(x_velocity, y_velocity)
  current_direction_2d,  // atan2(y_velocity, x_velocity), degrees
  current_speed_3d,      // hypot(x_velocity, y_velocity, z_velocity)
  wind_speed,            // hypot(windx, windy)
  wind_direction,        // atan2(windy, windx), degrees
};

/// "2D_current_speed", "2D_current_direction", "3D_current_speed",
/// "wind_speed", "wind_direction".
[[nodiscard]] std::string_view to_token(DflowDerived d) noexcept;
[[nodiscard]] std::optional<DflowDerived> parse_dflow_derived(
    std::string_view token) noexcept;

/// What a variable is read from: a variable of the file, or a derivation from
/// several (which must all have the same shape).
using DflowSource = std::variant<nc::NcName, DflowDerived>;

/// A variable over (time, stations).
struct Flat {
  DflowSource source;
  std::string long_name;
  friend bool operator==(const Flat&, const Flat&) = default;
};

/// A variable over (time, stations, laydim) with `layers` layers. A variable
/// derived from layered ones is layered.
struct Layered {
  DflowSource source;
  std::string long_name;
  std::size_t layers;
  friend bool operator==(const Layered&, const Layered&) = default;
};

/// What the file offers: file order, then the derived ones that the file has
/// the inputs for.
using DflowVariable = std::variant<Flat, Layered>;

/// A layer of a layered variable, counted from 1 as v4's layer argument was
/// (N16: v4 passed layer 0 for the wind of a 3-D file and underflowed).
class Layer {
 public:
  /// `layer_out_of_range` (subject: the variable's long name, index: the
  /// number given) unless 1 <= one_based <= v.layers.
  [[nodiscard]] static std::expected<Layer, FormatError> make(
      const Layered& v, std::size_t one_based);

  [[nodiscard]] std::size_t zero_based() const noexcept { return index_; }
  friend constexpr bool operator==(Layer, Layer) = default;

 private:
  explicit constexpr Layer(std::size_t index) noexcept : index_{index} {}
  std::size_t index_;
};

/// A layered variable at one of its layers.
struct AtLayer {
  Layered variable;
  Layer layer;
  friend bool operator==(const AtLayer&, const AtLayer&) = default;
};

/// What to read: a flat variable, or a layered one at a layer. The type is the
/// shape, so a layered variable cannot be asked for without a layer.
using DflowChoice = std::variant<Flat, AtLayer>;

struct DflowCatalog {
  /// Every station, in file order: id the 0-based index, name from
  /// `station_name` (cut at the first NUL, white space simplified; "Station
  /// <id>" if empty), position projected to WGS84, source `dflowfm`.
  std::vector<core::FileStation> stations;
  std::vector<DflowVariable> variables;
  std::size_t times;
  CfTimeUnits time_units;
  CfCalendar calendar;
  friend bool operator==(const DflowCatalog&, const DflowCatalog&) = default;
};

/// The stations and variables of a history file. `crs` is the CRS of
/// `station_x_coordinate` and `station_y_coordinate`; the file does not say
/// which CRS that is.
///
/// A variable over (time, stations[, laydim]) of a numeric type is offered;
/// another type is skipped with `skipped_variable`. The dimensions are found by
/// name and variables are matched to them by identity, never by position: a
/// file whose `time` is not the first dimension reads the same (B12).
///
/// Errors: FormatError `missing_dimension` (`time`, `stations`, `name_len`),
/// `missing_variable` (`time`, `station_x_coordinate`, `station_y_coordinate`,
/// `station_name`), `missing_attribute` (`time:units`), `dimension_mismatch`
/// (a variable not over the dimensions named above),
/// `unsupported_calendar`, `unsupported_crs`, `projection_unavailable`,
/// `bad_coordinates`; ParseError `bad_time_units` / `bad_date` (the `units`
/// of `time`); NcError (including `too_large`); Cancelled.
/// Warnings: `invalid_utf8_replaced`, `crs_approximate`, `skipped_variable`,
/// `time_precision_dropped`.
[[nodiscard]] std::expected<Read<DflowCatalog>, Error> inspect_dflow(
    const std::filesystem::path& path, core::Epsg crs, const ReadContext& ctx);

struct DflowRequest {
  DflowChoice choice;
  /// Required (never an implicit "all"); made for the file's station count.
  core::StationSelection stations;
  core::Epsg crs;
};

/// The selected stations of one variable, with one shared time axis and the
/// stations in the order of the selection.
///
/// Schema: one column. A variable named like a registry quantity (`waterlevel`
/// is `water_level`, `x_velocity` / `y_velocity` are `current_u` / `current_v`,
/// `windx` / `windy` are `wind_u` / `wind_v`) has it; any other becomes a
/// generic quantity with its name as the token and its `standard_name`, or
/// `value` with an `unknown_quantity` warning when the name is no token.
/// The label is `long_name` (else the name) and the unit the `units`
/// attribute (else the registry quantity's own unit, else none; an unknown
/// spelling warns `unrecognized_unit`). A derived variable has the meta of
/// core::VectorSeries::magnitude / cartesian_direction / magnitude3 (its
/// components must be in one known unit: `noncanonical_unit`).
///
/// A layered variable is read at the one layer; the layer is checked against
/// `laydim` of the file (`layer_out_of_range`).
///
/// Errors (those of inspect_dflow, and): FormatError `station_count_mismatch`
/// (the selection was made for another count), `missing_variable` (the
/// variable, or a component of a derived one, is not in the file),
/// `dimension_mismatch` (a layer given for a flat variable or none for a
/// layered one), `layer_out_of_range`, `time_missing`, `time_out_of_range`,
/// `time_not_increasing`; NcError `too_large` (selected stations x times over
/// `ReadLimits::max_elements`, or their samples over `max_result_bytes`).
/// Warnings: those of inspect_dflow (except `skipped_variable`), and
/// `nonfinite_masked`, `unknown_quantity`, `unrecognized_unit`.
[[nodiscard]] std::expected<Read<core::StationTable>, Error> read_dflow(
    const std::filesystem::path& path, const DflowRequest& request,
    const ReadContext& ctx);

}  // namespace mov::io
