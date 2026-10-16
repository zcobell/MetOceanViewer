// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// usgs::classify_body and classify_error: the two error shapes of the USGS
// Water Data API (docs/providers-design.md sections 4.3 and 4.9). Each
// mapping is pinned by a recorded body in tests/fixtures/io/usgs/.

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "mov/core/ascii.hpp"
#include "mov/io/detail/json.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/detail/usgs_json.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/body.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"
#include "mov/io/wire/usgs.hpp"

namespace mov::io::wire::usgs {

namespace {

namespace json = io::detail::json;

// ---- what is kept
// ------------------------------------------------------------

constexpr std::array gateway_fields{
    json::Field{.name = "code", .keep = &json::leaf},
    json::Field{.name = "message", .keep = &json::leaf}};
constexpr json::Keep gateway_schema{.fields = gateway_fields};
constexpr std::array classify_fields{
    json::Field{.name = "error", .keep = &gateway_schema},
    json::Field{.name = "code", .keep = &json::leaf},
    json::Field{.name = "description", .keep = &json::leaf}};
constexpr json::Keep classify_schema{.fields = classify_fields};

// ---- the code tables
// ------------------------------------------------------------

// api.data.gov's key refusals (its gateway answers before the API does).
constexpr std::array key_codes{std::string_view{"API_KEY_INVALID"},
                               std::string_view{"API_KEY_MISSING"},
                               std::string_view{"API_KEY_DISABLED"},
                               std::string_view{"API_KEY_UNAUTHORIZED"},
                               std::string_view{"API_KEY_UNVERIFIED"}};

// The OGC codes of a request the API refuses.
constexpr std::array parameter_codes{std::string_view{"InvalidParameterValue"},
                                     std::string_view{"InvalidQuery"}};

constexpr std::string_view envelope_text = "time envelope is too large";

const std::string* string_member(const json::Value& object,
                                 std::string_view name) {
  const json::Value* value = json::member(object, name);
  return value == nullptr ? nullptr : json::as_string(*value);
}

std::string_view or_empty(const std::string* text) {
  return text == nullptr ? std::string_view{} : std::string_view{*text};
}

// `{"error": {"code", "message"}}`.
ServiceError from_gateway(const json::Value& error) {
  const std::string_view code = or_empty(string_member(error, "code"));
  const std::string_view message = or_empty(string_member(error, "message"));
  const bool key = std::ranges::find(key_codes, code) != key_codes.end();
  return ServiceError::make(
      Origin::usgs, key ? ServiceErrc::api_key_invalid : ServiceErrc::unknown,
      message.empty() ? code : message);
}

// `{"code", "type", "description"}`.
ServiceError from_ogc(std::string_view code, std::string_view description) {
  const std::string_view text = description.empty() ? code : description;
  if (description.find(envelope_text) != std::string_view::npos) {
    return ServiceError::make(Origin::usgs, ServiceErrc::range_exceeded, text);
  }
  const bool refused =
      std::ranges::find(parameter_codes, code) != parameter_codes.end();
  return ServiceError::make(
      Origin::usgs,
      refused ? ServiceErrc::invalid_parameter : ServiceErrc::unknown, text);
}

// The refusal a parsed document states, if it has one of the two shapes.
std::optional<ServiceError> refusal_of(const json::Value& document) {
  if (const json::Value* error = json::member(document, "error");
      error != nullptr and json::as_object(*error) != nullptr) {
    return from_gateway(*error);
  }
  if (const std::string* code = string_member(document, "code")) {
    return from_ogc(*code, or_empty(string_member(document, "description")));
  }
  return std::nullopt;
}

}  // namespace

Classified classify_body(std::string_view body, std::string_view content_type,
                         const ReadContext& ctx) {
  if (auto markup = sniff_markup(Origin::usgs, body, content_type)) {
    return *std::move(markup);
  }
  const std::string_view rest =
      io::detail::skip_space(body.substr(json::bom_length(body)));
  if (rest.empty() or rest.front() != '{') {
    return Payload{};  // its parser refuses it with a location
  }
  // An error body names its error first; a page names its type first, and
  // is not read further here (section 4.8, N6).
  const auto parsed = json::parse(body, classify_schema, ctx, {},
                                  {.stop_at_unkept_first_member = true});
  if (not parsed) {
    return Payload{};
  }
  if (auto refusal = refusal_of(parsed->root)) {
    return *std::move(refusal);
  }
  return Payload{};
}

ServiceError classify_error(std::string_view body,
                            std::string_view content_type,
                            const ReadContext& ctx) {
  if (auto markup = sniff_markup(Origin::usgs, body, content_type)) {
    return *std::move(markup);
  }
  const auto parsed = json::parse(body, classify_schema, ctx);
  if (parsed) {
    if (auto refusal = refusal_of(parsed->root)) {
      return *std::move(refusal);
    }
  }
  return ServiceError::make(Origin::usgs, ServiceErrc::unknown,
                            body.substr(json::bom_length(body)));
}

}  // namespace mov::io::wire::usgs

namespace mov::io::detail::usgs {

const json::Keep& classify_keep() { return wire::usgs::classify_schema; }

}  // namespace mov::io::detail::usgs
