// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: parse_double must never crash, and
//  - its two implementations (std::from_chars and strtod_l with a "C" locale
//    object) agree on the verdict and on every bit of the value;
//  - it returns a finite double, and only for a token of the documented grammar
//    (checked against a std::regex of that grammar for tokens up to 128
//    bytes), and a token of the grammar is never `bad_syntax`;
//  - an integer token of up to 15 digits has exactly the integer's value;
//  - the shortest decimal text of the result parses back to the same bits.

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <regex>
#include <string>
#include <string_view>
#include <system_error>

#include "mov/core/detail/numeric.hpp"
#include "mov/io/detail/text.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace detail = mov::io::detail;

[[noreturn]] void fail() { std::abort(); }

using Result = std::expected<double, detail::NumberError>;

bool same_verdict(const Result& a, const Result& b) {
  if (a.has_value() != b.has_value()) {
    return false;
  }
  if (not a) {
    return a.error() == b.error();
  }
  return std::bit_cast<std::uint64_t>(*a) == std::bit_cast<std::uint64_t>(*b);
}

// The grammar of parse_double.
bool matches_grammar(const std::string& token) {
  static const std::regex grammar{
      R"(^[+-]?([0-9]+(\.[0-9]*)?|\.[0-9]+)([eE][+-]?[0-9]+)?$)"};
  return std::regex_match(token, grammar);
}

bool is_small_integer_token(std::string_view token) {
  constexpr std::size_t max_digits = 15;
  std::string_view digits = token;
  static_cast<void>(detail::strip_sign(digits));
  return not digits.empty() and digits.size() <= max_digits and
         std::ranges::all_of(digits,
                             [](char c) { return c >= '0' and c <= '9'; });
}

void check_grammar(const std::string& token, const Result& result) {
  constexpr std::size_t regex_limit = 128;
  if (token.size() > regex_limit) {
    return;
  }
  const bool in_grammar = matches_grammar(token);
  if (result) {
    if (not in_grammar) {
      fail();
    }
    return;
  }
  const bool syntax_error = result.error() == detail::NumberError::bad_syntax or
                            result.error() == detail::NumberError::empty;
  if (in_grammar == syntax_error) {
    fail();  // in the grammar but a syntax error, or the reverse
  }
}

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
void check_shortest_round_trip(double value) {
  std::array<char, 64> buffer{};
  const auto written =
      std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
  if (written.ec != std::errc{}) {
    return;
  }
  const std::string_view text{buffer.data(), written.ptr};
  const Result again = detail::parse_double(text);
  if (not again or std::bit_cast<std::uint64_t>(*again) !=
                       std::bit_cast<std::uint64_t>(value)) {
    fail();
  }
}
#endif

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string token(data, data + size);
  const Result result = detail::parse_double(token);
  if (not same_verdict(result, detail::parse_double_strtod(token))) {
    fail();
  }
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
  if (not same_verdict(result, detail::parse_double_from_chars(token))) {
    fail();
  }
#endif
  check_grammar(token, result);
  if (result) {
    if (not mov::core::detail::is_finite(*result)) {
      fail();
    }
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    check_shortest_round_trip(*result);
#endif
  }
  if (is_small_integer_token(token)) {
    const auto integer = detail::parse_int<std::int64_t>(token);
    if (not integer or not result or *result != static_cast<double>(*integer)) {
      fail();
    }
  }
  return 0;
}
