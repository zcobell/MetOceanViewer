// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: Licence::parse must never crash, must accept exactly the
// expressions a separate recursive-descent parser of the same grammar
// accepts (docs/harmonics-json.md 6.1), and must report the same licence and
// exception identifiers in the same order. A rejection's offset lies within
// the text. Also checks station_namespace against a plain reading of the id
// grammar.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mov/core/tide/licence.hpp"
#include "mov/core/tide/source.hpp"
#include "mov/core/utf8.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

[[noreturn]] void fail() { std::abort(); }

bool word_char(char c) {
  return (c >= 'A' and c <= 'Z') or (c >= 'a' and c <= 'z') or
         (c >= '0' and c <= '9') or c == '.' or c == '+' or c == '-';
}

/// The reference parser: tokens first, then recursive descent.
class Reference {
 public:
  explicit Reference(std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
      const char c = text[i];
      if (c == ' ') {
        ++i;
      } else if (c == '(' or c == ')') {
        tokens_.emplace_back(1, c);
        ++i;
      } else if (word_char(c)) {
        std::size_t end = i;
        while (end < text.size() and word_char(text[end])) {
          ++end;
        }
        tokens_.emplace_back(text.substr(i, end - i));
        i = end;
      } else {
        bad_ = true;
        return;
      }
    }
  }

  bool parse() {
    if (bad_ or tokens_.empty()) {
      return false;
    }
    return expression(0) and next_ == tokens_.size();
  }

  std::vector<std::string> identifiers;
  std::vector<std::string> exceptions;

 private:
  static bool is_operator(const std::string& t) {
    return t == "AND" or t == "OR" or t == "WITH";
  }
  static bool is_identifier(const std::string& t) {
    if (t == "(" or t == ")" or is_operator(t)) {
      return false;
    }
    const std::string_view prefix = "LicenseRef-";
    if (std::string_view{t}.starts_with(prefix)) {
      return t.size() > prefix.size() and
             t.find('+', prefix.size()) == std::string::npos;
    }
    return true;
  }
  [[nodiscard]] const std::string* peek() const {
    return next_ < tokens_.size() ? &tokens_[next_] : nullptr;
  }

  bool expression(int depth) {
    if (depth > 4096 or not term(depth)) {
      return false;
    }
    while (peek() != nullptr and (*peek() == "AND" or *peek() == "OR")) {
      ++next_;
      if (not term(depth)) {
        return false;
      }
    }
    return true;
  }

  bool term(int depth) {
    const std::string* t = peek();
    if (t == nullptr) {
      return false;
    }
    if (*t == "(") {
      ++next_;
      if (not expression(depth + 1)) {
        return false;
      }
      if (peek() == nullptr or *peek() != ")") {
        return false;
      }
      ++next_;
      return true;
    }
    if (not is_identifier(*t)) {
      return false;
    }
    identifiers.push_back(*t);
    ++next_;
    if (peek() != nullptr and *peek() == "WITH") {
      ++next_;
      const std::string* e = peek();
      if (e == nullptr or not is_identifier(*e)) {
        return false;
      }
      exceptions.push_back(*e);
      ++next_;
    }
    return true;
  }

  std::vector<std::string> tokens_;
  std::size_t next_ = 0;
  bool bad_ = false;
};

void check_licence(std::string_view text) {
  const auto licence = mov::core::tide::Licence::parse(text);
  Reference reference{text};
  const bool accepted = reference.parse();
  if (licence.has_value() != accepted) {
    fail();
  }
  if (not licence) {
    if (licence.error().offset > text.size()) {
      fail();
    }
    return;
  }
  if (licence->text() != text) {
    fail();
  }
  if (not std::ranges::equal(licence->identifiers(), reference.identifiers) or
      not std::ranges::equal(licence->exceptions(), reference.exceptions)) {
    fail();
  }
}

bool source_char(char c, bool first) {
  return (c >= 'a' and c <= 'z') or
         (not first and ((c >= '0' and c <= '9') or c == '-'));
}

void check_namespace(std::string_view id) {
  const auto ns = mov::core::tide::station_namespace(id);
  const std::size_t colon = id.find(':');
  bool expected = id.size() <= 255 and colon != std::string_view::npos and
                  colon >= 1 and colon <= 32 and colon + 1 < id.size() and
                  mov::core::is_valid_utf8(id);
  for (std::size_t i = 0; expected and i < colon; ++i) {
    expected = source_char(id[i], i == 0);
  }
  for (std::size_t i = colon + 1; expected and i < id.size(); ++i) {
    const auto byte = static_cast<unsigned char>(id[i]);
    expected = byte >= 0x20 and byte != 0x7F;
  }
  if (ns.has_value() != expected) {
    fail();
  }
  if (ns and ns->view() != id.substr(0, colon)) {
    fail();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string text(data, data + size);
  if (text.size() <= 8192) {  // bounds the reference parser's recursion
    check_licence(text);
  }
  check_namespace(text);
  return 0;
}
