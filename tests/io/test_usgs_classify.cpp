// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// usgs::classify_body and classify_error against the recorded refusals
// (docs/providers-design.md sections 4.3 and 10.1).

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <variant>

#include "mov/io/detail/json.hpp"
#include "mov/io/detail/usgs_json.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"
#include "mov/io/wire/usgs.hpp"
#include "usgs_test_support.hpp"

namespace {

namespace wu = mov::io::wire::usgs;
using mov::io::wire::Origin;
using mov::io::wire::Payload;
using mov::io::wire::ServiceErrc;
using mov::io::wire::ServiceError;
using mov::test::usgs_fixture;

constexpr std::string_view json_type = "application/json; charset=utf-8";

ServiceError error_of(std::string_view name) {
  return wu::classify_error(usgs_fixture(name), json_type);
}

}  // namespace

TEST_CASE("the recorded refusals map to their codes", "[io][usgs][classify]") {
  // api.data.gov's key refusal (HTTP 403), its own shape.
  const ServiceError key = error_of("error_invalid_key.json");
  CHECK(key.origin() == Origin::usgs);
  CHECK(key.code() == ServiceErrc::api_key_invalid);
  CHECK(key.text() ==
        "An invalid api_key was supplied. Get one at "
        "https://api.waterdata.usgs.gov/signup/");
  // OGC's shape (HTTP 400): a window over 1100 days is a planner bug.
  const ServiceError envelope = error_of("error_time_envelope.json");
  CHECK(envelope.code() == ServiceErrc::range_exceeded);
  CHECK(
      envelope.text().starts_with("The requested time envelope is too large"));
  CHECK(error_of("error_limit.json") ==
        ServiceError::make(Origin::usgs, ServiceErrc::invalid_parameter,
                           "Limit of 50000 exceeded"));
  CHECK(error_of("error_unknown_property.json").code() ==
        ServiceErrc::invalid_parameter);
  CHECK(error_of("error_property_not_found.json").code() ==
        ServiceErrc::invalid_parameter);
  CHECK(error_of("error_datetime.json") ==
        ServiceError::make(Origin::usgs, ServiceErrc::invalid_parameter,
                           "Unknown string format: yesterday"));
  CHECK(error_of("error_sortby_second_page.json") ==
        ServiceError::make(Origin::usgs, ServiceErrc::invalid_parameter,
                           "Only the first page of data can be returned "
                           "when specifying 'sortby'"));
  // A path that is not a collection (HTTP 404): a client bug, unknown.
  CHECK(error_of("error_collection_not_found.json") ==
        ServiceError::make(Origin::usgs, ServiceErrc::unknown,
                           "Collection not found"));
}

TEST_CASE("refusals the recordings do not show", "[io][usgs][classify]") {
  const auto classify = [](std::string_view body) {
    return wu::classify_error(body, json_type);
  };
  for (const std::string_view code :
       {"API_KEY_MISSING", "API_KEY_DISABLED", "API_KEY_UNAUTHORIZED",
        "API_KEY_UNVERIFIED"}) {
    CHECK(classify(R"({"error":{"code":")" + std::string{code} +
                   R"(","message":"m"}})")
              .code() == ServiceErrc::api_key_invalid);
  }
  // api.data.gov's rate limit (with HTTP 429, which the fetch layer handles).
  CHECK(classify(
            R"({"error":{"code":"OVER_RATE_LIMIT","message":"slow down"}})") ==
        ServiceError::make(Origin::usgs, ServiceErrc::unknown, "slow down"));
  CHECK(classify(R"({"error":{"code":"API_KEY_INVALID"}})") ==
        ServiceError::make(Origin::usgs, ServiceErrc::api_key_invalid,
                           "API_KEY_INVALID"));
  CHECK(classify(R"({"code":"NoApplicableCode","description":"boom"})") ==
        ServiceError::make(Origin::usgs, ServiceErrc::unknown, "boom"));
  CHECK(classify(R"({"code":"InvalidQuery"})") ==
        ServiceError::make(Origin::usgs, ServiceErrc::invalid_parameter,
                           "InvalidQuery"));
  // Markup (a maintenance or captive-portal page) and plain text.
  CHECK(wu::classify_error("  <html><body>down</body></html>", "text/html") ==
        ServiceError::make(Origin::usgs, ServiceErrc::unexpected_content,
                           "text/html"));
  CHECK(classify("upstream connect error") ==
        ServiceError::make(Origin::usgs, ServiceErrc::unknown,
                           "upstream connect error"));
  CHECK(classify(R"({"message":"other shape"})") ==
        ServiceError::make(Origin::usgs, ServiceErrc::unknown,
                           R"({"message":"other shape"})"));
  CHECK(classify("") ==
        ServiceError::make(Origin::usgs, ServiceErrc::unknown, ""));
  // Long server text is cut to 120 bytes.
  const ServiceError long_text = classify(R"({"code":"X","description":")" +
                                          std::string(500, 'a') + R"("})");
  CHECK(long_text.text().size() == ServiceError::max_text_bytes);
}

TEST_CASE("an accepted body is a page unless it names an error first",
          "[io][usgs][classify]") {
  for (const std::string_view page :
       {"continuous_chain_page1.json", "daily_full_properties.json",
        "series_site.json", "catalog_bbox.json", "continuous_empty.json"}) {
    CHECK(std::holds_alternative<Payload>(
        wu::classify_body(usgs_fixture(page), json_type)));
  }
  const auto refusal = wu::classify_body(usgs_fixture("error_invalid_key.json"),
                                         "application/json");
  REQUIRE(std::holds_alternative<ServiceError>(refusal));
  CHECK(std::get<ServiceError>(refusal).code() == ServiceErrc::api_key_invalid);
  CHECK(std::get<ServiceError>(
            wu::classify_body(
                "\xEF\xBB\xBF" + usgs_fixture("error_time_envelope.json"),
                json_type))
            .code() == ServiceErrc::range_exceeded);
  CHECK(
      std::get<ServiceError>(wu::classify_body("<!DOCTYPE html>", "text/html"))
          .code() == ServiceErrc::unexpected_content);
  // Not JSON, or JSON that does not parse: its parser says where.
  CHECK(std::holds_alternative<Payload>(wu::classify_body("", json_type)));
  CHECK(std::holds_alternative<Payload>(wu::classify_body("[1]", json_type)));
  CHECK(std::holds_alternative<Payload>(
      wu::classify_body("{\"code\":", json_type)));
}

TEST_CASE("classification stops at a page's first member",
          "[io][usgs][classify]") {
  // The first member of a page is its type: two SAX events (the object and
  // the key), however large the page.
  const std::string page = usgs_fixture("catalog_bbox.json");
  const auto parsed = mov::io::detail::json::parse(
      page, mov::io::detail::usgs::classify_keep(), mov::io::ReadContext{}, {},
      {.stop_at_unkept_first_member = true});
  REQUIRE(parsed.has_value());
  CHECK(parsed->events == 2);
}
