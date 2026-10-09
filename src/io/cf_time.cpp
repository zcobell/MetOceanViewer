// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/cf_time.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/core/ascii.hpp"
#include "mov/core/time.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read.hpp"
#include "mov/io/warning.hpp"

namespace mov::io {

namespace {

using core::ascii::equal_ignore_case;

// ---- units -----------------------------------------------------------------

struct UnitName {
  std::string_view name;
  CfTimeUnit unit;
};

// A plural adds an "s" to any of these.
constexpr std::array<UnitName, 5> unit_words{{
    {.name = "millisecond", .unit = CfTimeUnit::millisecond},
    {.name = "second", .unit = CfTimeUnit::second},
    {.name = "minute", .unit = CfTimeUnit::minute},
    {.name = "hour", .unit = CfTimeUnit::hour},
    {.name = "day", .unit = CfTimeUnit::day},
}};

// The UDUNITS abbreviations CF files use. "hr", "min" and "sec" take a plural
// "s" too; the one-letter ones do not ("ds" is not days).
constexpr std::array<UnitName, 6> unit_abbreviations{{
    {.name = "d", .unit = CfTimeUnit::day},
    {.name = "h", .unit = CfTimeUnit::hour},
    {.name = "hr", .unit = CfTimeUnit::hour},
    {.name = "min", .unit = CfTimeUnit::minute},
    {.name = "s", .unit = CfTimeUnit::second},
    {.name = "sec", .unit = CfTimeUnit::second},
}};

constexpr std::optional<CfTimeUnit> find_unit(std::span<const UnitName> table,
                                              std::string_view word) {
  const auto found = std::ranges::find_if(table, [word](const UnitName& entry) {
    return equal_ignore_case(word, entry.name);
  });
  return found == table.end() ? std::nullopt
                              : std::optional<CfTimeUnit>{found->unit};
}

constexpr std::optional<CfTimeUnit> parse_unit_word(std::string_view word) {
  if (const auto exact = find_unit(unit_words, word)) {
    return exact;
  }
  if (const auto abbreviation = find_unit(unit_abbreviations, word)) {
    return abbreviation;
  }
  // The plural: one more "s" on a word or on a multi-letter abbreviation.
  constexpr std::size_t shortest_pluralizable = 2;
  if (word.size() <= shortest_pluralizable or
      not equal_ignore_case(word.substr(word.size() - 1), "s")) {
    return std::nullopt;
  }
  const std::string_view stem = word.substr(0, word.size() - 1);
  if (const auto plural = find_unit(unit_words, stem)) {
    return plural;
  }
  return find_unit(unit_abbreviations, stem);
}

// ---- numbers ---------------------------------------------------------------

// The value of a run of decimal digits; the one place a digit run becomes a
// number.
constexpr int decode_decimal(std::string_view digits) noexcept {
  int value = 0;
  for (const char c : digits) {
    value = (value * 10) + (c - '0');
  }
  return value;
}

constexpr std::size_t digit_run(std::string_view text) noexcept {
  return static_cast<std::size_t>(
      std::ranges::find_if_not(text, core::ascii::is_digit) - text.begin());
}

// The fraction of a second as milliseconds, rounded half up, and whether a
// nonzero digit beyond the millisecond was dropped (0.0004 is dropped,
// 0.000000 loses nothing). `digits` is the run after the decimal point.
struct Milliseconds {
  int value;
  bool dropped;
};

constexpr Milliseconds fraction_to_milliseconds(
    std::string_view digits) noexcept {
  constexpr std::size_t ms_digits = 3;
  const std::string_view kept = digits.substr(0, ms_digits);
  int value = decode_decimal(kept);
  for (std::size_t missing = kept.size(); missing < ms_digits; ++missing) {
    value *= 10;
  }
  const std::string_view beyond =
      digits.size() > ms_digits ? digits.substr(ms_digits) : std::string_view{};
  if (not beyond.empty() and beyond.front() >= '5') {
    ++value;
  }
  const bool dropped =
      std::ranges::any_of(beyond, [](char c) noexcept { return c != '0'; });
  return Milliseconds{.value = value, .dropped = dropped};
}

// ---- scanning --------------------------------------------------------------

// A position in the text being parsed; every error is built from here.
class Scanner {
 public:
  explicit Scanner(std::string_view text) noexcept : text_{text}, rest_{text} {}

  [[nodiscard]] std::size_t pos() const noexcept {
    return text_.size() - rest_.size();
  }
  [[nodiscard]] bool at_end() const noexcept { return rest_.empty(); }
  [[nodiscard]] std::string_view rest() const noexcept { return rest_; }
  [[nodiscard]] bool peek_is(char c) const noexcept {
    return not rest_.empty() and rest_.front() == c;
  }
  void advance(std::size_t n = 1) noexcept { rest_.remove_prefix(n); }
  void skip_space() noexcept { rest_ = detail::skip_space(rest_); }

  [[nodiscard]] bool accept(char c) noexcept {
    const bool found = peek_is(c);
    if (found) {
      advance();
    }
    return found;
  }

  /// The next word (empty at the end of the text) and where it starts.
  struct Word {
    std::string_view text;
    std::size_t column;
  };
  [[nodiscard]] Word word() noexcept {
    skip_space();
    const std::size_t column = pos();
    return Word{.text = detail::next_word(rest_).value_or(std::string_view{}),
                .column = column};
  }

  /// `min_digits` to `max_digits` digits with a value in [lo, hi]. Too few
  /// digits is an error at the character that is not one; a value out of
  /// range at the start of the field.
  [[nodiscard]] std::expected<int, ParseError> number(std::size_t min_digits,
                                                      std::size_t max_digits,
                                                      int lo, int hi) {
    const std::size_t start = pos();
    const std::size_t length = std::min(digit_run(rest_), max_digits);
    if (length < min_digits) {
      return std::unexpected{error(ParseErrc::bad_date, start + length)};
    }
    const int value = decode_decimal(rest_.substr(0, length));
    if (value < lo or value > hi) {
      return std::unexpected{error(ParseErrc::bad_date, start)};
    }
    advance(length);
    return value;
  }

  [[nodiscard]] std::expected<void, ParseError> expect(char c, ParseErrc code) {
    if (not accept(c)) {
      return std::unexpected{error_here(code)};
    }
    return {};
  }

  [[nodiscard]] ParseError error(ParseErrc code, std::size_t column) const {
    return ParseError::make(code, {.line = 1, .column = column}, text_);
  }
  [[nodiscard]] ParseError error_here(ParseErrc code) const {
    return error(code, pos());
  }

 private:
  std::string_view text_;
  std::string_view rest_;
};

// "<unit> since": the unit, leaving the scanner at the reference time.
std::expected<CfTimeUnit, ParseError> scan_unit_and_since(Scanner& in) {
  const Scanner::Word unit_word = in.word();
  const auto unit = parse_unit_word(unit_word.text);
  if (not unit) {
    return std::unexpected{
        in.error(ParseErrc::bad_time_units, unit_word.column)};
  }
  const Scanner::Word since = in.word();
  if (not equal_ignore_case(since.text, "since")) {
    return std::unexpected{in.error(ParseErrc::bad_time_units, since.column)};
  }
  in.skip_space();
  return *unit;
}

// "yyyy-m-d" (month and day one or two digits).
std::expected<std::chrono::sys_days, ParseError> scan_date(Scanner& in) {
  using namespace std::chrono;
  const auto y = in.number(4, 4, 0, 9999);
  if (not y) {
    return std::unexpected{y.error()};
  }
  if (const auto dash = in.expect('-', ParseErrc::bad_date); not dash) {
    return std::unexpected{dash.error()};
  }
  const auto m = in.number(1, 2, 1, 12);
  if (not m) {
    return std::unexpected{m.error()};
  }
  if (const auto dash = in.expect('-', ParseErrc::bad_date); not dash) {
    return std::unexpected{dash.error()};
  }
  const std::size_t day_column = in.pos();
  const auto d = in.number(1, 2, 1, 31);
  if (not d) {
    return std::unexpected{d.error()};
  }
  const year_month_day ymd{year{*y}, month{static_cast<unsigned>(*m)},
                           day{static_cast<unsigned>(*d)}};
  if (not ymd.ok()) {
    return std::unexpected{
        in.error(ParseErrc::bad_date, day_column)};  // Feb 30
  }
  return sys_days{ymd};
}

struct Clock {
  std::chrono::milliseconds time;
  bool dropped;  // sub-millisecond digits were rounded away
};

// The seconds after "h:m:", with an optional fraction.
std::expected<Clock, ParseError> scan_seconds(Scanner& in) {
  const auto s = in.number(1, 2, 0, 59);
  if (not s) {
    return std::unexpected{s.error()};
  }
  Clock clock{.time = std::chrono::seconds{*s}, .dropped = false};
  if (not in.accept('.')) {
    return clock;
  }
  const std::size_t digits = digit_run(in.rest());
  if (digits == 0) {
    return std::unexpected{in.error_here(ParseErrc::bad_date)};
  }
  const Milliseconds fraction =
      fraction_to_milliseconds(in.rest().substr(0, digits));
  in.advance(digits);
  clock.time += std::chrono::milliseconds{fraction.value};
  clock.dropped = fraction.dropped;
  return clock;
}

// "h:m[:s[.f...]]" (hour, minute and second one or two digits).
std::expected<Clock, ParseError> scan_clock(Scanner& in) {
  using namespace std::chrono;
  const auto h = in.number(1, 2, 0, 23);
  if (not h) {
    return std::unexpected{h.error()};
  }
  if (const auto colon = in.expect(':', ParseErrc::bad_date); not colon) {
    return std::unexpected{colon.error()};
  }
  const auto m = in.number(1, 2, 0, 59);
  if (not m) {
    return std::unexpected{m.error()};
  }
  Clock clock{.time = hours{*h} + minutes{*m}, .dropped = false};
  if (not in.accept(':')) {
    return clock;
  }
  const auto seconds_part = scan_seconds(in);
  if (not seconds_part) {
    return std::unexpected{seconds_part.error()};
  }
  clock.time += seconds_part->time;
  clock.dropped = seconds_part->dropped;
  return clock;
}

// The date, and the clock after a 'T' or a single space before a digit.
std::expected<Clock, ParseError> scan_date_and_clock(
    Scanner& in, std::chrono::sys_days& date) {
  const auto parsed_date = scan_date(in);
  if (not parsed_date) {
    return std::unexpected{parsed_date.error()};
  }
  date = *parsed_date;
  const std::string_view after = in.rest();
  const bool has_clock =
      in.peek_is('T') or (in.peek_is(' ') and after.size() > 1 and
                          core::ascii::is_digit(after[1]));
  if (not has_clock) {
    return Clock{.time = std::chrono::milliseconds{0}, .dropped = false};
  }
  in.advance();
  return scan_clock(in);
}

// Exactly two digits, as a number no greater than `max`.
constexpr std::optional<int> two_digits(std::string_view text,
                                        int max) noexcept {
  if (text.size() != 2 or digit_run(text) != 2) {
    return std::nullopt;
  }
  const int value = decode_decimal(text);
  return value <= max ? std::optional{value} : std::nullopt;
}

// "+hh", "+hh:mm" or "+hhmm" (or "-"); the scanner is on the sign. Errors are
// reported at the sign.
std::expected<std::chrono::minutes, ParseError> scan_offset(Scanner& in) {
  constexpr int max_hours = 23;
  constexpr int max_minutes = 59;
  const Scanner::Word offset = in.word();
  const int sign = offset.text.starts_with('-') ? -1 : 1;
  std::string_view digits = offset.text;  // after the sign
  digits.remove_prefix(std::min<std::size_t>(1, digits.size()));
  const auto hours = two_digits(digits.substr(0, 2), max_hours);
  std::string_view rest =
      digits.substr(std::min<std::size_t>(2, digits.size()));
  const bool colon = rest.starts_with(':');
  rest.remove_prefix(colon ? 1 : 0);
  // "hh" alone has no minutes; "hh:" lacks them.
  const auto minutes = rest.empty() and not colon
                           ? std::optional{0}
                           : two_digits(rest, max_minutes);
  if (not hours or not minutes) {
    return std::unexpected{in.error(ParseErrc::bad_time_units, offset.column)};
  }
  return std::chrono::minutes{sign * ((*hours * 60) + *minutes)};
}

// The optional zone after the reference time; UTC (zero) when there is none.
std::expected<std::chrono::minutes, ParseError> scan_zone(Scanner& in) {
  in.skip_space();
  if (in.at_end()) {
    return std::chrono::minutes{0};
  }
  if (in.peek_is('+') or in.peek_is('-')) {
    return scan_offset(in);
  }
  if (in.accept('Z') or in.accept('z')) {
    return std::chrono::minutes{0};
  }
  if (equal_ignore_case(in.rest().substr(0, 3), "UTC")) {
    in.advance(3);
    return std::chrono::minutes{0};
  }
  return std::unexpected{in.error_here(ParseErrc::bad_time_units)};
}

}  // namespace

std::expected<Read<CfTimeUnits>, ParseError> parse_cf_time_units(
    std::string_view text) {
  Scanner in{text};
  const auto unit = scan_unit_and_since(in);
  if (not unit) {
    return std::unexpected{unit.error()};
  }
  std::chrono::sys_days date{};
  const auto clock = scan_date_and_clock(in, date);
  if (not clock) {
    return std::unexpected{clock.error()};
  }
  const auto zone = scan_zone(in);
  if (not zone) {
    return std::unexpected{zone.error()};
  }
  in.skip_space();
  if (not in.at_end()) {
    return std::unexpected{in.error_here(ParseErrc::trailing_text)};
  }
  const core::Time epoch =
      std::chrono::time_point_cast<std::chrono::milliseconds>(date) +
      clock->time - *zone;
  std::vector<Warning> warnings;
  if (clock->dropped) {
    warnings.push_back(
        Warning{.code = WarningCode::time_precision_dropped,
                .subject = std::string{detail::truncate_utf8(
                    core::ascii::trim(text), ParseError::max_context_bytes)}});
  }
  return Read<CfTimeUnits>{.value = CfTimeUnits{.unit = *unit, .epoch = epoch},
                           .warnings = std::move(warnings)};
}

}  // namespace mov::io
