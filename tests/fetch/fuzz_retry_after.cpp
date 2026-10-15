// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: the bytes are a Retry-After value, then optionally a
// newline and a Date header. Invariants:
//  - retry_after is nullopt or at least 1 s;
//  - an HTTP-date that parses (any of the three forms) formats as an
//    IMF-fixdate that parses back to the same instant;
//  - an RFC 850 date parsed against a reference year lands within
//    (reference - 50, reference + 50].

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "mov/fetch/retry.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

[[noreturn]] void fail() { std::abort(); }

void check_round_trip(std::string_view text) {
  const auto t = mov::fetch::parse_http_date(text);
  if (not t) {
    return;
  }
  const auto formatted = mov::fetch::format_imf_fixdate(*t);
  if (formatted and mov::fetch::parse_http_date(*formatted) != t) {
    fail();
  }
}

void check_reference(std::string_view text) {
  constexpr int reference = 2026;
  constexpr int half_century = 50;
  if (text.find('-') == std::string_view::npos) {
    return;  // only RFC 850 has two-digit years
  }
  const auto t =
      mov::fetch::parse_http_date(text, std::chrono::year{reference});
  if (not t or
      mov::fetch::parse_http_date(text, std::nullopt) == std::nullopt) {
    return;
  }
  // Only an RFC 850 date has a '-' (and its two-digit year).
  const int year = static_cast<int>(
      std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(*t)}
          .year());
  if (year <= reference - half_century or year > reference + half_century) {
    fail();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  constexpr std::size_t max_text = 256;
  if (size > max_text) {
    return -1;
  }
  const std::string text(data, data + size);
  const std::string_view all{text};
  const std::size_t newline = all.find('\n');
  const std::string_view value = all.substr(0, newline);
  const std::optional<std::string_view> date =
      newline == std::string_view::npos
          ? std::nullopt
          : std::optional{all.substr(newline + 1)};
  const auto wait = mov::fetch::retry_after(value, date);
  if (wait and *wait < std::chrono::seconds{1}) {
    fail();
  }
  check_round_trip(value);
  check_reference(value);
  if (date) {
    check_round_trip(*date);
  }
  return 0;
}
