// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: coops::classify_body, read_data and its parse
// (docs/providers-design.md section 10.4). Byte 0 picks the series (one of
// every kind), the rest is the body. Laws:
//  - no crash, UB or hang; classify_body is total;
//  - an accepted body gives exactly the series' columns, each in strictly
//    increasing time, and Missing samples only where the text had "" (so
//    never more Missing samples than occurrences of "");
//  - a refusal is a ParseError whose line lies inside the text.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <variant>

#include "mov/core/provider/coops.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/detail/coops_json.hpp"
#include "mov/io/error.hpp"
#include "mov/io/part.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/coops.hpp"
#include "mov/io/wire/origin.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace cc = mov::core::coops;
namespace wc = mov::io::wire::coops;

[[noreturn]] void fail() { std::abort(); }

wc::Series met(cc::Met m) {
  return cc::Meteorological{.product = m,
                            .interval = cc::MetInterval::six_minute};
}

// Every kind of series a body can be parsed as.
wc::Series series_at(std::uint8_t pick) {
  switch (pick % 14) {
    case 0:
      return wc::Observation{.series = cc::Observed::six_minute,
                             .datum = cc::Datum::msl};
    case 1:
      return wc::Observation{.series = cc::Observed::hourly,
                             .datum = cc::Datum::mllw};
    case 2:
      return wc::Observation{.series = cc::Observed::one_minute,
                             .datum = cc::Datum::stnd};
    case 3:
      return wc::Prediction{.interval = cc::PredictionInterval::six,
                            .datum = cc::Datum::mllw};
    case 4:
      return wc::Prediction{.interval = cc::PredictionInterval::sixty,
                            .datum = cc::Datum::navd88};
    case 5:
      return met(cc::Met::air_temperature);
    case 6:
      return met(cc::Met::water_temperature);
    case 7:
      return met(cc::Met::wind);
    case 8:
      return met(cc::Met::air_pressure);
    case 9:
      return met(cc::Met::humidity);
    case 10:
      return met(cc::Met::conductivity);
    case 11:
      return met(cc::Met::salinity);
    case 12:
      return met(cc::Met::visibility);
    default:
      return cc::AirGap{};
  }
}

std::size_t expected_columns(const wc::Series& series) {
  const auto* m = std::get_if<cc::Meteorological>(&series);
  return m != nullptr and m->product == cc::Met::wind ? 3 : 1;
}

std::size_t occurrences(std::string_view text, std::string_view what) {
  std::size_t n = 0;
  for (std::size_t at = text.find(what); at != std::string_view::npos;
       at = text.find(what, at + 1)) {
    ++n;
  }
  return n;
}

std::size_t line_count(std::string_view text) {
  return occurrences(text, "\n") + 1;
}

void check_columns(const mov::io::Part& part, const wc::Series& series,
                   std::string_view body) {
  const auto& columns = part.columns();
  if (columns.size() != expected_columns(series)) {
    fail();
  }
  const std::size_t empties = occurrences(body, "\"\"");
  std::size_t missing = 0;
  for (const auto& [key, column] : columns) {
    if (not(key == mov::io::ColumnKey::of(column))) {
      fail();
    }
    for (std::size_t i = 1; i < column.size(); ++i) {
      if (not(column.times()[i - 1] < column.times()[i])) {
        fail();
      }
    }
    for (const mov::core::Sample& s : column.samples()) {
      missing += s.is_missing() ? 1 : 0;
      if (s.is_dry()) {
        fail();
      }
    }
  }
  if (missing > empties) {
    fail();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size < 1) {
    return 0;
  }
  const wc::Series series = series_at(data[0]);
  const std::string storage(data + 1, data + size);
  const std::string_view body{storage};

  // read_data is classify_body, then the parse of a Payload.
  const auto reply = wc::read_data(body, "x/y", series, mov::io::ReadContext{});
  if (reply) {
    if (const auto* read = std::get_if<mov::io::Read<mov::io::Part>>(&*reply)) {
      check_columns(read->value, series, body);
    }
  }

  mov::io::ReadContext ctx;
  ctx.limits.max_elements = std::size_t{1} << 16;
  const auto parsed = mov::io::detail::coops::parse_data(body, series, ctx);
  if (parsed) {
    check_columns(parsed->value, series, body);
    return 0;
  }
  const auto* error = std::get_if<mov::io::ParseError>(&parsed.error());
  if (error == nullptr or error->line() < 1 or
      error->line() > line_count(body)) {
    fail();
  }
  return 0;
}
