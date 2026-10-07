// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: parse_adcirc_station_file (EPSG:4326, which needs no
// projection database) must never crash, and an accepted list
//  - has exactly as many stations as the count on the first non-blank line;
//  - numbers its stations 0, 1, 2, ... and names each one (the default name
//    when the line has none);
//  - keeps its positions as the file wrote them (WGS84 Locations);
//  - and warns only about names it repaired, never more times than stations.
// A rejected text reports a ParseError or FormatError within the text.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "mov/core/geo.hpp"
#include "mov/io/adcirc_ascii.hpp"
#include "mov/io/detail/line_cursor.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/warning.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace io = mov::io;

[[noreturn]] void fail() { std::abort(); }

// The count: the first field of the first non-blank line, where a comma or
// white space ends a field and runs of them are one.
std::optional<std::size_t> expected_count(std::string_view text) {
  io::detail::LineCursor cursor{text};
  while (const auto line = cursor.next()) {
    if (io::detail::skip_space(line->text).empty()) {
      continue;
    }
    std::string_view rest = line->text;
    const std::size_t first = rest.find_first_not_of(", \t\r\n\v\f");
    if (first == std::string_view::npos) {
      return std::nullopt;  // a line of commas has no first field
    }
    rest.remove_prefix(first);
    const auto count = io::detail::parse_int<std::size_t>(
        rest.substr(0, rest.find_first_of(", \t\r\n\v\f")));
    return count ? std::optional<std::size_t>{*count} : std::nullopt;
  }
  return std::nullopt;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string storage(data, data + size);
  const std::string_view text{storage};
  const io::ReadContext ctx{};
  const auto parsed =
      io::parse_adcirc_station_file(text, mov::core::Epsg::wgs84(), ctx);
  if (not parsed) {
    if (const auto* error = std::get_if<io::ParseError>(&parsed.error())) {
      if (error->context().size() > io::ParseError::max_context_bytes) {
        fail();
      }
    } else if (not std::holds_alternative<io::FormatError>(parsed.error())) {
      fail();
    }
    return 0;
  }
  const auto& stations = parsed->value;
  const auto count = expected_count(text);
  if (not count or *count != stations.size()) {
    fail();
  }
  for (std::size_t i = 0; i < stations.size(); ++i) {
    if (stations[i].id.view() != std::to_string(i) or
        stations[i].name.view().empty() or stations[i].native.has_value()) {
      fail();
    }
  }
  for (const io::Warning& w : parsed->warnings) {
    if (w.code != io::WarningCode::invalid_utf8_replaced or w.count == 0 or
        w.count > stations.size()) {
      fail();
    }
  }
  return 0;
}
