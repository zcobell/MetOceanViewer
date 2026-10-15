// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The JSON text helpers of the station catalog and the series JSON
// (mov/io/detail/json_write.hpp), and the SAX reader's unknown_names.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <chrono>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "mov/core/time.hpp"
#include "mov/io/detail/json.hpp"
#include "mov/io/detail/json_write.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "wire_test_support.hpp"

namespace {

namespace jt = mov::io::detail::json_text;
namespace json = mov::io::detail::json;
using mov::test::utc;

std::string string_text(std::string_view s) {
  std::string out;
  jt::append_string(out, s);
  return out;
}

std::string fixed(double v, int decimals) {
  std::string out;
  jt::append_fixed(out, v, decimals);
  return out;
}

std::string shortest(double v) {
  std::string out;
  jt::append_shortest(out, v);
  return out;
}

std::chrono::sys_days day(int y, unsigned m, unsigned d) {
  using namespace std::chrono;
  return sys_days{year{y} / month{m} / std::chrono::day{d}};
}

}  // namespace

TEST_CASE("strings are escaped as JSON requires", "[io][json]") {
  CHECK(string_text("Pensacola") == "\"Pensacola\"");
  CHECK(string_text("") == "\"\"");
  CHECK(string_text("a\"b\\c") == "\"a\\\"b\\\\c\"");
  CHECK(string_text("\b\f\n\r\t") == "\"\\b\\f\\n\\r\\t\"");
  CHECK(string_text(std::string_view{"\0\x01\x1f\x7f", 4}) ==
        "\"\\u0000\\u0001\\u001f\\u007f\"");
  // UTF-8 passes through; '/' needs no escape.
  CHECK(string_text("Mühlenstraße / 東京") == "\"Mühlenstraße / 東京\"");
}

TEST_CASE("fixed numbers are rounded and printed without trailing zeros",
          "[io][json]") {
  CHECK(fixed(-87.2112, 6) == "-87.2112");
  CHECK(fixed(30.0, 6) == "30");
  CHECK(fixed(30.404412345, 6) == "30.404412");
  CHECK(fixed(0.19500000000000028, 4) == "0.195");
  CHECK(fixed(-0.0, 6) == "0");
  CHECK(fixed(-0.0000001, 6) == "0");
  CHECK(fixed(1e300, 4).size() == 301);
  CHECK(fixed(123.0, 0) == "123");

  CHECK(jt::rounded(0.19500000000000028, 4) == 0.195);
  CHECK(jt::rounded(-0.0000001, 6) == 0.0);
  CHECK(not std::signbit(jt::rounded(-0.0000001, 6)));
  // Idempotent: the rounded value prints as itself.
  for (const double v : {-87.21123456, 1.0 / 3.0, 179.9999996, 1e-7}) {
    const double once = jt::rounded(v, 6);
    CHECK(jt::rounded(once, 6) == once);
  }
}

TEST_CASE("shortest numbers read back exactly", "[io][json]") {
  CHECK(shortest(0.1) == "0.1");
  CHECK(shortest(2.0) == "2");
  CHECK(shortest(1e300) == "1e+300");
  CHECK(shortest(std::numeric_limits<double>::max()) ==
        "1.7976931348623157e+308");
  CHECK(shortest(5e-324) == "5e-324");
}

TEST_CASE("dates and timestamps have four-digit years", "[io][json]") {
  std::string out;
  jt::append_date(out, day(1923, 4, 30));
  CHECK(out == "\"1923-04-30\"");
  out.clear();
  jt::append_date(out, day(0, 1, 1));
  CHECK(out == "\"0000-01-01\"");
  out.clear();
  jt::append_timestamp(
      out, utc(2026, 10, 9, 7, 0, 1) + std::chrono::milliseconds{999});
  CHECK(out == "\"2026-10-09T07:00:01Z\"");  // floored
  out.clear();
  jt::append_timestamp_ms(
      out, utc(1969, 12, 31, 23, 59, 59) + std::chrono::milliseconds{5});
  CHECK(out == "\"1969-12-31T23:59:59.005Z\"");

  CHECK(jt::four_digit_year(day(9999, 12, 31)));
  CHECK(not jt::four_digit_year(day(10000, 1, 1)));
  CHECK(jt::four_digit_year(day(0, 1, 1)));
  CHECK(not jt::four_digit_year(day(-1, 12, 31)));
  CHECK(jt::four_digit_year(utc(9999, 12, 31, 23, 59, 59)));
  CHECK(not jt::four_digit_year(utc(10000, 1, 1)));
}

TEST_CASE("dates and timestamps are parsed exactly", "[io][json]") {
  CHECK(jt::parse_date("2026-10-09") == day(2026, 10, 9));
  CHECK(jt::parse_date("2024-02-29") == day(2024, 2, 29));
  for (const std::string_view bad :
       {"2026-02-29", "2026-13-01", "2026-1-01", "2026-10-09 ", " 2026-10-09",
        "2026-10-09T00:00:00Z", "26-10-09", "", "abcd-ef-gh"}) {
    CAPTURE(bad);
    CHECK(not jt::parse_date(bad).has_value());
  }
  CHECK(jt::parse_timestamp("2026-10-09T07:00:01Z") ==
        utc(2026, 10, 9, 7, 0, 1));
  for (const std::string_view bad :
       {"2026-10-09T07:00:01", "2026-10-09 07:00:01Z", "2026-10-09T07:00Z",
        "2026-10-09T07:00:01.5Z", "2026-10-09T24:00:00Z",
        "2026-10-09T07:00:60Z", "2026-10-09T07:00:01+00:00",
        "2026-10-09T07:00:01Zx", "2026-10-09"}) {
    CAPTURE(bad);
    CHECK(not jt::parse_timestamp(bad).has_value());
  }
}

TEST_CASE("unknown_names keeps the members a Keep does not name, by name",
          "[io][json]") {
  static constexpr std::array fields{
      json::Field{.name = "a", .keep = &json::leaf}};
  static constexpr json::Keep open{.fields = fields, .unknown_names = true};
  static constexpr json::Keep closed{.fields = fields};
  const std::string_view body = R"({"a":1,"b":{"deep":[1,2]},"c":"x"})";

  const auto kept = json::parse(body, open, mov::io::ReadContext{});
  REQUIRE(kept.has_value());
  const json::Object* members = json::as_object(kept->root);
  REQUIRE(members != nullptr);
  REQUIRE(members->size() == 3);
  CHECK(members->at(1).name == "b");
  CHECK(members->at(1).key_offset == 7);
  // A container is kept empty.
  const json::Object* b = json::as_object(members->at(1).value);
  REQUIRE(b != nullptr);
  CHECK(b->empty());
  CHECK(*json::as_string(members->at(2).value) == "x");

  const auto skipped = json::parse(body, closed, mov::io::ReadContext{});
  REQUIRE(skipped.has_value());
  CHECK(json::as_object(skipped->root)->size() == 1);

  // A repeated unknown member is refused like a repeated kept one.
  const auto repeated =
      json::parse(R"({"b":1,"b":2})", open, mov::io::ReadContext{});
  CHECK(mov::test::parse_error_in(repeated).code() ==
        mov::io::ParseErrc::bad_member);

  // An object whose Keep names nothing but keeps unknown names.
  static constexpr json::Keep names_only{.unknown_names = true};
  const auto only =
      json::parse(R"({"x":true})", names_only, mov::io::ReadContext{});
  REQUIRE(only.has_value());
  REQUIRE(json::as_object(only->root)->size() == 1);
  CHECK(json::as_object(only->root)->front().name == "x");
}
