// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: the CO-OPS metadata parsers (docs/providers-design.md
// section 10.4). Byte 0 picks the parser (station list, tide-prediction
// list, datums.json), the rest is the body. Laws:
//  - no crash, UB or hang;
//  - every station kept has a CO-OPS id (seven digits), and every station
//    left out is one station_skipped warning: kept + skipped never exceeds
//    the number of elements the list can have (its commas plus one);
//  - a datum table, when there is one, is MSL-based (MSL at 0) and every
//    datum offered is one the body named;
//  - a refusal is a ParseError (FormatError for datums.json's units and
//    heights) whose line lies inside the text.
// (Every datum offered is one the body named: checked when the body has no
// escapes, which could spell a name without its letters.)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "mov/core/ascii.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/provider/coops.hpp"
#include "mov/core/units.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/coops.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace wc = mov::io::wire::coops;
namespace cc = mov::core::coops;

[[noreturn]] void fail() { std::abort(); }

std::size_t count_of(std::string_view text, char c) {
  return static_cast<std::size_t>(std::ranges::count(text, c));
}

template <class Result>
void check_error(const Result& r, std::string_view body, bool format_ok) {
  if (const auto* p = std::get_if<mov::io::ParseError>(&r.error())) {
    if (p->line() < 1 or p->line() > count_of(body, '\n') + 1) {
      fail();
    }
    return;
  }
  if (not(format_ok and
          std::holds_alternative<mov::io::FormatError>(r.error()))) {
    fail();
  }
}

std::size_t skipped(const std::vector<mov::io::Warning>& warnings) {
  return static_cast<std::size_t>(
      std::ranges::count(warnings, mov::io::WarningCode::station_skipped,
                         &mov::io::Warning::code));
}

bool seven_digits(std::string_view id) {
  return id.size() == 7 and std::ranges::all_of(id, mov::core::ascii::is_digit);
}

template <class Records, class IdOf>
void check_list(const Records& r, std::string_view body, IdOf id_of) {
  if (not r) {
    check_error(r, body, false);
    return;
  }
  for (const auto& record : r->value) {
    if (not seven_digits(id_of(record))) {
      fail();
    }
  }
  if (r->value.size() + skipped(r->warnings) > count_of(body, ',') + 1) {
    fail();
  }
}

void check_datums(std::string_view body) {
  const auto r = wc::parse_datums(body, mov::io::ReadContext{});
  if (not r) {
    check_error(r, body, true);
    return;
  }
  if (const auto& table = r->value.table) {
    const auto msl = table->height_above_msl(mov::core::VerticalDatum::msl);
    if (not msl or msl->as(mov::core::LengthUnit::meter) != 0.0) {
      fail();
    }
  }
  // A name spelled with escapes is not in the text as such.
  if (body.find('\\') != std::string_view::npos) {
    return;
  }
  for (const cc::Datum d : cc::every_datum) {
    if (r->value.offered.contains(d) and
        body.find(d == cc::Datum::navd88 ? "NAVD88" : cc::wire_token(d)) ==
            std::string_view::npos) {
      fail();
    }
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size < 1) {
    return 0;
  }
  const std::string storage(data + 1, data + size);
  const std::string_view body{storage};
  mov::io::ReadContext ctx;
  ctx.limits.max_elements = std::size_t{1} << 16;
  switch (data[0] % 3) {
    case 0:
      check_list(
          wc::parse_stations(body, ctx), body,
          [](const wc::StationRecord& s) { return s.station.id.value(); });
      break;
    case 1:
      check_list(wc::parse_prediction_stations(body, ctx), body,
                 [](const wc::PredictionStation& s) { return s.id.value(); });
      break;
    default:
      check_datums(body);
      break;
  }
  return 0;
}
