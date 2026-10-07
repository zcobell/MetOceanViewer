// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// What the netCDF model-output readers (ADCIRC, D-Flow FM) share: finding
// dimensions and variables by name, the station list, the time axis, and the
// block-wise gather of the selected stations' columns. Private to src/io/.

#pragma once

#include <algorithm>
#include <cstddef>
#include <expected>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/cf_time.hpp"
#include "mov/io/detail/checked_product.hpp"
#include "mov/io/detail/station_groups.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io::detail {

// ---- errors ----------------------------------------------------------------

[[nodiscard]] FormatError format_error(
    FormatErrc code, std::string subject,
    std::optional<std::size_t> station = std::nullopt,
    std::optional<std::size_t> index = std::nullopt);

template <class E>
[[nodiscard]] auto fail(E&& e) {
  return std::unexpected{lift<Error>(std::forward<E>(e))};
}

/// An NcError the reader itself raises (a limit, a type it refuses), naming
/// the variable and the file.
[[nodiscard]] NcError nc_fault(const nc::File& file, WrapperFault fault,
                               NcOp op, std::string_view object);

// ---- structure -------------------------------------------------------------

/// `missing_dimension` (subject: the name) unless the file has it.
[[nodiscard]] std::expected<nc::DimInfo, Error> require_dim(
    const nc::File& file, nc::NcNameRef name);

/// `missing_variable` (subject: the name) unless the file has it.
[[nodiscard]] std::expected<nc::VarInfo, Error> require_var(
    const nc::File& file, nc::NcNameRef name);

/// `dimension_mismatch` (subject: the variable) unless `var` is over exactly
/// the dimensions with these ids, in this order. Identity, never position: a
/// dimension is what find_dim found by name (B12).
[[nodiscard]] std::expected<void, Error> require_shape(
    const nc::VarInfo& var, std::initializer_list<int> dim_ids);

/// `station_count_mismatch` unless `selection` was made for a file of
/// `station_count` stations.
[[nodiscard]] std::expected<void, Error> require_selection(
    const core::StationSelection& selection, std::size_t station_count);

/// The text attribute `att` of `on`, or nullopt when it is absent or not a
/// text of one string (a numeric attribute is no label, no unit).
[[nodiscard]] std::expected<std::optional<std::string>, Error> optional_text(
    const nc::File& file, nc::AttTarget on, nc::NcNameRef att);

// ---- stations --------------------------------------------------------------

/// Where a model file keeps its stations.
struct StationVariables {
  const nc::DimInfo& dim;  // the station dimension
  nc::NcNameRef x;         // double (station)
  nc::NcNameRef y;         // double (station)
  /// char (station, length): absent in some ADCIRC files.
  std::optional<nc::NcNameRef> names;
  core::DataSource source;
};

/// The stations `which` (indices into the station dimension, in the order
/// wanted): id the 0-based index in decimal; name from the rows of `names`
/// (cut at the first NUL, white space simplified, bytes that are not UTF-8
/// replaced), or "Station <id>" if empty or the file has no names; position
/// projected from `crs` to WGS84, with the native point kept when `crs` is
/// not EPSG:4326. The coordinates are read with their own type converted to
/// double only if no value changes (B4); 64-bit integers are refused.
/// Errors: those of require_var / require_shape for the variables;
/// `unsupported_crs`, `projection_unavailable`, `bad_coordinates` (with the
/// station); NcError; Cancelled. Warnings: `invalid_utf8_replaced` (count:
/// names), `crs_approximate`.
[[nodiscard]] std::expected<Read<std::vector<core::FileStation>>, Error>
read_stations(const nc::File& file, const StationVariables& vars,
              core::Epsg crs, std::span<const std::size_t> which,
              const StopToken& stop);

// ---- time ------------------------------------------------------------------

/// The clock of a (units, calendar) pair: `unsupported_calendar` (subject:
/// the variable) when the standard calendar starts before 1582-10-15.
[[nodiscard]] std::expected<CfClock, Error> make_clock(
    const CfTimeUnits& units, CfCalendar calendar, std::string_view variable);

/// Every value of the time variable (over exactly the time dimension) as a
/// time on `clock`. A masked value is `time_missing` and one the clock
/// refuses is `time_out_of_range`, both with its index; the axis must be
/// strictly increasing (`time_not_increasing`, with the index of the first
/// element that is not above its predecessor). Integer (64-bit) times are
/// read as integers, all others masked in their own type.
[[nodiscard]] std::expected<core::TimeAxis, Error> read_time_axis(
    const nc::File& file, const nc::VarInfo& time, const CfClock& clock,
    const StopToken& stop);

// ---- values ----------------------------------------------------------------

/// `too_large` (an NcError about `variable`) when `stations` x `times` x
/// `columns` samples are more than limits.max_elements, or their bytes more
/// than limits.max_result_bytes.
[[nodiscard]] std::expected<void, Error> check_result_size(
    const nc::File& file, std::string_view variable, std::size_t stations,
    std::size_t times, std::size_t columns);

/// `too_large` (an NcError about the dimension) when the file has more
/// stations than limits.max_elements: checked before a reader makes a list of
/// them.
[[nodiscard]] std::expected<void, Error> check_station_count(
    const nc::File& file, const nc::DimInfo& dim);

/// The table of one shared time axis, the stations (`read`'s value and its
/// warnings come first, then `warnings`) and the variables, one column per
/// station each. StationTable::make failures are mapped like every reader's.
[[nodiscard]] std::expected<Read<core::StationTable>, Error> assemble_table(
    std::vector<core::Variable> variables, core::TimeAxis axis,
    Read<std::vector<core::FileStation>> stations,
    std::vector<Warning> warnings);

/// An empty column of `times` Missing samples for each of `stations`.
[[nodiscard]] std::vector<core::Column> empty_columns(std::size_t stations,
                                                      std::size_t times);

/// The groups of the selected stations that are read together for `var`, over
/// (time, station[, ...]): `forced` if given (tests, the measurement), else
/// grouping_for the chunk shape of the variable.
[[nodiscard]] std::expected<std::vector<StationGroup>, Error> plan_groups(
    const nc::File& file, const nc::VarInfo& var,
    std::span<const std::size_t> selection,
    const std::optional<GroupingPolicy>& forced);

/// What one gather reads.
struct GatherPlan {
  nc::NcNameRef variable;
  std::size_t times;                 // the leading dimension, whole
  std::optional<std::size_t> layer;  // the trailing index of a 3-D variable
  std::span<const StationGroup> groups;
};

/// The most time steps one read of `width` stations may cover under the
/// file's limits (at least 1): a read is checked as a whole against
/// max_elements and max_result_bytes before it starts, and a group spans the
/// stations between its first and last selected station, so a selection of
/// two far apart stations would otherwise fail on a span that is never held
/// in memory at once (File::read_blocks holds one block).
[[nodiscard]] inline std::size_t rows_per_window(const ReadLimits& limits,
                                                 std::size_t width,
                                                 std::size_t element_bytes) {
  const std::size_t by_count = limits.max_elements / width;
  const std::size_t by_bytes = limits.max_result_bytes / (width * element_bytes);
  return std::max<std::size_t>(1, std::min(by_count, by_bytes));
}

/// Reads `plan.variable` over (time, station[, layer]) one group at a time,
/// each as File::read_blocks of whole time steps over the stations the group
/// spans (in windows of rows_per_window time steps), and calls
/// `sink(position, time_index, raw)` for every selected station's value, in
/// time order per station. The block partition is read_blocks' (its
/// rows_per_block); this only walks it.
template <nc::Numeric T, class Sink>
[[nodiscard]] std::expected<void, Error> gather(const nc::File& file,
                                                const GatherPlan& plan,
                                                const StopToken& stop,
                                                Sink&& sink) {
  for (const StationGroup& group : plan.groups) {
    const std::size_t width = group.width();
    const std::size_t window = rows_per_window(file.limits(), width, sizeof(T));
    for (std::size_t t0 = 0; t0 < plan.times; t0 += window) {
      nc::Slab slab{{.start = t0, .count = std::min(window, plan.times - t0)},
                    {.start = group.first, .count = width}};
      if (plan.layer) {
        slab.push_back({.start = *plan.layer, .count = 1});
      }
      auto done = file.read_blocks<T>(
          plan.variable, slab,
          [&](std::span<const T> block,
              nc::DimRange rows) -> std::expected<void, Error> {
            for (const SelectedStation& member : group.members) {
              const std::size_t column = member.station - group.first;
              for (std::size_t r = 0; r < rows.count; ++r) {
                sink(member.position, rows.start + r,
                     block[r * width + column]);
              }
            }
            return {};
          },
          stop);
      if (not done) {
        return done;
      }
    }
  }
  return {};
}

}  // namespace mov::io::detail
