// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/detail/json_write.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>

#include "mov/core/time.hpp"
#include "mov/io/detail/civil_time.hpp"
#include "mov/io/detail/text.hpp"

namespace mov::io::detail::json_text {

namespace {

constexpr std::int64_t first_day = days_from_civil(0, 1, 1);
constexpr std::int64_t end_day = days_from_civil(10000, 1, 1);

std::int64_t day_number(std::chrono::sys_days day) noexcept {
  return day.time_since_epoch().count();
}

// A year as at least four digits, with a minus sign before a negative one.
void append_year(std::string& out, std::int64_t year) {
  std::format_to(std::back_inserter(out), "{}{:04}", year < 0 ? "-" : "",
                 year < 0 ? -year : year);
}

// The fixed text of `value` with `decimals` digits, zero without a sign.
std::string fixed_text(double value, int decimals) {
  std::string text = std::format("{:.{}f}", value, decimals);
  if (text.starts_with('-') and
      text.find_first_not_of("0.", 1) == std::string::npos) {
    text.erase(0, 1);  // "-0.000000": the value rounded to zero
  }
  return text;
}

}  // namespace

void append_string(std::string& out, std::string_view text) {
  out.push_back('"');
  for (const char c : text) {
    switch (c) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default: {
        const auto byte = static_cast<unsigned char>(c);
        if (byte < 0x20 or byte == 0x7F) {
          std::format_to(std::back_inserter(out), "\\u{:04x}",
                         static_cast<unsigned>(byte));
        } else {
          out.push_back(c);
        }
      }
    }
  }
  out.push_back('"');
}

double rounded(double value, int decimals) {
  // The text parses: it is what std::format printed for a finite double.
  return parse_double(fixed_text(value, decimals)).value_or(0.0);
}

void append_fixed(std::string& out, double value, int decimals) {
  std::string text = fixed_text(value, decimals);
  if (text.find('.') != std::string::npos) {
    text.erase(text.find_last_not_of('0') + 1);
    if (text.ends_with('.')) {
      text.pop_back();
    }
  }
  out += text;
}

void append_shortest(std::string& out, double value) {
  std::format_to(std::back_inserter(out), "{}", value);
}

bool four_digit_year(std::chrono::sys_days day) noexcept {
  const std::int64_t n = day_number(day);
  return n >= first_day and n < end_day;
}

bool four_digit_year(core::Time time) noexcept {
  return time >= imeds_first and time < imeds_end;
}

void append_date(std::string& out, std::chrono::sys_days day) {
  const CivilDate d = civil_from_days(day_number(day));
  out.push_back('"');
  append_year(out, d.year);
  std::format_to(std::back_inserter(out), "-{:02}-{:02}\"", d.month, d.day);
}

void append_timestamp(std::string& out, core::Time time) {
  const CivilTime t = civil_fields(time);
  out.push_back('"');
  append_year(out, t.year);
  std::format_to(std::back_inserter(out), "-{:02}-{:02}T{:02}:{:02}:{:02}Z\"",
                 t.month, t.day, t.hour, t.minute, t.second);
}

void append_timestamp_ms(std::string& out, core::Time time) {
  const CivilTime t = civil_fields(time);
  out.push_back('"');
  append_year(out, t.year);
  std::format_to(std::back_inserter(out),
                 "-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z\"", t.month, t.day,
                 t.hour, t.minute, t.second, t.millisecond);
}

// core::parse_utc_datetime is strict (four-digit year, two-digit fields, a
// real date and time of day, no offset); the length and the separators pin
// the one form each of these takes.

std::optional<std::chrono::sys_days> parse_date(
    std::string_view text) noexcept {
  constexpr std::size_t date_length = 10;  // YYYY-MM-DD
  if (text.size() != date_length) {
    return std::nullopt;
  }
  const auto t = core::parse_utc_datetime(text);
  if (not t) {
    return std::nullopt;
  }
  return std::chrono::floor<std::chrono::days>(*t);
}

std::optional<core::Time> parse_timestamp(std::string_view text) noexcept {
  constexpr std::size_t timestamp_length = 20;  // YYYY-MM-DDThh:mm:ssZ
  if (text.size() != timestamp_length or text[10] != 'T' or
      text.back() != 'Z') {
    return std::nullopt;
  }
  const auto t = core::parse_utc_datetime(text);
  if (not t) {
    return std::nullopt;
  }
  return *t;
}

}  // namespace mov::io::detail::json_text
