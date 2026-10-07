// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Text helpers for the readers. Private to mov::io (the header is only public
// because the tests and fuzz targets include it). Everything is ASCII-only
// and independent of the C and C++ locales.

#pragma once

#include <algorithm>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

// The result of a function so marked holds views of the argument, so the
// argument must outlive it. Clang and MSVC diagnose a temporary passed here;
// elsewhere it documents the contract. The std::string&& overloads below turn
// the commonest mistake into an error on every compiler.
#if defined(__clang__)
#define MOV_LIFETIMEBOUND \
  [[clang::lifetimebound]]  // NOLINT(cppcoreguidelines-macro-usage)
#elif defined(_MSC_VER) && defined(__has_cpp_attribute)
#if __has_cpp_attribute(msvc::lifetimebound)
#define MOV_LIFETIMEBOUND \
  [[msvc::lifetimebound]]  // NOLINT(cppcoreguidelines-macro-usage)
#endif
#endif
#ifndef MOV_LIFETIMEBOUND
#define MOV_LIFETIMEBOUND  // NOLINT(cppcoreguidelines-macro-usage)
#endif

namespace mov::io::detail {

/// A std::string passed by value-category rvalue: its views would dangle.
template <class S>
concept TemporaryString = std::same_as<std::remove_cvref_t<S>, std::string> and
                          not std::is_lvalue_reference_v<S>;

// ---- splitting -----------------------------------------------------------

/// The maximal runs of non-whitespace in `text`, as views of it. Whitespace
/// is ASCII space, tab, CR, LF, VT and FF. No empty tokens.
[[nodiscard]] std::vector<std::string_view> split_ws(
    std::string_view text MOV_LIFETIMEBOUND);
template <TemporaryString S>
std::vector<std::string_view> split_ws(S&&) = delete;

/// The fields of `text` between `delimiter`s, as views of it: one more field
/// than there are delimiters, so empty fields stay ("a,,b" has three, and ""
/// has one).
[[nodiscard]] std::vector<std::string_view> split_on(
    std::string_view text MOV_LIFETIMEBOUND, char delimiter);
template <TemporaryString S>
std::vector<std::string_view> split_on(S&&, char) = delete;

// ---- strings -------------------------------------------------------------

/// `text` with leading and trailing whitespace removed and every inner run of
/// whitespace replaced by one space (Qt's QString::simplified).
[[nodiscard]] std::string simplified(std::string_view text);

/// ASCII case folding; bytes outside A-Z (UTF-8 included) are unchanged.
[[nodiscard]] std::string to_lower_ascii(std::string_view text);
[[nodiscard]] std::string to_upper_ascii(std::string_view text);

/// The bytes before the first NUL, or all of `text` if it has none. Legacy
/// fixed-width name fields are NUL-padded and may have junk after the NUL.
[[nodiscard]] std::string_view cut_at_nul(
    std::string_view text MOV_LIFETIMEBOUND) noexcept;

/// Reserves min(wanted, cap) elements. `wanted` comes from a file header and
/// `cap` from what the input could possibly contain (its size divided by the
/// smallest row), so a hostile count cannot demand memory.
template <class Vector>
void reserve_capped(Vector& v, std::size_t wanted, std::size_t cap) {
  v.reserve(std::min(wanted, cap));
}

// ---- numbers -------------------------------------------------------------

enum class NumberError : std::uint8_t {
  empty,
  bad_syntax,    // not exactly one number in the token
  out_of_range,  // does not fit the type (a double: overflows or underflows to
                 // zero)
};

/// A whole token as a double, in the C locale whatever the global locale is.
///
///   [+-]? ( digits [ . digits? ] | . digits ) ( [eE] [+-]? digits )?
///
/// Rejected: the empty token, surrounding or inner whitespace, `1,5`, hex
/// floats (`0x1p3`), `nan`, `inf` and `infinity` in any case, `.`, `1e`,
/// `+-1`, `1.5x`. A magnitude too large for a double, or a nonzero value
/// that underflows to zero, is `out_of_range`; denormals are accepted. The
/// result is the correctly rounded double, so the two implementations below
/// agree bit for bit.
[[nodiscard]] std::expected<double, NumberError> parse_double(
    std::string_view token);

/// The two implementations behind parse_double, exposed so tests and the
/// differential fuzzer can compare them. strtod_l with a "C" locale object
/// exists on every platform; std::from_chars only where the standard library
/// has the floating-point overloads (`has_floating_from_chars`).
[[nodiscard]] std::expected<double, NumberError> parse_double_strtod(
    std::string_view token);

#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
inline constexpr bool has_floating_from_chars = true;
[[nodiscard]] std::expected<double, NumberError> parse_double_from_chars(
    std::string_view token);
#else
inline constexpr bool has_floating_from_chars = false;
#endif

/// A whole token as an integer: `[+-]? digits`, nothing else (no whitespace,
/// no `0x`). A value that does not fit `I`, and a minus sign on an unsigned
/// type, give `out_of_range`.
template <std::integral I>
  requires(not std::same_as<I, bool>)
[[nodiscard]] std::expected<I, NumberError> parse_int(
    std::string_view token) noexcept {
  if (token.empty()) {
    return std::unexpected{NumberError::empty};
  }
  std::string_view digits = token;
  if (digits.front() == '+') {
    digits.remove_prefix(1);
  }
  const std::string_view body =
      (not digits.empty() and digits.front() == '-' and token.front() != '+')
          ? digits.substr(1)
          : digits;
  if (body.empty() or not std::ranges::all_of(body, [](char c) noexcept {
        return c >= '0' and c <= '9';
      })) {
    return std::unexpected{NumberError::bad_syntax};
  }
  I value{};
  const auto parsed =
      std::from_chars(digits.data(), digits.data() + digits.size(), value);
  if (parsed.ec != std::errc{}) {
    return std::unexpected{NumberError::out_of_range};
  }
  return value;
}

}  // namespace mov::io::detail
