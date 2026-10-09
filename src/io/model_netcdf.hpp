// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// What the netCDF readers share: opening and closing a file, finding
// dimensions and variables by name, attribute text, the clock and the times
// of a time variable, and, for the model-output readers (ADCIRC, D-Flow FM),
// the station list and the block-wise gather of the selected stations'
// columns. Private to src/io/.

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/station.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/cf_time.hpp"
#include "mov/io/detail/checked_product.hpp"
#include "mov/io/detail/reporting.hpp"
#include "mov/io/detail/station_groups.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io::detail {

// ---- the file
// ----------------------------------------------------------------

/// Opens `path`, runs `read` on the open file and closes it, on every path:
/// the one way a netCDF entry point holds a file. A failed open is the result;
/// a failed close after a successful `read` is too (the read may have seen
/// data the library never managed to finish), and after a failed one the
/// read's error is kept.
template <class F>
  requires std::invocable<const F&, const nc::File&>
[[nodiscard]] auto with_file(const std::filesystem::path& path,
                             const ReadLimits& limits, const F& read)
    -> std::invoke_result_t<const F&, const nc::File&> {
  auto file = nc::File::open(path, limits);
  if (not file) {
    return fail(std::move(file).error());
  }
  auto result = read(std::as_const(*file));
  if (auto closed = std::move(*file).close(); not closed and result) {
    return fail(std::move(closed).error());
  }
  return result;
}

// ---- errors ----------------------------------------------------------------

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

/// The text attribute `att` of `on` up to its first NUL, or nullopt when it
/// is absent or not a text of one string (a numeric attribute is no label, no
/// unit). Every reader gets its attribute text here: fixed-width writers pad
/// with NULs and may leave junk after them, so the cut is this rule's, not each
/// caller's.
[[nodiscard]] std::expected<std::optional<std::string>, Error> optional_text(
    const nc::File& file, nc::AttTarget on, nc::NcNameRef att);

// ---- stations --------------------------------------------------------------

/// Where a model file keeps its stations. The whole file's stations are read
/// (every coordinate and every name) whatever `which` selects below: the
/// variables are small next to the data, and a slab of a char variable would
/// need the wrapper to read one.
struct StationVariables {
  const nc::DimInfo& dim;  // the station dimension
  nc::NcNameRef x;         // double (station)
  nc::NcNameRef y;         // double (station)
  /// char (station, length): absent in some ADCIRC files.
  std::optional<nc::NcNameRef> names;
  /// The id of the time dimension, for files whose coordinates may be over
  /// (time, station) (D-Flow FM with moving stations): step 0 is read, with
  /// the warning `coordinates_from_first_step`.
  std::optional<int> time_dim;
  core::DataSource source;
};

/// The stations `which` (indices into the station dimension, in the order
/// wanted): id the 0-based index in decimal; name from the rows of `names`
/// (cut at the first NUL, white space simplified, bytes that are not UTF-8
/// replaced), empty when nothing is left or the file has no names; position
/// projected from `crs` to WGS84, with the native point kept when `crs` is
/// not EPSG:4326. The coordinates are read with their own type converted to
/// double only if no value changes (B4); 64-bit integers are refused.
/// Errors: those of require_var / require_shape for the variables;
/// `unsupported_crs`, `projection_unavailable`, `bad_coordinates` (with the
/// station); NcError; Cancelled. Warnings: `invalid_utf8_replaced` (count:
/// names), `crs_approximate`, `coordinates_from_first_step`.
[[nodiscard]] std::expected<Read<std::vector<core::FileStation>>, Error>
read_stations(const nc::File& file, const StationVariables& vars,
              core::Epsg crs, std::span<const std::size_t> which,
              const StopToken& stop);

// ---- time ------------------------------------------------------------------

/// The `calendar` of the time variable (absent: standard), or
/// `unsupported_calendar` (subject: `<variable>:calendar`).
[[nodiscard]] std::expected<CfCalendar, Error> read_calendar(
    const nc::File& file, const nc::VarInfo& time);

/// The clock of a (units, calendar) pair: `unsupported_calendar` (subject:
/// the variable) when the standard calendar starts before 1582-10-15.
[[nodiscard]] std::expected<CfClock, Error> make_clock(
    const CfTimeUnits& units, CfCalendar calendar, std::string_view variable);

/// The clock of a time variable: its `units` (`missing_attribute`, subject
/// `<variable>:units`, without; the ParseError of parse_cf_time_units if they
/// do not parse) and its `calendar` (read_calendar, make_clock), with the
/// warnings of parse_cf_time_units. The ADCIRC reader puts its cold-start
/// rule on top of this.
[[nodiscard]] std::expected<Read<CfClock>, Error> clock_of(
    const nc::File& file, const nc::VarInfo& time_var);

/// The samples of `slab` of the time variable `time`, masked in its own type
/// (nc::File::read_samples). A 64-bit integer time is read as integers, masked
/// and only then widened: every time a clock accepts is below 2^53 in
/// magnitude, so the widening is exact for those and a larger value stays out
/// of range.
[[nodiscard]] std::expected<std::vector<core::Sample>, Error> read_time_samples(
    const nc::File& file, const nc::VarInfo& time, const nc::Slab& slab,
    const StopToken& stop);

/// The time of each of `values` (samples of the time variable `var`) on
/// `clock`, in file order: a Missing one is `time_missing` and one the clock
/// refuses `time_out_of_range`, each with `station` (nullopt: a time axis
/// every station shares) and its index.
[[nodiscard]] std::expected<core::TimeAxis, Error> times_of(
    std::span<const core::Sample> values, const CfClock& clock,
    std::string_view var, std::optional<std::size_t> station);

/// `times` if each is above the one before it; else `time_not_increasing`
/// about `var`, with `station` and the index of the first that is not. The
/// rule of every time axis a file must hold in order (CF coordinate variables,
/// v5 station files); the readers of lenient files put theirs in order
/// instead.
[[nodiscard]] std::expected<core::TimeAxis, Error> strictly_increasing(
    core::TimeAxis times, std::string_view var,
    std::optional<std::size_t> station);

/// Every value of the time variable (over exactly the time dimension) as a
/// time on `clock`: read_time_samples, times_of and strictly_increasing, with
/// no station.
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

/// Runs `run.template operator()<T>()` with the memory type of the data
/// variable `var` (double, float, int8_t, int16_t or int32_t: the types
/// nc::sample_readable lets through), or fails with `type_mismatch` for any
/// other: 64-bit integers, unsigned, text. There is no instantiation for the
/// types a model reader refuses.
template <class Run>
[[nodiscard]] std::expected<void, Error> dispatch_model_numeric(
    const nc::File& file, const nc::VarInfo& var, Run&& run) {
  if (nc::sample_readable(var.type)) {
    switch (var.type) {
      case nc::Type::double_:
        return run.template operator()<double>();
      case nc::Type::float_:
        return run.template operator()<float>();
      case nc::Type::byte:
        return run.template operator()<std::int8_t>();
      case nc::Type::short_:
        return run.template operator()<std::int16_t>();
      case nc::Type::int_:
        return run.template operator()<std::int32_t>();
      default:
        break;  // not sample_readable
    }
  }
  return fail(nc_fault(file, WrapperFault::type_mismatch, NcOp::get_var,
                       var.name.view()));
}

/// The groups of the selected stations that are read together for `var`
/// (whose station dimension has id `station_dim`): `forced` if given (tests,
/// the measurement), else grouping_for the chunk shape of the variable.
[[nodiscard]] std::expected<std::vector<StationGroup>, Error> plan_groups(
    const nc::File& file, const nc::VarInfo& var, int station_dim,
    std::span<const std::size_t> selection,
    const std::optional<GroupingPolicy>& forced);

/// What one gather reads.
struct GatherPlan {
  nc::NcNameRef variable;
  std::size_t times;                 // the leading dimension, whole
  std::optional<std::size_t> layer;  // the trailing index of a 3-D variable
  std::span<const StationGroup> groups;
};

/// Reads `plan.variable` over (time, station[, layer]) one group at a time,
/// each as one File::read_blocks of every time step over the stations the
/// group spans, and calls `sink(position, time_index, raw)` for every
/// selected station's value, in time order per station. The partition into
/// blocks, and the limits on a block, are read_blocks' (its rows_per_block);
/// this only walks it.
template <nc::Numeric T, class Sink>
[[nodiscard]] std::expected<void, Error> gather(const nc::File& file,
                                                const GatherPlan& plan,
                                                const StopToken& stop,
                                                Sink sink) {
  for (const StationGroup& group : plan.groups) {
    const std::size_t width = group.width();
    nc::Slab slab{{.start = 0, .count = plan.times},
                  {.start = group.first, .count = width}};
    if (plan.layer) {
      slab.push_back({.start = *plan.layer, .count = 1});
    }
    auto done = file.read_blocks<T>(
        plan.variable, slab,
        [&](std::span<const T> block,
            nc::DimRange rows) -> std::expected<void, Error> {
          // A raw loop where a per-member strided view would be: the sink
          // wants the position and the time as well, which views::enumerate
          // and views::stride would only add to (not on every libc++ yet).
          for (const SelectedStation& member : group.members) {
            const std::size_t column = member.station - group.first;
            for (std::size_t r = 0; r < rows.count; ++r) {
              sink(member.position, rows.start + r, block[r * width + column]);
            }
          }
          return {};
        },
        stop);
    if (not done) {
      return done;
    }
  }
  return {};
}

}  // namespace mov::io::detail
