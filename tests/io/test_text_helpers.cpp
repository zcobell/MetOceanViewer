// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Text helpers added with the IMEDS reader for the other text readers to adopt:
// next_word with a separator predicate, split_on_into, LineCursor's blank-line
// helpers, replace_invalid_utf8 and uniquify_ids.

#include <array>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/io/detail/line_cursor.hpp"
#include "mov/io/detail/station_names.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/warning.hpp"

namespace {

namespace detail = mov::io::detail;
using namespace std::string_view_literals;

constexpr bool comma_or_semicolon(char c) noexcept {
  return c == ',' or c == ';';
}

std::vector<std::string_view> words_of(std::string_view text) {
  std::vector<std::string_view> out;
  while (const auto word = detail::next_word(text, comma_or_semicolon)) {
    out.push_back(*word);
  }
  return out;
}

}  // namespace

TEST_CASE("next_word with a separator predicate", "[io][text]") {
  using V = std::vector<std::string_view>;
  CHECK(words_of("a,b;c") == V{"a", "b", "c"});
  CHECK(words_of(",,a;;b,") == V{"a", "b"});  // runs are one separator
  CHECK(words_of("").empty());
  CHECK(words_of(",;,").empty());
  CHECK(words_of("a b,c") == V{"a b", "c"});  // a space is not a separator here

  std::string_view rest = "ab,cd";
  CHECK(detail::next_word(rest, comma_or_semicolon) == "ab");
  CHECK(rest == ",cd");
  // The one-argument form still splits on white space.
  std::string_view spaced = "  x \t y";
  CHECK(detail::next_word(spaced) == "x");
  CHECK(detail::next_word(spaced) == "y");
  CHECK(detail::next_word(spaced) == std::nullopt);
}

namespace {

constexpr bool first_field_at_compile_time() {
  std::string_view text = ";x,y";
  const auto word = detail::next_word(text, comma_or_semicolon);
  std::array<std::string_view, 2> fields{};
  return word == "x" and detail::split_on_into("p,q", ',', fields) == 2 and
         fields[1] == "q";
}
static_assert(first_field_at_compile_time());

}  // namespace

TEST_CASE("split_on_into keeps empty fields and reports overflow",
          "[io][text]") {
  std::array<std::string_view, 3> out{};
  CHECK(detail::split_on_into("a,b,c", ',', out) == 3);
  CHECK(out[0] == "a");
  CHECK(out[2] == "c");
  CHECK(detail::split_on_into("a,,c", ',', out) == 3);
  CHECK(out[1].empty());
  CHECK(detail::split_on_into("", ',', out) == 1);
  CHECK(detail::split_on_into(",", ',', out) == 2);
  CHECK(detail::split_on_into("a,b,c,d", ',', out) == 4);  // one more than room
  CHECK(detail::split_on_into("a,b,c,d,e,f", ',', out) == 4);  // not counted on
  std::array<std::string_view, 0> none{};
  CHECK(detail::split_on_into("", ',', none) == 1);
  CHECK(detail::split_on_into("x,y", ',', none) == 1);  // out.size() + 1
  // The result agrees with split_on.
  for (const std::string_view text : {"", "a", ",", "a,b", "a,,b,", ",,,"}) {
    std::array<std::string_view, 8> wide{};
    const auto expected = detail::split_on(text, ',');
    REQUIRE(detail::split_on_into(text, ',', wide) == expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i) {
      CHECK(wide[i] == expected[i]);
    }
  }
}

TEST_CASE("LineCursor skips blank lines and peeks at the next one",
          "[io][text]") {
  detail::LineCursor cursor{"a\n\n  \t\nb\r\n\nc"};
  CHECK(cursor.peek_blank() == std::optional<bool>{false});
  const auto a = cursor.next_nonblank();
  CHECK((a.has_value() and a->text == "a"));
  CHECK(cursor.peek_blank() == std::optional<bool>{true});  // the empty line
  const auto b = cursor.next_nonblank();
  CHECK((b.has_value() and b->text == "b"));
  CHECK((b.has_value() and b->number == 4));  // blank lines are counted
  CHECK(cursor.peek_blank() == std::optional<bool>{true});
  const auto c = cursor.next_nonblank();
  CHECK((c.has_value() and c->text == "c"));
  CHECK((c.has_value() and c->number == 6));
  CHECK(cursor.peek_blank() == std::nullopt);
  CHECK(cursor.next_nonblank() == std::nullopt);

  detail::LineCursor only_blank{"\n \n\r\n"};
  CHECK(only_blank.next_nonblank() == std::nullopt);
  CHECK(only_blank.at_end());
  const detail::LineCursor empty{""};
  CHECK(empty.peek_blank() == std::nullopt);
  CHECK(empty.peek_nonblank() == std::nullopt);
}

TEST_CASE("is_blank and peek_nonblank", "[io][text]") {
  CHECK(detail::is_blank(""));
  CHECK(detail::is_blank(" \t\r\n\v\f"));
  CHECK(not detail::is_blank(" a "));
  CHECK(not detail::is_blank("\xC2\xA0"));  // a no-break space is a letter

  detail::LineCursor cursor{"\n \nfirst\nsecond\n\n"};
  const auto ahead = cursor.peek_nonblank();
  CHECK((ahead.has_value() and ahead->text == "first"));
  CHECK((ahead.has_value() and ahead->number == 3));
  // Nothing was consumed.
  CHECK(cursor.lines_read() == 0);
  CHECK(cursor.next_nonblank() == ahead);
  const auto second = cursor.next_nonblank();
  CHECK((second.has_value() and second->text == "second"));
  CHECK(cursor.peek_nonblank() == std::nullopt);  // only the blank tail left
  CHECK(not cursor.at_end());
}

TEST_CASE("replace_invalid_utf8 returns station text", "[io][text]") {
  const auto ok = detail::replace_invalid_utf8(
      "Ba\xC3\xAD"
      "a");
  CHECK(not ok.replaced);
  CHECK(ok.text.view() ==
        "Ba\xC3\xAD"
        "a");
  const auto bad = detail::replace_invalid_utf8(
      "a\xFF"
      "b\xC3");
  CHECK(bad.replaced);
  CHECK(bad.text.view() ==
        "a\xEF\xBF\xBD"
        "b\xEF\xBF\xBD");
  const auto nul = detail::replace_invalid_utf8("a\0b"sv);
  CHECK(nul.replaced);
  CHECK(nul.text.view() ==
        "a\xEF\xBF\xBD"
        "b");
  CHECK(detail::replace_invalid_utf8("").text.empty());
}

TEST_CASE("uniquify_ids numbers repeats and skips taken ids", "[io][text]") {
  const std::vector<std::string> names{"A", "B", "A", "A", "A#2", "B"};
  const auto unique = detail::uniquify_ids(names);
  CHECK(unique.ids ==
        std::vector<std::string>{"A", "B", "A#2", "A#3", "A#2#2", "B#2"});
  CHECK(unique.renamed ==
        std::vector{detail::RenamedName{.name = "A", .count = 2},
                    detail::RenamedName{.name = "A#2", .count = 1},
                    detail::RenamedName{.name = "B", .count = 1}});
  CHECK(detail::uniquify_ids({}).ids.empty());
}

TEST_CASE("append_if_counted appends only a count above zero",
          "[io][warning]") {
  std::vector<mov::io::Warning> warnings;
  mov::io::append_if_counted(
      warnings, {.code = mov::io::WarningCode::epoch_used, .count = 0});
  CHECK(warnings.empty());
  mov::io::append_if_counted(
      warnings,
      {.code = mov::io::WarningCode::epoch_used, .subject = "x", .count = 2});
  REQUIRE(warnings.size() == 1);
  CHECK(warnings.front().count == 2);
}
