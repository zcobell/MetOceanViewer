// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// coops::classify_body: whether a CO-OPS body is data, "no data" or a
// refusal (docs/providers-design.md section 4.3). Each mapping is pinned by a
// recorded body in tests/fixtures/io/coops/.

#include <algorithm>
#include <array>
#include <cstddef>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "document.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/wire/body.hpp"
#include "mov/io/wire/coops.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"

namespace mov::io::wire::coops {

namespace {

using json::Json;

// One rule of the message table: a message containing any of `texts` (the
// empty ones match nothing) is `verdict`.
struct MessageRule {
  std::array<std::string_view, 3> texts;
  std::variant<NoData, ServiceErrc> verdict;
};

// In order: the first rule with a match decides.
constexpr std::array message_rules{
    MessageRule{
        .texts = {"No data was found", "No Predictions data was found", ""},
        .verdict = NoData{}},
    MessageRule{.texts = {"Range Limit Exceeded", "", ""},
                .verdict = ServiceErrc::range_exceeded},
    MessageRule{
        .texts = {"The station is not a valid station or there is system error",
                  "", ""},
        .verdict = ServiceErrc::station_or_system},
    MessageRule{.texts = {"Wrong Datum", "supported Datum values", ""},
                .verdict = ServiceErrc::datum_unavailable},
    MessageRule{
        .texts = {"Wrong Date", "Wrong Product", "Invalid type specified"},
        .verdict = ServiceErrc::invalid_parameter},
};

// "There is no IGLD for the station: 8729840".
bool names_missing_datum(std::string_view message) {
  constexpr std::string_view head = "There is no ";
  constexpr std::string_view tail = " for the station";
  const std::size_t at = message.find(head);
  return at != std::string_view::npos and
         message.find(tail, at + head.size()) != std::string_view::npos;
}

bool mentions(std::string_view message, std::string_view text) {
  return not text.empty() and message.find(text) != std::string_view::npos;
}

BodyClass from_message(Origin origin, std::string_view message) {
  const auto rule =
      std::ranges::find_if(message_rules, [message](const MessageRule& r) {
        return std::ranges::any_of(r.texts, [message](std::string_view text) {
          return mentions(message, text);
        });
      });
  if (rule != message_rules.end()) {
    return std::visit(
        core::Overloaded{[](NoData) -> BodyClass { return NoData{}; },
                         [&](ServiceErrc code) -> BodyClass {
                           return ServiceError::make(origin, code, message);
                         }},
        rule->verdict);
  }
  const ServiceErrc code = names_missing_datum(message)
                               ? ServiceErrc::datum_unavailable
                               : ServiceErrc::unknown;
  return ServiceError::make(origin, code, message);
}

// The members of an error body, and whether a data member was there (dropped
// unread: classifying a body does not build its data).
struct Envelope {
  Json document;
  bool has_data_member = false;
};

constexpr std::array data_members{
    std::string_view{"data"}, std::string_view{"predictions"},
    std::string_view{"stations"}, std::string_view{"datums"}};
constexpr std::array message_members{std::string_view{"error"},
                                     std::string_view{"errorMsg"},
                                     std::string_view{"message"}};

std::optional<Envelope> envelope_of(std::string_view body) {
  Envelope out;
  const auto keep = [&out](int depth, Json::parse_event_t event, Json& parsed) {
    if (depth != 1 or event != Json::parse_event_t::key) {
      return true;
    }
    const std::string* name = json::as_string(parsed);
    if (name == nullptr) {
      return false;
    }
    if (std::ranges::contains(data_members, std::string_view{*name})) {
      out.has_data_member = true;
    }
    return std::ranges::contains(message_members, std::string_view{*name});
  };
  // No exceptions: a body that does not parse is a Payload, for its parser
  // to refuse with a location.
  out.document = Json::parse(body.begin(), body.end(), keep,
                             /*allow_exceptions=*/false);
  if (out.document.is_discarded() or not out.document.is_object()) {
    return std::nullopt;
  }
  return out;
}

// The message of `{"error": "..."}` or `{"error": {"message": "..."}}`.
const std::string* error_message(const Json& error) {
  if (const std::string* text = json::as_string(error)) {
    return text;
  }
  const Json* message = json::find_member(error, "message");
  return message == nullptr ? nullptr : json::as_string(*message);
}

const std::string* string_member_of(const Json& document,
                                    std::string_view name) {
  const Json* member = json::find_member(document, name);
  return member == nullptr ? nullptr : json::as_string(*member);
}

}  // namespace

BodyClass classify_body(Origin origin, std::string_view body,
                        std::string_view content_type) {
  if (auto markup = sniff_markup(origin, body, content_type)) {
    return *std::move(markup);
  }
  const std::string_view rest = detail::skip_space(body);
  // Empty, or an array: no CO-OPS refusal looks like that.
  if (rest.empty() or rest.front() == '[') {
    return Payload{};
  }
  // Not JSON at all: the data API answers a bad product name in plain text.
  if (rest.front() != '{') {
    return from_message(origin, rest);
  }
  // Too deep, or a NUL (which nlohmann takes for the end of the input): the
  // parser refuses it.
  if (not check_json_depth(body) or body.find('\0') != std::string_view::npos) {
    return Payload{};
  }
  const auto envelope = envelope_of(body);
  if (not envelope) {
    return Payload{};
  }
  const Json& document = envelope->document;
  if (const Json* error = json::find_member(document, "error")) {
    const std::string* text = error_message(*error);
    return text == nullptr
               ? BodyClass{ServiceError::make(origin, ServiceErrc::unknown, "")}
               : from_message(origin, *text);
  }
  if (const std::string* text = string_member_of(document, "errorMsg")) {
    return from_message(origin, *text);
  }
  // An API gateway's own failure: {"message": "..."} and no data member.
  if (const std::string* text = string_member_of(document, "message");
      text != nullptr and not envelope->has_data_member) {
    return from_message(origin, *text);
  }
  return Payload{};
}

}  // namespace mov::io::wire::coops
