// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// detail::json::parse: nlohmann's lexer and parser with a SAX handler that
// builds only what a Keep schema names (mov/io/detail/json.hpp). Nothing here
// scans a container of the body's choosing: every event costs O(1) plus the
// value it builds (a key is compared with the members kept so far, which its
// Keep bounds: its fields and max_unknown), so a parse is linear in the body.
// (nlohmann's own callback parser erases discarded elements by scanning their
// container, which is quadratic.)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <expected>
#include <iterator>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/ascii.hpp"
#include "mov/core/cancelled.hpp"
#include "mov/io/detail/json.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/detail/text_position.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/body.hpp"

namespace mov::io::detail::json {

namespace {

using Nlohmann = nlohmann::json;

// A forward iterator over the body that publishes how far nlohmann's lexer
// has read, so the handler knows where each token ends. The lexer reads one
// byte per increment.
class Reading {
 public:
  using iterator_category = std::forward_iterator_tag;
  using value_type = char;
  using difference_type = std::ptrdiff_t;
  using pointer = const char*;
  using reference = const char&;

  Reading() = default;
  Reading(const char* at, const char** cursor) noexcept
      : at_{at}, cursor_{cursor} {}

  reference operator*() const noexcept { return *at_; }
  Reading& operator++() noexcept {
    ++at_;
    if (cursor_ != nullptr) {
      *cursor_ = at_;
    }
    return *this;
  }
  Reading operator++(int) noexcept {
    Reading before = *this;
    ++*this;
    return before;
  }
  friend bool operator==(const Reading& a, const Reading& b) noexcept {
    return a.at_ == b.at_;
  }

 private:
  const char* at_ = nullptr;
  const char** cursor_ = nullptr;
};

bool is_number_char(char c) noexcept {
  return core::ascii::is_digit(c) or c == '-' or c == '+' or c == '.' or
         c == 'e' or c == 'E';
}

// Where the string token that ends just before `end` starts: its opening
// quote, the nearest one before the closing quote that an even number of
// backslashes precedes (a quote inside a JSON string is always escaped).
std::size_t string_start(std::string_view body, std::size_t end) noexcept {
  std::size_t at = end >= 2 ? end - 2 : 0;
  while (at > 0) {
    if (body[at] == '"') {
      std::size_t slashes = 0;
      while (slashes < at and body[at - 1 - slashes] == '\\') {
        ++slashes;
      }
      if (slashes % 2 == 0) {
        return at;
      }
    }
    --at;
  }
  return 0;
}

// Where a number ending at `end` (after the byte the lexer read past it, if
// any) starts.
std::size_t number_start(std::string_view body, std::size_t end) noexcept {
  std::size_t at = std::min(end, body.size());
  if (at > 0 and not is_number_char(body[at - 1])) {
    --at;
  }
  while (at > 0 and is_number_char(body[at - 1])) {
    --at;
  }
  return at;
}

// One open container.
struct Frame {
  enum class Kind : std::uint8_t { object, array };
  Kind kind;
  // What to keep of its contents; nullptr: nothing.
  const Keep* keep;
  // The built container; nullptr when it is not built (skipped, or the
  // streamed array).
  Value* value;
  std::size_t offset;
  bool streamed = false;
  std::size_t elements = 0;
  // Objects: the kept field of the key just read (nullptr: skip its value).
  const Field* pending = nullptr;
  std::size_t key_offset = 0;
  bool repeated = false;
  // Members kept by name (Keep::unknown_names), and the name of the pending
  // one when it is unknown_field.
  std::size_t unknown = 0;
  std::string unknown_key{};
};

// The pending field of a member its object's Keep does not name, kept by
// name (Keep::unknown_names): its value is a leaf.
constexpr Field unknown_field{.name = "", .keep = nullptr};

// What a Value holds.
using Data = decltype(Value::data);

class Builder final : public nlohmann::json_sax<Nlohmann> {
 public:
  Builder(std::string_view body, const char** cursor, const Keep& keep,
          const ReadContext& ctx, const OnElement& on_element,
          ParseOptions options)
      : body_{body},
        cursor_{cursor},
        keep_{keep},
        ctx_{ctx},
        on_element_{on_element},
        options_{options} {}

  // ---- scalars ----
  bool null() override { return scalar(Data{Null{}}, literal_start(4)); }
  bool boolean(bool val) override {
    return scalar(Data{val}, literal_start(val ? 4 : 5));
  }
  bool number_integer(number_integer_t val) override {
    return scalar(Data{std::int64_t{val}}, number_start(body_, position()));
  }
  bool number_unsigned(number_unsigned_t val) override {
    return scalar(Data{std::uint64_t{val}}, number_start(body_, position()));
  }
  bool number_float(number_float_t val, const string_t& /*s*/) override {
    return scalar(Data{double{val}}, number_start(body_, position()));
  }
  bool string(string_t& val) override {
    return scalar(Data{std::move(val)}, string_start(body_, position()));
  }
  bool binary(binary_t& /*val*/) override { return true; }

  // ---- containers ----
  bool start_object(std::size_t /*elements*/) override {
    return open(Frame::Kind::object, Data{Object{}});
  }
  bool start_array(std::size_t /*elements*/) override {
    return open(Frame::Kind::array, Data{Array{}});
  }
  bool end_object() override { return close(); }
  bool end_array() override { return close(); }

  bool key(string_t& val) override {
    if (not tick()) {
      return false;
    }
    Frame& top = frames_.back();
    top.pending = nullptr;
    if (top.keep == nullptr) {
      return true;
    }
    const auto field = std::ranges::find(top.keep->fields,
                                         std::string_view{val}, &Field::name);
    if (frames_.size() == 1 and options_.stop_at_unkept_first_member and
        not first_member_seen_ and field == top.keep->fields.end()) {
      stopped_early_ = true;
      return false;
    }
    first_member_seen_ = first_member_seen_ or frames_.size() == 1;
    if (field == top.keep->fields.end() and not top.keep->unknown_names) {
      return true;
    }
    if (top.value != nullptr) {
      const Object& members = std::get<Object>(top.value->data);
      top.repeated =
          top.repeated or
          std::ranges::find(members, val, &Member::name) != members.end();
    }
    if (field == top.keep->fields.end()) {
      if (++top.unknown > top.keep->max_unknown) {
        fail(Error{ParseError::make(
            ParseErrc::too_large, where_of(body_, top.offset),
            std::to_string(top.unknown) + " unknown members, limit " +
                std::to_string(top.keep->max_unknown))});
        return false;
      }
      top.unknown_key = val;
      top.pending = &unknown_field;
    } else {
      top.pending = &*field;
    }
    top.key_offset = string_start(body_, position());
    return true;
  }

  bool parse_error(std::size_t position, const std::string& /*last_token*/,
                   const nlohmann::detail::exception& ex) override {
    // position counts the bytes read, 1-based: the one where it stopped.
    const std::size_t at = position > 0 ? position - 1 : 0;
    if (ex.id == number_overflow) {
      fail(error_at(ParseErrc::out_of_range, body_,
                    number_start(body_, this->position())));
    } else {
      fail(error_at(ParseErrc::not_json, body_, at));
    }
    return false;
  }

  [[nodiscard]] std::optional<Error> take_error() {
    return std::exchange(error_, std::nullopt);
  }
  [[nodiscard]] bool stopped_early() const noexcept { return stopped_early_; }
  [[nodiscard]] std::uint64_t events() const noexcept { return events_; }
  [[nodiscard]] Value take_root() { return std::exchange(root_, Value{}); }

 private:
  // nlohmann's exception id for a number past a double.
  static constexpr int number_overflow = 406;

  [[nodiscard]] std::size_t position() const noexcept {
    return static_cast<std::size_t>(*cursor_ - body_.data());
  }
  [[nodiscard]] std::size_t literal_start(std::size_t length) const noexcept {
    const std::size_t end = position();
    return end >= length ? end - length : 0;
  }

  void fail(Error e) {
    if (not error_) {
      error_ = std::move(e);
    }
  }

  // Counts an event, and polls the stop request at the first event and
  // every events_per_stop_poll after it.
  [[nodiscard]] bool tick() {
    if (events_++ % events_per_stop_poll == 0 and ctx_.stop.stop_requested()) {
      fail(Error{core::Cancelled{}});
      return false;
    }
    return true;
  }

  // Where a new value goes: the Keep of its own contents (nullptr: skip it)
  // and the slot it is built in (nullptr: not built).
  struct Slot {
    const Keep* keep;
    Value* value;
    bool streamed;
  };

  // Places a new value of `data` at `offset` in the innermost container
  // (the root when there is none). Returns its slot, or nullopt after an
  // error.
  std::optional<Slot> add(Data data, std::size_t offset) {
    if (frames_.empty()) {
      root_ = Value{.data = std::move(data), .offset = offset};
      return Slot{.keep = &keep_, .value = &root_, .streamed = false};
    }
    Frame& top = frames_.back();
    if (top.kind == Frame::Kind::object) {
      const Field* field = top.pending;
      top.pending = nullptr;
      if (field == nullptr or top.value == nullptr) {
        return Slot{.keep = nullptr, .value = nullptr, .streamed = false};
      }
      auto& members = std::get<Object>(top.value->data);
      members.push_back(
          Member{.name = field == &unknown_field ? std::move(top.unknown_key)
                                                 : std::string{field->name},
                 .key_offset = top.key_offset,
                 .value = Value{.data = std::move(data), .offset = offset}});
      return Slot{.keep = field->keep,
                  .value = &members.back().value,
                  .streamed = field->streamed and frames_.size() == 1};
    }
    if (top.streamed) {
      element_ = Value{.data = std::move(data), .offset = offset};
      return Slot{.keep = top.keep, .value = &element_, .streamed = false};
    }
    if (top.keep == nullptr or top.keep->element == nullptr or
        top.value == nullptr) {
      return Slot{.keep = nullptr, .value = nullptr, .streamed = false};
    }
    if (++top.elements > top.keep->max_elements) {
      fail(Error{
          ParseError::make(ParseErrc::too_large, where_of(body_, top.offset),
                           std::to_string(top.elements) + " elements, limit " +
                               std::to_string(top.keep->max_elements))});
      return std::nullopt;
    }
    auto& elements = std::get<Array>(top.value->data);
    elements.push_back(Value{.data = std::move(data), .offset = offset});
    return Slot{.keep = top.keep->element,
                .value = &elements.back(),
                .streamed = false};
  }

  // A complete element of the streamed array, if the innermost container is
  // it: handed over and dropped.
  bool hand_over_if_element() {
    if (frames_.empty() or not frames_.back().streamed) {
      return true;
    }
    const std::size_t index = element_count_++;
    if (on_element_) {
      if (auto handled = on_element_(index, element_); not handled) {
        fail(std::move(handled).error());
        return false;
      }
    }
    element_ = Value{};
    return true;
  }

  bool scalar(Data data, std::size_t offset) {
    if (not tick()) {
      return false;
    }
    // A scalar no Keep reaches is not built at all.
    if (not frames_.empty() and not builds_children(frames_.back())) {
      if (frames_.back().kind == Frame::Kind::object) {
        frames_.back().pending = nullptr;
      }
      return true;
    }
    if (not add(std::move(data), offset)) {
      return false;
    }
    return hand_over_if_element();
  }

  [[nodiscard]] static bool builds_children(const Frame& f) noexcept {
    return f.streamed or (f.keep != nullptr and f.value != nullptr);
  }

  bool open(Frame::Kind kind, Data empty) {
    if (not tick()) {
      return false;
    }
    const std::size_t offset = position() > 0 ? position() - 1 : 0;
    if (not frames_.empty() and not builds_children(frames_.back())) {
      if (frames_.back().kind == Frame::Kind::object) {
        frames_.back().pending = nullptr;
      }
      frames_.push_back(Frame{
          .kind = kind, .keep = nullptr, .value = nullptr, .offset = offset});
      return true;
    }
    const std::optional<Slot> slot = add(std::move(empty), offset);
    if (not slot) {
      return false;
    }
    const bool leaf_keep =
        slot->keep == nullptr or
        (slot->keep->fields.empty() and slot->keep->element == nullptr and
         not slot->keep->unknown_names);
    if (slot->streamed and kind == Frame::Kind::array) {
      frames_.push_back(Frame{.kind = kind,
                              .keep = slot->keep->element,
                              .value = nullptr,
                              .offset = offset,
                              .streamed = true});
      return true;
    }
    frames_.push_back(Frame{.kind = kind,
                            .keep = leaf_keep ? nullptr : slot->keep,
                            .value = leaf_keep ? nullptr : slot->value,
                            .offset = offset});
    return true;
  }

  bool close() {
    if (not tick()) {
      return false;
    }
    const bool repeated = frames_.back().repeated;
    const std::size_t offset = frames_.back().offset;
    frames_.pop_back();
    if (repeated) {
      fail(error_at(ParseErrc::bad_member, body_, offset));
      return false;
    }
    return hand_over_if_element();
  }

  std::string_view body_;
  const char** cursor_;
  const Keep& keep_;
  const ReadContext& ctx_;
  const OnElement& on_element_;
  ParseOptions options_;
  std::vector<Frame> frames_;
  Value root_;
  Value element_;
  std::size_t element_count_ = 0;
  std::uint64_t events_ = 0;
  std::optional<Error> error_;
  bool first_member_seen_ = false;
  bool stopped_early_ = false;
};

}  // namespace

const Value* member(const Value& object, std::string_view name) noexcept {
  const Object* members = as_object(object);
  if (members == nullptr) {
    return nullptr;
  }
  const auto found = std::ranges::find(*members, name, &Member::name);
  return found == members->end() ? nullptr : &found->value;
}

const std::string* as_string(const Value& value) noexcept {
  return std::get_if<std::string>(&value.data);
}

std::optional<double> as_double(const Value& value) noexcept {
  if (const auto* f = std::get_if<double>(&value.data)) {
    return *f;
  }
  if (const auto* i = std::get_if<std::int64_t>(&value.data)) {
    return static_cast<double>(*i);
  }
  if (const auto* u = std::get_if<std::uint64_t>(&value.data)) {
    return static_cast<double>(*u);
  }
  return std::nullopt;
}

std::optional<std::int64_t> as_int(const Value& value) noexcept {
  if (const auto* i = std::get_if<std::int64_t>(&value.data)) {
    return *i;
  }
  if (const auto* u = std::get_if<std::uint64_t>(&value.data);
      u != nullptr and *u <= static_cast<std::uint64_t>(INT64_MAX)) {
    return static_cast<std::int64_t>(*u);
  }
  return std::nullopt;
}

bool is_null(const Value& value) noexcept {
  return std::holds_alternative<Null>(value.data);
}
const Object* as_object(const Value& value) noexcept {
  return std::get_if<Object>(&value.data);
}
const Array* as_array(const Value& value) noexcept {
  return std::get_if<Array>(&value.data);
}

ParseError error_at(ParseErrc code, std::string_view body, std::size_t offset) {
  return detail::error_at_offset(code, body, offset);
}

std::expected<Document, Error> parse(std::string_view body, const Keep& keep,
                                     const ReadContext& ctx,
                                     const OnElement& on_element,
                                     ParseOptions options) {
  const std::size_t bom = bom_length(body);
  const std::string_view text = body.substr(bom);
  if (detail::is_blank(text)) {
    return std::unexpected{
        Error{ParseError::make(ParseErrc::empty_input, {.line = 1}, "")}};
  }
  if (body.size() > ctx.limits.max_text_bytes) {
    return std::unexpected{
        Error{ParseError::make(ParseErrc::too_large, {.line = 1},
                               std::to_string(body.size()) + " bytes, limit " +
                                   std::to_string(ctx.limits.max_text_bytes))}};
  }
  if (auto deep = wire::check_json_depth(body); not deep) {
    return std::unexpected{Error{std::move(deep).error()}};
  }
  if (const std::size_t nul = body.find('\0'); nul != std::string_view::npos) {
    return std::unexpected{Error{error_at(ParseErrc::not_json, body, nul)}};
  }
  // The lexer reads [begin, end) through Reading, never past `end`: no
  // terminator is needed.
  const char* const begin = std::to_address(text.begin());
  const char* const end = begin + text.size();
  const char* cursor = begin;
  Builder builder{body, &cursor, keep, ctx, on_element, options};
  const Reading first{begin, &cursor};
  const Reading last{end, nullptr};
  const bool parsed =
      Nlohmann::sax_parse(first, last, &builder, Nlohmann::input_format_t::json,
                          /*strict=*/true,
                          /*ignore_comments=*/false);
  const bool early = builder.stopped_early();
  const std::uint64_t events = builder.events();
  if (auto error = builder.take_error()) {
    return std::unexpected{*std::move(error)};
  }
  if (not parsed and not early) {
    // nlohmann stopped without saying why (it always reports a syntax error
    // through parse_error); refuse rather than return half a document.
    return std::unexpected{
        Error{error_at(ParseErrc::not_json, body,
                       static_cast<std::size_t>(cursor - body.data()))}};
  }
  return Document{.root = builder.take_root(), .events = events};
}

}  // namespace mov::io::detail::json
