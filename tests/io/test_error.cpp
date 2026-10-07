// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <variant>

#include "mov/core/units.hpp"
#include "mov/io/error.hpp"

namespace {

using mov::io::ParseErrc;
using mov::io::ParseError;
using mov::io::detail::truncate_utf8;

// A UTF-8 string of `lead` ASCII bytes followed by `glyph`.
std::string padded(std::size_t lead, std::string_view glyph) {
  return std::string(lead, 'a') + std::string{glyph};
}

bool is_valid_utf8_prefix(std::string_view text) {
  // Every lead byte must be followed by exactly the continuation bytes it
  // announces.
  std::size_t i = 0;
  while (i < text.size()) {
    const auto lead = static_cast<unsigned char>(text[i]);
    std::size_t length = 1;
    if (lead >= 0xF0U) {
      length = 4;
    } else if (lead >= 0xE0U) {
      length = 3;
    } else if (lead >= 0xC0U) {
      length = 2;
    } else if (lead >= 0x80U) {
      return false;
    }
    if (i + length > text.size()) {
      return false;
    }
    for (std::size_t k = 1; k < length; ++k) {
      if ((static_cast<unsigned char>(text[i + k]) & 0xC0U) != 0x80U) {
        return false;
      }
    }
    i += length;
  }
  return true;
}

}  // namespace

TEST_CASE("truncate_utf8 keeps short text and cuts long ASCII exactly",
          "[io][error]") {
  CHECK(truncate_utf8("", 120).empty());
  CHECK(truncate_utf8("abc", 3) == "abc");
  CHECK(truncate_utf8("abcd", 3) == "abc");
  CHECK(truncate_utf8("abcd", 0).empty());
  const std::string long_line(1000, 'x');
  CHECK(truncate_utf8(long_line, 120).size() == 120);
}

TEST_CASE("truncate_utf8 never ends inside a UTF-8 sequence", "[io][error]") {
  // U+00E9 (2 bytes), U+20AC (3 bytes), U+1F30A (4 bytes).
  for (const std::string_view glyph :
       {"\xC3\xA9", "\xE2\x82\xAC", "\xF0\x9F\x8C\x8A"}) {
    for (std::size_t lead = 0; lead < 8; ++lead) {
      const std::string text = padded(lead, glyph) + "tail";
      for (std::size_t max = 0; max <= text.size(); ++max) {
        const std::string_view cut = truncate_utf8(text, max);
        CHECK(cut.size() <= max);
        CHECK(is_valid_utf8_prefix(cut));
        CHECK(text.starts_with(cut));
        // The cut is as long as it can be: one more byte would not fit or
        // would split the glyph.
        if (cut.size() < max and max < text.size()) {
          CHECK(cut.size() >= lead);
        }
      }
    }
  }
}

TEST_CASE("truncate_utf8 drops the whole glyph that straddles the limit",
          "[io][error]") {
  // 119 ASCII bytes and a 2-byte glyph: byte 120 is a continuation byte.
  const std::string text = padded(119, "\xC3\xA9") + "more";
  const std::string_view cut = truncate_utf8(text, 120);
  CHECK(cut.size() == 119);
  // A glyph that ends exactly on the limit stays.
  const std::string fits = padded(118, "\xC3\xA9") + "more";
  CHECK(truncate_utf8(fits, 120).size() == 120);
}

TEST_CASE("truncate_utf8 cuts text that is not UTF-8 at the limit",
          "[io][error]") {
  const std::string stray(200, static_cast<char>(0x80));
  CHECK(truncate_utf8(stray, 120).size() == 120);
  CHECK(truncate_utf8(stray, 0).empty());
}

TEST_CASE("ParseError::make records the position and truncates the context",
          "[io][error]") {
  const ParseError e =
      ParseError::make(ParseErrc::bad_number, {.line = 7, .column = 3}, "1.5x");
  CHECK(e.code() == ParseErrc::bad_number);
  CHECK(e.line() == 7);
  CHECK(e.column() == std::optional<std::size_t>{3});
  CHECK(e.context() == "1.5x");

  const ParseError whole_line =
      ParseError::make(ParseErrc::empty_input, {.line = 1}, "");
  CHECK(not whole_line.column().has_value());

  const std::string hostile =
      padded(ParseError::max_context_bytes - 1, "\xE2\x82\xAC") +
      std::string(10'000, 'z');
  const ParseError cut = ParseError::make(ParseErrc::corrupt_record,
                                          {.line = 2, .column = 0}, hostile);
  CHECK(cut.context().size() == ParseError::max_context_bytes - 1);
  CHECK(is_valid_utf8_prefix(cut.context()));
}

TEST_CASE("ParseError compares by value", "[io][error]") {
  const ParseError a =
      ParseError::make(ParseErrc::bad_date, {.line = 1, .column = 2}, "x");
  const ParseError same =
      ParseError::make(ParseErrc::bad_date, {.line = 1, .column = 2}, "x");
  CHECK(a == same);
  CHECK(a !=
        ParseError::make(ParseErrc::bad_date, {.line = 1, .column = 3}, "x"));
  CHECK(a !=
        ParseError::make(ParseErrc::bad_date, {.line = 1, .column = 2}, "y"));
  CHECK(a != ParseError::make(ParseErrc::bad_time_units,
                              {.line = 1, .column = 2}, "x"));
}

TEST_CASE("the other error types compare by value", "[io][error]") {
  using namespace mov::io;
  const FileError file{.op = FileOp::read,
                       .path = "a.txt",
                       .ec = std::make_error_code(std::errc::io_error)};
  CHECK(file == FileError{.op = FileOp::read,
                          .path = "a.txt",
                          .ec = std::make_error_code(std::errc::io_error)});
  CHECK(file != FileError{.op = FileOp::write,
                          .path = "a.txt",
                          .ec = std::make_error_code(std::errc::io_error)});

  const NcError nc{.status = LibraryStatus{-51},
                   .op = NcOp::open,
                   .object = "",
                   .file = "a.nc"};
  CHECK(nc == NcError{.status = LibraryStatus{-51},
                      .op = NcOp::open,
                      .object = "",
                      .file = "a.nc"});
  CHECK(nc != NcError{.status = WrapperFault::closed,
                      .op = NcOp::open,
                      .object = "",
                      .file = "a.nc"});

  const FormatError format{.code = FormatErrc::missing_variable,
                           .subject = "time",
                           .station = 2,
                           .index = std::nullopt};
  CHECK(format == FormatError{.code = FormatErrc::missing_variable,
                              .subject = "time",
                              .station = 2,
                              .index = std::nullopt});
  CHECK(format !=
        FormatError{.code = FormatErrc::missing_variable, .subject = "time"});
  CHECK(Cancelled{} == Cancelled{});
}

TEST_CASE("lift moves narrow errors into a layer's variant", "[io][error]") {
  using namespace mov::io;
  const Error from_file =
      lift<Error>(FileError{.op = FileOp::open, .path = "x", .ec = {}});
  CHECK(std::holds_alternative<FileError>(from_file));
  const Error from_format = lift<Error>(
      FormatError{.code = FormatErrc::not_this_format, .subject = ""});
  CHECK(std::holds_alternative<FormatError>(from_format));
  CHECK(std::holds_alternative<Cancelled>(lift<Error>(Cancelled{})));

  // An lvalue and a value that already is the target pass through.
  const Error again = lift<Error>(from_file);
  CHECK(again == from_file);
}

TEST_CASE("lift reaches through a nested layer variant", "[io][error]") {
  using namespace mov::io;
  using CliError = std::variant<Error, mov::core::UnitError>;

  const CliError from_unit =
      lift<CliError>(mov::core::UnitError{mov::core::UnknownUnit{}});
  REQUIRE(std::holds_alternative<mov::core::UnitError>(from_unit));

  const CliError from_io = lift<CliError>(
      Error{FormatError{.code = FormatErrc::time_missing, .subject = "time"}});
  REQUIRE(std::holds_alternative<Error>(from_io));

  // A narrow io error goes through io::Error into the CLI variant.
  const CliError from_narrow =
      lift<CliError>(ParseError::make(ParseErrc::bad_integer, {.line = 1}, ""));
  REQUIRE(std::holds_alternative<Error>(from_narrow));
  CHECK(std::holds_alternative<ParseError>(std::get<Error>(from_narrow)));
}

TEST_CASE("lift rejects a type the variant cannot hold", "[io][error]") {
  using mov::io::Error;
  STATIC_REQUIRE(
      std::invocable<decltype(mov::io::lift<Error>), mov::io::FileError>);
  STATIC_REQUIRE(not std::invocable<decltype(mov::io::lift<Error>), int>);
  STATIC_REQUIRE(
      not std::invocable<decltype(mov::io::lift<Error>), mov::core::UnitError>);
}
