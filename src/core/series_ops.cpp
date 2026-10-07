// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/series_ops.hpp"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <numeric>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "core_access.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "series_rebuild.hpp"

namespace mov::core {

// ---- Bucket, extent, quick_stats ----

Bucket summarize(std::span<const Time> times, std::span<const Sample> samples) {
  assert(times.size() == samples.size());
  // An ordered left fold, not reduce: the sum is only approximately
  // associative, and ties between extremes keep the left operand. (Not
  // ranges::fold_left, which Apple libc++ lacks.)
  const auto indices = std::views::iota(std::size_t{0}, samples.size());
  return std::accumulate(indices.begin(), indices.end(), Bucket{},
                         [&](const Bucket& acc, std::size_t i) {
                           return acc + Bucket::of(times[i], samples[i]);
                         });
}

Bucket summarize(const TimeSeries& s) {
  return summarize(s.times(), s.samples());
}

std::optional<Extent> extent(const TimeSeries& s) {
  if (s.empty()) {
    return std::nullopt;
  }
  const auto range_of = [](const ValueSummary& v) {
    return ValueRange{.min = v.min().value, .max = v.max().value};
  };
  return Extent{.first = s.times().front(),
                .last = s.times().back(),
                .values = summarize(s).summary().transform(range_of)};
}

std::optional<Extent> extent(std::span<const TimeSeries> all) {
  // The join is a commutative, associative, exact semilattice, so the
  // grouping a transform_reduce picks cannot change the answer.
  return std::transform_reduce(
      all.begin(), all.end(), std::optional<Extent>{},
      [](const std::optional<Extent>& a, const std::optional<Extent>& b) {
        return join(a, b);
      },
      [](const TimeSeries& s) { return extent(s); });
}

QuickStats quick_stats(const TimeSeries& s) {
  const Bucket b = summarize(s);
  const auto stats_of = [](const ValueSummary& v) {
    return ValueStats{
        .count = v.count(), .min = v.min(), .max = v.max(), .mean = v.mean()};
  };
  return {.missing = b.missing(),
          .dry = b.dry(),
          .stats = b.summary().transform(stats_of)};
}

// ---- residual ----

namespace {

// "<obs> - <pred>" with U+2212 as UTF-8 bytes (sources hold no non-ASCII).
constexpr std::string_view minus_sign = " \xE2\x88\x92 ";

bool is(const QuantityId& q, Quantity expected) noexcept {
  const Quantity* registry = std::get_if<Quantity>(&q);
  return registry != nullptr and *registry == expected;
}

// The same quantity (generic ones by token), or an observed water level and
// its prediction.
bool compatible_quantities(const QuantityId& obs,
                           const QuantityId& pred) noexcept {
  return token(obs) == token(pred) or
         (is(obs, Quantity::water_level) and
          is(pred, Quantity::water_level_prediction));
}

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

// The quantities are compatible, so both series have the same
// datum_applicable answer; asking the observed one is enough.
std::optional<ResidualErrc> datum_fault(const SeriesMeta& o,
                                        const SeriesMeta& p) {
  if (not datum_applicable(o.quantity())) {
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
  if (not compatible_quantities(o.quantity(), p.quantity())) {
    return std::unexpected{ResidualErrc::quantities_differ};
  }
  return common_unit(o, p).and_then(
      [&](Unit unit) -> std::expected<SeriesMeta, ResidualErrc> {
        if (const auto fault = datum_fault(o, p)) {
          return std::unexpected{*fault};
        }
        std::string label{o.label()};
        label += minus_sign;
        label += p.label();
        return SeriesMeta::make({.quantity = Quantity::difference,
                                 .label = std::move(label),
                                 .unit = std::move(unit)});
      });
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
  return residual_meta(o.meta(), p.meta()).transform([&](SeriesMeta meta) {
    TimeAxis times;
    std::vector<Sample> samples;
    const std::size_t capacity = std::min(o.size(), p.size());
    times.reserve(capacity);
    samples.reserve(capacity);
    for_each_common_time(
        o.times(), p.times(), [&](std::size_t i, std::size_t j) {
          times.push_back(o.times()[i]);
          samples.push_back(
              combine(o.samples()[i], p.samples()[j], std::minus<double>{}));
        });
    return TimeSeries{detail::CoreAccess::key(), std::move(times),
                      std::move(samples), std::move(meta)};
  });
}

// ---- slice, shift_time, scale_offset ----

Window index_window(std::span<const Time> times, TimeRange r) {
  const auto first = std::ranges::lower_bound(times, r.begin());
  const auto last = std::ranges::lower_bound(first, times.end(), r.end());
  return {.lo = static_cast<std::size_t>(first - times.begin()),
          .hi = static_cast<std::size_t>(last - times.begin())};
}

TimeSeries slice(const TimeSeries& s, TimeRange r) {
  const Window w = index_window(s.times(), r);
  const auto times = s.times().subspan(w.lo, w.hi - w.lo);
  const auto samples = s.samples().subspan(w.lo, w.hi - w.lo);
  return TimeSeries{
      detail::CoreAccess::key(), TimeAxis(times.begin(), times.end()),
      std::vector<Sample>(samples.begin(), samples.end()), s.meta()};
}

TimeSeries slice(TimeSeries&& s, TimeRange r) {
  const Window w = index_window(s.times(), r);
  TimeSeriesParts parts = std::move(s).into_parts();
  const auto drop = [&w](auto& v) {
    v.erase(v.begin() + static_cast<std::ptrdiff_t>(w.hi), v.end());
    v.erase(v.begin(), v.begin() + static_cast<std::ptrdiff_t>(w.lo));
  };
  drop(parts.times);
  drop(parts.samples);
  return detail::assembled(std::move(parts));
}

namespace {

// The index of the first time that t + dt pushes out of the 64-bit range, if
// any. The times increase, so for dt < 0 only a prefix can underflow (the
// front decides) and for dt > 0 only a suffix can overflow (a binary search
// finds where it starts).
std::optional<std::size_t> first_overflow(std::span<const Time> times,
                                          std::chrono::milliseconds dt) {
  constexpr std::int64_t int64_min = std::numeric_limits<std::int64_t>::min();
  constexpr std::int64_t int64_max = std::numeric_limits<std::int64_t>::max();
  const std::int64_t d = dt.count();
  if (times.empty() or d == 0) {
    return std::nullopt;
  }
  if (d < 0) {
    const bool front_underflows =
        times.front().time_since_epoch().count() < int64_min - d;
    return front_underflows ? std::optional<std::size_t>{0} : std::nullopt;
  }
  const std::int64_t limit = int64_max - d;
  const auto fits = [limit](Time t) noexcept {
    return t.time_since_epoch().count() <= limit;
  };
  const auto it = std::ranges::partition_point(times, fits);
  if (it == times.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(it - times.begin());
}

}  // namespace

std::expected<TimeSeries, TimeOverflow> shift_time(
    TimeSeries s, std::chrono::milliseconds dt) {
  if (const auto bad = first_overflow(s.times(), dt)) {
    return std::unexpected{TimeOverflow{.index = *bad}};
  }
  // Nothing overflows, so the addition cannot, and a uniform shift keeps the
  // times strictly increasing.
  TimeSeriesParts parts = std::move(s).into_parts();
  std::ranges::transform(parts.times, parts.times.begin(),
                         [dt](Time t) noexcept { return t + dt; });
  return detail::assembled(std::move(parts));
}

TimeSeries scale_offset(TimeSeries s, const Calibration& c) {
  return detail::assembled(detail::mapped(std::move(s), c.affine()));
}

// ---- convert ----

namespace {

// The value map from the unit of `meta` to `to`.
std::expected<Affine, UnitError> conversion_from(const SeriesMeta& meta,
                                                 const Unit& to) {
  const std::optional<Unit>& from = meta.unit();
  if (not from) {
    return std::unexpected{UnitError{UnknownUnit{}}};
  }
  return conversion(*from, to).transform_error(
      [](IncompatibleUnits e) { return UnitError{std::move(e)}; });
}

}  // namespace

std::expected<TimeSeries, UnitError> convert(TimeSeries s, const Unit& to) {
  return conversion_from(s.meta(), to).transform([&](const Affine& a) {
    TimeSeriesParts parts = detail::mapped(std::move(s), a);
    parts.meta = parts.meta.rewrite_unit(detail::CoreAccess::key(), to);
    return detail::assembled(std::move(parts));
  });
}

std::expected<StationTable, UnitError> convert(StationTable t,
                                               ColumnIndex column,
                                               const Unit& to) {
  assert(column.value() < t.schema().size());
  return conversion_from(t.schema()[column.value()], to)
      .transform([&](const Affine& a) {
        const detail::CoreKey key = detail::CoreAccess::key();
        StationTable renamed =
            std::move(t).rewrite_column_unit(key, column, to);
        if (a == Affine{}) {  // the values stay as they are
          return renamed;
        }
        return std::move(renamed).transform_column(
            key, column,
            [a](Sample x) noexcept { return detail::apply_affine(a, x); });
      });
}

}  // namespace mov::core
