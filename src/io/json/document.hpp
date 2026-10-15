// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Reading provider JSON with nlohmann/json, private to mov_io (the only place
// nlohmann is included: the third_party_include_gate test). The pattern is
// the parser callback of docs/harmonics-engine.md section 6.2: the elements
// of the one large array of a body are handed over one at a time as they are
// parsed and then dropped, so the tree never holds more than one of them.
//
// mov_io compiles with JSON_USE_IMPLICIT_CONVERSIONS=0 (a value is read only
// through is_* and get_ptr, which do not throw) and with
// JSON_DIAGNOSTIC_POSITIONS=1, so every value knows the byte offset where it
// starts (start_pos()), which the errors report as a line and column.

#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io::json {

using Json = nlohmann::json;

/// A body and the errors located in it.
class Body {
 public:
  explicit Body(std::string_view text) noexcept : text_{text} {}

  [[nodiscard]] std::string_view text() const noexcept { return text_; }

  /// A ParseError of `code` at the start of `value` (a value of this body):
  /// its line and column, and the text from there as context.
  [[nodiscard]] ParseError error_at(ParseErrc code, const Json& value) const;

  /// The same at a byte offset.
  [[nodiscard]] ParseError error_at(ParseErrc code, std::size_t offset) const;

  /// The line and column where `value` starts, for an error whose context
  /// is not the text (a limit).
  [[nodiscard]] ParseError::Where where(const Json& value) const noexcept;

 private:
  std::string_view text_;
};

/// What parse_streaming hands over: the index of an element of the streamed
/// array (0-based) and its value. Returns an error to stop the parse.
using OnElement =
    std::function<std::expected<void, Error>(std::size_t index, Json& value)>;

/// Parses `body`, a JSON object, handing each element of its top-level
/// member `array_key` (when that is an array) to `on_element` and dropping
/// it; elements of the other top-level arrays are dropped unread. Returns the
/// rest of the document: the streamed array is left empty.
///
/// Before parsing: an empty or blank body is `empty_input`, one over
/// ctx.limits.max_text_bytes or nested deeper than wire::max_json_depth
/// `too_large`, one with a NUL byte `not_json` there (nlohmann would take it
/// for the end of the input). While parsing: a syntax error is `not_json`
/// (where the parser stopped), a number too large for a double
/// `out_of_range`, a member that an object repeats `bad_member` (at that
/// object), and every 4096 elements ctx.stop is polled (Cancelled). A
/// document that is not an object is `bad_member`. The first error of
/// `on_element` stops the parse.
[[nodiscard]] std::expected<Json, Error> parse_streaming(
    const Body& body, std::string_view array_key, const ReadContext& ctx,
    const OnElement& on_element);

// ---- members
// ------------------------------------------------------------------

/// The member `name` of `object` (an object): nullopt when it is absent.
[[nodiscard]] const Json* find_member(const Json& object,
                                      std::string_view name);

/// A string member: `bad_member` at `object` when it is absent, at the value
/// when it is not a string.
[[nodiscard]] std::expected<std::string_view, ParseError> string_member(
    const Body& body, const Json& object, std::string_view name);

/// A string member that may be absent or null (nullopt); `bad_member` at the
/// value when it is anything else.
[[nodiscard]] std::expected<std::optional<std::string_view>, ParseError>
optional_string_member(const Body& body, const Json& object,
                       std::string_view name);

/// The string value of `value`, or nullptr when it is not a string.
[[nodiscard]] const std::string* as_string(const Json& value) noexcept;

/// The number `value` (integer or floating point) as a double; nullopt when
/// it is not a number.
[[nodiscard]] std::optional<double> as_double(const Json& value) noexcept;

}  // namespace mov::io::json
