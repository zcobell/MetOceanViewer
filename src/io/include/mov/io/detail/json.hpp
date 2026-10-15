// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Reading provider JSON (docs/providers-design.md sections 4.2 and 4.8).
// Private to mov::io: public only because tests include it, and free of
// nlohmann/json, which only src/io/json/ includes (third_party_include_gate).
//
// nlohmann's lexer and parser drive a SAX handler of our own that builds only
// what a format reads: a `Keep` schema names the members kept (each with the
// schema of its value), and everything else is lexed and skipped without
// being built. The elements of the one large array of a body are built one
// at a time, handed over and dropped. Every value built knows the byte
// offset where it starts, which the errors report as a line and column.

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"

namespace mov::io::detail::json {

// ---- what is kept
// ---------------------------------------------------------------

struct Keep;

/// A kept member. `streamed` (top-level members only): an array whose
/// elements are handed to the OnElement callback one at a time, never kept;
/// the document holds it as an empty array.
struct Field {
  std::string_view name;
  const Keep* keep;
  bool streamed = false;
};

/// What a parse keeps of one value. For an object: the members named by
/// `fields`, each with its own Keep; other members are skipped. For an array:
/// its elements, each kept as `*element`, at most `max_elements` of them
/// (more is ParseError `too_large` at the array). A Keep with neither keeps a
/// scalar as it is and a container as an empty one of its kind (so a decoder
/// can still say the type is wrong), its contents skipped.
struct Keep {
  std::span<const Field> fields{};
  const Keep* element = nullptr;
  std::size_t max_elements = 0;
};

/// Keeps a scalar; a container is kept empty.
inline constexpr Keep leaf{};

// ---- what is built
// ----------------------------------------------------------------

struct Value;
struct Member;

using Object = std::vector<Member>;
using Array = std::vector<Value>;

struct Null {
  friend constexpr bool operator==(Null, Null) = default;
};

/// A built value and the byte offset where it starts in the body. Integers
/// keep their exact value; nlohmann gives a number without a sign or a
/// fraction as unsigned.
struct Value {
  std::variant<Null, bool, std::int64_t, std::uint64_t, double, std::string,
               Object, Array>
      data;
  std::size_t offset = 0;
};

/// A kept member of an object, in document order, and where its key starts.
struct Member {
  std::string name;
  std::size_t key_offset = 0;
  Value value;
};

/// The member `name` of `object` (nullptr when absent, or when `object` is
/// not an object).
[[nodiscard]] const Value* member(const Value& object,
                                  std::string_view name) noexcept;

/// The string, or nullptr when the value is not one.
[[nodiscard]] const std::string* as_string(const Value& value) noexcept;

/// The number as a double, or nullopt when the value is not a number.
[[nodiscard]] std::optional<double> as_double(const Value& value) noexcept;

/// An integer number, or nullopt (a fraction, an unsigned value past
/// INT64_MAX, or not a number).
[[nodiscard]] std::optional<std::int64_t> as_int(const Value& value) noexcept;

[[nodiscard]] bool is_null(const Value& value) noexcept;
[[nodiscard]] const Object* as_object(const Value& value) noexcept;
[[nodiscard]] const Array* as_array(const Value& value) noexcept;

// ---- parsing
// ------------------------------------------------------------------------

/// The parsed document (what its Keep kept) and how many SAX events the parse
/// took: the measure of its work, which is linear in the body.
struct Document {
  Value root;
  std::uint64_t events = 0;
};

/// Handed each element of the streamed array, with its 0-based index. An
/// error stops the parse.
using OnElement = std::function<std::expected<void, Error>(
    std::size_t index, const Value& element)>;

struct ParseOptions {
  /// Stop after the first top-level member when it is not one that `keep`
  /// names, leaving the document with what was kept so far (classify_body:
  /// an error body names its error first).
  bool stop_at_unkept_first_member = false;
};

/// The SAX events between two polls of ctx.stop.
inline constexpr std::uint64_t events_per_stop_poll = 4096;

/// Parses `body` keeping what `keep` names. A leading UTF-8 byte order mark
/// is skipped (offsets stay those of the body). Before parsing: an empty or
/// blank body is `empty_input`; one over ctx.limits.max_text_bytes, or nested
/// deeper than wire::max_json_depth, `too_large`; one with a NUL byte
/// `not_json` there (nlohmann's lexer would take it for the end of the
/// input). While parsing: a syntax error is `not_json` where nlohmann
/// stopped; a number past a double `out_of_range` at the number; a kept
/// member that its object repeats `bad_member` at that object; a kept array
/// past its max_elements `too_large`; every events_per_stop_poll events
/// ctx.stop is polled (Cancelled). The first error of `on_element` stops the
/// parse.
[[nodiscard]] std::expected<Document, Error> parse(
    std::string_view body, const Keep& keep, const ReadContext& ctx,
    const OnElement& on_element = {}, ParseOptions options = {});

/// The body without a leading UTF-8 byte order mark, and the mark's length.
[[nodiscard]] constexpr std::size_t bom_length(std::string_view body) noexcept {
  return body.starts_with("\xEF\xBB\xBF") ? 3 : 0;
}

/// A ParseError of `code` at `offset` of `body`: its line and column, and the
/// text from there as context.
[[nodiscard]] ParseError error_at(ParseErrc code, std::string_view body,
                                  std::size_t offset);

}  // namespace mov::io::detail::json
