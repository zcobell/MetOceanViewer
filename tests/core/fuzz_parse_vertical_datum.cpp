// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: parse_vertical_datum must never crash; the token of every
// datum it accepts parses back to the same datum; an unknown text is reported
// trimmed; and the answer (datum, no datum or unknown) does not depend on the
// case of the text or on whitespace around it.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>

#include "mov/core/datum.hpp"
#include "mov/core/detail/ascii.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

[[noreturn]] void fail() { std::abort(); }

std::string upper(std::string text) {
  for (char& c : text) {
    if (c >= 'a' and c <= 'z') {
      c = static_cast<char>(c - 'a' + 'A');
    }
  }
  return text;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string text(data, data + size);
  const auto parsed = mov::core::parse_vertical_datum(text);

  if (parsed and parsed->has_value() and
      mov::core::parse_vertical_datum(mov::core::to_string(**parsed)) !=
          parsed) {
    fail();
  }
  if (not parsed) {
    const std::string_view reported = parsed.error().text;
    if (reported.empty() or mov::core::detail::is_space(reported.front()) or
        mov::core::detail::is_space(reported.back())) {
      fail();  // reported text is trimmed and never empty
    }
  }

  // Same answer for the upper-cased, padded text. The reported text of an
  // unknown input differs (case, padding), so only its presence is compared.
  const auto other =
      mov::core::parse_vertical_datum(" \t" + upper(text) + "\n");
  if (parsed.has_value() != other.has_value() or
      (parsed and *parsed != *other)) {
    fail();
  }
  return 0;
}
