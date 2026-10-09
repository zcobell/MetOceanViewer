// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/detail/text.hpp"

#include <algorithm>
#include <charconv>
#include <clocale>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "mov/core/ascii.hpp"

#if defined(__APPLE__)
#include <xlocale.h>  // strtod_l, newlocale, freelocale
#endif

namespace mov::io::detail {

// ---- splitting and strings -----------------------------------------------

std::vector<std::string_view> split_ws(std::string_view text) {
  std::vector<std::string_view> tokens;
  while (const auto word = next_word(text)) {
    tokens.push_back(*word);
  }
  return tokens;
}

std::vector<std::string_view> split_on(std::string_view text, char delimiter) {
  std::vector<std::string_view> fields;
  std::size_t start = 0;
  for (std::size_t at = text.find(delimiter); at != std::string_view::npos;
       at = text.find(delimiter, start)) {
    fields.push_back(text.substr(start, at - start));
    start = at + 1;
  }
  fields.push_back(text.substr(start));
  return fields;
}

std::string simplified(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (const std::string_view token : split_ws(text)) {
    if (not out.empty()) {
      out += ' ';
    }
    out += token;
  }
  return out;
}

std::string to_lower_ascii(std::string_view text) {
  std::string out{text};
  std::ranges::transform(out, out.begin(), core::ascii::to_lower);
  return out;
}

std::string to_upper_ascii(std::string_view text) {
  std::string out{text};
  std::ranges::transform(out, out.begin(), core::ascii::to_upper);
  return out;
}

std::string_view cut_at_nul(std::string_view text) noexcept {
  return text.substr(0, text.find('\0'));
}

// ---- numbers -------------------------------------------------------------

namespace {

// Length of the run of digits at the start of `s`.
std::size_t digit_run(std::string_view s) noexcept {
  const auto end = std::ranges::find_if_not(s, core::ascii::is_digit);
  return static_cast<std::size_t>(end - s.begin());
}

// A syntactically valid decimal token: `text` is the token without a leading
// '+' (which std::from_chars rejects), and `nonzero` says whether any mantissa
// digit is not '0', so a result of zero can be told from an underflow.
struct Decimal {
  std::string_view text;
  bool nonzero;
};

// Consumes "[eE][+-]?digits" if `rest` starts with an exponent marker; false
// when the marker has no digits after it.
bool consume_exponent(std::string_view& rest) noexcept {
  if (rest.empty() or (rest.front() != 'e' and rest.front() != 'E')) {
    return true;
  }
  rest.remove_prefix(1);
  if (not rest.empty() and (rest.front() == '+' or rest.front() == '-')) {
    rest.remove_prefix(1);
  }
  const std::size_t digits = digit_run(rest);
  rest.remove_prefix(digits);
  return digits > 0;
}

bool has_nonzero_digit(std::string_view digits) noexcept {
  return std::ranges::any_of(
      digits, [](char c) noexcept { return c >= '1' and c <= '9'; });
}

std::expected<Decimal, NumberError> scan_decimal(std::string_view token) {
  if (token.empty()) {
    return std::unexpected{NumberError::empty};
  }
  std::string_view rest = token;
  const Sign sign = strip_sign(rest);
  const std::size_t integer_digits = digit_run(rest);
  const std::string_view integer_part = rest.substr(0, integer_digits);
  rest.remove_prefix(integer_digits);
  std::string_view fraction_part;
  if (not rest.empty() and rest.front() == '.') {
    rest.remove_prefix(1);
    const std::size_t fraction_digits = digit_run(rest);
    fraction_part = rest.substr(0, fraction_digits);
    rest.remove_prefix(fraction_digits);
  }
  if ((integer_part.empty() and fraction_part.empty()) or
      not consume_exponent(rest) or not rest.empty()) {
    return std::unexpected{NumberError::bad_syntax};
  }
  return Decimal{.text = sign == Sign::plus ? token.substr(1) : token,
                 .nonzero = has_nonzero_digit(integer_part) or
                            has_nonzero_digit(fraction_part)};
}

// The shared verdict on a converted value: a nonzero decimal that became zero
// has underflowed.
std::expected<double, NumberError> checked(double value, const Decimal& d) {
  if (not std::isfinite(value) or (value == 0.0 and d.nonzero)) {
    return std::unexpected{NumberError::out_of_range};
  }
  return value;
}

// An owned "C" locale object: strtod_l with it ignores the global locale.
class CLocale {
 public:
  CLocale() : handle_{create()} {
    if (handle_ == nullptr) {
      throw std::bad_alloc{};
    }
  }
  CLocale(const CLocale&) = delete;
  CLocale& operator=(const CLocale&) = delete;
  CLocale(CLocale&&) = delete;
  CLocale& operator=(CLocale&&) = delete;
  ~CLocale() {
#if defined(_WIN32)
    _free_locale(handle_);
#else
    freelocale(handle_);
#endif
  }

  double parse(const char* text, char** end) const {
#if defined(_WIN32)
    return _strtod_l(text, end, handle_);
#else
    return strtod_l(text, end, handle_);
#endif
  }

 private:
#if defined(_WIN32)
  using Handle = _locale_t;
  static Handle create() { return _create_locale(LC_ALL, "C"); }
#else
  using Handle = locale_t;
  static Handle create() { return newlocale(LC_ALL_MASK, "C", nullptr); }
#endif

  Handle handle_;
};

}  // namespace

std::expected<double, NumberError> parse_double_strtod(std::string_view token) {
  const auto decimal = scan_decimal(token);
  if (not decimal) {
    return std::unexpected{decimal.error()};
  }
  static const CLocale c_locale;
  const std::string text{decimal->text};  // strtod needs the terminating NUL
  char* end = nullptr;
  const double value = c_locale.parse(text.c_str(), &end);
  // The scan above admits only text that strtod consumes entirely.
  return end == text.c_str() + text.size()
             ? checked(value, *decimal)
             : std::unexpected{NumberError::bad_syntax};
}

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
std::expected<double, NumberError> parse_double_from_chars(
    std::string_view token) {
  const auto decimal = scan_decimal(token);
  if (not decimal) {
    return std::unexpected{decimal.error()};
  }
  double value = 0.0;
  const char* const first = decimal->text.data();
  const char* const last = first + decimal->text.size();
  const auto result = std::from_chars(first, last, value);
  if (result.ec != std::errc{} or result.ptr != last) {
    return std::unexpected{NumberError::out_of_range};
  }
  return checked(value, *decimal);
}
#endif

std::expected<double, NumberError> parse_double(std::string_view token) {
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
  return parse_double_from_chars(token);
#else
  return parse_double_strtod(token);
#endif
}

}  // namespace mov::io::detail
