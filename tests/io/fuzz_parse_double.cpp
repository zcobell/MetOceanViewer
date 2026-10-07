// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: parse_double must never crash, only ever return a finite
// double for a token of the documented grammar, and both of its
// implementations (std::from_chars and strtod_l with a "C" locale object)
// must agree on the verdict and on every bit of the value.

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <string>
#include <string_view>

#include "mov/core/detail/numeric.hpp"
#include "mov/io/detail/text.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace detail = mov::io::detail;

[[noreturn]] void fail() { std::abort(); }

bool same_verdict(const std::expected<double, detail::NumberError>& a,
                  const std::expected<double, detail::NumberError>& b) {
  if (a.has_value() != b.has_value()) {
    return false;
  }
  if (not a) {
    return a.error() == b.error();
  }
  return std::bit_cast<std::uint64_t>(*a) == std::bit_cast<std::uint64_t>(*b);
}

bool only_number_characters(std::string_view token) {
  return std::ranges::all_of(token, [](char c) {
    return (c >= '0' and c <= '9') or c == '+' or c == '-' or c == '.' or
           c == 'e' or c == 'E';
  });
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string token(data, data + size);
  const auto result = detail::parse_double(token);
  const auto via_strtod = detail::parse_double_strtod(token);
  if (not same_verdict(result, via_strtod)) {
    fail();
  }
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
  if (not same_verdict(result, detail::parse_double_from_chars(token))) {
    fail();
  }
#endif
  if (result) {
    if (not mov::core::detail::is_finite(*result) or
        not only_number_characters(token)) {
      fail();
    }
  }
  // An integer token is a double token too, with the same value when it fits.
  if (const auto integer = detail::parse_int<std::int64_t>(token); integer) {
    if (not result) {
      fail();
    }
  }
  return 0;
}
