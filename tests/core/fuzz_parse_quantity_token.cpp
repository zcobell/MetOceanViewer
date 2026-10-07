// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target for the quantity vocabulary: a text is a registry token,
// or (if it is a legal name) a GenericQuantity, never both, and whichever it
// is, its token() is the text.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "mov/core/quantity.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

[[noreturn]] void fail() { std::abort(); }

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string text(data, data + size);
  const auto registry = mov::core::parse_quantity_token(text);
  const auto generic = mov::core::GenericQuantity::parse(text, text);
  if (registry and generic) {
    fail();  // a registry token is never generic
  }
  if (registry) {
    const mov::core::QuantityId id{*registry};
    if (mov::core::token(id) != text) {
      fail();
    }
  }
  if (generic) {
    const mov::core::QuantityId id{*generic};
    if (mov::core::token(id) != text or generic->standard_name() != text or
        not mov::core::datum_applicable(id)) {
      fail();
    }
  }
  return 0;
}
