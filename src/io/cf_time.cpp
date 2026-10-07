// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/cf_time.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <string_view>
#include <utility>

#include "mov/core/detail/ascii.hpp"
#include "mov/core/time.hpp"
#include "mov/io/error.hpp"

namespace mov::io {

namespace {

using core::detail::equal_ignore_case;
using core::detail::is_space;

constexpr std::array<std::pair<std::string_view, CfTimeUnit>, 10> unit_words{{
    {"millisecond", CfTimeUnit::millisecond},
    {"milliseconds", CfTimeUnit::millisecond},
    {"second", CfTimeUnit::second},
    {"seconds", CfTimeUnit::second},
    {"minute", CfTimeUnit::minute},
    {"minutes", CfTimeUnit::minute},
    {"hour", CfTimeUnit::hour},
    {"hours", CfTimeUnit::hour},
    {"day", CfTimeUnit::day},
    {"days", CfTimeUnit::day},
}};

constexpr std::optional<CfTimeUnit> parse_unit_word(std::string_view word) {
  for (const auto& [name, unit] : unit_words) {
    if (equal_ignore_case(word, name)) {
      return unit;
    }
  }
  return std::nullopt;
}

constexpr bool is_digit(char c) noexcept { return c >= '0' and c <= '9'; }

// Zone offsets are accepted up to +-23:59; the real range is narrower, and a
// reader has no business second-guessing a file's zone.
constexpr int max_offset_hours = 23;
constexpr int max_offset_minutes = 59;

// A position in the text being parsed; every error is built from here.
class Scanner {
 public:
  explicit Scanner(std::string_view text) noexcept : text_{text} {}

  [[nodiscard]] std::size_t pos() const noexcept { return pos_; }
  [[nodiscard]] bool at_end() const noexcept { return pos_ >= text_.size(); }
  [[nodiscard]] char peek() const noexcept { return text_[pos_]; }
  [[nodiscard]] bool peek_is(char c) const noexcept {
    return not at_end() and text_[pos_] == c;
  }
  void advance(std::size_t n = 1) noexcept { pos_ += n; }

  void skip_space() noexcept {
    while (not at_end() and is_space(text_[pos_])) {
      ++pos_;
    }
  }

  /// The maximal run of non-space characters; moves past it.
  std::string_view take_word() noexcept {
    const std::size_t start = pos_;
    while (not at_end() and not is_space(text_[pos_])) {
      ++pos_;
    }
    return text_.substr(start, pos_ - start);
  }

  /// The next `count` characters if they are all digits; moves past them.
  std::optional<int> take_digits(std::size_t count) noexcept {
    if (pos_ + count > text_.size()) {
      return std::nullopt;
    }
    int value = 0;
    for (std::size_t i = 0; i < count; ++i) {
      if (not is_digit(text_[pos_ + i])) {
        return std::nullopt;
      }
      value = (value * 10) + (text_[pos_ + i] - '0');
    }
    pos_ += count;
    return value;
  }

  /// The next character if it is `c`; moves past it.
  bool accept(char c) noexcept {
    const bool found = peek_is(c);
    pos_ += found ? 1U : 0U;
    return found;
  }

  [[nodiscard]] std::string_view text() const noexcept { return text_; }
  [[nodiscard]] std::string_view rest() const noexcept {
    return text_.substr(pos_);
  }
  [[nodiscard]] std::string_view slice(std::size_t from,
                                       std::size_t to) const noexcept {
    return text_.substr(from, to - from);
  }

  [[nodiscard]] ParseError error(ParseErrc code, std::size_t column) const {
    return ParseError::make(code, {.line = 1, .column = column}, text_);
  }
  [[nodiscard]] ParseError error_here(ParseErrc code) const {
    return error(code, pos_);
  }

 private:
  std::string_view text_;
  std::size_t pos_{0};
};

// "<unit> since": the unit, leaving the scanner at the reference time.
std::expected<CfTimeUnit, ParseError> scan_unit_and_since(Scanner& in) {
  in.skip_space();
  const std::size_t unit_start = in.pos();
  const auto unit = parse_unit_word(in.take_word());
  if (not unit) {
    return std::unexpected{in.error(ParseErrc::bad_time_units, unit_start)};
  }
  in.skip_space();
  const std::size_t since_start = in.pos();
  if (not equal_ignore_case(in.take_word(), "since")) {
    return std::unexpected{in.error(ParseErrc::bad_time_units, since_start)};
  }
  in.skip_space();
  return *unit;
}

// The end of the run of characters at `from` that satisfy `accept`, looking at
// no more than `limit` of them.
template <class Predicate>
std::size_t run_end(std::string_view text, std::size_t from,
                    const Predicate& accept, std::size_t limit) {
  std::size_t i = from;
  while (i < text.size() and i - from < limit and accept(text[i])) {
    ++i;
  }
  return i;
}

// The end of "yyyy-mm-dd[( |T)hh:mm[:ss[.f]]]" starting at `start`: the date
// is cut at ten characters so that "2000-01-01-06:00" ends at the zone, and
// the clock is the run of digits, ':' and '.' after the separator. Only the
// shape is found here; core::parse_utc_datetime judges it.
std::size_t datetime_end(std::string_view text, std::size_t start) {
  constexpr std::size_t date_chars = 10;
  const std::size_t date_end = run_end(
      text, start, [](char c) { return is_digit(c) or c == '-'; }, date_chars);
  const bool has_clock =
      date_end < text.size() and
      (text[date_end] == 'T' or
       (text[date_end] == ' ' and date_end + 1 < text.size() and
        is_digit(text[date_end + 1])));
  if (not has_clock) {
    return date_end;
  }
  return run_end(
      text, date_end + 1,
      [](char c) { return is_digit(c) or c == ':' or c == '.'; },
      std::string_view::npos);
}

// The reference date and time, leaving the scanner after it.
std::expected<core::Time, ParseError> scan_datetime(Scanner& in) {
  const std::size_t start = in.pos();
  const std::size_t end = datetime_end(in.text(), start);
  const auto time = core::parse_utc_datetime(in.slice(start, end));
  if (not time) {
    return std::unexpected{
        in.error(ParseErrc::bad_date, start + time.error().column)};
  }
  in.advance(end - start);
  return *time;
}

// "+hh", "+hh:mm" or "+hhmm" (or "-"): the scanner is on the sign.
std::expected<std::chrono::minutes, ParseError> scan_offset(Scanner& in) {
  const std::size_t start = in.pos();
  const int sign = in.peek() == '-' ? -1 : 1;
  in.advance();
  const auto hours = in.take_digits(2);
  if (not hours or *hours > max_offset_hours) {
    return std::unexpected{in.error(ParseErrc::bad_time_units, start)};
  }
  int minutes = 0;
  const bool colon = in.accept(':');
  if (colon or (not in.at_end() and is_digit(in.peek()))) {
    const auto digits = in.take_digits(2);
    if (not digits or *digits > max_offset_minutes) {
      return std::unexpected{in.error(ParseErrc::bad_time_units, start)};
    }
    minutes = *digits;
  }
  return std::chrono::minutes{sign * ((*hours * 60) + minutes)};
}

// The optional zone after the reference time; UTC (zero) when there is none.
std::expected<std::chrono::minutes, ParseError> scan_zone(Scanner& in) {
  in.skip_space();
  if (in.at_end()) {
    return std::chrono::minutes{0};
  }
  if (in.peek() == '+' or in.peek() == '-') {
    return scan_offset(in);
  }
  if (in.peek() == 'Z' or in.peek() == 'z') {
    in.advance();
    return std::chrono::minutes{0};
  }
  if (equal_ignore_case(in.rest().substr(0, 3), "UTC")) {
    in.advance(3);
    return std::chrono::minutes{0};
  }
  return std::unexpected{in.error_here(ParseErrc::bad_time_units)};
}

}  // namespace

std::expected<CfTimeUnits, ParseError> parse_cf_time_units(
    std::string_view text) {
  Scanner in{text};
  const auto unit = scan_unit_and_since(in);
  if (not unit) {
    return std::unexpected{unit.error()};
  }
  const auto reference = scan_datetime(in);
  if (not reference) {
    return std::unexpected{reference.error()};
  }
  const auto zone = scan_zone(in);
  if (not zone) {
    return std::unexpected{zone.error()};
  }
  in.skip_space();
  if (not in.at_end()) {
    return std::unexpected{in.error_here(ParseErrc::trailing_text)};
  }
  return CfTimeUnits{.unit = *unit, .epoch = *reference - *zone};
}

}  // namespace mov::io
