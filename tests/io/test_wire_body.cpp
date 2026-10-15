// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The body checks every provider response passes (docs/providers-design.md
// section 4.2) and the service error value.

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <string>
#include <string_view>

#include "mov/io/error.hpp"
#include "mov/io/wire/body.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"

namespace {

using mov::io::ParseErrc;
using mov::io::wire::check_json_depth;
using mov::io::wire::max_json_depth;
using mov::io::wire::Origin;
using mov::io::wire::ServiceErrc;
using mov::io::wire::ServiceError;
using mov::io::wire::sniff_markup;

std::string nested(std::size_t depth) {
  return std::string(depth, '[') + std::string(depth, ']');
}

}  // namespace

TEST_CASE("the depth pre-scan lets the deepest allowed nesting through",
          "[io][wire]") {
  CHECK(check_json_depth(nested(max_json_depth)).has_value());
  CHECK(check_json_depth("{}").has_value());
  CHECK(check_json_depth("").has_value());
  CHECK(check_json_depth("not json at all").has_value());
}

TEST_CASE("the depth pre-scan refuses one level more, at the bracket",
          "[io][wire]") {
  const std::string text = "\n " + nested(max_json_depth + 1);
  const auto checked = check_json_depth(text);
  REQUIRE(not checked.has_value());
  CHECK(checked.error().code() == ParseErrc::too_large);
  CHECK(checked.error().line() == 2);
  CHECK(checked.error().column() == 1 + max_json_depth);
  CHECK(checked.error().context() == "17 levels, limit 16");
  CHECK(not check_json_depth(nested(3), 2).has_value());
  CHECK(check_json_depth(nested(2), 2).has_value());
}

TEST_CASE("the depth pre-scan ignores brackets inside strings", "[io][wire]") {
  const std::string inside = "[\"" + std::string(100, '[') + "\"]";
  CHECK(check_json_depth(inside).has_value());
  // An escaped quote does not end the string.
  const std::string escaped = R"(["\"[[[[[[[[[[[[[[[[[[[[[[["])";
  CHECK(check_json_depth(escaped).has_value());
  // An escaped backslash does.
  const std::string closed = R"(["\\", )" + nested(max_json_depth) + "]";
  CHECK(not check_json_depth(closed).has_value());
}

TEST_CASE("the depth pre-scan counts nesting, not brackets", "[io][wire]") {
  std::string wide;
  for (int i = 0; i < 1000; ++i) {
    wide += "[]";
  }
  CHECK(check_json_depth("[" + wide + "]").has_value());
  // Unbalanced closers never drive the count below zero.
  CHECK(check_json_depth("]]]]]]]]" + nested(max_json_depth)).has_value());
}

TEST_CASE("markup where JSON was expected is unexpected content",
          "[io][wire]") {
  const auto page =
      sniff_markup(Origin::coops_data, "\r\n  <!DOCTYPE html><html>Maintenance",
                   "text/html; charset=UTF-8");
  REQUIRE(page.has_value());
  if (page) {
    CHECK(page->code() == ServiceErrc::unexpected_content);
    CHECK(page->origin() == Origin::coops_data);
    CHECK(page->text() == "text/html; charset=UTF-8");
  }
  CHECK(not sniff_markup(Origin::usgs, R"({"a": "<b>"})", "x").has_value());
  CHECK(not sniff_markup(Origin::usgs, "", "text/html").has_value());
  CHECK(not sniff_markup(Origin::usgs, "   ", "text/html").has_value());
}

TEST_CASE("a service error's text is trimmed and cut on a UTF-8 boundary",
          "[io][wire]") {
  const ServiceError trimmed = ServiceError::make(
      Origin::coops_data, ServiceErrc::unknown, "  message \r\n\n");
  CHECK(trimmed.text() == "message");

  // 119 ASCII bytes, then a two-byte character that would end at byte 121.
  const std::string long_text = std::string(119, 'a') + "\xC3\xA9" + "tail";
  const ServiceError cut =
      ServiceError::make(Origin::usgs, ServiceErrc::unknown, long_text);
  CHECK(cut.text() == std::string(119, 'a'));
  CHECK(cut.text().size() <= ServiceError::max_text_bytes);

  const std::string exact(ServiceError::max_text_bytes, 'b');
  const ServiceError whole =
      ServiceError::make(Origin::usgs, ServiceErrc::unknown, exact);
  CHECK(whole.text() == exact);
  CHECK(trimmed == ServiceError::make(Origin::coops_data, ServiceErrc::unknown,
                                      "message"));
  CHECK(trimmed != ServiceError::make(Origin::coops_metadata,
                                      ServiceErrc::unknown, "message"));
}
