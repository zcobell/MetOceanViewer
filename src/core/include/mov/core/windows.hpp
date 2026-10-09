// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cassert>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <iterator>
#include <limits>
#include <optional>
#include <ranges>

#include "mov/core/time.hpp"

namespace mov::core {

// Window counts are computed in uint64 and handed out as size_t.
static_assert(sizeof(std::size_t) >= sizeof(std::uint64_t),
              "mov::core::Windows needs a 64-bit size_t");

namespace detail {

/// Milliseconds in one Duration, whose period is a whole number of
/// milliseconds.
template <class Duration>
  requires((Duration::period::num * 1000) % Duration::period::den == 0)
[[nodiscard]] constexpr std::int64_t ms_per() noexcept {
  using Period = typename Duration::period;
  return static_cast<std::int64_t>((Period::num * 1000) / Period::den);
}

}  // namespace detail

/// A positive duration in milliseconds: the length of a request window and
/// the grain windows are aligned to. Built only by make (nullopt for less
/// than 1 ms) or, for a constant, by of<Count, Duration>(), which does not
/// compile for a count that is not positive or does not fit.
class WindowSpan {
 public:
  [[nodiscard]] static constexpr std::optional<WindowSpan> make(
      std::chrono::milliseconds span) noexcept {
    if (span.count() < 1) {
      return std::nullopt;
    }
    return WindowSpan{span};
  }

  /// WindowSpan::of<31, std::chrono::days>(): Count units of a Duration
  /// that is a whole number of milliseconds. Computed in int64 milliseconds,
  /// whatever Duration's own rep (std::chrono::days has 32 bits on MSVC and
  /// libc++).
  template <std::int64_t Count, class Duration = std::chrono::milliseconds>
    requires(Count >= 1 and Count <= std::numeric_limits<std::int64_t>::max() /
                                         detail::ms_per<Duration>())
  [[nodiscard]] static constexpr WindowSpan of() noexcept {
    return WindowSpan{
        std::chrono::milliseconds{Count * detail::ms_per<Duration>()}};
  }

  [[nodiscard]] constexpr std::chrono::milliseconds duration() const noexcept {
    return span_;
  }

  friend constexpr auto operator<=>(WindowSpan, WindowSpan) = default;

 private:
  explicit constexpr WindowSpan(std::chrono::milliseconds span) noexcept
      : span_{span} {}

  std::chrono::milliseconds span_;
};

namespace detail {

/// b - a for a <= b, exactly: the difference of two int64 values fits an
/// uint64 when it is not negative, and unsigned arithmetic cannot overflow.
[[nodiscard]] constexpr std::uint64_t positive_distance(
    std::int64_t a, std::int64_t b) noexcept {
  return static_cast<std::uint64_t>(b) - static_cast<std::uint64_t>(a);
}

/// The largest multiple of g at or below v; nullopt when it is below
/// INT64_MIN. Precondition (asserted): g >= 1.
[[nodiscard]] constexpr std::optional<std::int64_t> floor_to(
    std::int64_t v, std::int64_t g) noexcept {
  assert(g >= 1);
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
/// INT64_MAX. Precondition (asserted): g >= 1.
[[nodiscard]] constexpr std::optional<std::int64_t> ceil_to(
    std::int64_t v, std::int64_t g) noexcept {
  assert(g >= 1);
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

namespace detail {

/// Window i of [first, end) in steps of `step`. Precondition: first < end
/// and first + i * step < end, so neither the product nor the sums overflow
/// in uint64 and both bounds fit int64.
[[nodiscard]] constexpr TimeRange window_at(std::int64_t first,
                                            std::int64_t end,
                                            std::uint64_t step,
                                            std::uint64_t i) noexcept {
  const auto begin =
      static_cast<std::int64_t>(static_cast<std::uint64_t>(first) + (i * step));
  const std::int64_t stop =
      positive_distance(begin, end) > step
          ? static_cast<std::int64_t>(static_cast<std::uint64_t>(begin) + step)
          : end;
  return *TimeRange::make(Time{std::chrono::milliseconds{begin}},
                          Time{std::chrono::milliseconds{stop}});
}

}  // namespace detail

/// The consecutive half-open windows of a range, [b, b + span),
/// [b + span, b + 2 span), ..., the last one cut at the range's end: never
/// empty, disjoint, ordered, covering the range exactly, every window but the
/// last exactly `span` long. A view computed on demand: it allocates nothing,
/// is trivially copyable, and its iterators are random access, so the
/// ranges algorithms and std::ranges::to take it. Each window is a value:
/// operator* returns a TimeRange.
class Windows : public std::ranges::view_interface<Windows> {
 public:
  class iterator {
   public:
    using value_type = TimeRange;
    using difference_type = std::ptrdiff_t;
    using iterator_concept = std::random_access_iterator_tag;
    /// operator* returns a value, so to a C++17 algorithm this is an input
    /// iterator.
    using iterator_category = std::input_iterator_tag;

    constexpr iterator() noexcept = default;

    [[nodiscard]] constexpr TimeRange operator*() const noexcept {
      return detail::window_at(first_, end_, step_, index_);
    }
    [[nodiscard]] constexpr TimeRange operator[](
        difference_type n) const noexcept {
      return *(*this + n);
    }

    constexpr iterator& operator++() noexcept {
      ++index_;
      return *this;
    }
    constexpr iterator operator++(int) noexcept {
      iterator before = *this;
      ++index_;
      return before;
    }
    constexpr iterator& operator--() noexcept {
      --index_;
      return *this;
    }
    constexpr iterator operator--(int) noexcept {
      iterator before = *this;
      --index_;
      return before;
    }
    constexpr iterator& operator+=(difference_type n) noexcept {
      index_ =
          static_cast<std::uint64_t>(static_cast<difference_type>(index_) + n);
      return *this;
    }
    constexpr iterator& operator-=(difference_type n) noexcept {
      return *this += -n;
    }
    [[nodiscard]] friend constexpr iterator operator+(
        iterator it, difference_type n) noexcept {
      return it += n;
    }
    [[nodiscard]] friend constexpr iterator operator+(difference_type n,
                                                      iterator it) noexcept {
      return it += n;
    }
    [[nodiscard]] friend constexpr iterator operator-(
        iterator it, difference_type n) noexcept {
      return it -= n;
    }
    [[nodiscard]] friend constexpr difference_type operator-(
        iterator a, iterator b) noexcept {
      return static_cast<difference_type>(a.index_) -
             static_cast<difference_type>(b.index_);
    }
    /// Iterators of one view compare by position.
    friend constexpr bool operator==(iterator a, iterator b) noexcept {
      return a.index_ == b.index_;
    }
    friend constexpr std::strong_ordering operator<=>(iterator a,
                                                      iterator b) noexcept {
      return a.index_ <=> b.index_;
    }

   private:
    friend class Windows;
    constexpr iterator(std::int64_t first, std::int64_t end, std::uint64_t step,
                       std::uint64_t index) noexcept
        : first_{first}, end_{end}, step_{step}, index_{index} {}

    // The view's own numbers, so an iterator outlives its view.
    std::int64_t first_{0};
    std::int64_t end_{0};
    std::uint64_t step_{1};
    std::uint64_t index_{0};
  };

  /// No windows; it exists so the view is default-constructible.
  constexpr Windows() noexcept = default;

  constexpr Windows(TimeRange r, WindowSpan span) noexcept
      : first_{r.begin().time_since_epoch().count()},
        end_{r.end().time_since_epoch().count()},
        step_{static_cast<std::uint64_t>(span.duration().count())},
        // begin < end, so the length is at least 1.
        count_{((detail::positive_distance(first_, end_) - 1) / step_) + 1} {}

  [[nodiscard]] constexpr iterator begin() const noexcept {
    return iterator{first_, end_, step_, 0};
  }
  [[nodiscard]] constexpr iterator end() const noexcept {
    return iterator{first_, end_, step_, count_};
  }
  /// The number of windows: ceil(length / span), at least 1 for a split.
  [[nodiscard]] constexpr std::size_t size() const noexcept { return count_; }

  /// Window i. Precondition (asserted): i < size().
  [[nodiscard]] constexpr TimeRange operator[](std::size_t i) const noexcept {
    assert(i < count_);
    return detail::window_at(first_, end_, step_, i);
  }

  friend constexpr bool operator==(const Windows& a,
                                   const Windows& b) noexcept {
    return a.first_ == b.first_ and a.end_ == b.end_ and a.step_ == b.step_ and
           a.count_ == b.count_;
  }

 private:
  std::int64_t first_{0};
  std::int64_t end_{0};
  std::uint64_t step_{1};
  std::uint64_t count_{0};
};

/// The windows of r, `span` long but the last (Windows).
[[nodiscard]] constexpr Windows split(TimeRange r, WindowSpan span) noexcept {
  return Windows{r, span};
}

/// r widened outward to whole multiples of `grain` since the epoch (the begin
/// floored, the end ceiled), so windows split from it by a span that is a
/// multiple of `grain` begin and end on the grain. nullopt only when a
/// widened bound would leave the int64 time line.
[[nodiscard]] constexpr std::optional<TimeRange> aligned(
    TimeRange r, WindowSpan grain) noexcept {
  const std::int64_t g = grain.duration().count();
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
  empty_or_inverted,  // not first < end
  out_of_range,       // a day outside [earliest_day, latest_day]
};

/// Half-open local calendar days [first, end) with first < end: the days of
/// a USGS daily-values request. A day is a station's calendar day, not an
/// instant (decision 34), so this is std::chrono::local_days and nothing in
/// the core turns an instant into one. Every day lies in
/// [earliest_day, latest_day], the days std::chrono::year can name, so every
/// standard library holds every size. A constructed DayRange is always
/// valid; there is no default one.
class DayRange {
 public:
  static constexpr std::chrono::local_days earliest_day{
      std::chrono::year::min() / std::chrono::January / 1};
  static constexpr std::chrono::local_days latest_day{
      std::chrono::year::max() / std::chrono::December / 31};

  /// empty_or_inverted unless first < end, then out_of_range unless
  /// earliest_day <= first and end - 1 day <= latest_day.
  [[nodiscard]] static constexpr std::expected<DayRange, DayRangeError> make(
      std::chrono::local_days first, std::chrono::local_days end) noexcept {
    if (not(first < end)) {
      return std::unexpected{DayRangeError::empty_or_inverted};
    }
    if (first < earliest_day or latest_day < end - std::chrono::days{1}) {
      return std::unexpected{DayRangeError::out_of_range};
    }
    return DayRange{first, end};
  }

  [[nodiscard]] constexpr std::chrono::local_days first() const noexcept {
    return first_;
  }
  /// The first day after the range.
  [[nodiscard]] constexpr std::chrono::local_days end() const noexcept {
    return end_;
  }
  /// The number of days, at least 1.
  [[nodiscard]] constexpr std::chrono::days size() const noexcept {
    return end_ - first_;
  }

  friend constexpr bool operator==(const DayRange&, const DayRange&) = default;

 private:
  constexpr DayRange(std::chrono::local_days first,
                     std::chrono::local_days end) noexcept
      : first_{first}, end_{end} {}

  std::chrono::local_days first_;
  std::chrono::local_days end_;
};

}  // namespace mov::core
