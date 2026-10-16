// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: ndbc::read_stdmet and parse_stdmet
// (docs/providers-design.md section 10.4). Byte 0 picks the file kind, the
// rest is the body (gzip or text). Laws:
//  - no crash, UB or hang; no Cancelled without a stop request;
//  - an accepted body's columns are the header's recognized tokens (WD and
//    WDIR, BAR and PRES one each), each column in strictly increasing time
//    on one axis, no Dry sample;
//  - Missing samples never exceed the 9-filled values counted plus the
//    `MM` words of the text, and no present sample equals its column's
//    9-filled value (999.0 is data only for pressure, whose value is 9999);
//  - markup is the service's refusal only when the text starts with '<';
//  - a refusal is a ParseError whose line lies inside the text, or a
//    FormatError; read_stdmet of a text body is parse_stdmet of it.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/provider/ndbc.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/detail/text.hpp"
#include "mov/io/error.hpp"
#include "mov/io/gzip.hpp"
#include "mov/io/part.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/ndbc.hpp"
#include "mov/io/wire/service.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace wn = mov::io::wire::ndbc;
using mov::core::ndbc::FileKind;

[[noreturn]] void fail() { std::abort(); }

constexpr std::size_t max_text = std::size_t{4} << 20;

// Every name the parser knows, an alias mapping to its column's index.
struct Known {
  std::string_view token;
  int column;
};
constexpr std::array<Known, 16> known{{{.token = "WDIR", .column = 0},
                                       {.token = "WD", .column = 0},
                                       {.token = "WSPD", .column = 1},
                                       {.token = "GST", .column = 2},
                                       {.token = "WVHT", .column = 3},
                                       {.token = "DPD", .column = 4},
                                       {.token = "APD", .column = 5},
                                       {.token = "MWD", .column = 6},
                                       {.token = "PRES", .column = 7},
                                       {.token = "BAR", .column = 7},
                                       {.token = "ATMP", .column = 8},
                                       {.token = "WTMP", .column = 9},
                                       {.token = "DEWP", .column = 10},
                                       {.token = "VIS", .column = 11},
                                       {.token = "PTDY", .column = 12},
                                       {.token = "TIDE", .column = 13}}};

std::string_view without_bom(std::string_view text) {
  constexpr std::string_view bom{"\xEF\xBB\xBF"};
  return text.starts_with(bom) ? text.substr(bom.size()) : text;
}

// The distinct columns the first non-blank line names after its time
// columns, counted independently of the parser.
std::size_t header_columns(std::string_view text) {
  std::string_view rest = without_bom(text);
  while (not rest.empty()) {
    const std::size_t end = rest.find('\n');
    const std::string_view line = rest.substr(0, end);
    rest = end == std::string_view::npos ? std::string_view{}
                                         : rest.substr(end + 1);
    const std::vector<std::string_view> words = mov::io::detail::split_ws(line);
    if (words.empty()) {
      continue;
    }
    const std::size_t first = words.size() > 4 and words[4] == "mm" ? 5 : 4;
    std::set<int> columns;
    for (std::size_t i = first; i < words.size(); ++i) {
      for (const Known& k : known) {
        if (k.token == words[i]) {
          columns.insert(k.column);
        }
      }
    }
    return columns.size();
  }
  return 0;
}

// The 9-filled missing value of a column, written independently of the
// parser's table (provider-apis.md section 3.2); PTDY has none.
std::optional<double> sentinel_of(const mov::io::ColumnKey& key) {
  struct Sentinel {
    std::string_view quantity;
    double value;
  };
  constexpr std::array<Sentinel, 13> sentinels{{
      {.quantity = "wind_direction", .value = 999.0},
      {.quantity = "wave_direction", .value = 999.0},
      {.quantity = "wind_speed", .value = 99.0},
      {.quantity = "wind_gust", .value = 99.0},
      {.quantity = "visibility", .value = 99.0},
      {.quantity = "wave_height", .value = 99.0},
      {.quantity = "wave_period_dominant", .value = 99.0},
      {.quantity = "wave_period_average", .value = 99.0},
      {.quantity = "water_level", .value = 99.0},
      {.quantity = "air_temperature", .value = 999.0},
      {.quantity = "water_temperature", .value = 999.0},
      {.quantity = "dew_point", .value = 999.0},
      {.quantity = "air_pressure", .value = 9999.0},
  }};
  const std::string_view token = mov::core::token(key.quantity);
  for (const Sentinel& s : sentinels) {
    if (s.quantity == token) {
      return s.value;
    }
  }
  return std::nullopt;
}

std::size_t mm_words(std::string_view text) {
  std::size_t n = 0;
  std::string_view rest = text;
  while (const auto word = mov::io::detail::next_word(rest)) {
    n += *word == "MM" ? 1 : 0;
  }
  return n;
}

std::size_t lines_in(std::string_view text) {
  return static_cast<std::size_t>(std::ranges::count(text, '\n')) + 1;
}

void check_part(const mov::io::Read<mov::io::Part>& r, std::string_view text) {
  const auto& columns = r.value.columns();
  if (columns.size() != header_columns(text)) {
    fail();
  }
  std::size_t missing = 0;
  const mov::core::TimeSeries* first = nullptr;
  for (const auto& [key, series] : columns) {
    if (not(key == mov::io::ColumnKey::of(series))) {
      fail();
    }
    if (first != nullptr and
        not std::ranges::equal(series.times(), first->times())) {
      fail();
    }
    first = &series;
    for (std::size_t i = 1; i < series.size(); ++i) {
      if (not(series.times()[i - 1] < series.times()[i])) {
        fail();
      }
    }
    const std::optional<double> sentinel = sentinel_of(key);
    for (const auto& s : series.samples()) {
      if (s.is_dry() or (sentinel and s.value() == sentinel)) {
        fail();
      }
      missing += s.is_missing() ? 1 : 0;
    }
  }
  std::size_t masked = 0;
  for (const auto& w : r.warnings) {
    masked +=
        w.code == mov::io::WarningCode::legacy_sentinel_masked ? w.count : 0;
  }
  if (missing > masked + mm_words(text)) {
    fail();
  }
}

void check_refusal(const mov::io::Error& error, std::string_view text) {
  if (std::holds_alternative<mov::io::FormatError>(error)) {
    return;
  }
  const auto* p = std::get_if<mov::io::ParseError>(&error);
  if (p == nullptr or p->line() < 1 or p->line() > lines_in(text)) {
    fail();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size < 1) {
    return 0;
  }
  constexpr std::array kinds{FileKind::historical, FileKind::monthly,
                             FileKind::monthly_latest, FileKind::realtime};
  const FileKind kind = kinds.at(data[0] % kinds.size());
  const auto body = std::as_bytes(std::span{data + 1, size - 1});
  mov::io::ReadContext ctx;
  ctx.limits.max_elements = std::size_t{1} << 16;
  ctx.limits.max_text_bytes = max_text;

  std::string text;
  if (mov::io::has_gzip_magic(body)) {
    auto inflated = mov::io::gunzip(body, max_text);
    if (not inflated) {
      if (wn::read_stdmet(body, "", kind, ctx)) {
        fail();  // a body gunzip refuses is never read
      }
      return 0;
    }
    text = *std::move(inflated);
  } else {
    text = std::string(data + 1, data + size);
  }

  const auto reply = wn::read_stdmet(body, "x/y", kind, ctx);
  if (not reply) {
    if (std::holds_alternative<mov::io::Cancelled>(reply.error())) {
      fail();
    }
    if (not mov::io::has_gzip_magic(body)) {
      check_refusal(reply.error(), text);
    }
  } else if (const auto* part =
                 std::get_if<mov::io::Read<mov::io::Part>>(&*reply)) {
    check_part(*part, text);
  } else if (not mov::io::detail::skip_space(text).starts_with('<')) {
    fail();  // a ServiceError for text that is not markup
  }

  if (not mov::io::has_gzip_magic(body) and
      not mov::io::detail::skip_space(text).starts_with('<')) {
    const auto parsed = wn::parse_stdmet(text, kind, ctx);
    if (parsed.has_value() != reply.has_value()) {
      fail();
    }
    if (parsed and
        not(std::get<mov::io::Read<mov::io::Part>>(*reply) == *parsed)) {
      fail();
    }
  }
  return 0;
}
