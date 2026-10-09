// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Text helpers for the readers. Private to mov::io (the header is only public
// because the tests and fuzz targets include it). Everything is ASCII-only
// and independent of the C and C++ locales.

#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <type_traits>
#include <vector>

#include "mov/core/ascii.hpp"

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

/// `text` without its leading whitespace (ASCII space, tab, CR, LF, VT, FF).
[[nodiscard]] constexpr std::string_view skip_space(
    std::string_view text) noexcept {
  const auto first = std::ranges::find_if_not(text, core::ascii::is_space);
  return text.substr(static_cast<std::size_t>(first - text.begin()));
}

/// True when `text` has no character but white space (the empty text too).
[[nodiscard]] constexpr bool is_blank(std::string_view text) noexcept {
  return skip_space(text).empty();
}

/// The primitive behind every whitespace-separated reader: skips the
/// whitespace at the start of `rest`, returns the run of non-whitespace after
/// it as a view, and moves `rest` past that run. nullopt when only whitespace
/// remains.
/// The same for any separator: the words of "a,b;c" with `is_separator` true
/// for ',' and ';'. Runs of separators count as one, so no empty word comes
/// back (use split_on_into to keep empty fields).
template <std::predicate<char> IsSeparator>
[[nodiscard]] constexpr std::optional<std::string_view> next_word(
    std::string_view& rest, IsSeparator is_separator) noexcept {
  const auto first = std::ranges::find_if_not(rest, is_separator);
  rest.remove_prefix(static_cast<std::size_t>(first - rest.begin()));
  if (rest.empty()) {
    return std::nullopt;
  }
  const auto last = std::ranges::find_if(rest, is_separator);
  const auto length = static_cast<std::size_t>(last - rest.begin());
  const std::string_view word = rest.substr(0, length);
  rest.remove_prefix(length);
  return word;
}

[[nodiscard]] constexpr std::optional<std::string_view> next_word(
    std::string_view& rest) noexcept {
  return next_word(rest, core::ascii::is_space);
}

/// The non-allocating comma-style counterpart of split_ws_into: puts the first
/// `out.size()` fields of `text` (between `delimiter`s, empty fields kept:
/// "a,,b" has three and "" has one) in `out` and returns how many fields there
/// are, counting no further than `out.size() + 1`.
[[nodiscard]] constexpr std::size_t split_on_into(
    std::string_view text, char delimiter,
    std::span<std::string_view> out) noexcept {
  std::size_t count = 0;
  while (count <= out.size()) {
    const std::size_t end = text.find(delimiter);
    if (count < out.size()) {
      out[count] = text.substr(0, end);
    }
    ++count;
    if (end == std::string_view::npos) {
      break;
    }
    text.remove_prefix(end + 1);
  }
  return count;
}

/// The maximal runs of non-whitespace in `text`, as views of it. No empty
/// tokens.
[[nodiscard]] std::vector<std::string_view> split_ws(
    std::string_view text MOV_LIFETIMEBOUND);
template <TemporaryString S>
std::vector<std::string_view> split_ws(S&&) = delete;

/// The non-allocating form for a reader's hot path: puts the first
/// `out.size()` words of `line` in `out` and returns how many words there are,
/// counting no further than `out.size() + 1`. A result above `out.size()`
/// means the line has more words than the caller has room for.
[[nodiscard]] constexpr std::size_t split_ws_into(
    std::string_view line, std::span<std::string_view> out) noexcept {
  std::size_t count = 0;
  while (count <= out.size()) {
    const auto word = next_word(line);
    if (not word) {
      break;
    }
    if (count < out.size()) {
      out[count] = *word;
    }
    ++count;
  }
  return count;
}

/// Exactly `N` words, or nullopt for fewer or more: a fixed-column row.
template <std::size_t N>
[[nodiscard]] constexpr std::optional<std::array<std::string_view, N>> split_ws(
    std::string_view line MOV_LIFETIMEBOUND) noexcept {
  std::array<std::string_view, N> words{};
  if (split_ws_into(line, words) != N) {
    return std::nullopt;
  }
  return words;
}
template <std::size_t N, TemporaryString S>
std::optional<std::array<std::string_view, N>> split_ws(S&&) = delete;

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

enum class Sign : std::uint8_t { none, plus, minus };

/// Removes one leading '+' or '-' from `text` and says which it was.
[[nodiscard]] constexpr Sign strip_sign(std::string_view& text) noexcept {
  if (text.empty() or (text.front() != '+' and text.front() != '-')) {
    return Sign::none;
  }
  const Sign sign = text.front() == '+' ? Sign::plus : Sign::minus;
  text.remove_prefix(1);
  return sign;
}

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
  std::string_view body = token;
  const Sign sign = strip_sign(body);
  if (body.empty() or not std::ranges::all_of(body, core::ascii::is_digit)) {
    return std::unexpected{NumberError::bad_syntax};
  }
  // std::from_chars takes a '-' but not a '+'.
  const std::string_view parsed_text = sign == Sign::minus ? token : body;
  I value{};
  const auto parsed = std::from_chars(
      parsed_text.data(), parsed_text.data() + parsed_text.size(), value);
  if (parsed.ec != std::errc{}) {
    return std::unexpected{NumberError::out_of_range};
  }
  return value;
}

}  // namespace mov::io::detail
