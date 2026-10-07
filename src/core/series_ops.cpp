// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/series_ops.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <expected>
#include <functional>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core_access.hpp"
#include "mov/core/detail/numeric.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "series_rebuild.hpp"

namespace mov::core {

// ---- Bucket, extent, quick_stats
// ---------------------------------------------

Bucket summarize(const TimeSeries& s) {
  // An ordered left fold, not reduce: the sum is only approximately
  // associative, and ties between extremes keep the left operand. (Not
  // ranges::fold_left, which Apple libc++ lacks.)
  const auto points = s.points();
  return std::accumulate(points.begin(), points.end(), Bucket{},
                         [](const Bucket& acc, const Point& p) noexcept {
                           return acc + Bucket::of(p.time, p.sample);
                         });
}

std::optional<Extent> extent(const TimeSeries& s) {
  if (s.empty()) {
    return std::nullopt;
  }
  const Bucket b = summarize(s);
  const std::optional<Extreme> lo = b.min();
  const std::optional<Extreme> hi = b.max();
  std::optional<ValueRange> values;
  if (lo and hi) {
    values = ValueRange{.min = lo->value, .max = hi->value};
  }
  return Extent{
      .first = s.times().front(), .last = s.times().back(), .values = values};
}

std::optional<Extent> extent(std::span<const TimeSeries> all) {
  // The join is a commutative, associative, exact semilattice, so the
  // grouping a transform_reduce picks cannot change the answer.
  return std::transform_reduce(
      all.begin(), all.end(), std::optional<Extent>{},
      [](const std::optional<Extent>& a, const std::optional<Extent>& b) {
        return combine(a, b);
      },
      [](const TimeSeries& s) { return extent(s); });
}

namespace {

// sum / n, or, when the sum overflowed (or became NaN), the sum of the
// samples divided by n one at a time: each term is at most the largest
// magnitude, so nothing overflows.
double mean_of(const Bucket& b, const TimeSeries& s) {
  const auto n = static_cast<double>(b.values());
  const double mean = b.sum() / n;
  if (detail::is_finite(mean)) {
    return mean;
  }
  const auto scaled = [n](Sample x) noexcept {
    return x.visit([](Missing) noexcept { return 0.0; },
                   [](Dry) noexcept { return 0.0; },
                   [n](double v) noexcept { return v / n; });
  };
  const std::span<const Sample> samples = s.samples();
  return std::transform_reduce(samples.begin(), samples.end(), 0.0, std::plus{},
                               scaled);
}

}  // namespace

QuickStats quick_stats(const TimeSeries& s) {
  const Bucket b = summarize(s);
  const std::optional<Extreme> lo = b.min();
  const std::optional<Extreme> hi = b.max();
  std::optional<ValueStats> stats;
  if (lo and hi) {
    stats = ValueStats{.min = *lo, .max = *hi, .mean = mean_of(b, s)};
  }
  return {.values = b.values(),
          .missing = b.missing(),
          .dry = b.dry(),
          .stats = stats};
}

// ---- residual ---------------------------------------------------------------

namespace {

// "<obs> - <pred>" with U+2212 as UTF-8 bytes (sources hold no non-ASCII).
constexpr std::string_view minus_sign = " \xE2\x88\x92 ";

std::expected<Unit, ResidualErrc> common_unit(const SeriesMeta& o,
                                              const SeriesMeta& p) {
  const std::optional<Unit>& uo = o.unit();
  const std::optional<Unit>& up = p.unit();
  if (not uo or not up) {
    return std::unexpected{ResidualErrc::unit_unknown};
  }
  if (*uo != *up) {
    return std::unexpected{ResidualErrc::units_differ};
  }
  if (is_temperature(*uo)) {
    return std::unexpected{ResidualErrc::temperature_difference};
  }
  return *uo;
}

std::optional<ResidualErrc> datum_fault(const SeriesMeta& o,
                                        const SeriesMeta& p) {
  if (not datum_applicable(o.quantity()) and
      not datum_applicable(p.quantity())) {
    return std::nullopt;
  }
  const std::optional<VerticalDatum> d_o = o.datum();
  const std::optional<VerticalDatum> d_p = p.datum();
  if (not d_o or not d_p) {
    return ResidualErrc::datum_unknown;
  }
  if (*d_o != *d_p) {
    return ResidualErrc::datums_differ;
  }
  return std::nullopt;
}

std::expected<SeriesMeta, ResidualErrc> residual_meta(const SeriesMeta& o,
                                                      const SeriesMeta& p) {
  auto unit = common_unit(o, p);
  if (not unit) {
    return std::unexpected{unit.error()};
  }
  if (const auto fault = datum_fault(o, p)) {
    return std::unexpected{*fault};
  }
  std::string label{o.label()};
  label += minus_sign;
  label += p.label();
  return SeriesMeta::make({.quantity = GenericQuantity::value(),
                           .label = std::move(label),
                           .unit = std::move(*unit)});
}

// The merge-join of two strictly increasing axes: calls f(i, j) for every
// pair with a[i] == b[j], in time order. O(n + m).
template <class F>
void for_each_common_time(std::span<const Time> a, std::span<const Time> b,
                          F f) {
  std::size_t i = 0;
  std::size_t j = 0;
  while (i < a.size() and j < b.size()) {
    if (a[i] < b[j]) {
      ++i;
    } else if (b[j] < a[i]) {
      ++j;
    } else {
      f(i++, j++);
    }
  }
}

}  // namespace

std::expected<TimeSeries, ResidualErrc> residual(const ObsVsPred& pair) {
  const TimeSeries& o = pair.observed;
  const TimeSeries& p = pair.predicted;
  auto meta = residual_meta(o.meta(), p.meta());
  if (not meta) {
    return std::unexpected{meta.error()};
  }
  TimeAxis times;
  std::vector<Sample> samples;
  const std::size_t capacity = std::min(o.size(), p.size());
  times.reserve(capacity);
  samples.reserve(capacity);
  for_each_common_time(o.times(), p.times(), [&](std::size_t i, std::size_t j) {
    times.push_back(o.times()[i]);
    samples.push_back(
        combine(o.samples()[i], p.samples()[j], std::minus<double>{}));
  });
  return TimeSeries{detail::CoreAccess::key(), std::move(times),
                    std::move(samples), std::move(*meta)};
}

// ---- slice, shift_time, scale_offset
// ------------------------------------------

namespace {

// The half-open index window [lo, hi) of the times in r.
struct Window {
  std::size_t lo;
  std::size_t hi;
};

Window window_of(std::span<const Time> times, TimeRange r) {
  const auto first = std::ranges::lower_bound(times, r.begin());
  const auto last = std::ranges::lower_bound(first, times.end(), r.end());
  return {.lo = static_cast<std::size_t>(first - times.begin()),
          .hi = static_cast<std::size_t>(last - times.begin())};
}

}  // namespace

TimeSeries slice(const TimeSeries& s, TimeRange r) {
  const Window w = window_of(s.times(), r);
  const auto times = s.times().subspan(w.lo, w.hi - w.lo);
  const auto samples = s.samples().subspan(w.lo, w.hi - w.lo);
  return TimeSeries{
      detail::CoreAccess::key(), TimeAxis(times.begin(), times.end()),
      std::vector<Sample>(samples.begin(), samples.end()), s.meta()};
}

TimeSeries slice(TimeSeries&& s, TimeRange r) {
  const Window w = window_of(s.times(), r);
  TimeSeriesParts parts = std::move(s).into_parts();
  const auto drop = [&w](auto& v) {
    v.erase(v.begin() + static_cast<std::ptrdiff_t>(w.hi), v.end());
    v.erase(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(w.lo));
  };
  drop(parts.times);
  drop(parts.samples);
  return TimeSeries{detail::CoreAccess::key(), std::move(parts.times),
                    std::move(parts.samples), std::move(parts.meta)};
}

std::expected<TimeSeries, TimeOverflow> shift_time(
    TimeSeries s, std::chrono::milliseconds dt) {
  const std::span<const Time> times = s.times();
  const auto leaves_range = [dt](Time t) noexcept {
    return not detail::offset_time(t, dt.count());
  };
  const auto bad = std::ranges::find_if(times, leaves_range);
  if (bad != times.end()) {
    return std::unexpected{
        TimeOverflow{.index = static_cast<std::size_t>(bad - times.begin())}};
  }
  // Every shifted time is in range, so the addition below cannot overflow,
  // and a uniform shift keeps the times strictly increasing.
  TimeSeriesParts parts = std::move(s).into_parts();
  std::ranges::transform(parts.times, parts.times.begin(),
                         [dt](Time t) noexcept { return t + dt; });
  return TimeSeries{detail::CoreAccess::key(), std::move(parts.times),
                    std::move(parts.samples), std::move(parts.meta)};
}

TimeSeries scale_offset(TimeSeries s, Affine a) {
  return std::move(s).transform_samples(
      detail::CoreAccess::key(),
      [a](Sample x) noexcept { return detail::apply_affine(a, x); });
}

// ---- convert
// ---------------------------------------------------------------------

namespace {

// How to move a series (or column) to another unit: the value map and the
// metadata that goes with it.
struct ConversionPlan {
  Affine affine;
  SeriesMeta meta;
};

std::expected<ConversionPlan, UnitError> plan_conversion(const SeriesMeta& meta,
                                                         const Unit& to) {
  const std::optional<Unit>& from = meta.unit();
  if (not from) {
    return std::unexpected{UnitError{UnknownUnit{}}};
  }
  const auto affine = conversion(*from, to);
  if (not affine) {
    return std::unexpected{UnitError{affine.error()}};
  }
  return ConversionPlan{
      .affine = *affine,
      .meta = meta.rewrite_unit(detail::CoreAccess::key(), to)};
}

}  // namespace

std::expected<TimeSeries, UnitError> convert(TimeSeries s, const Unit& to) {
  auto plan = plan_conversion(s.meta(), to);
  if (not plan) {
    return std::unexpected{plan.error()};
  }
  return detail::rebuilt(std::move(s), std::move(plan->meta), plan->affine);
}

std::expected<StationTable, UnitError> convert(StationTable t,
                                               ColumnIndex column,
                                               const Unit& to) {
  assert(column.value() < t.schema().size());
  auto plan = plan_conversion(t.schema()[column.value()], to);
  if (not plan) {
    return std::unexpected{plan.error()};
  }
  const Affine affine = plan->affine;
  return std::move(t).rewrite_column(
      detail::CoreAccess::key(), column, std::move(plan->meta),
      [affine](Sample x) noexcept { return detail::apply_affine(affine, x); });
}

}  // namespace mov::core
