// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Writing JSON text with std::format: strings, numbers, dates and times as
// the station catalog and the series JSON print them. Private to mov::io:
// public only because tests include it. Locale-independent.

#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "mov/core/time.hpp"

namespace mov::io::detail::json_text {

/// Appends `text` (valid UTF-8, which the caller guarantees) as a JSON string:
/// quoted, `"` and `\` escaped, control characters and DEL as \b, \f, \n, \r,
/// \t or \u00xx (lower-case hex); everything else, non-ASCII included, as it
/// is.
void append_string(std::string& out, std::string_view text);

/// `value` rounded to `decimals` digits after the point, as the text
/// append_fixed prints and a reader parses it: the double nearest that
/// decimal. Zero is +0. Rounding is std::format's (to nearest, ties to even
/// on the exact binary value).
[[nodiscard]] double rounded(double value, int decimals);

/// Appends rounded(value, decimals) in fixed notation without trailing
/// zeros or a trailing point ("-87.2112", "30", "0"). `value` is finite.
void append_fixed(std::string& out, double value, int decimals);

/// Appends `value` in the shortest form that reads back as the same double
/// (std::format's "{}"), which JSON accepts ("1e+300", "-0"). `value` is
/// finite.
void append_shortest(std::string& out, double value);

/// Whether the day is in the years 0000-9999 (what four-digit dates hold).
[[nodiscard]] bool four_digit_year(std::chrono::sys_days day) noexcept;

/// Whether the time is in the years 0000-9999.
[[nodiscard]] bool four_digit_year(core::Time time) noexcept;

/// Appends "YYYY-MM-DD" (quoted). A year outside 0000-9999 is printed with
/// as many digits as it needs, a negative one with a minus sign (as the CSV
/// export does); a format that holds four-digit years only checks
/// four_digit_year first.
void append_date(std::string& out, std::chrono::sys_days day);

/// Appends "YYYY-MM-DDThh:mm:ssZ" (quoted), the time floored to the second;
/// years as append_date.
void append_timestamp(std::string& out, core::Time time);

/// Appends "YYYY-MM-DDThh:mm:ss.sssZ" (quoted); years as append_date.
void append_timestamp_ms(std::string& out, core::Time time);

/// Parses exactly "YYYY-MM-DD" (a real date, years 0000-9999).
[[nodiscard]] std::optional<std::chrono::sys_days> parse_date(
    std::string_view text) noexcept;

/// Parses exactly "YYYY-MM-DDThh:mm:ssZ" (a real date and time of day,
/// years 0000-9999, no leap second).
[[nodiscard]] std::optional<core::Time> parse_timestamp(
    std::string_view text) noexcept;

}  // namespace mov::io::detail::json_text
