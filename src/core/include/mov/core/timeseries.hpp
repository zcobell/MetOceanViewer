// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <expected>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/detail/core_key.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"

namespace mov::core {

/// The times of a series or of a file station, strictly increasing wherever
/// a type guarantees it.
using TimeAxis = std::vector<Time>;

/// One row of an unordered source (a text file), and one element of
/// TimeSeries::points().
struct Point {
  Time time;
  Sample sample;
  friend constexpr bool operator==(const Point&, const Point&) = default;
};

/// The time and sample vectors have different lengths.
struct LengthMismatch {
  std::size_t times;
  std::size_t samples;
  friend constexpr bool operator==(LengthMismatch, LengthMismatch) = default;
};

/// t[index - 1] < t[index] does not hold (index >= 1).
struct TimeNotIncreasing {
  std::size_t index;
  friend constexpr bool operator==(TimeNotIncreasing,
                                   TimeNotIncreasing) = default;
};

using ConstructionError = std::variant<LengthMismatch, TimeNotIncreasing>;

namespace detail {

/// The first index i >= 1 with not (t[i-1] < t[i]); nullopt if t is strictly
/// increasing. Every strictly-increasing check of the core goes through here.
[[nodiscard]] constexpr std::optional<std::size_t> first_not_increasing(
    std::span<const Time> t) noexcept {
  const auto it = std::ranges::adjacent_find(
      t, [](Time a, Time b) noexcept { return not(a < b); });
  if (it == t.end()) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(it - t.begin()) + 1;
}

/// Element i of TimeSeries::points(). (A transform over iota rather than
/// views::zip, which Apple libc++ does not have everywhere.)
struct PointAt {
  std::span<const Time> times;
  std::span<const Sample> samples;
  [[nodiscard]] constexpr Point operator()(std::size_t i) const noexcept {
    return {.time = times[i], .sample = samples[i]};
  }
};

}  // namespace detail

class TimeSeries;
struct Normalized;

/// What TimeSeries::into_parts hands back.
struct TimeSeriesParts {
  TimeAxis times;
  std::vector<Sample> samples;
  SeriesMeta meta;
};

/// A series of samples at strictly increasing UTC times, with its
/// metadata. A plain value: copies are deep, moves are cheap, and a
/// constructed TimeSeries always satisfies the invariant. There is no
/// builder: build the vectors and call make, or normalize unordered rows.
class TimeSeries {
 public:
  using PointsView = decltype(std::views::iota(std::size_t{0}, std::size_t{0}) |
                              std::views::transform(detail::PointAt{}));

  /// Empty, with default metadata.
  TimeSeries() = default;

  /// LengthMismatch if the sizes differ, then the first TimeNotIncreasing.
  [[nodiscard]] static std::expected<TimeSeries, ConstructionError> make(
      TimeAxis times, std::vector<Sample> samples, SeriesMeta meta);

  /// Core only (detail/core_key.hpp): parts whose invariant the caller has
  /// established. Asserted in debug builds, not checked otherwise.
  TimeSeries(const detail::CoreKey& /*key*/, TimeAxis times,
             std::vector<Sample> samples, SeriesMeta meta)
      : TimeSeries{std::move(times), std::move(samples), std::move(meta)} {
    assert(times_.size() == samples_.size());
    assert(not detail::first_not_increasing(times_));
  }

  [[nodiscard]] std::span<const Time> times() const& noexcept { return times_; }
  std::span<const Time> times() const&& = delete;
  [[nodiscard]] std::span<const Sample> samples() const& noexcept {
    return samples_;
  }
  std::span<const Sample> samples() const&& = delete;
  [[nodiscard]] const SeriesMeta& meta() const& noexcept { return meta_; }
  const SeriesMeta& meta() const&& = delete;
  /// (time, sample) Points, computed from views into this series.
  [[nodiscard]] PointsView points() const& {
    return std::views::iota(std::size_t{0}, size()) |
           std::views::transform(
               detail::PointAt{.times = times_, .samples = samples_});
  }
  PointsView points() const&& = delete;

  [[nodiscard]] std::size_t size() const noexcept { return times_.size(); }
  [[nodiscard]] bool empty() const noexcept { return times_.empty(); }

  [[nodiscard]] TimeSeries with_label(std::string label) const&;
  [[nodiscard]] TimeSeries with_label(std::string label) &&;

  /// SeriesMeta::assume_unit / assume_datum on the metadata. The const&
  /// overloads copy the series only on success.
  [[nodiscard]] std::expected<TimeSeries, AssumeUnitError> assume_unit(
      Unit u) const&;
  [[nodiscard]] std::expected<TimeSeries, AssumeUnitError> assume_unit(
      Unit u) &&;
  [[nodiscard]] std::expected<TimeSeries, AssumeDatumError> assume_datum(
      VerticalDatum d) const&;
  [[nodiscard]] std::expected<TimeSeries, AssumeDatumError> assume_datum(
      VerticalDatum d) &&;

  /// A series on the same times with other samples and metadata (a derived
  /// series). The only check is the O(1) length comparison.
  [[nodiscard]] std::expected<TimeSeries, LengthMismatch> with_samples(
      std::vector<Sample> samples, SeriesMeta meta) const&;
  [[nodiscard]] std::expected<TimeSeries, LengthMismatch> with_samples(
      std::vector<Sample> samples, SeriesMeta meta) &&;

  /// Core only: f applied to every sample, times and metadata unchanged. The
  /// public sample-changing operations keep the metadata coherent with the
  /// values: convert rewrites the unit and shift the datum. scale_offset
  /// changes the values under unchanged metadata (a calibration: the
  /// caller owns what it means).
  template <std::invocable<Sample> F>
    requires std::same_as<std::invoke_result_t<F&, Sample>, Sample>
  [[nodiscard]] TimeSeries transform_samples(const detail::CoreKey& key,
                                             F f) const& {
    TimeSeries copy = *this;
    return std::move(copy).transform_samples(key, std::move(f));
  }
  template <std::invocable<Sample> F>
    requires std::same_as<std::invoke_result_t<F&, Sample>, Sample>
  [[nodiscard]] TimeSeries transform_samples(const detail::CoreKey& /*key*/,
                                             F f) && {
    std::ranges::transform(samples_, samples_.begin(), f);
    return std::move(*this);
  }

  /// Gives up the vectors (e.g. to move them into a StationTable).
  [[nodiscard]] TimeSeriesParts into_parts() && {
    return {.times = std::move(times_),
            .samples = std::move(samples_),
            .meta = std::move(meta_)};
  }

  friend bool operator==(const TimeSeries&, const TimeSeries&) = default;

 private:
  friend Normalized normalize(std::vector<Point> rows, SeriesMeta meta);

  TimeSeries(TimeAxis times, std::vector<Sample> samples, SeriesMeta meta)
      : times_{std::move(times)},
        samples_{std::move(samples)},
        meta_{std::move(meta)} {}

  TimeAxis times_;
  std::vector<Sample> samples_;
  SeriesMeta meta_;
};

/// What normalize changed. Readers turn non-zero counts into warnings
/// (times_reordered, duplicate_times_dropped, conflicting_duplicate_times).
struct NormalizeReport {
  /// Adjacent input pairs with t[i+1] < t[i].
  std::size_t descents{};
  /// Rows dropped because an earlier row (in input order) had the same time.
  std::size_t duplicates_dropped{};
  /// The dropped rows whose Sample differs from the kept one.
  std::size_t conflicting_duplicates{};

  /// Nothing was reordered or dropped.
  [[nodiscard]] constexpr bool clean() const noexcept {
    return descents == 0 and duplicates_dropped == 0;
  }
  friend constexpr bool operator==(const NormalizeReport&,
                                   const NormalizeReport&) = default;
};

struct Normalized {
  TimeSeries series;
  NormalizeReport report;
  friend bool operator==(const Normalized&, const Normalized&) = default;
};

/// The rule of normalize, on indices, for tables whose rows are more than one
/// sample (a model file's records share one time axis): `kept` lists the
/// input rows that survive, in time order, and `report` counts what was
/// moved and dropped. When `report.clean()` the times were already strictly
/// increasing and `kept` is 0, 1, ..., n-1.
struct NormalizingOrder {
  std::vector<std::size_t> kept;
  NormalizeReport report;
};

/// Total. Rows are stably sorted by time (only if some time descends) and, of
/// each run of equal times, the first in input order is kept.
/// `differs(kept, dropped)` says whether two rows of the same time hold
/// different data; it is called once per dropped row, with the kept row's
/// index first, to fill `conflicting_duplicates`.
template <std::predicate<std::size_t, std::size_t> Differs>
[[nodiscard]] NormalizingOrder normalizing_order(std::span<const Time> times,
                                                 Differs differs) {
  NormalizingOrder out;
  out.kept.reserve(times.size());
  std::ranges::copy(std::views::iota(std::size_t{0}, times.size()),
                    std::back_inserter(out.kept));
  if (not detail::first_not_increasing(times)) {
    return out;
  }
  // Adjacent input pairs with t[i+1] < t[i]. (A loop over the shifted range:
  // views::pairwise is not on every target standard library.)
  for (std::size_t i = 1; i < times.size(); ++i) {
    out.report.descents += times[i] < times[i - 1] ? 1U : 0U;
  }
  std::vector<std::size_t> order = std::move(out.kept);
  if (out.report.descents > 0) {
    std::ranges::stable_sort(order, {},
                             [times](std::size_t i) { return times[i]; });
  }
  out.kept.clear();
  out.kept.reserve(order.size());
  for (const std::size_t row : order) {
    if (not out.kept.empty() and times[out.kept.back()] == times[row]) {
      ++out.report.duplicates_dropped;
      out.report.conflicting_duplicates +=
          differs(out.kept.back(), row) ? 1U : 0U;
    } else {
      out.kept.push_back(row);
    }
  }
  return out;
}

/// Total: any rows become a valid series. If the times are already strictly
/// increasing the rows are taken as they are. Otherwise they are stably
/// sorted by time and, of each run of equal times, the first in input order
/// is kept (normalizing_order is the same rule).
[[nodiscard]] Normalized normalize(std::vector<Point> rows, SeriesMeta meta);

/// An observed series and its prediction (v4 kept them as index 0 and 1).
struct ObsVsPred {
  TimeSeries observed;
  TimeSeries predicted;
  friend bool operator==(const ObsVsPred&, const ObsVsPred&) = default;
};

/// Data tied to the station it belongs to.
template <class Station, class Data>
struct AtStation {
  Station station;
  Data data;
  friend bool operator==(const AtStation&, const AtStation&) = default;
};

}  // namespace mov::core
