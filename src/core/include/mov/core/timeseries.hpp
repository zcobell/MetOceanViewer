// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <ranges>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "mov/core/datum.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"

namespace mov::core {

enum class ConstructionErrc : std::uint8_t {
  length_mismatch,      // index: the shorter length
  time_not_increasing,  // index: the first i with not (t[i-1] < t[i])
};

struct ConstructionError {
  ConstructionErrc code;
  std::size_t index;
  friend constexpr bool operator==(ConstructionError,
                                   ConstructionError) = default;
};

class TimeSeries;

namespace detail {

/// Core-internal: a TimeSeries from parts whose invariant the caller has
/// established (equal lengths, strictly increasing times), without checking
/// it again. The invariant is asserted in debug builds.
[[nodiscard]] TimeSeries trusted_series(std::vector<Time> times,
                                        std::vector<Sample> samples,
                                        SeriesMeta meta);

}  // namespace detail

/// A series of samples at strictly increasing UTC times (C2), with its
/// metadata. A plain value: copies are deep, moves are cheap, and a
/// constructed TimeSeries always satisfies the invariant. There is no
/// builder: build the vectors and call make, or normalize unordered rows.
class TimeSeries {
 public:
  using PointsView =
      decltype(std::views::zip(std::declval<const std::vector<Time>&>(),
                               std::declval<const std::vector<Sample>&>()));

  /// Empty, with default metadata.
  TimeSeries() = default;

  /// length_mismatch if the sizes differ, then time_not_increasing.
  [[nodiscard]] static std::expected<TimeSeries, ConstructionError> make(
      std::vector<Time> times, std::vector<Sample> samples, SeriesMeta meta);

  [[nodiscard]] std::span<const Time> times() const& noexcept { return times_; }
  std::span<const Time> times() const&& = delete;
  [[nodiscard]] std::span<const Sample> samples() const& noexcept {
    return samples_;
  }
  std::span<const Sample> samples() const&& = delete;
  [[nodiscard]] const SeriesMeta& meta() const& noexcept { return meta_; }
  const SeriesMeta& meta() const&& = delete;
  /// (time, sample) pairs; the elements are views into this series.
  [[nodiscard]] PointsView points() const& {
    return std::views::zip(times_, samples_);
  }
  PointsView points() const&& = delete;

  [[nodiscard]] std::size_t size() const noexcept { return times_.size(); }
  [[nodiscard]] bool empty() const noexcept { return times_.empty(); }

  [[nodiscard]] TimeSeries with_label(std::string label) const&;
  [[nodiscard]] TimeSeries with_label(std::string label) &&;

  /// SeriesMeta::assume_unit / assume_datum on the metadata.
  [[nodiscard]] std::expected<TimeSeries, MetaError> assume_unit(Unit u) const&;
  [[nodiscard]] std::expected<TimeSeries, MetaError> assume_unit(Unit u) &&;
  [[nodiscard]] std::expected<TimeSeries, MetaError> assume_datum(
      VerticalDatum d) const&;
  [[nodiscard]] std::expected<TimeSeries, MetaError> assume_datum(
      VerticalDatum d) &&;

  /// f applied to every sample; times and metadata unchanged. Sample keeps
  /// the values finite.
  template <std::invocable<Sample> F>
    requires std::same_as<std::invoke_result_t<F&, Sample>, Sample>
  [[nodiscard]] TimeSeries transform_samples(F f) const& {
    TimeSeries copy = *this;
    return std::move(copy).transform_samples(std::move(f));
  }
  template <std::invocable<Sample> F>
    requires std::same_as<std::invoke_result_t<F&, Sample>, Sample>
  [[nodiscard]] TimeSeries transform_samples(F f) && {
    std::ranges::transform(samples_, samples_.begin(), f);
    return std::move(*this);
  }

  friend bool operator==(const TimeSeries&, const TimeSeries&) = default;

 private:
  friend TimeSeries detail::trusted_series(std::vector<Time> times,
                                           std::vector<Sample> samples,
                                           SeriesMeta meta);

  TimeSeries(std::vector<Time> times, std::vector<Sample> samples,
             SeriesMeta meta)
      : times_{std::move(times)},
        samples_{std::move(samples)},
        meta_{std::move(meta)} {}

  std::vector<Time> times_;
  std::vector<Sample> samples_;
  SeriesMeta meta_;
};

/// One row of an unordered source (a text file).
struct Point {
  Time time;
  Sample sample;
  friend constexpr bool operator==(const Point&, const Point&) = default;
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

/// Total: any rows become a valid series. If the times are already strictly
/// increasing the rows are taken as they are. Otherwise they are stably
/// sorted by time and, of each run of equal times, the first in input order
/// is kept.
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
