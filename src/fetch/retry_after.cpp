// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

#include "mov/fetch/retry.hpp"

namespace mov::fetch {

namespace {

using std::chrono::seconds;
using std::chrono::sys_seconds;

/// The value RFC 9111 §1.2.2 says an overflowing delta-seconds becomes.
constexpr std::int64_t delta_seconds_ceiling = std::int64_t{1} << 31;

[[nodiscard]] constexpr bool is_digit(char c) noexcept {
  return c >= '0' and c <= '9';
}

[[nodiscard]] constexpr std::string_view trim_ows(
    std::string_view text) noexcept {
  const auto is_ows = [](char c) { return c == ' ' or c == '\t'; };
  while (not text.empty() and is_ows(text.front())) {
    text.remove_prefix(1);
  }
  while (not text.empty() and is_ows(text.back())) {
    text.remove_suffix(1);
  }
  return text;
}

/// Reads an HTTP-date front to back; every take_ consumes on success only.
class Cursor {
 public:
  explicit constexpr Cursor(std::string_view text) noexcept : rest_{text} {}

  [[nodiscard]] constexpr bool done() const noexcept { return rest_.empty(); }

  [[nodiscard]] constexpr bool take(std::string_view literal) noexcept {
    if (not rest_.starts_with(literal)) {
      return false;
    }
    rest_.remove_prefix(literal.size());
    return true;
  }

  /// Exactly `n` digits.
  [[nodiscard]] constexpr std::optional<int> take_digits(
      std::size_t n) noexcept {
    if (rest_.size() < n) {
      return std::nullopt;
    }
    int value = 0;
    for (std::size_t i = 0; i < n; ++i) {
      const char c = rest_[i];
      if (not is_digit(c)) {
        return std::nullopt;
      }
      value = (value * 10) + (c - '0');
    }
    rest_.remove_prefix(n);
    return value;
  }

  /// The index of the first of `names` that the text starts with.
  template <std::size_t N>
  [[nodiscard]] constexpr std::optional<std::size_t> take_one_of(
      const std::array<std::string_view, N>& names) noexcept {
    for (std::size_t i = 0; i < N; ++i) {
      if (take(names[i])) {
        return i;
      }
    }
    return std::nullopt;
  }

 private:
  std::string_view rest_;
};

constexpr std::array<std::string_view, 7> short_days{"Mon", "Tue", "Wed", "Thu",
                                                     "Fri", "Sat", "Sun"};
constexpr std::array<std::string_view, 7> long_days{
    "Monday", "Tuesday",  "Wednesday", "Thursday",
    "Friday", "Saturday", "Sunday"};
constexpr std::array<std::string_view, 12> months{"Jan", "Feb", "Mar", "Apr",
                                                  "May", "Jun", "Jul", "Aug",
                                                  "Sep", "Oct", "Nov", "Dec"};

struct Civil {
  int year;
  std::size_t month_index;  // 0-based
  int day;
  int hour;
  int minute;
  int second;
};

/// time-of-day = hour ":" minute ":" second, with second up to 60 (a leap
/// second, which RFC 9110 allows and which lands on the next minute).
[[nodiscard]] constexpr bool take_time(Cursor& c, Civil& out) noexcept {
  const auto h = c.take_digits(2);
  if (not h or not c.take(":")) {
    return false;
  }
  const auto m = c.take_digits(2);
  if (not m or not c.take(":")) {
    return false;
  }
  const auto s = c.take_digits(2);
  if (not s) {
    return false;
  }
  constexpr int max_hour = 23;
  constexpr int max_minute = 59;
  constexpr int max_second = 60;
  if (*h > max_hour or *m > max_minute or *s > max_second) {
    return false;
  }
  out.hour = *h;
  out.minute = *m;
  out.second = *s;
  return true;
}

[[nodiscard]] std::optional<sys_seconds> to_instant(const Civil& c) noexcept {
  const std::chrono::year_month_day ymd{
      std::chrono::year{c.year},
      std::chrono::month{static_cast<unsigned>(c.month_index + 1)},
      std::chrono::day{static_cast<unsigned>(c.day)}};
  if (not ymd.ok()) {
    return std::nullopt;
  }
  return sys_seconds{std::chrono::sys_days{ymd}} + std::chrono::hours{c.hour} +
         std::chrono::minutes{c.minute} + seconds{c.second};
}

// Sun, 06 Nov 1994 08:49:37 GMT
[[nodiscard]] std::optional<sys_seconds> imf_fixdate(
    std::string_view text) noexcept {
  Cursor c{text};
  Civil out{};
  if (not c.take_one_of(short_days) or not c.take(", ")) {
    return std::nullopt;
  }
  const auto day = c.take_digits(2);
  if (not day or not c.take(" ")) {
    return std::nullopt;
  }
  const auto month = c.take_one_of(months);
  if (not month or not c.take(" ")) {
    return std::nullopt;
  }
  const auto year = c.take_digits(4);
  if (not year or not c.take(" ") or not take_time(c, out) or
      not c.take(" GMT") or not c.done()) {
    return std::nullopt;
  }
  out.year = *year;
  out.month_index = *month;
  out.day = *day;
  return to_instant(out);
}

// Sunday, 06-Nov-94 08:49:37 GMT
[[nodiscard]] std::optional<sys_seconds> rfc850_date(
    std::string_view text) noexcept {
  Cursor c{text};
  Civil out{};
  if (not c.take_one_of(long_days) or not c.take(", ")) {
    return std::nullopt;
  }
  const auto day = c.take_digits(2);
  if (not day or not c.take("-")) {
    return std::nullopt;
  }
  const auto month = c.take_one_of(months);
  if (not month or not c.take("-")) {
    return std::nullopt;
  }
  const auto yy = c.take_digits(2);
  if (not yy or not c.take(" ") or not take_time(c, out) or
      not c.take(" GMT") or not c.done()) {
    return std::nullopt;
  }
  constexpr int pivot = 70;
  constexpr int nineteen_hundred = 1900;
  constexpr int two_thousand = 2000;
  out.year = *yy >= pivot ? nineteen_hundred + *yy : two_thousand + *yy;
  out.month_index = *month;
  out.day = *day;
  return to_instant(out);
}

// Sun Nov  6 08:49:37 1994
[[nodiscard]] std::optional<sys_seconds> asctime_date(
    std::string_view text) noexcept {
  Cursor c{text};
  Civil out{};
  if (not c.take_one_of(short_days) or not c.take(" ")) {
    return std::nullopt;
  }
  const auto month = c.take_one_of(months);
  if (not month or not c.take(" ")) {
    return std::nullopt;
  }
  const auto day = c.take(" ") ? c.take_digits(1) : c.take_digits(2);
  if (not day or not c.take(" ") or not take_time(c, out) or not c.take(" ")) {
    return std::nullopt;
  }
  const auto year = c.take_digits(4);
  if (not year or not c.done()) {
    return std::nullopt;
  }
  out.year = *year;
  out.month_index = *month;
  out.day = *day;
  return to_instant(out);
}

/// 1*DIGIT, saturating at 2^31.
[[nodiscard]] std::optional<std::int64_t> delta_seconds(
    std::string_view text) noexcept {
  if (text.empty() or not std::ranges::all_of(text, is_digit)) {
    return std::nullopt;
  }
  std::int64_t value = 0;
  for (const char c : text) {
    value = std::min(delta_seconds_ceiling, (value * 10) + (c - '0'));
  }
  return value;
}

}  // namespace

std::optional<sys_seconds> parse_http_date(std::string_view text) noexcept {
  if (const auto t = imf_fixdate(text)) {
    return t;
  }
  if (const auto t = rfc850_date(text)) {
    return t;
  }
  return asctime_date(text);
}

std::optional<seconds> retry_after(
    std::string_view value, std::optional<std::string_view> date) noexcept {
  const std::string_view text = trim_ows(value);
  if (const auto delta = delta_seconds(text)) {
    return seconds{std::max<std::int64_t>(1, *delta)};
  }
  const auto target = parse_http_date(text);
  if (not target or not date) {
    return std::nullopt;
  }
  const auto now = parse_http_date(trim_ows(*date));
  if (not now) {
    return std::nullopt;
  }
  return std::max(seconds{1}, *target - *now);
}

}  // namespace mov::fetch
