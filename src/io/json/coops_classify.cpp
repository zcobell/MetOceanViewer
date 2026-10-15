// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// coops::classify_body: whether a CO-OPS body is data, "no data" or a
// refusal (docs/providers-design.md sections 4.3 and 4.8). Each mapping is
// pinned by a recorded body in tests/fixtures/io/coops/.

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "mov/core/overloaded.hpp"
#include "mov/io/detail/coops_json.hpp"
#include "mov/io/detail/json.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/body.hpp"
#include "mov/io/wire/coops.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"

namespace mov::io::wire::coops {

namespace {

namespace json = io::detail::json;

// ---- the message table
// ------------------------------------------------------------

constexpr bool mentions(std::string_view message,
                        std::string_view text) noexcept {
  return message.find(text) != std::string_view::npos;
}

constexpr bool says_no_data(std::string_view m) noexcept {
  return mentions(m, "No data was found") or
         mentions(m, "No Predictions data was found");
}
constexpr bool says_range_exceeded(std::string_view m) noexcept {
  return mentions(m, "Range Limit Exceeded");
}
constexpr bool says_station_or_system(std::string_view m) noexcept {
  return mentions(
      m, "The station is not a valid station or there is system error");
}
// "There is no IGLD for the station: 8729840".
constexpr bool names_missing_datum(std::string_view m) noexcept {
  constexpr std::string_view head = "There is no ";
  const std::size_t at = m.find(head);
  return at != std::string_view::npos and
         m.find(" for the station", at + head.size()) != std::string_view::npos;
}
constexpr bool says_datum_unavailable(std::string_view m) noexcept {
  return mentions(m, "Wrong Datum") or mentions(m, "supported Datum values") or
         names_missing_datum(m);
}
constexpr bool says_invalid_parameter(std::string_view m) noexcept {
  return mentions(m, "Wrong Date") or mentions(m, "Wrong Product") or
         mentions(m, "Invalid type specified");
}

// One rule: a message it `matches` is `verdict`. In order: the first match
// decides.
struct MessageRule {
  bool (*matches)(std::string_view) noexcept;
  std::variant<NoData, ServiceErrc> verdict;
};

constexpr std::array message_rules{
    MessageRule{.matches = says_no_data, .verdict = NoData{}},
    MessageRule{.matches = says_range_exceeded,
                .verdict = ServiceErrc::range_exceeded},
    MessageRule{.matches = says_station_or_system,
                .verdict = ServiceErrc::station_or_system},
    MessageRule{.matches = says_datum_unavailable,
                .verdict = ServiceErrc::datum_unavailable},
    MessageRule{.matches = says_invalid_parameter,
                .verdict = ServiceErrc::invalid_parameter},
};

BodyClass from_message(Origin origin, std::string_view message) {
  const auto rule = std::ranges::find_if(
      message_rules,
      [message](const MessageRule& r) { return r.matches(message); });
  if (rule == message_rules.end()) {
    return ServiceError::make(origin, ServiceErrc::unknown, message);
  }
  return std::visit(
      core::Overloaded{[](NoData) -> BodyClass { return NoData{}; },
                       [&](ServiceErrc code) -> BodyClass {
                         return ServiceError::make(origin, code, message);
                       }},
      rule->verdict);
}

// ---- what is kept
// ---------------------------------------------------------------------

constexpr std::array error_fields{
    json::Field{.name = "message", .keep = &json::leaf}};
constexpr json::Keep error_schema{.fields = error_fields};
// The data members are kept empty: only their presence is read.
constexpr std::array classify_fields{
    json::Field{.name = "error", .keep = &error_schema},
    json::Field{.name = "errorMsg", .keep = &json::leaf},
    json::Field{.name = "message", .keep = &json::leaf},
    json::Field{.name = "data", .keep = &json::leaf},
    json::Field{.name = "predictions", .keep = &json::leaf},
    json::Field{.name = "stations", .keep = &json::leaf},
    json::Field{.name = "datums", .keep = &json::leaf}};
constexpr json::Keep classify_schema{.fields = classify_fields};

bool has_member(const json::Value& document, std::string_view name) {
  return json::member(document, name) != nullptr;
}

const std::string* string_member(const json::Value& document,
                                 std::string_view name) {
  const json::Value* value = json::member(document, name);
  return value == nullptr ? nullptr : json::as_string(*value);
}

// The message of `{"error": "..."}` or `{"error": {"message": "..."}}`.
const std::string* error_message(const json::Value& error) {
  if (const std::string* text = json::as_string(error)) {
    return text;
  }
  return string_member(error, "message");
}

}  // namespace

BodyClass classify_body(Api api, std::string_view body,
                        std::string_view content_type) {
  const Origin origin = origin_of(api);
  if (auto markup = sniff_markup(origin, body, content_type)) {
    return *std::move(markup);
  }
  const std::string_view rest =
      io::detail::skip_space(body.substr(json::bom_length(body)));
  // Empty, or an array: no CO-OPS refusal looks like that.
  if (rest.empty() or rest.front() == '[') {
    return Payload{};
  }
  // Not JSON at all: the data API answers a bad product name in plain text.
  if (rest.front() != '{') {
    return from_message(origin, rest);
  }
  // An error body names its error first, so a body whose first member is
  // anything else is not parsed further (section 4.8, N6).
  const auto parsed = json::parse(body, classify_schema, ReadContext{}, {},
                                  {.stop_at_unkept_first_member = true});
  if (not parsed) {
    return Payload{};  // its parser refuses it with a location
  }
  const json::Value& document = parsed->root;
  if (const json::Value* error = json::member(document, "error")) {
    const std::string* text = error_message(*error);
    return text == nullptr
               ? BodyClass{ServiceError::make(origin, ServiceErrc::unknown, "")}
               : from_message(origin, *text);
  }
  if (const std::string* text = string_member(document, "errorMsg")) {
    return from_message(origin, *text);
  }
  // An API gateway's own failure: {"message": "..."} and no data member.
  const bool has_data =
      has_member(document, "data") or has_member(document, "predictions") or
      has_member(document, "stations") or has_member(document, "datums");
  if (const std::string* text = string_member(document, "message");
      text != nullptr and not has_data) {
    return ServiceError::make(origin, ServiceErrc::gateway, *text);
  }
  return Payload{};
}

}  // namespace mov::io::wire::coops

namespace mov::io::detail::coops {

const json::Keep& classify_keep() { return wire::coops::classify_schema; }

}  // namespace mov::io::detail::coops
