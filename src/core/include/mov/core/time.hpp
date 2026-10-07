// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>

#include "mov/core/detail/numeric.hpp"

namespace mov::core {

/// A UTC instant with millisecond resolution. The time zone is a display
/// concern and never appears in the core.
using Time = std::chrono::sys_time<std::chrono::milliseconds>;

/// Largest |time| in milliseconds since the epoch that a double represents
/// exactly (SN section 7): 2^53 - 1.
inline constexpr std::int64_t max_abs_time_ms = (std::int64_t{1} << 53) - 1;

enum class TimeRangeError : std::uint8_t { empty_or_inverted };

/// A half-open interval [begin, end) with begin < end. A constructed
/// TimeRange is always valid, and there is no default one.
class TimeRange {
 public:
  [[nodiscard]] static constexpr std::expected<TimeRange, TimeRangeError> make(
      Time begin, Time end) noexcept {
    if (not(begin < end)) {
      return std::unexpected{TimeRangeError::empty_or_inverted};
    }
    return TimeRange{begin, end};
  }

  [[nodiscard]] constexpr Time begin() const noexcept { return begin_; }
  [[nodiscard]] constexpr Time end() const noexcept { return end_; }
  [[nodiscard]] constexpr bool contains(Time t) const noexcept {
    return begin_ <= t and t < end_;
  }

  friend constexpr bool operator==(const TimeRange&,
                                   const TimeRange&) = default;

 private:
  constexpr TimeRange(Time begin, Time end) noexcept
      : begin_{begin}, end_{end} {}

  Time begin_;
  Time end_;
};

enum class ValidRangeError : std::uint8_t { inverted };

/// The days a station reports data. `first` is nullopt when unknown and `last`
/// is nullopt while the station is ongoing. One valid day (first == last) is
/// allowed; the default is unknown and ongoing.
class ValidRange {
 public:
  /// Designated fields, so first and last cannot be swapped by position:
  /// ValidRange::make({.first = start, .last = std::nullopt}).
  struct Bounds {
    std::optional<std::chrono::sys_days> first;
    std::optional<std::chrono::sys_days> last;
  };

  [[nodiscard]] static constexpr std::expected<ValidRange, ValidRangeError>
  make(Bounds b) noexcept {
    if (b.first and b.last and *b.last < *b.first) {
      return std::unexpected{ValidRangeError::inverted};
    }
    return ValidRange{b.first, b.last};
  }

  constexpr ValidRange() noexcept = default;

  [[nodiscard]] constexpr std::optional<std::chrono::sys_days> first()
      const noexcept {
    return first_;
  }
  [[nodiscard]] constexpr std::optional<std::chrono::sys_days> last()
      const noexcept {
    return last_;
  }

  friend constexpr bool operator==(const ValidRange&,
                                   const ValidRange&) = default;

 private:
  constexpr ValidRange(std::optional<std::chrono::sys_days> first,
                       std::optional<std::chrono::sys_days> last) noexcept
      : first_{first}, last_{last} {}

  std::optional<std::chrono::sys_days> first_;
  std::optional<std::chrono::sys_days> last_;
};

namespace detail {

/// epoch + offset_ms, or nullopt when the sum overflows int64 or leaves
/// +-max_abs_time_ms.
[[nodiscard]] constexpr std::optional<Time> offset_time(
    Time epoch, std::int64_t offset_ms) noexcept {
  constexpr std::int64_t int64_max = std::numeric_limits<std::int64_t>::max();
  constexpr std::int64_t int64_min = std::numeric_limits<std::int64_t>::min();
  const std::int64_t base = epoch.time_since_epoch().count();
  if ((offset_ms > 0 and base > int64_max - offset_ms) or
      (offset_ms < 0 and base < int64_min - offset_ms)) {
    return std::nullopt;
  }
  const std::int64_t sum = base + offset_ms;
  if (sum > max_abs_time_ms or sum < -max_abs_time_ms) {
    return std::nullopt;
  }
  return Time{std::chrono::milliseconds{sum}};
}

}  // namespace detail

namespace detail {

/// The double path after the value is known to be within +-max_abs_time_ms.
[[nodiscard]] constexpr std::optional<Time> checked_time_double(
    double value, std::int64_t unit_ms, Time epoch) noexcept {
  constexpr auto limit = static_cast<double>(max_abs_time_ms);
  const double offset = value * static_cast<double>(unit_ms);
  if (not(magnitude(offset) <= limit)) {
    return std::nullopt;
  }
  return offset_time(epoch, round_half_away(offset));
}

}  // namespace detail

/// epoch + value * unit, for a floating-point value in units of `unit`.
/// nullopt if value is NaN or infinite, unit < 1 ms, |value * unit| >
/// max_abs_time_ms or |result| > max_abs_time_ms. Rounds half away from zero
/// to a whole millisecond. A `float` or `long double` is judged by its own
/// range first, so nothing is narrowed out of range.
template <std::floating_point F>
[[nodiscard]] constexpr std::optional<Time> checked_time(
    F value, std::chrono::milliseconds unit, Time epoch) noexcept {
  constexpr auto limit = static_cast<F>(max_abs_time_ms);
  // Bound the value before multiplying: unit >= 1 ms, so a larger value is
  // out of range anyway and the rest cannot overflow. NaN fails both
  // comparisons.
  if (unit.count() < 1 or not(value >= -limit and value <= limit)) {
    return std::nullopt;
  }
  return detail::checked_time_double(static_cast<double>(value), unit.count(),
                                     epoch);
}

/// The integer path, for any integer type (not bool): exact, with a bound
/// check instead of an overflowing multiply. An unsigned value above INT64_MAX
/// is out of range, not wrapped. For an integral double |v| < 2^53 both paths
/// give the same answer.
template <std::integral I>
  requires(not std::same_as<I, bool>)
[[nodiscard]] constexpr std::optional<Time> checked_time(
    I value, std::chrono::milliseconds unit, Time epoch) noexcept {
  if (unit.count() < 1) {
    return std::nullopt;
  }
  const std::int64_t max_value = max_abs_time_ms / unit.count();
  if (std::cmp_greater(value, max_value) or std::cmp_less(value, -max_value)) {
    return std::nullopt;
  }
  return detail::offset_time(epoch,
                             static_cast<std::int64_t>(value) * unit.count());
}

/// Where parse_utc_datetime stopped: the 0-based offset of the first
/// character that does not fit (the end of the text when it is truncated), or
/// the start of the field whose value is out of range.
struct DateTimeError {
  std::size_t column{};
  friend constexpr bool operator==(DateTimeError, DateTimeError) = default;
};

namespace detail {

/// A read position over a text, with the parse primitives of
/// parse_utc_datetime.
class DateTimeCursor {
 public:
  explicit constexpr DateTimeCursor(std::string_view text) noexcept
      : text_{text} {}

  [[nodiscard]] constexpr bool at_end() const noexcept {
    return pos_ >= text_.size();
  }
  [[nodiscard]] constexpr bool next_is(char c) const noexcept {
    return not at_end() and text_[pos_] == c;
  }
  [[nodiscard]] constexpr DateTimeError error_here() const noexcept {
    return DateTimeError{.column = pos_};
  }

  /// Consumes `c` or fails at the current position.
  [[nodiscard]] constexpr std::expected<void, DateTimeError> expect(
      char c) noexcept {
    if (not next_is(c)) {
      return std::unexpected{error_here()};
    }
    ++pos_;
    return {};
  }

  /// Consumes `c` if it is next.
  [[nodiscard]] constexpr bool accept(char c) noexcept {
    const bool found = next_is(c);
    pos_ += found ? 1U : 0U;
    return found;
  }

  /// Consumes exactly `count` digits and checks lo <= value <= hi. A missing
  /// digit fails at that character, a value out of range at the field start.
  [[nodiscard]] constexpr std::expected<int, DateTimeError> field(
      std::size_t count, int lo, int hi) noexcept {
    const std::size_t start = pos_;
    int value = 0;
    for (std::size_t i = 0; i < count; ++i) {
      const auto digit = digit_here();
      if (not digit) {
        return std::unexpected{error_here()};
      }
      value = (value * 10) + *digit;
      ++pos_;
    }
    if (value < lo or value > hi) {
      return std::unexpected{DateTimeError{.column = start}};
    }
    return value;
  }

  /// One to three fraction digits, scaled to milliseconds.
  [[nodiscard]] constexpr std::expected<int, DateTimeError>
  fraction_ms() noexcept {
    if (not digit_here()) {
      return std::unexpected{error_here()};
    }
    int value = 0;
    int scale = 100;
    for (; scale > 0; scale /= 10) {
      const auto digit = digit_here();
      if (not digit) {
        break;
      }
      value += *digit * scale;
      ++pos_;
    }
    return value;
  }

 private:
  [[nodiscard]] constexpr std::optional<int> digit_here() const noexcept {
    if (at_end() or text_[pos_] < '0' or text_[pos_] > '9') {
      return std::nullopt;
    }
    return text_[pos_] - '0';
  }

  std::string_view text_;
  std::size_t pos_{0};
};

/// "hh:mm[:ss[.f[f[f]]]]"
[[nodiscard]] constexpr std::expected<std::chrono::milliseconds, DateTimeError>
parse_clock(DateTimeCursor& cursor) noexcept {
  using std::chrono::milliseconds;
  const auto hour = cursor.field(2, 0, 23);
  if (not hour) {
    return std::unexpected{hour.error()};
  }
  if (const auto colon = cursor.expect(':'); not colon) {
    return std::unexpected{colon.error()};
  }
  const auto minute = cursor.field(2, 0, 59);
  if (not minute) {
    return std::unexpected{minute.error()};
  }
  milliseconds total =
      std::chrono::hours{*hour} + std::chrono::minutes{*minute};
  if (not cursor.accept(':')) {
    return total;
  }
  const auto second = cursor.field(2, 0, 59);
  if (not second) {
    return std::unexpected{second.error()};
  }
  total += std::chrono::seconds{*second};
  if (not cursor.accept('.')) {
    return total;
  }
  const auto fraction = cursor.fraction_ms();
  if (not fraction) {
    return std::unexpected{fraction.error()};
  }
  return total + milliseconds{*fraction};
}

/// "yyyy-mm-dd"
[[nodiscard]] constexpr std::expected<std::chrono::sys_days, DateTimeError>
parse_date(DateTimeCursor& cursor) noexcept {
  using namespace std::chrono;
  const auto y = cursor.field(4, 0, 9999);
  if (not y) {
    return std::unexpected{y.error()};
  }
  if (const auto dash = cursor.expect('-'); not dash) {
    return std::unexpected{dash.error()};
  }
  const auto m = cursor.field(2, 1, 12);
  if (not m) {
    return std::unexpected{m.error()};
  }
  if (const auto dash = cursor.expect('-'); not dash) {
    return std::unexpected{dash.error()};
  }
  const DateTimeError day_column = cursor.error_here();
  const auto d = cursor.field(2, 1, 31);
  if (not d) {
    return std::unexpected{d.error()};
  }
  const year_month_day ymd{year{*y}, month{static_cast<unsigned>(*m)},
                           day{static_cast<unsigned>(*d)}};
  if (not ymd.ok()) {
    return std::unexpected{day_column};  // e.g. February 30
  }
  return sys_days{ymd};
}

}  // namespace detail

namespace detail {

/// The optional "( |T)hh:mm[:ss[.f]]" after the date: midnight when absent.
[[nodiscard]] constexpr std::expected<std::chrono::milliseconds, DateTimeError>
parse_optional_clock(DateTimeCursor& cursor) noexcept {
  if (cursor.accept(' ') or cursor.accept('T')) {
    return parse_clock(cursor);
  }
  return std::chrono::milliseconds{};
}

}  // namespace detail

/// Parses "yyyy-mm-dd[( |T)hh:mm[:ss[.f[f[f]]]]][Z]", always as UTC, whatever
/// the machine's time zone (v4 used local time, N5). Strict: four-digit
/// year, two-digit fields, no surrounding whitespace, no UTC offsets, no
/// leap seconds. 1 to 3 fraction digits are milliseconds.
[[nodiscard]] constexpr std::expected<Time, DateTimeError> parse_utc_datetime(
    std::string_view text) noexcept {
  detail::DateTimeCursor cursor{text};
  const auto date = detail::parse_date(cursor);
  if (not date) {
    return std::unexpected{date.error()};
  }
  const auto clock = detail::parse_optional_clock(cursor);
  if (not clock) {
    return std::unexpected{clock.error()};
  }
  static_cast<void>(cursor.accept('Z'));  // UTC marker; the zone is always UTC
  if (not cursor.at_end()) {
    return std::unexpected{cursor.error_here()};
  }
  return std::chrono::time_point_cast<std::chrono::milliseconds>(*date) +
         *clock;
}

}  // namespace mov::core
