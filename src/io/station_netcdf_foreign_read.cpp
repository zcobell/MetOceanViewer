// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The samples of selected stations of a foreign CF discrete-sampling-geometry
// file (CF 9.3), by representation: a matrix over (station, time) or (time,
// station) for the orthogonal and incomplete layouts, runs of an observation
// dimension for the contiguous ragged layout and for one station, and a pass
// over the observation dimension for the indexed ragged layout. Every value is
// masked in the variable's own type (nc::Masking: _FillValue, missing_value,
// valid_*, the library's default fill, NaN; packing applied after). Samples a
// known quality scheme calls bad become Missing; a series whose times are not
// strictly increasing is put in order, as IMEDS does.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "model_netcdf.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/cf_time.hpp"
#include "mov/io/detail/checked_product.hpp"
#include "mov/io/detail/station_groups.hpp"
#include "mov/io/detail/table_error.hpp"
#include "mov/io/error.hpp"
#include "mov/io/netcdf/file.hpp"
#include "mov/io/read.hpp"
#include "mov/io/station_netcdf.hpp"
#include "mov/io/warning.hpp"
#include "station_netcdf_dialects.hpp"
#include "station_netcdf_shared.hpp"

namespace mov::io::detail::station_nc {

namespace {

using core::Overloaded;

/// Where the next sample of each selected station goes in an indexed ragged
/// pass; made once per read, reset for each variable.
struct OwnerSlots {
  std::vector<std::optional<std::size_t>> position;
  std::vector<std::size_t> next;
};

/// What reading the selected stations of an opened file needs.
struct Reading {
  const nc::File& file;
  const ForeignOpened& opened;
  std::span<const std::size_t> selected;
  /// How much padding a read checks; nullopt: none (the layout has no padding
  /// to check, or the file's counts already account for all of it).
  std::optional<PaddingCheck> padding;
  const StopToken& stop;
  OwnerSlots slots;
};

bool station_major(const nc::VarInfo& var, const Matrix& matrix) {
  return var.dims.front().id == matrix.station.id;
}

/// A matrix over (station, sample): the first `counts` samples of each row.
std::expected<std::vector<core::Column>, Error> station_major_columns(
    const Reading& r, const Matrix& matrix, const nc::VarInfo& var,
    RowRole role, std::optional<PaddingCheck> padding) {
  const RowSpec spec{.station_dim = matrix.station.id,
                     .sample_length = r.opened.s.sample.length,
                     .counts = r.opened.counts,
                     .selected = r.selected,
                     .padding = padding,
                     .stop = r.stop};
  std::vector<core::Column> columns(r.selected.size());
  auto done = sample_rows(
      r.file, var, spec, role,
      [&columns](const SelectedStation& m, std::span<const core::Sample> kept)
          -> std::expected<void, Error> {
        columns[m.position].assign(kept.begin(), kept.end());
        return {};
      });
  if (not done) {
    return std::unexpected{std::move(done).error()};
  }
  return columns;
}

/// How many time steps a (time, station) read covers: as far as the longest
/// selected station's samples reach, and the first padding element (boundary)
/// or all of the dimension (whole) when the padding is checked.
std::size_t steps_to_read(const Reading& r,
                          std::optional<PaddingCheck> padding) {
  const std::size_t length = r.opened.s.sample.length;
  if (padding == PaddingCheck::whole) {
    return length;
  }
  std::size_t longest = 0;
  for (const std::size_t i : r.selected) {
    longest = std::max(longest, r.opened.counts[i]);
  }
  return std::min(length, padding ? longest + 1 : longest);
}

/// Where a padding element that is not missing was found.
struct Misplaced {
  std::size_t position;
  std::size_t step;
};

/// A matrix over (sample, station): the time steps the stations' samples
/// reach (steps_to_read), whole rows of the stations' groups; only each
/// station's first `counts` are kept, the rest is checked as padding.
std::expected<std::vector<core::Column>, Error> time_major_columns(
    const Reading& r, const Matrix& matrix, const nc::VarInfo& var,
    RowRole role, std::optional<PaddingCheck> padding) {
  std::vector<core::Column> columns(r.selected.size());
  for (std::size_t p = 0; p < r.selected.size(); ++p) {
    columns[p].assign(r.opened.counts[r.selected[p]], core::Sample{});
  }
  auto groups =
      plan_groups(r.file, var, matrix.station.id, r.selected, std::nullopt);
  if (not groups) {
    return std::unexpected{std::move(groups).error()};
  }
  const GatherPlan plan{.variable = var.name,
                        .times = steps_to_read(r, padding),
                        .layer = std::nullopt,
                        .groups = *groups};
  std::optional<Misplaced> misplaced;
  auto done = dispatch_role_numeric(
      role, r.file, var, [&]<class T>() -> std::expected<void, Error> {
        auto mask = r.file.masking<T>(var.name);
        if (not mask) {
          return fail(std::move(mask).error());
        }
        return gather<T>(
            r.file, plan, r.stop,
            [&](std::size_t position, std::size_t t, T raw) {
              core::Column& column = columns[position];
              const core::Sample x = mask->apply(raw);
              if (t < column.size()) {
                column[t] = x;
              } else if (padding and not misplaced and not x.is_missing()) {
                misplaced = Misplaced{.position = position, .step = t};
              }
            });
      });
  if (not done) {
    return std::unexpected{std::move(done).error()};
  }
  if (misplaced) {
    return fail(format_error(FormatErrc::padding_not_missing,
                             subject_of(var.name.view()),
                             r.selected[misplaced->position], misplaced->step));
  }
  return columns;
}

/// Runs of the observation dimension: the stations are in file order there,
/// so consecutive stations of the selection are one read.
std::expected<std::vector<core::Column>, Error> run_columns(
    const Reading& r, const RunStarts& runs, const nc::VarInfo& var,
    RowRole role) {
  const std::span<const std::size_t> counts = r.opened.counts;
  std::vector<std::size_t> order(r.selected.size());
  std::ranges::copy(std::views::iota(std::size_t{0}, order.size()),
                    order.begin());
  std::ranges::sort(order, {}, [&r](std::size_t p) { return r.selected[p]; });
  std::vector<core::Column> columns(r.selected.size());
  for (std::size_t k = 0; k < order.size();) {
    std::size_t last = k;
    while (last + 1 < order.size() and
           r.selected[order[last + 1]] == r.selected[order[last]] + 1) {
      ++last;
    }
    const std::size_t first_station = r.selected[order[k]];
    const std::size_t last_station = r.selected[order[last]];
    const std::size_t begin = runs.starts[first_station];
    const std::size_t end = runs.starts[last_station] + counts[last_station];
    if (end > begin) {
      auto samples = read_masked(
          r.file, var, {{.start = begin, .count = end - begin}}, role, r.stop);
      if (not samples) {
        return std::unexpected{std::move(samples).error()};
      }
      for (std::size_t j = k; j <= last; ++j) {
        const std::size_t station = r.selected[order[j]];
        const auto from = samples->begin() + static_cast<std::ptrdiff_t>(
                                                 runs.starts[station] - begin);
        columns[order[j]].assign(
            from, from + static_cast<std::ptrdiff_t>(counts[station]));
      }
    }
    k = last + 1;
  }
  return columns;
}

/// Indexed ragged: one pass over the observation dimension, each sample to the
/// station its index names (in file order within a station).
std::expected<std::vector<core::Column>, Error> indexed_columns(
    Reading& r, const SampleOwners& owners, const nc::VarInfo& var,
    RowRole role) {
  std::vector<core::Column> columns(r.selected.size());
  bool any = false;
  for (std::size_t p = 0; p < r.selected.size(); ++p) {
    const std::size_t count = r.opened.counts[r.selected[p]];
    columns[p].assign(count, core::Sample{});
    any = any or count > 0;
  }
  if (not any) {
    return columns;
  }
  std::ranges::fill(r.slots.next, std::size_t{0});
  auto done = dispatch_role_numeric(
      role, r.file, var, [&]<class T>() -> std::expected<void, Error> {
        auto mask = r.file.masking<T>(var.name);
        if (not mask) {
          return fail(std::move(mask).error());
        }
        return r.file.read_blocks<T>(
            var.name, nc::whole(var),
            [&](std::span<const T> block,
                nc::DimRange outer) -> std::expected<void, Error> {
              for (std::size_t k = 0; k < block.size(); ++k) {
                const std::size_t station =
                    owners.station_of_sample[outer.start + k];
                if (const auto position = r.slots.position[station]) {
                  columns[*position][r.slots.next[station]++] =
                      mask->apply(block[k]);
                }
              }
              return {};
            },
            r.stop);
      });
  if (not done) {
    return std::unexpected{std::move(done).error()};
  }
  return columns;
}

/// The selected stations' samples of `var`, in the order of the selection.
/// `padding` is how much of the padding of a matrix is checked.
std::expected<std::vector<core::Column>, Error> read_column(
    Reading& r, const nc::VarInfo& var, RowRole role,
    std::optional<PaddingCheck> padding) {
  return std::visit(Overloaded{[&](const Matrix& matrix) {
                                 return station_major(var, matrix)
                                            ? station_major_columns(
                                                  r, matrix, var, role, padding)
                                            : time_major_columns(r, matrix, var,
                                                                 role, padding);
                               },
                               [&](const RunStarts& runs) {
                                 return run_columns(r, runs, var, role);
                               },
                               [&](const SampleOwners& owners) {
                                 return indexed_columns(r, owners, var, role);
                               }},
                    r.opened.placement);
}

// ---- time
// ---------------------------------------------------------------------

/// The time axes, not yet in order: one shared (orthogonal) or one per
/// selected station.
std::expected<std::vector<core::TimeAxis>, Error> read_axes(
    Reading& r, const CfClock& clock) {
  const ForeignStructure& s = r.opened.s;
  if (layout_of(s.sampling) == CfDsgLayout::orthogonal) {
    return read_masked(r.file, s.time, nc::whole(s.time), RowRole::times,
                       r.stop)
        .and_then([&](const std::vector<core::Sample>& row) {
          return times_of(row, clock, s.time.name.view(), std::nullopt);
        })
        .transform([](core::TimeAxis axis) {
          return std::vector<core::TimeAxis>{std::move(axis)};
        });
  }
  auto columns = read_column(r, s.time, RowRole::times, r.padding);
  if (not columns) {
    return std::unexpected{std::move(columns).error()};
  }
  std::vector<core::TimeAxis> axes;
  axes.reserve(columns->size());
  for (std::size_t p = 0; p < columns->size(); ++p) {
    auto axis =
        times_of((*columns)[p], clock, s.time.name.view(), r.selected[p]);
    if (not axis) {
      return std::unexpected{std::move(axis).error()};
    }
    axes.push_back(*std::move(axis));
  }
  return axes;
}

/// Puts every axis in time order together with the columns over it: the
/// shared axis of an orthogonal file with every column of every station, any
/// other axis with the columns of its station.
core::NormalizeReport normalize_all(std::vector<core::TimeAxis>& axes,
                                    std::vector<core::Variable>& variables,
                                    bool shared_axis) {
  core::NormalizeReport report;
  for (std::size_t a = 0; a < axes.size(); ++a) {
    std::vector<core::Column*> over;
    for (core::Variable& v : variables) {
      if (shared_axis) {
        for (core::Column& column : v.per_station) {
          over.push_back(&column);
        }
      } else {
        over.push_back(&v.per_station[a]);
      }
    }
    merge_reports(report, normalize_columns(axes[a], over));
  }
  return report;
}

// ---- quality flags
// ------------------------------------------------------------

/// The flag of a sample, if it has one.
std::optional<std::int64_t> flag_of(const core::Sample& s) {
  const std::optional<double> x = s.value();
  return x ? std::optional{static_cast<std::int64_t>(std::llround(*x))}
           : std::nullopt;
}

struct FlagCounts {
  std::size_t masked{0};
  std::size_t suspect{0};
};

bool listed(const std::vector<std::int64_t>& flags, std::int64_t flag) {
  return std::ranges::find(flags, flag) != flags.end();
}

/// Applies the rules of one quality variable to the columns of its data
/// variable: a value flagged bad becomes Missing, one flagged suspect is
/// counted.
FlagCounts apply_quality(std::vector<core::Column>& columns,
                         const std::vector<core::Column>& flags,
                         const QualityRules& rules) {
  FlagCounts counts;
  for (std::size_t p = 0; p < columns.size(); ++p) {
    for (std::size_t j = 0; j < columns[p].size(); ++j) {
      const auto flag = flag_of(flags[p][j]);
      if (not flag or not columns[p][j].is_value()) {
        continue;
      }
      if (listed(rules.bad, *flag)) {
        columns[p][j] = core::Sample{};
        ++counts.masked;
      } else if (listed(rules.suspect, *flag)) {
        ++counts.suspect;
      }
    }
  }
  return counts;
}

/// The columns of one data variable with its quality rules applied; the
/// warnings go to `warnings` (one of each kind per variable, counted).
std::expected<std::vector<core::Column>, Error> read_data(
    Reading& r, const ForeignData& d, std::vector<Warning>& warnings) {
  auto columns = read_column(r, d.var, RowRole::values, r.padding);
  if (not columns) {
    return columns;
  }
  FlagCounts total;
  for (const QualityRules& rules : d.quality) {
    // The padding of a flag variable is not looked at: the data's is.
    auto flags = read_column(r, rules.var, RowRole::values, std::nullopt);
    if (not flags) {
      return std::unexpected{std::move(flags).error()};
    }
    const FlagCounts counts = apply_quality(*columns, *flags, rules);
    total.masked += counts.masked;
    total.suspect += counts.suspect;
  }
  append_if_counted(warnings, {.code = WarningCode::flagged_samples_masked,
                               .subject = subject_of(d.var.name.view()),
                               .count = total.masked});
  append_if_counted(warnings, {.code = WarningCode::suspect_samples_kept,
                               .subject = subject_of(d.var.name.view()),
                               .count = total.suspect});
  return columns;
}

// ---- the table
// ----------------------------------------------------------------

/// `too_large` unless what a read holds fits ReadLimits: the kept samples, or
/// (the padding of a matrix checked whole) every selected station's whole row.
std::expected<void, Error> check_size(const Reading& r) {
  const ForeignStructure& s = r.opened.s;
  const std::array<std::size_t, 2> whole_rows{r.selected.size(),
                                              s.sample.length};
  const std::optional<std::size_t> samples =
      r.padding == PaddingCheck::whole
          ? checked_product(whole_rows)
          : sum_selected(r.opened.counts, r.selected);
  return check_rows_size(r.file, s.data.front().var.name.view(), samples,
                         s.data.size() + 1);
}

/// How much padding is checked: only an incomplete layout with `obs_count`
/// has padding the counts do not already account for.
std::optional<PaddingCheck> padding_checked(const ForeignOpened& opened,
                                            PaddingCheck requested) {
  const auto* incomplete = std::get_if<Incomplete>(&opened.s.sampling);
  return incomplete != nullptr and incomplete->obs_count
             ? std::optional{requested}
             : std::nullopt;
}

OwnerSlots owner_slots(const ForeignOpened& opened,
                       std::span<const std::size_t> selected) {
  OwnerSlots slots;
  if (std::holds_alternative<SampleOwners>(opened.placement)) {
    slots.position.assign(opened.counts.size(), std::nullopt);
    slots.next.assign(opened.counts.size(), 0);
    for (std::size_t p = 0; p < selected.size(); ++p) {
      slots.position[selected[p]] = p;
    }
  }
  return slots;
}

}  // namespace

std::expected<Read<core::StationTable>, Error> read_foreign(
    const nc::File& file, const ForeignOpened& opened,
    std::span<const std::size_t> selected, PaddingCheck padding,
    const StopToken& stop) {
  Reading r{.file = file,
            .opened = opened,
            .selected = selected,
            .padding = padding_checked(opened, padding),
            .stop = stop,
            .slots = owner_slots(opened, selected)};
  const ForeignStructure& s = opened.s;
  auto clock = check_size(r).and_then([&] { return clock_of(file, s.time); });
  if (not clock) {
    return std::unexpected{std::move(clock).error()};
  }
  auto axes = read_axes(r, clock->value);
  if (not axes) {
    return std::unexpected{std::move(axes).error()};
  }
  std::vector<Warning> warnings = std::move(clock->warnings);
  std::vector<Warning> quality_warnings;
  std::vector<core::Variable> variables;
  variables.reserve(s.data.size());
  for (std::size_t k = 0; k < s.data.size(); ++k) {
    auto columns = read_data(r, s.data[k], quality_warnings);
    if (not columns) {
      return std::unexpected{std::move(columns).error()};
    }
    variables.push_back(
        {.meta = opened.catalog.schema[k], .per_station = *std::move(columns)});
  }
  const bool shared_axis = layout_of(s.sampling) == CfDsgLayout::orthogonal;
  add_normalize_warnings(warnings, normalize_all(*axes, variables, shared_axis),
                         s.time.name.view());
  append(warnings, std::move(quality_warnings));
  std::vector<core::StationRow> rows;
  rows.reserve(selected.size());
  for (std::size_t p = 0; p < selected.size(); ++p) {
    rows.push_back({.station = opened.catalog.stations[selected[p]].station,
                    .axis = shared_axis ? 0 : p});
  }
  auto table = core::StationTable::make(std::move(variables), *std::move(axes),
                                        std::move(rows));
  if (not table) {
    return fail(to_format_error(table.error()));
  }
  return Read<core::StationTable>{.value = *std::move(table),
                                  .warnings = std::move(warnings)};
}

}  // namespace mov::io::detail::station_nc
