// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: io::gunzip (docs/harmonics-engine.md section 6.7,
// docs/providers-design.md section 4.2). Byte 0 picks the mode:
//  - even: the rest is the compressed body, inflated with a 4 MiB cap. No
//    crash, UB, leak or hang; the output never exceeds the cap; the only
//    errors are GzipError (no stop was requested); an accepted body starts
//    with the magic; a body with bytes appended after an accepted one is
//    refused at the old end.
//  - odd: the rest is compressed here with zlib and inflated again: the
//    round trip is the identity.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <span>
#include <string>
#include <variant>

#include "gzip_test_support.hpp"
#include "mov/io/gzip.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

[[noreturn]] void fail() { std::abort(); }

constexpr std::uintmax_t cap = std::uintmax_t{4} << 20;

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size < 1) {
    return 0;
  }
  const std::string rest(data + 1, data + size);
  if (data[0] % 2 == 1) {
    const std::string gz = mov::test::gzip_of(rest);
    const auto out = mov::io::gunzip(mov::test::bytes_of(gz), cap);
    if (not out or *out != rest) {
      fail();
    }
    return 0;
  }
  const auto body = mov::test::bytes_of(rest);
  const auto out = mov::io::gunzip(body, cap);
  if (not out) {
    if (not std::holds_alternative<mov::io::GzipError>(out.error())) {
      fail();
    }
    return 0;
  }
  if (out->size() > cap or not mov::io::has_gzip_magic(body)) {
    fail();
  }
  const auto longer = mov::io::gunzip(mov::test::bytes_of(rest + "x"), cap);
  const auto* e =
      longer ? nullptr : std::get_if<mov::io::GzipError>(&longer.error());
  if (e == nullptr or e->code != mov::io::GzipErrc::trailing_bytes or
      e->offset != rest.size()) {
    fail();
  }
  return 0;
}
