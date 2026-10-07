// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <array>
#include <bit>
#include <catch2/catch_template_test_macros.hpp>
#include <catch2/catch_test_macros.hpp>
#include <clocale>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <initializer_list>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "mov/io/detail/line_cursor.hpp"
#include "mov/io/detail/parse_at.hpp"
#include "mov/io/detail/text.hpp"

namespace {

namespace detail = mov::io::detail;
using detail::NumberError;
using namespace std::string_literals;
using namespace std::string_view_literals;

bool inside(std::string_view piece, std::string_view whole) {
  return piece.data() >= whole.data() and
         piece.data() + piece.size() <= whole.data() + whole.size();
}

}  // namespace

// ---- splitting -------------------------------------------------------------

TEST_CASE("split_ws returns the runs of non-whitespace", "[io][detail][text]") {
  using Tokens = std::vector<std::string_view>;
  CHECK(detail::split_ws("").empty());
  CHECK(detail::split_ws(" \t\r\n\v\f ").empty());
  CHECK(detail::split_ws("a") == Tokens{"a"});
  CHECK(detail::split_ws("  a  b\tc\r\n d ") == Tokens{"a", "b", "c", "d"});
  CHECK(detail::split_ws("2005-08-28 12:00:00 29.98") ==
        Tokens{"2005-08-28", "12:00:00", "29.98"});
  // Only ASCII is whitespace: a no-break space (U+00A0) is part of a token,
  // and so is a NUL.
  CHECK(detail::split_ws("a\xC2\xA0 b") == Tokens{"a\xC2\xA0", "b"});
  CHECK(detail::split_ws("a\0b c"sv) == Tokens{"a\0b"sv, "c"});
}

TEST_CASE("split_ws tokens are views of the input", "[io][detail][text]") {
  const std::string text = "  alpha  beta ";
  const auto tokens = detail::split_ws(text);
  REQUIRE(tokens.size() == 2);
  for (const std::string_view token : tokens) {
    CHECK(inside(token, text));
  }
  CHECK(tokens[0].data() == text.data() + 2);
}

TEST_CASE("split_on keeps empty fields", "[io][detail][text]") {
  using Fields = std::vector<std::string_view>;
  CHECK(detail::split_on("", ',') == Fields{""});
  CHECK(detail::split_on("a", ',') == Fields{"a"});
  CHECK(detail::split_on("a,b", ',') == Fields{"a", "b"});
  CHECK(detail::split_on(",a,,b,", ',') == Fields{"", "a", "", "b", ""});
  CHECK(detail::split_on(",", ',') == Fields{"", ""});
  CHECK(detail::split_on("a\tb c", '\t') == Fields{"a", "b c"});
  CHECK(detail::split_on("a,b", ';') == Fields{"a,b"});
  // One more field than delimiters, for any text.
  const std::string_view text = "x,,y,z,";
  CHECK(detail::split_on(text, ',').size() == 5);
}

TEST_CASE("split_on fields are views of the input", "[io][detail][text]") {
  const std::string text = "ab,cd";
  const auto fields = detail::split_on(text, ',');
  REQUIRE(fields.size() == 2);
  CHECK(fields[0].data() == text.data());
  CHECK(fields[1].data() == text.data() + 3);
}

namespace {

// Concepts, not requires-expressions in the test body: a deleted overload in a
// non-template context is a hard error, not a failed constraint.
template <class S>
concept CanSplitWs = requires(S&& s) { detail::split_ws(std::forward<S>(s)); };
template <class S>
concept CanSplitOn =
    requires(S&& s) { detail::split_on(std::forward<S>(s), ','); };
template <class S>
concept CanStartCursor = std::constructible_from<detail::LineCursor, S>;

}  // namespace

TEST_CASE("splitting refuses a temporary std::string", "[io][detail][text]") {
  // A view of a temporary would dangle; the deleted overloads reject it on
  // every compiler (MOV_LIFETIMEBOUND adds a diagnostic on Clang and MSVC).
  STATIC_REQUIRE_FALSE(CanSplitWs<std::string>);
  STATIC_REQUIRE_FALSE(CanSplitWs<const std::string>);
  STATIC_REQUIRE_FALSE(CanSplitOn<std::string>);
  STATIC_REQUIRE_FALSE(CanStartCursor<std::string>);
  // Everything with a lifetime of its own is fine.
  STATIC_REQUIRE(CanSplitWs<std::string&>);
  STATIC_REQUIRE(CanSplitWs<const std::string&>);
  STATIC_REQUIRE(CanSplitWs<std::string_view>);
  STATIC_REQUIRE(CanSplitWs<decltype("literal")>);
  STATIC_REQUIRE(CanSplitWs<const char*>);
  STATIC_REQUIRE(CanSplitOn<const std::string&>);
  STATIC_REQUIRE(CanSplitOn<std::string_view>);
  STATIC_REQUIRE(CanStartCursor<const std::string&>);
  STATIC_REQUIRE(CanStartCursor<std::string_view>);
  STATIC_REQUIRE(CanStartCursor<decltype("literal")>);
}

// ---- strings ---------------------------------------------------------------

TEST_CASE("simplified trims and collapses whitespace", "[io][detail][text]") {
  CHECK(detail::simplified("").empty());
  CHECK(detail::simplified(" \t\r\n ").empty());
  CHECK(detail::simplified("Station 42") == "Station 42");
  CHECK(detail::simplified("  Station \t\r\n  42  ") == "Station 42");
  CHECK(detail::simplified("a\tb\nc") == "a b c");
  // Not whitespace: NUL and non-ASCII bytes.
  CHECK(detail::simplified("a\0b"sv) == "a\0b"sv);
  CHECK(detail::simplified(" na\xC3\xAFve  bar ") == "na\xC3\xAFve bar");
  STATIC_REQUIRE(std::same_as<decltype(detail::simplified("x")), std::string>);
}

TEST_CASE("case folding is ASCII-only", "[io][detail][text]") {
  CHECK(detail::to_lower_ascii("MixedCase_123") == "mixedcase_123");
  CHECK(detail::to_upper_ascii("MixedCase_123") == "MIXEDCASE_123");
  CHECK(detail::to_lower_ascii("").empty());
  // Bytes of a UTF-8 sequence (U+00C9) are not letters here.
  CHECK(detail::to_lower_ascii("\xC3\x89T\xC3\x89") == "\xC3\x89t\xC3\x89");
  CHECK(detail::to_upper_ascii("\xC3\xA9t\xC3\xA9") == "\xC3\xA9T\xC3\xA9");
  CHECK(detail::to_lower_ascii("A\0B"sv) == "a\0b"sv);
}

TEST_CASE("cut_at_nul keeps the bytes before the first NUL",
          "[io][detail][text]") {
  CHECK(detail::cut_at_nul("name") == "name");
  CHECK(detail::cut_at_nul("name\0junk"sv) == "name");
  CHECK(detail::cut_at_nul("name\0\0more\0"sv) == "name");
  CHECK(detail::cut_at_nul("\0name"sv).empty());
  CHECK(detail::cut_at_nul("").empty());
}

TEST_CASE("reserve_capped never reserves more than the cap",
          "[io][detail][text]") {
  std::vector<int> small;
  detail::reserve_capped(small, 1'000'000'000, 16);
  CHECK(small.capacity() >= 16);
  CHECK(small.capacity() < 1'000);

  std::vector<int> wanted;
  detail::reserve_capped(wanted, 8, 1'000'000);
  CHECK(wanted.capacity() >= 8);
  CHECK(wanted.capacity() < 1'000);

  std::vector<int> none;
  detail::reserve_capped(none, 0, 100);
  CHECK(none.capacity() == 0);
}

// ---- numbers: the token table of core-design.md section 7.2 ----------------

namespace {

struct DoubleCase {
  std::string_view token;
  double value;  // when `error` is empty
  std::optional<NumberError> error{};
};

constexpr DoubleCase accept(std::string_view token, double value) {
  return DoubleCase{.token = token, .value = value, .error = std::nullopt};
}

constexpr DoubleCase reject(std::string_view token, NumberError error) {
  return DoubleCase{.token = token, .value = 0.0, .error = error};
}

// Accepted tokens and the double each must give, exactly.
constexpr std::array accepted{
    accept("+1.5", 1.5),
    accept("1.5", 1.5),
    accept("-1.5", -1.5),
    accept(".5", 0.5),
    accept("5.", 5.0),
    accept("+.5", 0.5),
    accept("-.5e1", -5.0),
    accept("1e3", 1000.0),
    accept("1E3", 1000.0),
    accept("1e+3", 1000.0),
    accept("1e-3", 0.001),
    accept("-9.9999E+004", -99999.0),
    accept("0", 0.0),
    accept("0.000", 0.0),
    accept("007", 7.0),
    accept("0.1", 0.1),
    accept("123456789012345678901234567890", 1.2345678901234568e29),
    accept("1.7976931348623157e308", std::numeric_limits<double>::max()),
    accept("2.2250738585072014e-308", std::numeric_limits<double>::min()),
    // Denormals are values; the smallest one is 4.94e-324.
    accept("1e-310", 1e-310),
    accept("3e-324", std::numeric_limits<double>::denorm_min()),
    accept("5e-324", std::numeric_limits<double>::denorm_min()),
    // Zero stays zero whatever the exponent.
    accept("0e99999999999999999999", 0.0),
    accept("0e-99999999999999999999", 0.0),
};

constexpr std::array rejected{
    // Not a decimal number.
    reject("0x1p3", NumberError::bad_syntax),
    reject("0x10", NumberError::bad_syntax),
    reject("1,5", NumberError::bad_syntax),
    reject(" 1", NumberError::bad_syntax),
    reject("1 ", NumberError::bad_syntax),
    reject("1 5", NumberError::bad_syntax),
    reject("\t1", NumberError::bad_syntax),
    reject("1.5x", NumberError::bad_syntax),
    reject("nan", NumberError::bad_syntax),
    reject("NaN", NumberError::bad_syntax),
    reject("-nan", NumberError::bad_syntax),
    reject("inf", NumberError::bad_syntax),
    reject("-Inf", NumberError::bad_syntax),
    reject("Infinity", NumberError::bad_syntax),
    reject("+-1", NumberError::bad_syntax),
    reject("-+1", NumberError::bad_syntax),
    reject("--1", NumberError::bad_syntax),
    reject("+", NumberError::bad_syntax),
    reject("-", NumberError::bad_syntax),
    reject(".", NumberError::bad_syntax),
    reject("+.", NumberError::bad_syntax),
    reject("e5", NumberError::bad_syntax),
    reject(".e5", NumberError::bad_syntax),
    reject("1e", NumberError::bad_syntax),
    reject("1e+", NumberError::bad_syntax),
    reject("1e-", NumberError::bad_syntax),
    reject("1e5.5", NumberError::bad_syntax),
    reject("1.2.3", NumberError::bad_syntax),
    reject("1_000", NumberError::bad_syntax),
    reject("1d5", NumberError::bad_syntax),
    reject("1.0-100", NumberError::bad_syntax),
    reject("****", NumberError::bad_syntax),
    reject("\xEF\xBC\x91", NumberError::bad_syntax),  // full-width 1
    reject("1\0"sv, NumberError::bad_syntax),
    reject("\0"sv, NumberError::bad_syntax),
    reject("1\0"
           "5"sv,
           NumberError::bad_syntax),
    reject("", NumberError::empty),
    // Out of range: overflow, and a nonzero value that rounds to zero.
    reject("1e400", NumberError::out_of_range),
    reject("-1e400", NumberError::out_of_range),
    reject("1.8e308", NumberError::out_of_range),
    reject("1e-400", NumberError::out_of_range),
    reject("-1e-400", NumberError::out_of_range),
    reject("2e-324", NumberError::out_of_range),
    reject("1e99999999999999999999", NumberError::out_of_range),
    reject("1e-99999999999999999999", NumberError::out_of_range),
};

std::string repeated(std::string_view unit, std::size_t times) {
  std::string out;
  for (std::size_t i = 0; i < times; ++i) {
    out += unit;
  }
  return out;
}

// Every implementation under test, by name.
using ParseFunction = std::expected<double, NumberError> (*)(std::string_view);

struct Implementation {
  const char* name;
  ParseFunction parse;
};

Implementation implementation(const char* name, ParseFunction parse) {
  return Implementation{.name = name, .parse = parse};
}

std::vector<Implementation> implementations() {
  std::vector<Implementation> all{
      implementation("parse_double", &detail::parse_double),
      implementation("parse_double_strtod", &detail::parse_double_strtod),
  };
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
  all.push_back(implementation("parse_double_from_chars",
                               &detail::parse_double_from_chars));
#endif
  return all;
}

}  // namespace

TEST_CASE("parse_double accepts exactly the decimal tokens of the table",
          "[io][detail][number]") {
  for (const auto& [name, parse] : implementations()) {
    CAPTURE(name);
    for (const DoubleCase& c : accepted) {
      CAPTURE(c.token);
      const auto result = parse(c.token);
      REQUIRE(result.has_value());
      CHECK(*result == c.value);
    }
  }
}

TEST_CASE("parse_double rejects every other token, with the reason",
          "[io][detail][number]") {
  for (const auto& [name, parse] : implementations()) {
    CAPTURE(name);
    for (const DoubleCase& c : rejected) {
      CAPTURE(c.token);
      const auto result = parse(c.token);
      REQUIRE(not result.has_value());
      CHECK(result.error() == c.error);
    }
  }
}

TEST_CASE("parse_double handles very long mantissas", "[io][detail][number]") {
  const std::string third = "0." + repeated("3", 400);
  const std::string leading_zeros = repeated("0", 400) + "1";
  const std::string overflow = "1" + repeated("0", 399);         // 1e399
  const std::string tiny = "0." + repeated("0", 399) + "1";      // 1e-400
  const std::string fraction_zeros = "1." + repeated("0", 400);  // exactly 1
  for (const auto& [name, parse] : implementations()) {
    CAPTURE(name);
    CHECK(parse(third) == 1.0 / 3.0);
    CHECK(parse(leading_zeros) == 1.0);
    CHECK(parse(fraction_zeros) == 1.0);
    CHECK(parse(overflow) == std::unexpected{NumberError::out_of_range});
    CHECK(parse(tiny) == std::unexpected{NumberError::out_of_range});
  }
}

TEST_CASE("parse_double keeps the sign of zero", "[io][detail][number]") {
  for (const auto& [name, parse] : implementations()) {
    CAPTURE(name);
    const auto negative = parse("-0");
    const auto positive = parse("0");
    REQUIRE(negative.has_value());
    REQUIRE(positive.has_value());
    CHECK(std::bit_cast<std::uint64_t>(*negative) == (std::uint64_t{1} << 63));
    CHECK(std::bit_cast<std::uint64_t>(*positive) == 0);
  }
}

TEST_CASE("the implementations agree on every token of the table",
          "[io][detail][number]") {
  const auto all = implementations();
  for (const std::span<const DoubleCase> table :
       {std::span<const DoubleCase>{accepted},
        std::span<const DoubleCase>{rejected}}) {
    for (const DoubleCase& c : table) {
      CAPTURE(c.token);
      const auto reference = all.front().parse(c.token);
      for (const auto& implementation : all) {
        CAPTURE(implementation.name);
        CHECK(implementation.parse(c.token) == reference);
      }
    }
  }
}

TEST_CASE("parse_double never returns a non-finite value",
          "[io][detail][number]") {
  for (const std::span<const DoubleCase> table :
       {std::span<const DoubleCase>{accepted},
        std::span<const DoubleCase>{rejected}}) {
    for (const DoubleCase& c : table) {
      CAPTURE(c.token);
      const auto result = detail::parse_double(c.token);
      CHECK((not result.has_value() or std::isfinite(*result)));
    }
  }
}

// ---- integers --------------------------------------------------------------

TEST_CASE("parse_int takes a whole signed or unsigned token",
          "[io][detail][number]") {
  CHECK(detail::parse_int<int>("0") == 0);
  CHECK(detail::parse_int<int>("-0") == 0);
  CHECK(detail::parse_int<int>("+5") == 5);
  CHECK(detail::parse_int<int>("-5") == -5);
  CHECK(detail::parse_int<int>("007") == 7);
  CHECK(detail::parse_int<std::int64_t>("9223372036854775807") ==
        std::numeric_limits<std::int64_t>::max());
  CHECK(detail::parse_int<std::int64_t>("-9223372036854775808") ==
        std::numeric_limits<std::int64_t>::min());
  CHECK(detail::parse_int<std::uint64_t>("18446744073709551615") ==
        std::numeric_limits<std::uint64_t>::max());
  CHECK(detail::parse_int<std::size_t>("12") == 12U);
}

TEST_CASE("parse_int rejects what is not exactly an integer",
          "[io][detail][number]") {
  for (const std::string_view token : std::initializer_list<std::string_view>{
           " 1", "1 ", "1.5", "1e3", "0x10", "+-1", "-+1", "--1", "++1", "-",
           "+", "1,000", "1\0"sv, "abc", "\xEF\xBC\x91"}) {
    CAPTURE(token);
    CHECK(detail::parse_int<int>(token) ==
          std::unexpected{NumberError::bad_syntax});
  }
  CHECK(detail::parse_int<int>("") == std::unexpected{NumberError::empty});
}

TEST_CASE("parse_int reports a value that does not fit the type",
          "[io][detail][number]") {
  constexpr auto out_of_range = std::unexpected{NumberError::out_of_range};
  CHECK(detail::parse_int<std::int8_t>("127") == 127);
  CHECK(detail::parse_int<std::int8_t>("128") == out_of_range);
  CHECK(detail::parse_int<std::int8_t>("-128") == -128);
  CHECK(detail::parse_int<std::int8_t>("-129") == out_of_range);
  CHECK(detail::parse_int<std::uint8_t>("255") == 255);
  CHECK(detail::parse_int<std::uint8_t>("256") == out_of_range);
  CHECK(detail::parse_int<unsigned>("-1") == out_of_range);
  CHECK(detail::parse_int<std::uint64_t>("18446744073709551616") ==
        out_of_range);
  CHECK(detail::parse_int<std::int64_t>("99999999999999999999") ==
        out_of_range);
}

TEMPLATE_TEST_CASE("parse_int behaves the same for every integer type",
                   "[io][detail][number]", signed char, unsigned char, short,
                   unsigned short, int, unsigned, long, unsigned long,
                   long long, unsigned long long) {
  CHECK(detail::parse_int<TestType>("7") == TestType{7});
  CHECK(detail::parse_int<TestType>("+7") == TestType{7});
  CHECK(detail::parse_int<TestType>("007") == TestType{7});
  CHECK(detail::parse_int<TestType>("") == std::unexpected{NumberError::empty});
  CHECK(detail::parse_int<TestType>("x") ==
        std::unexpected{NumberError::bad_syntax});
  CHECK(detail::parse_int<TestType>("1.5") ==
        std::unexpected{NumberError::bad_syntax});
  CHECK(detail::parse_int<TestType>("+-1") ==
        std::unexpected{NumberError::bad_syntax});
  CHECK(detail::parse_int<TestType>("-") ==
        std::unexpected{NumberError::bad_syntax});
  CHECK(detail::parse_int<TestType>("99999999999999999999999") ==
        std::unexpected{NumberError::out_of_range});
  if constexpr (std::is_signed_v<TestType>) {
    CHECK(detail::parse_int<TestType>("-7") == TestType{-7});
  } else {
    CHECK(detail::parse_int<TestType>("-7") ==
          std::unexpected{NumberError::out_of_range});
  }
}

namespace {
template <class I>
concept CanParseInt = requires { detail::parse_int<I>("1"); };
}  // namespace

TEST_CASE("parse_int does not take bool", "[io][detail][number]") {
  STATIC_REQUIRE_FALSE(CanParseInt<bool>);
  STATIC_REQUIRE(CanParseInt<int>);
  STATIC_REQUIRE(CanParseInt<std::uint8_t>);
}

// ---- LineCursor ------------------------------------------------------------

namespace {

// The next line, or number 0 and no text at the end.
detail::LineCursor::Line next_or_none(detail::LineCursor& cursor) {
  return cursor.next().value_or(
      detail::LineCursor::Line{.number = 0, .text = {}});
}

std::vector<std::string> all_lines(std::string_view text) {
  std::vector<std::string> lines;
  detail::LineCursor cursor{text};
  while (const auto line = cursor.next()) {
    lines.emplace_back(line->text);
  }
  return lines;
}

}  // namespace

TEST_CASE("LineCursor splits on LF and numbers lines from 1",
          "[io][detail][lines]") {
  detail::LineCursor cursor{"alpha\nbeta\ngamma\n"};
  CHECK(cursor.lines_read() == 0);
  CHECK(not cursor.at_end());
  CHECK(cursor.next() ==
        detail::LineCursor::Line{.number = 1, .text = "alpha"});
  CHECK(cursor.next() == detail::LineCursor::Line{.number = 2, .text = "beta"});
  CHECK(cursor.lines_read() == 2);
  CHECK(cursor.next() ==
        detail::LineCursor::Line{.number = 3, .text = "gamma"});
  CHECK(cursor.at_end());
  CHECK(not cursor.next().has_value());
  CHECK(not cursor.next().has_value());
  CHECK(cursor.lines_read() == 3);
}

TEST_CASE("LineCursor: empty text has no lines, a bare newline has one",
          "[io][detail][lines]") {
  CHECK(all_lines("").empty());
  CHECK(all_lines("\n") == std::vector<std::string>{""});
  CHECK(all_lines("\n\n") == std::vector<std::string>{"", ""});
}

TEST_CASE("LineCursor returns a last line without a terminator",
          "[io][detail][lines]") {
  CHECK(all_lines("a\nb") == std::vector<std::string>{"a", "b"});
  CHECK(all_lines("a") == std::vector<std::string>{"a"});
}

TEST_CASE("LineCursor keeps blank lines", "[io][detail][lines]") {
  CHECK(all_lines("a\n\nb\n") == std::vector<std::string>{"a", "", "b"});
}

TEST_CASE("LineCursor strips one CR per line", "[io][detail][lines]") {
  CHECK(all_lines("a\r\nb\r\n") == std::vector<std::string>{"a", "b"});
  CHECK(all_lines("a\r\nb\r") == std::vector<std::string>{"a", "b"});
  CHECK(all_lines("a\r") == std::vector<std::string>{"a"});
  CHECK(all_lines("a\r\r\nb") == std::vector<std::string>{"a\r", "b"});
  // A CR inside a line is content.
  CHECK(all_lines("a\rb\n") == std::vector<std::string>{"a\rb"});
  // Mixed endings.
  CHECK(all_lines("a\nb\r\nc\n") == std::vector<std::string>{"a", "b", "c"});
}

TEST_CASE("LineCursor skips one UTF-8 byte order mark at the start",
          "[io][detail][lines]") {
  CHECK(all_lines("\xEF\xBB\xBFheader\nrow\n") ==
        std::vector<std::string>{"header", "row"});
  CHECK(all_lines("\xEF\xBB\xBF").empty());
  CHECK(all_lines("\xEF\xBB\xBF\n") == std::vector<std::string>{""});
  // Once: a second mark is data, and a mark later in the text is data.
  CHECK(all_lines("\xEF\xBB\xBF\xEF\xBB\xBFx") ==
        std::vector<std::string>{"\xEF\xBB\xBFx"});
  CHECK(all_lines("a\n\xEF\xBB\xBF"
                  "b") == std::vector<std::string>{"a",
                                                   "\xEF\xBB\xBF"
                                                   "b"});
  // Not a mark: a prefix of one.
  CHECK(all_lines("\xEF\xBB") == std::vector<std::string>{"\xEF\xBB"});
}

TEST_CASE("LineCursor lines are views of the text and keep NULs",
          "[io][detail][lines]") {
  const std::string text = "ab\0cd\nef"s;
  detail::LineCursor cursor{text};
  const auto first = next_or_none(cursor);
  CHECK(first.number == 1);
  CHECK(first.text == "ab\0cd"sv);
  CHECK(first.text.data() == text.data());
  const auto second = next_or_none(cursor);
  CHECK(second.number == 2);
  CHECK(second.text.data() == text.data() + 6);
}

// ---- locale independence ---------------------------------------------------

namespace {

// Switches the C locale for its lifetime, then restores it.
class ScopedLocale {
 public:
  explicit ScopedLocale(std::initializer_list<const char*> candidates) {
    const char* previous = std::setlocale(LC_ALL, nullptr);
    previous_ = previous != nullptr ? previous : "C";
    for (const char* name : candidates) {
      if (std::setlocale(LC_ALL, name) != nullptr) {
        active_ = name;
        return;
      }
    }
  }
  ScopedLocale(const ScopedLocale&) = delete;
  ScopedLocale& operator=(const ScopedLocale&) = delete;
  ScopedLocale(ScopedLocale&&) = delete;
  ScopedLocale& operator=(ScopedLocale&&) = delete;
  ~ScopedLocale() { std::setlocale(LC_ALL, previous_.c_str()); }

  [[nodiscard]] const char* active() const noexcept { return active_; }

 private:
  std::string previous_;
  const char* active_ = nullptr;
};

// The tests that need a comma-decimal locale fail, not skip, when it is
// missing and the build says it must exist (the presets, CI and the dev image
// do). Anywhere else they skip.
bool locale_required() {
#if defined(MOV_REQUIRE_LOCALES) && MOV_REQUIRE_LOCALES
  return true;
#else
  const char* env =
      std::getenv("MOV_REQUIRE_LOCALES");  // NOLINT(concurrency-mt-unsafe)
  return env != nullptr and std::string_view{env} == "1";
#endif
}

}  // namespace

TEST_CASE("parse_double ignores the global C locale",
          "[io][detail][number][locale][regression]") {
  const ScopedLocale german{"de_DE.UTF-8", "de_DE.utf8", "de_DE", "de-DE"};
  if (german.active() == nullptr) {
    if (locale_required()) {
      FAIL(
          "de_DE.UTF-8 is not installed, but this build requires it "
          "(MOV_REQUIRE_LOCALES): run in the dev image or "
          "`locale-gen de_DE.UTF-8`");
    }
    SKIP("de_DE.UTF-8 is not installed");
  }

  // The precondition, so that the test cannot pass vacuously: in this locale
  // the C library really does use a comma as the decimal point.
  CHECK(std::strtod("1,5", nullptr) == 1.5);
  CHECK(std::strtod("1.5", nullptr) == 1.0);

  for (const auto& [name, parse] : implementations()) {
    CAPTURE(name);
    CHECK(parse("1.5") == 1.5);
    CHECK(parse("-9.9999E+004") == -99999.0);
    CHECK(parse("1,5") == std::unexpected{NumberError::bad_syntax});
    CHECK(parse("1.5e3") == 1500.0);
  }
  CHECK(detail::parse_int<int>("1234") == 1234);
}

// ---- next_word, bounded splits, signs, parse_at
// ------------------------------

TEST_CASE("next_word walks the words of a line", "[io][detail][text]") {
  std::string_view rest = "  alpha \t beta\r\n gamma  ";
  CHECK(detail::next_word(rest) == "alpha");
  CHECK(detail::next_word(rest) == "beta");
  CHECK(detail::next_word(rest) == "gamma");
  CHECK(detail::next_word(rest) == std::nullopt);
  CHECK(detail::next_word(rest) == std::nullopt);
  std::string_view empty;
  CHECK(detail::next_word(empty) == std::nullopt);
  STATIC_REQUIRE(detail::skip_space("  \tx y") == "x y");
  STATIC_REQUIRE(detail::skip_space("   ").empty());
  STATIC_REQUIRE(detail::skip_space("").empty());
}

TEST_CASE("split_ws_into counts no further than one past the room",
          "[io][detail][text]") {
  std::array<std::string_view, 3> out{};
  CHECK(detail::split_ws_into("", out) == 0);
  CHECK(detail::split_ws_into("a b", out) == 2);
  CHECK(out[0] == "a");
  CHECK(out[1] == "b");
  CHECK(detail::split_ws_into(" a b c ", out) == 3);
  CHECK(out[2] == "c");
  // A fourth word is counted (the caller learns the line is too long) and a
  // fifth is not looked for.
  CHECK(detail::split_ws_into("a b c d e f", out) == 4);
  CHECK(out[2] == "c");
  CHECK(detail::split_ws_into("a", std::span<std::string_view>{}) == 1);
  CHECK(detail::split_ws_into("a b", std::span<std::string_view>{}) == 1);
}

TEST_CASE("split_ws<N> wants exactly N words", "[io][detail][text]") {
  using Three = std::array<std::string_view, 3>;
  CHECK(detail::split_ws<3>("29.98  -90.01\t12") ==
        std::optional<Three>{Three{"29.98", "-90.01", "12"}});
  CHECK(not detail::split_ws<3>("1 2").has_value());
  CHECK(not detail::split_ws<3>("1 2 3 4").has_value());
  CHECK(not detail::split_ws<3>("").has_value());
  CHECK(detail::split_ws<0>("  ").has_value());
  CHECK(not detail::split_ws<0>("x").has_value());

  const std::string line = "a b";
  const auto two =
      detail::split_ws<2>(line).value_or(std::array<std::string_view, 2>{});
  CHECK(two[1].data() == line.data() + 2);  // a view of the line
}

namespace {
template <class S>
concept CanSplitThree =
    requires(S&& s) { detail::split_ws<3>(std::forward<S>(s)); };
}  // namespace

TEST_CASE("split_ws<N> refuses a temporary std::string", "[io][detail][text]") {
  STATIC_REQUIRE_FALSE(CanSplitThree<std::string>);
  STATIC_REQUIRE(CanSplitThree<std::string&>);
  STATIC_REQUIRE(CanSplitThree<std::string_view>);
  STATIC_REQUIRE(CanSplitThree<decltype("a b c")>);
}

TEST_CASE("strip_sign removes one sign and says which",
          "[io][detail][number]") {
  std::string_view text = "+12";
  CHECK(detail::strip_sign(text) == detail::Sign::plus);
  CHECK(text == "12");
  text = "-12";
  CHECK(detail::strip_sign(text) == detail::Sign::minus);
  CHECK(text == "12");
  text = "12";
  CHECK(detail::strip_sign(text) == detail::Sign::none);
  CHECK(text == "12");
  text = "+-1";
  CHECK(detail::strip_sign(text) == detail::Sign::plus);
  CHECK(text == "-1");
  text = "";
  CHECK(detail::strip_sign(text) == detail::Sign::none);
}

TEST_CASE("at, double_at and int_at report the line, column and context",
          "[io][detail][lines]") {
  const std::string text = "header\n12 3.5x -7 99999999999 1e400\n";
  detail::LineCursor cursor{text};
  static_cast<void>(cursor.next());
  const detail::LineCursor::Line line = next_or_none(cursor);
  REQUIRE(line.number == 2);
  const auto words = detail::split_ws(line.text);
  REQUIRE(words.size() == 5);

  CHECK(detail::double_at(line, words[0]) == 12.0);
  const auto bad = detail::double_at(line, words[1]);
  REQUIRE(not bad.has_value());
  CHECK(bad.error().code() == mov::io::ParseErrc::bad_number);
  CHECK(bad.error().line() == 2);
  CHECK(bad.error().column() == std::optional<std::size_t>{3});
  CHECK(bad.error().context() == line.text);

  const auto big = detail::double_at(line, words[4]);
  REQUIRE(not big.has_value());
  CHECK(big.error().code() == mov::io::ParseErrc::out_of_range);
  CHECK(big.error().column() == std::optional<std::size_t>{23});

  CHECK(detail::int_at<int>(line, words[2]) == -7);
  const auto not_int = detail::int_at<int>(line, words[1]);
  REQUIRE(not not_int.has_value());
  CHECK(not_int.error().code() == mov::io::ParseErrc::bad_integer);
  const auto overflow = detail::int_at<int>(line, words[3]);
  REQUIRE(not overflow.has_value());
  CHECK(overflow.error().code() == mov::io::ParseErrc::out_of_range);
  CHECK(overflow.error().column() == std::optional<std::size_t>{11});

  // A token that is not a view of the line has no column.
  const auto elsewhere =
      detail::at(line, "stray", mov::io::ParseErrc::corrupt_record);
  CHECK(not elsewhere.column().has_value());
  CHECK(elsewhere.line() == 2);
  CHECK(not detail::at(line, std::string_view{},
                       mov::io::ParseErrc::corrupt_record)
                .column()
                .has_value());
}

TEST_CASE("position_at blames the latitude, else the first coordinate",
          "[io][text]") {
  const mov::io::detail::LineCursor::Line line{.number = 7,
                                               .text = "200.5 95.25 name"};
  const std::string_view x = std::string_view{line.text}.substr(0, 5);
  const std::string_view y = std::string_view{line.text}.substr(6, 5);

  using mov::core::LocationError;
  const auto lat =
      detail::position_at(line, x, y, LocationError::latitude_out_of_range);
  CHECK(lat.code() == mov::io::ParseErrc::out_of_range);
  CHECK(lat.line() == 7);
  CHECK(lat.column() == std::optional<std::size_t>{6});

  for (const LocationError other :
       {LocationError::longitude_out_of_range, LocationError::not_finite}) {
    const auto first = detail::position_at(line, x, y, other);
    CHECK(first.code() == mov::io::ParseErrc::out_of_range);
    CHECK(first.column() == std::optional<std::size_t>{0});
  }
}
