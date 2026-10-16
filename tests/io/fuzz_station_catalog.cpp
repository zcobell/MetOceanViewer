// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: the station catalog reader and writer
// (docs/station-catalog.md, docs/providers-design.md section 10.4). Laws:
//  - no crash, UB or hang;
//  - a catalog the reader returns is one the writer can write, and it
//    survives the round trip: parse(format(c)) == c, with no warning, and
//    format(parse(format(c))) == format(c) byte for byte;
//  - a refusal is a ParseError whose line lies inside the text, or a
//    FormatError, or too_large; never a FileError, NcError or Cancelled.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <variant>

#include "mov/core/time.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/station_catalog.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

[[noreturn]] void fail() { std::abort(); }

// A clock after every four-digit year: nothing is future-dated.
mov::core::Time end_of_time() {
  using namespace std::chrono;
  return time_point_cast<milliseconds>(
      sys_days{year{9999} / December / day{31}});
}

void check_error(const mov::io::Error& error, std::string_view text) {
  if (const auto* p = std::get_if<mov::io::ParseError>(&error)) {
    const auto lines =
        static_cast<std::size_t>(std::ranges::count(text, '\n')) + 1;
    if (p->line() < 1 or p->line() > lines) {
      fail();
    }
    return;
  }
  if (not std::holds_alternative<mov::io::FormatError>(error)) {
    fail();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string storage(data, data + size);
  const std::string_view text{storage};
  mov::io::ReadContext ctx;
  ctx.limits.max_elements = std::size_t{1} << 16;
  const auto read = mov::io::parse_station_catalog(text, end_of_time(), ctx);
  if (not read) {
    check_error(read.error(), text);
    return 0;
  }
  const auto once = mov::io::format_station_catalog(read->value);
  if (not once) {
    fail();  // what the reader returns, the writer must write
  }
  const auto again = mov::io::parse_station_catalog(*once, end_of_time(), ctx);
  if (not again or not again->warnings.empty() or again->value != read->value) {
    fail();
  }
  const auto twice = mov::io::format_station_catalog(again->value);
  if (not twice or *twice != *once) {
    fail();
  }
  return 0;
}
