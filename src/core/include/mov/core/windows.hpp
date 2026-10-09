// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cassert>
#include <chrono>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "mov/core/time.hpp"

namespace mov::core {

namespace detail {

/// b - a for a < b, exactly: the difference of two int64 values fits an
/// uint64 when it is positive, and unsigned arithmetic cannot overflow.
[[nodiscard]] constexpr std::uint64_t positive_distance(
    std::int64_t a, std::int64_t b) noexcept {
  return static_cast<std::uint64_t>(b) - static_cast<std::uint64_t>(a);
}

/// The largest multiple of g at or below v; nullopt when it is below INT64_MIN.
[[nodiscard]] constexpr std::optional<std::int64_t> floor_to(
    std::int64_t v, std::int64_t g) noexcept {
  const std::int64_t rem = v % g;  // the sign of v
  const std::int64_t toward_zero = v - rem;
  if (rem >= 0) {
    return toward_zero;
  }
  if (toward_zero < std::numeric_limits<std::int64_t>::min() + g) {
    return std::nullopt;
  }
  return toward_zero - g;
}

/// The smallest multiple of g at or above v; nullopt when it is above
/// INT64_MAX.
[[nodiscard]] constexpr std::optional<std::int64_t> ceil_to(
    std::int64_t v, std::int64_t g) noexcept {
  const std::int64_t rem = v % g;
  const std::int64_t toward_zero = v - rem;
  if (rem <= 0) {
    return toward_zero;
  }
  if (toward_zero > std::numeric_limits<std::int64_t>::max() - g) {
    return std::nullopt;
  }
  return toward_zero + g;
}

}  // namespace detail

/// Consecutive half-open windows [b, b + span), [b + span, b + 2 span), ...,
/// the last one cut at r.end(): non-empty, disjoint, ordered, covering r
/// exactly, every window but the last exactly `span` long.
/// Precondition (asserted): span >= 1 ms. A release build given a smaller
/// span returns r as one window instead of looping.
[[nodiscard]] constexpr std::vector<TimeRange> split(
    TimeRange r, std::chrono::milliseconds span) {
  assert(span.count() >= 1);
  const std::int64_t end = r.end().time_since_epoch().count();
  const std::int64_t first = r.begin().time_since_epoch().count();
  const std::uint64_t length = detail::positive_distance(first, end);
  const std::uint64_t step =
      span.count() >= 1 ? static_cast<std::uint64_t>(span.count()) : length;
  // 64-bit targets only: size_t holds any window count.
  const std::size_t window_count = ((length - 1) / step) + 1;
  std::vector<TimeRange> windows;
  windows.reserve(window_count);
  std::int64_t begin = first;
  // begin + step stays below end inside the loop, so it cannot overflow.
  while (detail::positive_distance(begin, end) > step) {
    const std::int64_t next = begin + static_cast<std::int64_t>(step);
    windows.push_back(*TimeRange::make(Time{std::chrono::milliseconds{begin}},
                                       Time{std::chrono::milliseconds{next}}));
    begin = next;
  }
  windows.push_back(
      *TimeRange::make(Time{std::chrono::milliseconds{begin}}, r.end()));
  return windows;
}

/// r widened outward to whole multiples of `grain` since the epoch (the begin
/// floored, the end ceiled), so windows split from it by a span that is a
/// multiple of `grain` begin and end on the grain. nullopt when grain < 1 ms
/// or a widened bound overflows.
[[nodiscard]] constexpr std::optional<TimeRange> aligned(
    TimeRange r, std::chrono::milliseconds grain) noexcept {
  const std::int64_t g = grain.count();
  if (g < 1) {
    return std::nullopt;
  }
  const auto lo = detail::floor_to(r.begin().time_since_epoch().count(), g);
  const auto hi = detail::ceil_to(r.end().time_since_epoch().count(), g);
  if (not lo or not hi) {
    return std::nullopt;
  }
  // lo <= begin < end <= hi, so the range is not empty.
  return *TimeRange::make(Time{std::chrono::milliseconds{*lo}},
                          Time{std::chrono::milliseconds{*hi}});
}

enum class DayRangeError : std::uint8_t {
  empty_or_inverted,  // make: not first < end
  not_whole_days,     // covering: a bound is not a UTC midnight
  out_of_range,       // more days, or a day further out, than
                      // std::chrono::days holds (its rep is 32 bits on
                      // MSVC and libc++)
};

/// Half-open calendar days [first, end) with first < end: the days of a USGS
/// daily-values request (decision 34: local calendar days, not instants).
/// A constructed DayRange is always valid; there is no default one.
class DayRange {
 public:
  /// empty_or_inverted unless first < end; out_of_range when end - first
  /// does not fit std::chrono::days.
  [[nodiscard]] static constexpr std::expected<DayRange, DayRangeError> make(
      std::chrono::sys_days first, std::chrono::sys_days end) noexcept {
    if (not(first < end)) {
      return std::unexpected{DayRangeError::empty_or_inverted};
    }
    using Rep = std::chrono::days::rep;
    if (detail::positive_distance(first.time_since_epoch().count(),
                                  end.time_since_epoch().count()) >
        static_cast<std::uint64_t>(std::numeric_limits<Rep>::max())) {
      return std::unexpected{DayRangeError::out_of_range};
    }
    return DayRange{first, end};
  }

  /// The days of r, which must begin and end at UTC midnights
  /// (not_whole_days otherwise): [2026-01-01T00:00, 2026-01-03T00:00) is
  /// 2026-01-01 and 2026-01-02.
  [[nodiscard]] static constexpr std::expected<DayRange, DayRangeError>
  covering(TimeRange r) noexcept {
    const auto first = whole_day(r.begin());
    if (not first) {
      return std::unexpected{first.error()};
    }
    const auto end = whole_day(r.end());
    if (not end) {
      return std::unexpected{end.error()};
    }
    return make(*first, *end);
  }

  [[nodiscard]] constexpr std::chrono::sys_days first() const noexcept {
    return first_;
  }
  /// The first day after the range.
  [[nodiscard]] constexpr std::chrono::sys_days end() const noexcept {
    return end_;
  }
  /// The number of days, at least 1.
  [[nodiscard]] constexpr std::chrono::days size() const noexcept {
    return end_ - first_;
  }

  friend constexpr bool operator==(const DayRange&, const DayRange&) = default;

 private:
  constexpr DayRange(std::chrono::sys_days first,
                     std::chrono::sys_days end) noexcept
      : first_{first}, end_{end} {}

  [[nodiscard]] static constexpr std::expected<std::chrono::sys_days,
                                               DayRangeError>
  whole_day(Time t) noexcept {
    constexpr std::int64_t ms_per_day = 86'400'000;
    const std::int64_t ms = t.time_since_epoch().count();
    if (ms % ms_per_day != 0) {
      return std::unexpected{DayRangeError::not_whole_days};
    }
    const std::int64_t count = ms / ms_per_day;
    if (not std::in_range<std::chrono::days::rep>(count)) {
      return std::unexpected{DayRangeError::out_of_range};
    }
    return std::chrono::sys_days{
        std::chrono::days{static_cast<std::chrono::days::rep>(count)}};
  }

  std::chrono::sys_days first_;
  std::chrono::sys_days end_;
};

}  // namespace mov::core
