// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "model_netcdf.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <initializer_list>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/geo.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/io/detail/station_names.hpp"
#include "mov/io/detail/table_error.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/projection.hpp"
#include "mov/io/warning.hpp"

namespace mov::io::detail {

namespace {

// ---- stations --------------------------------------------------------------

constexpr std::size_t stations_per_stop_poll = 1024;

// The coordinates of every station: float and double variables only convert
// to double without changing a value (v4 read them as float); 64-bit integers
// do not.
struct Coordinate {
  std::vector<double> values;
  bool first_step;  // read from step 0 of a (time, station) variable
};

std::expected<Coordinate, Error> read_coordinate(const nc::File& file,
                                                 const StationVariables& vars,
                                                 nc::NcNameRef name,
                                                 const StopToken& stop) {
  auto info = require_var(file, name);
  if (not info) {
    return std::unexpected{std::move(info.error())};
  }
  const bool over_time = vars.time_dim and info->dims.size() == 2 and
                         info->dims[0].id == *vars.time_dim and
                         info->dims[1].id == vars.dim.id;
  if (not over_time) {
    if (auto shape = require_shape(*info, {vars.dim.id}); not shape) {
      return std::unexpected{std::move(shape.error())};
    }
  }
  if (info->type == nc::Type::int64) {
    return fail(nc_fault(file, WrapperFault::type_mismatch, NcOp::get_var,
                         name.view()));
  }
  const nc::Slab slab = over_time
                            ? nc::Slab{{.start = 0, .count = 1},
                                       {.start = 0, .count = vars.dim.length}}
                            : nc::whole(*info);
  return file.read<double>(name, slab, stop).transform([&](auto&& values) {
    return Coordinate{.values = std::forward<decltype(values)>(values),
                      .first_step = over_time};
  });
}

// One row of a char variable of names: cut at the first NUL, white space
// simplified, bytes that are not UTF-8 replaced.
CleanedText clean_name(std::string_view row, std::size_t station) {
  auto cleaned = replace_invalid_utf8(simplified(cut_at_nul(row)));
  if (cleaned.text.empty()) {
    // Nothing readable: the default name (always valid text).
    cleaned.text = core::StationText::make("Station " + std::to_string(station))
                       .value_or(core::StationText{});
  }
  return cleaned;
}

FormatError position_error(const ToLocationError& why, std::size_t station) {
  if (const auto* projection = std::get_if<ProjectionError>(&why)) {
    return to_format_error(*projection, station);
  }
  return format_error(FormatErrc::bad_coordinates, "x, y", station);
}

// The rows of the char variable `names` (over the station dimension and a
// length), or none if the file has no names.
std::expected<std::optional<std::vector<std::string>>, Error> read_names(
    const nc::File& file, const StationVariables& vars, const StopToken& stop) {
  if (not vars.names) {
    return std::nullopt;
  }
  auto info = require_var(file, *vars.names);
  if (not info) {
    return std::unexpected{std::move(info.error())};
  }
  // Over (station, length): any other shape has rows that are not stations.
  if (info->dims.size() != 2 or info->dims.front().id != vars.dim.id) {
    return fail(format_error(FormatErrc::dimension_mismatch,
                             std::string{info->name.view()}));
  }
  return file.read_char_rows(*vars.names, stop).transform([](auto&& rows) {
    return std::optional{std::forward<decltype(rows)>(rows)};
  });
}

// What the file says about every station; makes one FileStation at a time.
class StationMaker {
 public:
  StationMaker(std::vector<double> x, std::vector<double> y,
               std::optional<std::vector<std::string>> names,
               Projector projector, core::DataSource source)
      : x_{std::move(x)},
        y_{std::move(y)},
        names_{std::move(names)},
        projector_{std::move(projector)},
        source_{source} {}

  [[nodiscard]] std::expected<core::FileStation, Error> make(
      std::size_t index) {
    const core::Xy xy{.x = x_[index], .y = y_[index]};
    const auto where = projector_.to_location(xy);
    if (not where) {
      return fail(position_error(where.error(), index));
    }
    CleanedText name = clean_name(
        names_ ? std::string_view{(*names_)[index]} : std::string_view{},
        index);
    replaced_ += name.replaced ? 1U : 0U;
    auto key = core::StationKey::make(std::to_string(index));
    if (not key) {
      return fail(to_format_error(key.error(), index));
    }
    return core::FileStation{.id = *std::move(key),
                             .name = std::move(name.text),
                             .location = *where,
                             .native = native_of(xy),
                             .source = source_};
  }

  [[nodiscard]] std::vector<Warning> warnings() && {
    std::vector<Warning> out;
    append_if_counted(out, {.code = WarningCode::invalid_utf8_replaced,
                            .subject = {},
                            .count = replaced_});
    if (auto approximate = projector_.approximation_warning()) {
      out.push_back(std::move(*approximate));
    }
    return out;
  }

 private:
  // The file's own point, when its CRS is not WGS84. Finite: the Location of
  // the same point exists.
  [[nodiscard]] std::optional<core::NativePoint> native_of(core::Xy xy) const {
    if (projector_.crs() == core::Epsg::wgs84()) {
      return std::nullopt;
    }
    if (auto point = core::NativePoint::make(xy, projector_.crs())) {
      return *point;
    }
    return std::nullopt;
  }

  std::vector<double> x_;
  std::vector<double> y_;
  std::optional<std::vector<std::string>> names_;
  Projector projector_;
  core::DataSource source_;
  std::size_t replaced_{0};
};

// ---- values ----------------------------------------------------------------

// The bytes of one value of an external type (0: not a number).
std::size_t external_size(nc::Type t) {
  switch (t) {
    case nc::Type::byte:
    case nc::Type::ubyte:
    case nc::Type::char_:
      return 1;
    case nc::Type::short_:
    case nc::Type::ushort:
      return 2;
    case nc::Type::int_:
    case nc::Type::uint:
    case nc::Type::float_:
      return 4;
    case nc::Type::int64:
    case nc::Type::uint64:
    case nc::Type::double_:
      return 8;
    case nc::Type::string:
    case nc::Type::other:
      break;
  }
  return 0;
}

// A chunk is read and decompressed whole, and read in blocks that are smaller
// than it, it would be decompressed again for each block unless the library's
// chunk cache holds it. So the cache is made big enough for one chunk, as far
// as the limits allow that memory. Measured on 1000 stations x 10000 steps of
// doubles in one deflated 80 MB chunk, ten blocks long: all stations took
// 2.4 s with netCDF-C's default 16 MiB cache and 0.7 s with one chunk's.
std::expected<void, Error> keep_chunk_in_cache(
    const nc::File& file, const nc::VarInfo& var,
    const std::optional<std::vector<std::size_t>>& chunks) {
  if (not chunks) {
    return {};
  }
  const auto elements = checked_product(*chunks);
  const std::size_t element_bytes = external_size(var.type);
  const std::size_t limit = file.limits().max_result_bytes;
  if (not elements or element_bytes == 0 or *elements > limit / element_bytes) {
    return {};  // too big to hold: the block reads re-read it
  }
  return file.reserve_chunk_cache(var.name, *elements * element_bytes)
      .transform_error(lift<Error>);
}

}  // namespace

// ---- errors ----------------------------------------------------------------

NcError nc_fault(const nc::File& file, WrapperFault fault, NcOp op,
                 std::string_view object) {
  return NcError{.status = fault,
                 .op = op,
                 .object = std::string{object},
                 .file = file.path()};
}

// ---- structure -------------------------------------------------------------

std::expected<nc::DimInfo, Error> require_dim(const nc::File& file,
                                              nc::NcNameRef name) {
  auto found = file.find_dim(name);
  if (not found) {
    return fail(std::move(found.error()));
  }
  if (not *found) {
    return fail(
        format_error(FormatErrc::missing_dimension, std::string{name.view()}));
  }
  return *std::move(*found);
}

std::expected<nc::VarInfo, Error> require_var(const nc::File& file,
                                              nc::NcNameRef name) {
  auto found = file.find_var(name);
  if (not found) {
    return fail(std::move(found.error()));
  }
  if (not *found) {
    return fail(
        format_error(FormatErrc::missing_variable, std::string{name.view()}));
  }
  return *std::move(*found);
}

std::expected<void, Error> require_shape(const nc::VarInfo& var,
                                         std::initializer_list<int> dim_ids) {
  const bool same = std::ranges::equal(var.dims, dim_ids, {}, &nc::DimInfo::id);
  if (not same) {
    return fail(format_error(FormatErrc::dimension_mismatch,
                             std::string{var.name.view()}));
  }
  return {};
}

std::expected<void, Error> require_selection(
    const core::StationSelection& selection, std::size_t station_count) {
  if (not selection.applies_to(station_count)) {
    return fail(format_error(FormatErrc::station_count_mismatch,
                             "the file has " + std::to_string(station_count) +
                                 " stations, the selection is for " +
                                 std::to_string(selection.station_count())));
  }
  return {};
}

std::expected<std::optional<std::string>, Error> optional_text(
    const nc::File& file, nc::AttTarget on, nc::NcNameRef att) {
  auto text = file.text_att(on, att);
  if (text) {
    if (*text) {
      (*text)->resize(cut_at_nul(**text).size());
    }
    return *std::move(text);
  }
  // An attribute of another type or with several strings is not a label.
  const auto* fault = std::get_if<WrapperFault>(&text.error().status);
  if (fault != nullptr and (*fault == WrapperFault::type_mismatch or
                            *fault == WrapperFault::count_mismatch)) {
    return std::optional<std::string>{};
  }
  return fail(std::move(text.error()));
}

// ---- stations --------------------------------------------------------------

std::expected<Read<std::vector<core::FileStation>>, Error> read_stations(
    const nc::File& file, const StationVariables& vars, core::Epsg crs,
    std::span<const std::size_t> which, const StopToken& stop) {
  auto x = read_coordinate(file, vars, vars.x, stop);
  if (not x) {
    return std::unexpected{std::move(x.error())};
  }
  auto y = read_coordinate(file, vars, vars.y, stop);
  if (not y) {
    return std::unexpected{std::move(y.error())};
  }
  auto names = read_names(file, vars, stop);
  if (not names) {
    return std::unexpected{std::move(names.error())};
  }
  auto projector = Projector::make(crs);
  if (not projector) {
    return fail(to_format_error(projector.error(), std::nullopt));
  }
  const bool first_step = x->first_step or y->first_step;
  StationMaker maker{std::move(x->values), std::move(y->values),
                     *std::move(names), *std::move(projector), vars.source};
  std::vector<core::FileStation> stations;
  stations.reserve(which.size());
  for (const std::size_t index : which) {
    if (stations.size() % stations_per_stop_poll == 0 and
        stop.stop_requested()) {
      return fail(Cancelled{});
    }
    auto station = maker.make(index);
    if (not station) {
      return std::unexpected{std::move(station.error())};
    }
    stations.push_back(*std::move(station));
  }
  std::vector<Warning> warnings = std::move(maker).warnings();
  if (first_step) {
    warnings.push_back({.code = WarningCode::coordinates_from_first_step,
                        .subject = std::string{vars.x.view()},
                        .count = 1});
  }
  return Read<std::vector<core::FileStation>>{.value = std::move(stations),
                                              .warnings = std::move(warnings)};
}

// ---- time ------------------------------------------------------------------

std::expected<CfCalendar, Error> read_calendar(const nc::File& file,
                                               const nc::VarInfo& time) {
  auto text = optional_text(file, time.name, "calendar");
  if (not text) {
    return std::unexpected{std::move(text.error())};
  }
  const auto calendar = parse_cf_calendar(*text);
  if (not calendar) {
    return fail(format_error(FormatErrc::unsupported_calendar,
                             std::string{time.name.view()} + ":calendar"));
  }
  return *calendar;
}

std::expected<CfClock, Error> make_clock(const CfTimeUnits& units,
                                         CfCalendar calendar,
                                         std::string_view variable) {
  auto clock = CfClock::make(units, calendar);
  if (not clock) {
    return fail(
        format_error(FormatErrc::unsupported_calendar, std::string{variable}));
  }
  return *clock;
}

std::expected<Read<CfClock>, Error> clock_of(const nc::File& file,
                                             const nc::VarInfo& time_var) {
  auto units = optional_text(file, time_var.name, "units");
  if (not units) {
    return std::unexpected{std::move(units).error()};
  }
  if (not *units) {
    return fail(format_error(FormatErrc::missing_attribute,
                             std::string{time_var.name.view()} + ":units"));
  }
  auto parsed = parse_cf_time_units(**units);
  if (not parsed) {
    return fail(std::move(parsed).error());
  }
  return read_calendar(file, time_var)
      .and_then([&](CfCalendar calendar) {
        return make_clock(parsed->value, calendar, time_var.name.view());
      })
      .transform([&](CfClock clock) {
        return Read<CfClock>{.value = clock,
                             .warnings = std::move(parsed->warnings)};
      });
}

std::expected<std::vector<core::Sample>, Error> read_time_samples(
    const nc::File& file, const nc::VarInfo& time, const nc::Slab& slab,
    const StopToken& stop) {
  if (time.type != nc::Type::int64) {
    return file.read_samples(time.name, slab, stop);
  }
  auto mask = file.masking<std::int64_t>(time.name);
  if (not mask) {
    return fail(std::move(mask).error());
  }
  auto raw = file.read<std::int64_t>(time.name, slab, stop);
  if (not raw) {
    return std::unexpected{std::move(raw).error()};
  }
  std::vector<core::Sample> out(raw->size());
  std::ranges::transform(*raw, out.begin(),
                         [&mask](std::int64_t x) { return mask->apply(x); });
  return out;
}

std::expected<core::TimeAxis, Error> times_of(
    std::span<const core::Sample> values, const CfClock& clock,
    std::string_view var, std::optional<std::size_t> station) {
  core::TimeAxis times;
  times.reserve(values.size());
  // (An enumerate over the values; not yet on every standard library.)
  for (std::size_t i = 0; i < values.size(); ++i) {
    const std::optional<double> value = values[i].value();
    if (not value) {
      return fail(
          format_error(FormatErrc::time_missing, std::string{var}, station, i));
    }
    const std::optional<core::Time> time = clock.at(*value);
    if (not time) {
      return fail(format_error(FormatErrc::time_out_of_range, std::string{var},
                               station, i));
    }
    times.push_back(*time);
  }
  return times;
}

std::expected<core::TimeAxis, Error> strictly_increasing(
    core::TimeAxis times, std::string_view var,
    std::optional<std::size_t> station) {
  const auto descent = std::ranges::adjacent_find(
      times, [](core::Time a, core::Time b) { return not(a < b); });
  if (descent != times.end()) {
    return fail(
        format_error(FormatErrc::time_not_increasing, std::string{var}, station,
                     static_cast<std::size_t>(descent - times.begin()) + 1));
  }
  return times;
}

std::expected<core::TimeAxis, Error> read_time_axis(const nc::File& file,
                                                    const nc::VarInfo& time,
                                                    const CfClock& clock,
                                                    const StopToken& stop) {
  const std::string_view name = time.name.view();
  return read_time_samples(file, time, nc::whole(time), stop)
      .and_then([&](const std::vector<core::Sample>& values) {
        return times_of(values, clock, name, std::nullopt);
      })
      .and_then([&](core::TimeAxis times) {
        return strictly_increasing(std::move(times), name, std::nullopt);
      });
}

// ---- values ----------------------------------------------------------------

std::expected<void, Error> check_result_size(const nc::File& file,
                                             std::string_view variable,
                                             std::size_t stations,
                                             std::size_t times,
                                             std::size_t columns) {
  const std::array<std::size_t, 3> factors{stations, times, columns};
  const auto samples = checked_product(factors);
  const ReadLimits& limits = file.limits();
  const bool fits = samples and *samples <= limits.max_elements and
                    *samples <= limits.max_result_bytes / sizeof(core::Sample);
  if (not fits) {
    return fail(
        nc_fault(file, WrapperFault::too_large, NcOp::get_var, variable));
  }
  return {};
}

std::expected<void, Error> check_station_count(const nc::File& file,
                                               const nc::DimInfo& dim) {
  if (dim.length > file.limits().max_elements) {
    return fail(nc_fault(file, WrapperFault::too_large, NcOp::inquire,
                         dim.name.view()));
  }
  return {};
}

std::expected<std::vector<StationGroup>, Error> plan_groups(
    const nc::File& file, const nc::VarInfo& var, int station_dim,
    std::span<const std::size_t> selection,
    const std::optional<GroupingPolicy>& forced) {
  if (forced) {
    return station_groups(selection, *forced);
  }
  const auto axis = std::ranges::find(var.dims, station_dim, &nc::DimInfo::id);
  if (axis == var.dims.end()) {
    return fail(format_error(FormatErrc::dimension_mismatch,
                             std::string{var.name.view()}));
  }
  auto chunks = file.chunk_shape(var.name);
  if (not chunks) {
    return fail(std::move(chunks.error()));
  }
  if (auto cached = keep_chunk_in_cache(file, var, *chunks); not cached) {
    return std::unexpected{std::move(cached.error())};
  }
  return station_groups(
      selection,
      grouping_for(*chunks, static_cast<std::size_t>(axis - var.dims.begin())));
}

std::expected<Read<core::StationTable>, Error> assemble_table(
    std::vector<core::Variable> variables, core::TimeAxis axis,
    Read<std::vector<core::FileStation>> stations,
    std::vector<Warning> warnings) {
  std::vector<core::StationRow> rows;
  rows.reserve(stations.value.size());
  for (core::FileStation& station : stations.value) {
    rows.push_back({.station = std::move(station), .axis = 0});
  }
  std::vector<core::TimeAxis> axes;
  axes.push_back(std::move(axis));
  auto table = core::StationTable::make(std::move(variables), std::move(axes),
                                        std::move(rows));
  if (not table) {
    return fail(to_format_error(table.error()));
  }
  // The station warnings come before the ones of the values.
  std::vector<Warning> all = std::move(stations.warnings);
  append(all, std::move(warnings));
  return Read<core::StationTable>{.value = *std::move(table),
                                  .warnings = std::move(all)};
}

std::vector<core::Column> empty_columns(std::size_t stations,
                                        std::size_t times) {
  return std::vector<core::Column>(stations, core::Column(times));
}

}  // namespace mov::io::detail
