// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: the JSON nesting pre-scan (docs/providers-design.md
// section 4.2) agrees with a reference count. For every text nlohmann
// accepts, check_json_depth passes exactly when the deepest nesting, counted
// by a SAX handler, is at most max_json_depth; a refusal names a line and
// column inside the text. Text nlohmann refuses only must not crash the
// scan. A NUL ends nlohmann's input, so a text with one is not compared (the
// parsers refuse it first: test_coops_data.cpp).

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

#include "mov/io/wire/body.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

using Json = nlohmann::json;

[[noreturn]] void fail() { std::abort(); }

// Counts nesting without building anything.
class DepthCounter : public nlohmann::json_sax<Json> {
 public:
  [[nodiscard]] std::size_t deepest() const noexcept { return deepest_; }

  bool null() override { return true; }
  bool boolean(bool /*val*/) override { return true; }
  bool number_integer(number_integer_t /*val*/) override { return true; }
  bool number_unsigned(number_unsigned_t /*val*/) override { return true; }
  bool number_float(number_float_t /*val*/, const string_t& /*s*/) override {
    return true;
  }
  bool string(string_t& /*val*/) override { return true; }
  bool binary(binary_t& /*val*/) override { return true; }
  bool start_object(std::size_t /*elements*/) override { return open(); }
  bool key(string_t& /*val*/) override { return true; }
  bool end_object() override { return close(); }
  bool start_array(std::size_t /*elements*/) override { return open(); }
  bool end_array() override { return close(); }
  bool parse_error(std::size_t /*position*/, const std::string& /*last_token*/,
                   const nlohmann::detail::exception& /*ex*/) override {
    return false;
  }

 private:
  bool open() {
    ++depth_;
    deepest_ = depth_ > deepest_ ? depth_ : deepest_;
    return true;
  }
  bool close() {
    --depth_;
    return true;
  }

  std::size_t depth_ = 0;
  std::size_t deepest_ = 0;
};

std::size_t line_count(std::string_view text) {
  std::size_t lines = 1;
  for (const char c : text) {
    lines += c == '\n' ? 1 : 0;
  }
  return lines;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string text(data, data + size);
  const auto verdict = mov::io::wire::check_json_depth(text);
  if (not verdict and verdict.error().line() > line_count(text)) {
    fail();
  }
  // nlohmann's lexer takes a NUL for the end of the input (the parsers refuse
  // a NUL before they call it), so the reference reads text without one.
  if (text.find('\0') != std::string::npos) {
    return 0;
  }
  DepthCounter counter;
  const bool valid = Json::sax_parse(text, &counter, Json::input_format_t::json,
                                     /*strict=*/true);
  if (valid and verdict.has_value() !=
                    (counter.deepest() <= mov::io::wire::max_json_depth)) {
    fail();
  }
  return 0;
}
