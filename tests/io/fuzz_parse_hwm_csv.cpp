// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: parse_hwm_csv must never crash, and the marks it returns
// are what the statistics need:
//  - every elevation is finite and at most 1e4 m in magnitude
//    (max_elevation_m, so no sum of squares overflows), and a wet modeled
//    value is above the dry threshold;
//  - hwm_stats on them never reports NonFiniteMoments, in either mode;
//  - the same text with CRLF line endings, or behind a byte order mark, gives
//    the same marks and warnings (or the same error code).
// Input: byte 0 selects the unit (even: metres, odd: feet), then the text.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/hwm.hpp"
#include "mov/core/hwm_stats.hpp"
#include "mov/core/units.hpp"
#include "mov/io/error.hpp"
#include "mov/io/hwm_file.hpp"
#include "mov/io/read_limits.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace core = mov::core;
namespace io = mov::io;

[[noreturn]] void fail() { std::abort(); }

bool within_bound(core::Length v) {
  const double metres = v.as(core::LengthUnit::meter);
  return std::isfinite(metres) and std::abs(metres) <= core::max_elevation_m;
}

void check_marks(const std::vector<core::HighWaterMark>& marks,
                 core::LengthUnit unit) {
  for (const core::HighWaterMark& mark : marks) {
    if (not within_bound(mark.ground) or not within_bound(mark.observed)) {
      fail();
    }
    if (const auto* wet = std::get_if<core::Wet>(&mark.modeled)) {
      // The dry rule is on the raw number in the file's unit; the converted
      // length may differ from it by a rounding.
      if (not within_bound(wet->elevation) or
          wet->elevation.as(unit) <= core::dry_threshold - 1e-6) {
        fail();
      }
    }
  }
  for (const core::Intercept mode :
       {core::Intercept::free, core::Intercept::through_origin}) {
    const auto stats = core::hwm_stats(marks, mode);
    if (not stats and
        std::holds_alternative<core::NonFiniteMoments>(stats.error())) {
      fail();
    }
  }
}

std::string with_crlf(std::string_view text) {
  std::string out;
  for (const char c : text) {
    if (c == '\n') {
      out += '\r';
    }
    out += c;
  }
  return out;
}

using Parsed =
    std::expected<io::Read<std::vector<core::HighWaterMark>>, io::Error>;

// The same marks and warnings, or the same error at the same line.
void check_equivalent(const Parsed& a, const Parsed& b) {
  if (a.has_value() != b.has_value()) {
    fail();
  }
  if (a) {
    if (not(*a == *b)) {
      fail();
    }
    return;
  }
  const auto* pa = std::get_if<io::ParseError>(&a.error());
  const auto* pb = std::get_if<io::ParseError>(&b.error());
  if (pa == nullptr or pb == nullptr or pa->code() != pb->code() or
      pa->line() != pb->line()) {
    fail();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size < 1) {
    return 0;
  }
  const core::LengthUnit unit =
      (data[0] % 2) == 0 ? core::LengthUnit::meter : core::LengthUnit::foot;
  const std::string storage(data + 1, data + size);
  const std::string_view text{storage};
  const io::ReadContext ctx{};
  const auto parsed = io::parse_hwm_csv(text, unit, ctx);
  if (parsed) {
    check_marks(parsed->value, unit);
  } else if (not std::holds_alternative<io::ParseError>(parsed.error())) {
    fail();
  }

  // A second byte order mark is content, so text that has one is skipped.
  if (not text.starts_with("\xEF\xBB\xBF")) {
    const auto bom =
        io::parse_hwm_csv("\xEF\xBB\xBF" + std::string{text}, unit, ctx);
    check_equivalent(parsed, bom);
  }

  // A CR before a CR is not the end of a line, so only text with no CR of its
  // own can be compared.
  if (text.find('\r') == std::string_view::npos) {
    check_equivalent(parsed, io::parse_hwm_csv(with_crlf(text), unit, ctx));
  }
  return 0;
}
