// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// coops::classify_body against every recorded CO-OPS refusal
// (docs/providers-design.md section 4.3).

#include <catch2/catch_test_macros.hpp>
#include <string>
#include <string_view>
#include <variant>

#include "mov/io/wire/coops.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"
#include "wire_test_support.hpp"

namespace {

using mov::io::wire::BodyClass;
using mov::io::wire::NoData;
using mov::io::wire::Origin;
using mov::io::wire::Payload;
using mov::io::wire::ServiceErrc;
using mov::io::wire::ServiceError;
using mov::io::wire::coops::Api;
using mov::io::wire::coops::classify_body;
using mov::test::coops_fixture;

constexpr std::string_view json_type = "application/json;charset=UTF-8";

BodyClass classify(std::string_view body, Api api = Api::data) {
  return classify_body(api, body, json_type);
}

ServiceError refusal(const BodyClass& c) {
  const auto* error = std::get_if<ServiceError>(&c);
  REQUIRE(error != nullptr);
  return *error;
}

ServiceErrc code_of(std::string_view fixture) {
  return refusal(classify(coops_fixture(fixture))).code();
}

}  // namespace

TEST_CASE("recorded data bodies are payloads", "[io][wire][coops]") {
  for (const std::string_view name :
       {"water_level.json", "water_level_mllw.json", "predictions.json",
        "wind.json", "salinity.json", "air_gap.json", "datums.json",
        "datums_null.json", "stations_waterlevels.json",
        "stations_tidepredictions.json"}) {
    INFO(name);
    CHECK(std::holds_alternative<Payload>(classify(coops_fixture(name))));
  }
}

TEST_CASE("no data is not a failure", "[io][wire][coops]") {
  CHECK(std::holds_alternative<NoData>(
      classify(coops_fixture("error_no_data.json"))));
  // A subordinate station asked for an interval it does not have.
  CHECK(std::holds_alternative<NoData>(
      classify(coops_fixture("error_no_predictions.json"))));
}

TEST_CASE("each recorded refusal has its code", "[io][wire][coops]") {
  CHECK(code_of("error_range.json") == ServiceErrc::range_exceeded);
  CHECK(code_of("error_station.json") == ServiceErrc::station_or_system);
  CHECK(code_of("error_datum_missing.json") == ServiceErrc::datum_unavailable);
  CHECK(code_of("error_datum_invalid.json") == ServiceErrc::datum_unavailable);
  CHECK(code_of("error_datum_not_at_station.json") ==
        ServiceErrc::datum_unavailable);
  CHECK(code_of("error_wrong_date.json") == ServiceErrc::invalid_parameter);
  // Plain text, not JSON.
  CHECK(code_of("error_bad_product.txt") == ServiceErrc::invalid_parameter);
  // The gateway's own failure: transient, retried like a 5xx.
  CHECK(code_of("gateway_error.json") == ServiceErrc::gateway);
  CHECK(refusal(classify(coops_fixture("mdapi_error_type.json"), Api::metadata))
            .code() == ServiceErrc::invalid_parameter);
}

TEST_CASE("a refusal keeps the service's words, trimmed and cut",
          "[io][wire][coops]") {
  const ServiceError station =
      refusal(classify(coops_fixture("error_station.json")));
  CHECK(station.text() ==
        "The station is not a valid station or there is system error.");
  CHECK(station.origin() == Origin::coops_data);
  const ServiceError metadata =
      refusal(classify(coops_fixture("mdapi_error_type.json"), Api::metadata));
  CHECK(metadata.origin() == Origin::coops_metadata);
  CHECK(metadata.text().size() == ServiceError::max_text_bytes);
  CHECK(metadata.text().starts_with("Invalid type specified: valid options"));
  const ServiceError gateway =
      refusal(classify(coops_fixture("gateway_error.json")));
  CHECK(gateway.text() == "Network error communicating with endpoint");
}

TEST_CASE("markup is unexpected content with its content type",
          "[io][wire][coops]") {
  const auto c = classify_body(
      Api::data, "<html><body>Down for maintenance</body></html>", "text/html");
  const ServiceError e = refusal(c);
  CHECK(e.code() == ServiceErrc::unexpected_content);
  CHECK(e.text() == "text/html");
}

TEST_CASE("shapes the table does not know", "[io][wire][coops]") {
  // An error object without a message, or with another member type.
  CHECK(refusal(classify(R"({"error": {}})")).code() == ServiceErrc::unknown);
  const ServiceError number = refusal(classify(R"({"error": 5})"));
  CHECK(number.text().empty());
  CHECK(refusal(classify(R"({"error": "Range Limit Exceeded"})")).code() ==
        ServiceErrc::range_exceeded);
  CHECK(
      refusal(classify(R"({"error": {"message": "something new"}})")).code() ==
      ServiceErrc::unknown);
  // A gateway message next to data is data.
  CHECK(std::holds_alternative<Payload>(
      classify(R"({"message": "x", "data": []})")));
  // errorMsg that is not a string, message that is not a string.
  CHECK(std::holds_alternative<Payload>(classify(R"({"errorMsg": 1})")));
  CHECK(std::holds_alternative<Payload>(classify(R"({"message": null})")));
  // Plain text that matches nothing.
  CHECK(refusal(classify("Service Unavailable")).code() ==
        ServiceErrc::unknown);
}

TEST_CASE("bodies a parser must refuse are payloads", "[io][wire][coops]") {
  CHECK(std::holds_alternative<Payload>(classify("")));
  CHECK(std::holds_alternative<Payload>(classify("   \n")));
  CHECK(std::holds_alternative<Payload>(classify("[1, 2]")));
  CHECK(std::holds_alternative<Payload>(classify(R"({"error": )")));
  CHECK(std::holds_alternative<Payload>(classify(R"({"data": [}})")));
  // Too deep for the pre-scan: the parser reports it.
  const std::string deep =
      "{\"error\": " + std::string(40, '[') + std::string(40, ']') + "}";
  CHECK(std::holds_alternative<Payload>(classify(deep)));
  using namespace std::string_literals;
  CHECK(std::holds_alternative<Payload>(
      classify(R"({"error": "Range Limit Exceeded"})"s + '\0')));
}

TEST_CASE("a byte order mark is skipped, as the parsers skip it",
          "[io][wire][coops]") {
  const std::string bom = "\xEF\xBB\xBF";
  CHECK(refusal(classify(bom + coops_fixture("error_station.json"))).code() ==
        ServiceErrc::station_or_system);
  CHECK(refusal(classify(bom + " Wrong Product: x")).code() ==
        ServiceErrc::invalid_parameter);
  CHECK(std::holds_alternative<Payload>(
      classify(bom + coops_fixture("water_level.json"))));
}

TEST_CASE("an error body names its error first", "[io][wire][coops]") {
  // A body whose first member is not error, errorMsg or message is read no
  // further: it is data, whatever follows.
  CHECK(std::holds_alternative<Payload>(classify(
      R"({"metadata": {"id": "1"}, "error": {"message": "No data was found"}})")));
  CHECK(std::holds_alternative<NoData>(classify(
      R"({"error": {"message": "No data was found"}, "metadata": {}})")));
  // A lone message is the gateway's, whatever it says.
  CHECK(refusal(classify(R"({"message": "Range Limit Exceeded"})")).code() ==
        ServiceErrc::gateway);
}
