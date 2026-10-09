// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: ConstituentName::make and parse_node_factor must never
// crash. A name is accepted exactly when it matches the format's grammar
// ^[A-Za-z0-9][A-Za-z0-9()_.+-]{0,31}$ (checked here by a separate, plain
// implementation), an accepted name views the input unchanged, and a
// node-factor token is accepted exactly when to_token gives it back.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/node_factor.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

[[noreturn]] void fail() { std::abort(); }

bool alnum(char c) {
  return (c >= 'A' and c <= 'Z') or (c >= 'a' and c <= 'z') or
         (c >= '0' and c <= '9');
}

bool matches_grammar(std::string_view s) {
  if (s.empty() or s.size() > 32 or not alnum(s[0])) {
    return false;
  }
  for (const char c : s.substr(1)) {
    if (not alnum(c) and
        std::string_view{"()_.+-"}.find(c) == std::string_view::npos) {
      return false;
    }
  }
  return true;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string text(data, data + size);
  const auto name = mov::core::tide::ConstituentName::make(text);
  if (name.has_value() != matches_grammar(text)) {
    fail();
  }
  if (name and name->view() != text) {
    fail();
  }
  const auto factor = mov::core::tide::parse_node_factor(text);
  if (factor and mov::core::tide::to_token(*factor) != text) {
    fail();
  }
  return 0;
}
