// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "document.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <functional>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

#include "mov/core/cancelled.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/detail/text_position.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/body.hpp"

namespace mov::io::json {

namespace {

constexpr std::size_t elements_per_stop_poll = 4096;

// Thrown by the callback to stop the parse at the first error; caught in
// parse_streaming, the one place it is thrown through nlohmann.
class Abort : public std::exception {
 public:
  explicit Abort(Error error) : error_{std::move(error)} {}
  [[nodiscard]] const Error& error() const noexcept { return error_; }
  [[nodiscard]] const char* what() const noexcept override {
    return "mov::io::json parse stopped";
  }

 private:
  Error error_;
};

// What the depth-1 container being parsed is.
enum class Member : std::uint8_t { other, streamed_array, other_array };

// The parser callback (nlohmann's depth: the top-level value is 0, its
// members 1, the elements of a top-level array member 2).
class Streamer {
 public:
  Streamer(const Body& body, std::string_view array_key, const ReadContext& ctx,
           const OnElement& on_element)
      : body_{body},
        array_key_{array_key},
        ctx_{ctx},
        on_element_{on_element} {}

  bool operator()(int depth, Json::parse_event_t event, Json& parsed) {
    using enum Json::parse_event_t;
    const auto level = static_cast<std::size_t>(std::max(depth, 0));
    switch (event) {
      case object_start:
        start_object(level);
        return keep_element_start(level);
      case key:
        record_key(level, parsed);
        return true;
      case object_end:
        check_repeats(level, parsed);
        return end_container(level, parsed);
      case array_start:
        if (level == 1) {
          member_ = top_key_ == array_key_ ? Member::streamed_array
                                           : Member::other_array;
        }
        return keep_element_start(level);
      case array_end:
        return end_container(level, parsed);
      case value:
        break;
    }
    return end_value(level, parsed);
  }

 private:
  // One more level than the deepest the pre-scan lets through, for the keys
  // of the deepest objects.
  static constexpr std::size_t levels = wire::max_json_depth + 2;

  void start_object(std::size_t level) {
    if (level == 1) {
      member_ = Member::other;
    }
    if (level + 1 < levels) {
      keys_[level + 1].clear();
      repeated_[level + 1] = false;
    }
  }

  // An element of another top-level array is dropped before it is built.
  [[nodiscard]] bool keep_element_start(std::size_t level) const noexcept {
    return level != 2 or member_ != Member::other_array;
  }

  void record_key(std::size_t level, const Json& parsed) {
    const std::string* name = as_string(parsed);
    if (name == nullptr or level >= levels) {
      return;
    }
    if (level == 1) {
      top_key_ = *name;
    }
    if (not keys_[level].insert(*name).second) {
      repeated_[level] = true;
    }
  }

  // nlohmann keeps one of a repeated member; the object is refused instead.
  void check_repeats(std::size_t level, const Json& parsed) const {
    if (level + 1 < levels and repeated_[level + 1]) {
      throw Abort{body_.error_at(ParseErrc::bad_member, parsed)};
    }
  }

  [[nodiscard]] bool end_container(std::size_t level, Json& parsed) {
    if (level == 1) {
      member_ = Member::other;
    }
    return end_value(level, parsed);
  }

  // The end of any value: an element of the streamed array is handed over
  // and dropped, any other element too.
  [[nodiscard]] bool end_value(std::size_t level, Json& parsed) {
    if (level != 2 or member_ == Member::other) {
      return true;
    }
    if (member_ == Member::streamed_array) {
      hand_over(parsed);
    }
    return false;
  }

  void hand_over(Json& element) {
    const std::size_t index = count_++;
    if (index % elements_per_stop_poll == 0 and ctx_.stop.stop_requested()) {
      throw Abort{Error{core::Cancelled{}}};
    }
    if (auto handled = on_element_(index, element); not handled) {
      throw Abort{std::move(handled).error()};
    }
  }

  const Body& body_;
  std::string_view array_key_;
  const ReadContext& ctx_;
  const OnElement& on_element_;
  std::string top_key_;
  Member member_ = Member::other;
  std::size_t count_ = 0;
  std::array<std::set<std::string, std::less<>>, levels> keys_{};
  std::array<bool, levels> repeated_{};
};

}  // namespace

ParseError Body::error_at(ParseErrc code, const Json& value) const {
  return error_at(code, value.start_pos());
}

ParseError Body::error_at(ParseErrc code, std::size_t offset) const {
  return detail::error_at_offset(code, text_, offset);
}

ParseError::Where Body::where(const Json& value) const noexcept {
  return detail::where_of(text_, value.start_pos());
}

std::expected<Json, Error> parse_streaming(const Body& body,
                                           std::string_view array_key,
                                           const ReadContext& ctx,
                                           const OnElement& on_element) {
  const std::string_view text = body.text();
  if (detail::is_blank(text)) {
    return std::unexpected{
        Error{ParseError::make(ParseErrc::empty_input, {.line = 1}, "")}};
  }
  if (text.size() > ctx.limits.max_text_bytes) {
    return std::unexpected{
        Error{ParseError::make(ParseErrc::too_large, {.line = 1},
                               std::to_string(text.size()) + " bytes, limit " +
                                   std::to_string(ctx.limits.max_text_bytes))}};
  }
  if (auto deep = wire::check_json_depth(text); not deep) {
    return std::unexpected{Error{std::move(deep).error()}};
  }
  // nlohmann's lexer takes a NUL byte for the end of the input, so it would
  // accept "{...}\0anything". A NUL is never JSON text (nor allowed raw in a
  // string): the body is not JSON there.
  if (const std::size_t nul = text.find('\0'); nul != std::string_view::npos) {
    return std::unexpected{Error{body.error_at(ParseErrc::not_json, nul)}};
  }
  // The first byte of the document: where a document-level error points.
  const std::size_t start = text.size() - detail::skip_space(text).size();
  Streamer streamer{body, array_key, ctx, on_element};
  try {
    Json document = Json::parse(
        text.begin(), text.end(),
        [&streamer](int depth, Json::parse_event_t event, Json& parsed) {
          return streamer(depth, event, parsed);
        });
    if (not document.is_object()) {
      return std::unexpected{
          Error{body.error_at(ParseErrc::bad_member, start)}};
    }
    return document;
  } catch (const Abort& abort) {
    return std::unexpected{abort.error()};
  } catch (const Json::parse_error& e) {
    // byte is 1-based: the byte the parser stopped at.
    const std::size_t offset = e.byte > 0 ? e.byte - 1 : 0;
    return std::unexpected{Error{body.error_at(ParseErrc::not_json, offset)}};
  } catch (const Json::out_of_range&) {
    // A number past the range of a double (nlohmann's 406); it does not say
    // where.
    return std::unexpected{Error{ParseError::make(
        ParseErrc::out_of_range, {.line = 1}, "number overflow")}};
  }
}

const Json* find_member(const Json& object, std::string_view name) {
  if (not object.is_object()) {
    return nullptr;
  }
  const auto found = object.find(name);
  return found == object.end() ? nullptr : &*found;
}

const std::string* as_string(const Json& value) noexcept {
  return value.get_ptr<const Json::string_t*>();
}

std::optional<double> as_double(const Json& value) noexcept {
  if (const auto* f = value.get_ptr<const Json::number_float_t*>()) {
    return *f;
  }
  if (const auto* i = value.get_ptr<const Json::number_integer_t*>()) {
    return static_cast<double>(*i);
  }
  if (const auto* u = value.get_ptr<const Json::number_unsigned_t*>()) {
    return static_cast<double>(*u);
  }
  return std::nullopt;
}

std::expected<std::string_view, ParseError> string_member(
    const Body& body, const Json& object, std::string_view name) {
  const Json* member = find_member(object, name);
  if (member == nullptr) {
    return std::unexpected{body.error_at(ParseErrc::bad_member, object)};
  }
  const std::string* text = as_string(*member);
  if (text == nullptr) {
    return std::unexpected{body.error_at(ParseErrc::bad_member, *member)};
  }
  return std::string_view{*text};
}

std::expected<std::optional<std::string_view>, ParseError>
optional_string_member(const Body& body, const Json& object,
                       std::string_view name) {
  const Json* member = find_member(object, name);
  if (member == nullptr or member->is_null()) {
    return std::optional<std::string_view>{};
  }
  const std::string* text = as_string(*member);
  if (text == nullptr) {
    return std::unexpected{body.error_at(ParseErrc::bad_member, *member)};
  }
  return std::optional<std::string_view>{*text};
}

}  // namespace mov::io::json
