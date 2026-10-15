// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// detail::json::parse, the provider JSON reader (mov/io/detail/json.hpp):
// what a Keep keeps, where values start, the repeated-member, NUL and limit
// rules, and that its work is linear in the body, counted in SAX events (not
// time): 300,000 `{}` in each place a body can hold them.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/provider/coops.hpp"
#include "mov/core/stop_token.hpp"
#include "mov/io/detail/coops_json.hpp"
#include "mov/io/detail/json.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/coops.hpp"
#include "wire_test_support.hpp"

namespace {

namespace json = mov::io::detail::json;
namespace cc = mov::core::coops;
namespace wc = mov::io::wire::coops;
using mov::io::ParseErrc;
using mov::io::ReadContext;
using mov::test::parse_error_in;

constexpr std::array point_fields{
    json::Field{.name = "x", .keep = &json::leaf},
    json::Field{.name = "y", .keep = &json::leaf}};
constexpr json::Keep point{.fields = point_fields};
constexpr json::Keep points{.element = &point, .max_elements = 3};
constexpr json::Keep streamed_points{.element = &point};
constexpr std::array document_fields{
    json::Field{.name = "name", .keep = &json::leaf},
    json::Field{.name = "points", .keep = &points},
    json::Field{.name = "stream", .keep = &streamed_points, .streamed = true}};
constexpr json::Keep document{.fields = document_fields};

json::Document parsed(std::string_view body,
                      const json::OnElement& on_element = {}) {
  auto r = json::parse(body, document, ReadContext{}, on_element);
  REQUIRE(r.has_value());
  return *std::move(r);
}

// `count` copies of `item`, comma separated.
std::string repeated(std::string_view item, std::size_t count) {
  std::string out;
  out.reserve((item.size() + 1) * count);
  for (std::size_t i = 0; i < count; ++i) {
    out += i == 0 ? "" : ",";
    out += item;
  }
  return out;
}

constexpr std::size_t many = 300'000;

// Two SAX events per `{}`, plus a bounded overhead.
constexpr std::uint64_t linear_bound = (2 * many) + 64;

const wc::Series water_level = wc::Observation{
    .series = cc::Observed::six_minute, .datum = cc::Datum::msl};

}  // namespace

TEST_CASE("only what the Keep names is built", "[io][json]") {
  const auto d =
      parsed(R"({"skip": {"a": [1, 2, {"b": 3}]}, "name": "n", "points": [)"
             R"({"x": 1, "y": -2.5, "z": [1]}, {"x": "s"}]})");
  const json::Object* top = json::as_object(d.root);
  REQUIRE(top != nullptr);
  REQUIRE(top->size() == 2);
  CHECK(top->at(0).name == "name");
  CHECK(top->at(1).name == "points");
  const json::Value* list = json::member(d.root, "points");
  REQUIRE(list != nullptr);
  const json::Array* elements = json::as_array(*list);
  REQUIRE(elements != nullptr);
  REQUIRE(elements->size() == 2);
  const json::Value* x = json::member(elements->at(0), "x");
  REQUIRE(x != nullptr);
  CHECK(json::as_int(*x) == 1);
  CHECK(json::as_double(*json::member(elements->at(0), "y")) == -2.5);
  CHECK(json::member(elements->at(0), "z") == nullptr);
  CHECK(json::as_string(*json::member(elements->at(1), "x")) != nullptr);
}

TEST_CASE("a value knows where it starts", "[io][json]") {
  const std::string body =
      "{\"name\": \"a\\\"b\",\n \"points\": [ {\"x\": 12.5e1}, {\"x\": null},"
      " {\"x\": true} ]}";
  const auto d = parsed(body);
  const json::Value* name = json::member(d.root, "name");
  REQUIRE(name != nullptr);
  CHECK(name->offset == body.find("\"a\\\""));
  CHECK(*json::as_string(*name) == "a\"b");
  const json::Value& list = *json::member(d.root, "points");
  CHECK(list.offset == body.find('['));
  const json::Array& elements = *json::as_array(list);
  CHECK(elements.at(0).offset == body.find("{\"x\": 12.5e1"));
  CHECK(json::member(elements.at(0), "x")->offset == body.find("12.5e1"));
  CHECK(json::member(elements.at(1), "x")->offset == body.find("null"));
  CHECK(json::member(elements.at(2), "x")->offset == body.find("true"));
  CHECK(json::as_object(d.root)->at(1).key_offset == body.find("\"points\""));
}

TEST_CASE("the streamed array is handed over one element at a time",
          "[io][json]") {
  std::vector<std::size_t> offsets;
  std::vector<std::size_t> indices;
  const std::string body =
      R"({"stream": [{"x": 1}, 2, {"y": [3]}], "name": 1})";
  const auto d = parsed(
      body,
      [&](std::size_t index,
          const json::Value& element) -> std::expected<void, mov::io::Error> {
        indices.push_back(index);
        offsets.push_back(element.offset);
        return {};
      });
  CHECK(indices == std::vector<std::size_t>{0, 1, 2});
  CHECK(offsets == std::vector<std::size_t>{body.find("{\"x\""),
                                            body.find("2,"),
                                            body.find("{\"y\"")});
  // The document keeps an empty array in its place.
  const json::Value* stream = json::member(d.root, "stream");
  REQUIRE(stream != nullptr);
  CHECK(json::as_array(*stream)->empty());
  // An error from the handler stops the parse.
  const auto stopped = json::parse(
      body, document, ReadContext{},
      [](std::size_t,
         const json::Value& element) -> std::expected<void, mov::io::Error> {
        return std::unexpected{mov::io::Error{
            json::error_at(ParseErrc::bad_member, "", element.offset)}};
      });
  CHECK(parse_error_in(stopped).code() == ParseErrc::bad_member);
}

TEST_CASE("a kept member repeated is refused at its object", "[io][json]") {
  const std::string body = R"({"points": [{"x": 1, "y": 2, "x": 3}]})";
  const auto r = json::parse(body, document, ReadContext{});
  CHECK(parse_error_in(r).code() == ParseErrc::bad_member);
  CHECK(parse_error_in(r).column() == body.find("{\"x\""));
  // Unread members may repeat.
  CHECK(json::parse(R"({"other": 1, "other": 2})", document, ReadContext{})
            .has_value());
}

TEST_CASE("a kept array holds at most its max_elements", "[io][json]") {
  const std::string body = R"({"points": [{}, {}, {}, {}]})";
  const auto r = json::parse(body, document, ReadContext{});
  CHECK(parse_error_in(r).code() == ParseErrc::too_large);
  CHECK(parse_error_in(r).column() == body.find('['));
  CHECK(parse_error_in(r).context() == "4 elements, limit 3");
  CHECK(json::parse(R"({"points": [{}, {}, {}]})", document, ReadContext{})
            .has_value());
  // Scalars count too.
  CHECK(parse_error_in(json::parse(R"({"points": [1, -2, "3", null]})",
                                   document, ReadContext{}))
            .code() == ParseErrc::too_large);
}

TEST_CASE("integers are exact, and doubles when asked", "[io][json]") {
  const auto d = parsed(R"({"points": [{"x": -5, "y": 1.5}]})");
  const json::Value& point_value =
      json::as_array(*json::member(d.root, "points"))->at(0);
  const json::Value& x = *json::member(point_value, "x");
  CHECK(json::as_int(x) == -5);
  CHECK(json::as_double(x) == -5.0);
  CHECK(not json::as_int(*json::member(point_value, "y")));
  CHECK(not json::as_double(json::Value{.data = std::string{"1"}}));
}

TEST_CASE("bodies refused before and while parsing", "[io][json]") {
  const auto code = [](std::string_view body) {
    return parse_error_in(json::parse(body, document, ReadContext{})).code();
  };
  CHECK(code("") == ParseErrc::empty_input);
  CHECK(code("\xEF\xBB\xBF  ") == ParseErrc::empty_input);
  CHECK(code("{\"a\": 1") == ParseErrc::not_json);
  CHECK(code("{} {}") == ParseErrc::not_json);
  CHECK(code(std::string(30, '[')) == ParseErrc::too_large);
  using namespace std::string_literals;
  const std::string with_nul = "{}"s + '\0' + "x";
  CHECK(code(with_nul) == ParseErrc::not_json);
  CHECK(
      parse_error_in(json::parse(with_nul, document, ReadContext{})).column() ==
      2);
  // A number past a double is out of range, at the number.
  const std::string huge = R"({"name": 1e999})";
  CHECK(code(huge) == ParseErrc::out_of_range);
  CHECK(parse_error_in(json::parse(huge, document, ReadContext{})).column() ==
        huge.find("1e999"));
  const std::string bad = "{\"name\": nul}";  // a cut-off literal
  CHECK(parse_error_in(json::parse(bad, document, ReadContext{})).column() ==
        bad.find('}'));
}

TEST_CASE("a byte order mark is skipped, and offsets stay the body's",
          "[io][json]") {
  const std::string body = "\xEF\xBB\xBF{\"name\": \"x\"}";
  const auto d = parsed(body);
  CHECK(json::member(d.root, "name")->offset == body.find("\"x\""));
}

TEST_CASE("a stop request is polled from the first event", "[io][json]") {
  ReadContext ctx;
  ctx.stop = mov::core::StopToken{[] { return true; }};
  const auto r = json::parse(R"({"name": 1})", document, ctx);
  REQUIRE(not r.has_value());
  CHECK(std::holds_alternative<mov::core::Cancelled>(r.error()));
}

// ---- 300,000 `{}`: linear work, or an early refusal
// -----------------------------

TEST_CASE("300k elements of the streamed data array", "[io][json][limits]") {
  const std::string body = R"({"data": [)" + repeated("{}", many) + "]}";
  // Accepted one at a time: linear.
  std::size_t handed = 0;
  const auto all = json::parse(
      body, mov::io::detail::coops::data_keep(water_level), ReadContext{},
      [&handed](std::size_t,
                const json::Value&) -> std::expected<void, mov::io::Error> {
        ++handed;
        return {};
      });
  REQUIRE(all.has_value());
  CHECK(handed == many);
  CHECK(all->events <= linear_bound);
  // The reader refuses the first record (no t) at once.
  const auto refused =
      mov::io::detail::coops::parse_data(body, water_level, ReadContext{});
  CHECK(parse_error_in(refused).code() == ParseErrc::bad_member);
}

TEST_CASE("300k elements in a member the format does not read",
          "[io][json][limits]") {
  const std::string body = R"({"metadata": [)" + repeated("{}", many) +
                           R"(], "data": [{"t": "2026-10-01 00:00", "v": "1",)"
                           R"( "x": [)" +
                           repeated("{}", many) + "]}]}";
  const auto d = json::parse(
      body, mov::io::detail::coops::data_keep(water_level), ReadContext{});
  REQUIRE(d.has_value());
  CHECK(d->events <= 2 * linear_bound);
  const auto read =
      mov::io::detail::coops::parse_data(body, water_level, ReadContext{});
  REQUIRE(read.has_value());
  CHECK(read->value.size() == 1);
}

TEST_CASE("300k elements in a kept container (a station's sensors)",
          "[io][json][limits]") {
  const std::string body =
      R"({"stations": [{"id": "8729840", "lat": 1, "lng": 2, "sensors":)"
      R"( {"sensors": [)" +
      repeated("{}", many) + "]}}]}";
  // Refused at the 257th sensor.
  const auto d = json::parse(body, mov::io::detail::coops::station_list_keep(),
                             ReadContext{});
  CHECK(parse_error_in(d).code() == ParseErrc::too_large);
  CHECK(parse_error_in(d).context() == "257 elements, limit 256");
  const auto read = wc::parse_stations(body, ReadContext{});
  CHECK(parse_error_in(read).code() == ParseErrc::too_large);
}

TEST_CASE("300k elements of the station and datum lists",
          "[io][json][limits]") {
  const std::string stations =
      R"({"stations": [)" + repeated("{}", many) + "]}";
  const auto list = json::parse(
      stations, mov::io::detail::coops::prediction_list_keep(), ReadContext{});
  REQUIRE(list.has_value());
  CHECK(list->events <= linear_bound);
  // Each station without an id is left out and counted.
  const auto skipped = wc::parse_prediction_stations(stations, ReadContext{});
  REQUIRE(skipped.has_value());
  CHECK(skipped->value.empty());
  REQUIRE(skipped->warnings.size() == 1);
  CHECK(skipped->warnings[0].count == many);
  // datums.json refuses its first row without a name.
  const std::string datums =
      R"({"units": "meters", "datums": [)" + repeated("{}", many) + "]}";
  CHECK(parse_error_in(wc::parse_datums(datums, ReadContext{})).code() ==
        ParseErrc::bad_member);
}

TEST_CASE("300k elements before or inside an error body",
          "[io][json][limits]") {
  // The first member is not an error: classification stops there.
  const std::string data_first =
      R"({"metadata": [)" + repeated("{}", many) +
      R"(], "error": {"message": "No data was found"}})";
  const auto early =
      json::parse(data_first, mov::io::detail::coops::classify_keep(),
                  ReadContext{}, {}, {.stop_at_unkept_first_member = true});
  REQUIRE(early.has_value());
  CHECK(early->events <= 8);
  CHECK(std::holds_alternative<mov::io::wire::Payload>(
      wc::classify_body(wc::Api::data, data_first, "application/json")));
  // Inside the error: read past, linear.
  const std::string inside = R"({"error": {"junk": [)" + repeated("{}", many) +
                             R"(], "message": "No data was found"}})";
  const auto full =
      json::parse(inside, mov::io::detail::coops::classify_keep(),
                  ReadContext{}, {}, {.stop_at_unkept_first_member = true});
  REQUIRE(full.has_value());
  CHECK(full->events <= linear_bound);
  CHECK(std::holds_alternative<mov::io::wire::NoData>(
      wc::classify_body(wc::Api::data, inside, "application/json")));
}
