// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: usgs::read_series, read_catalog and their parses
// (docs/providers-design.md section 10.4). Byte 0 picks the reader (even:
// a site's series, odd: the catalog), the rest is the body. Laws:
//  - no crash, UB or hang;
//  - every row kept is one feature: rows plus series_skipped never exceed
//    the features, and equal them when the page has no error;
//  - every station id passes StationId<Usgs> again, unchanged; every
//    validity is ordered; every location is a Location;
//  - the next link is absent, or a Verbatim of the USGS origin whose path
//    is combined-metadata's;
//  - a refusal is a ParseError whose line lies inside the text, or a
//    FormatError foreign_link.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/provider/usgs.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/io/detail/usgs_json.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/usgs.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace cu = mov::core::usgs;
namespace wu = mov::io::wire::usgs;

[[noreturn]] void fail() { std::abort(); }

std::size_t lines(std::string_view text) {
  std::size_t n = 1;
  for (const char c : text) {
    n += c == '\n' ? 1 : 0;
  }
  return n;
}

template <class Result>
void check_error(const Result& r, std::string_view body) {
  if (const auto* p = std::get_if<mov::io::ParseError>(&r.error())) {
    if (p->line() < 1 or p->line() > lines(body)) {
      fail();
    }
    return;
  }
  const auto* f = std::get_if<mov::io::FormatError>(&r.error());
  if (f == nullptr or f->code != mov::io::FormatErrc::foreign_link) {
    fail();
  }
}

void check_series(const cu::SeriesInfo& s) {
  const auto first = s.validity.first();
  const auto last = s.validity.last();
  if (first and last and *last < *first) {
    fail();
  }
  if (s.id.view().size() != 32 or s.parameter.view().size() != 5) {
    fail();
  }
}

void check_paging(const wu::Paging& paging, std::size_t rows,
                  const std::vector<mov::io::Warning>& warnings) {
  std::size_t skipped = 0;
  for (const mov::io::Warning& w : warnings) {
    skipped += w.code == mov::io::WarningCode::series_skipped ? w.count : 0;
  }
  if (rows + skipped != paging.features) {
    fail();
  }
  if (not paging.next) {
    return;
  }
  const std::string prefix =
      std::string{mov::io::wire::base(mov::io::wire::Origin::usgs)} +
      std::string{wu::items_path(wu::Collection::combined_metadata)};
  const std::string& url = paging.next->url();
  if (not url.starts_with(prefix) or
      (url.size() > prefix.size() and url[prefix.size()] != '?')) {
    fail();
  }
}

void series(std::string_view body) {
  if (const auto entry = wu::read_series(body, "application/json", {});
      not entry) {
    check_error(entry, body);
  }
  const auto r = mov::io::detail::usgs::parse_series(body, {});
  if (not r) {
    check_error(r, body);
    return;
  }
  for (const cu::SeriesInfo& s : r->value.series) {
    check_series(s);
  }
  check_paging(r->value.paging, r->value.series.size(), r->warnings);
}

void catalog(std::string_view body) {
  if (const auto entry = wu::read_catalog(body, "application/json", {});
      not entry) {
    check_error(entry, body);
  }
  const auto r = mov::io::detail::usgs::parse_catalog(body, {});
  if (not r) {
    check_error(r, body);
    return;
  }
  for (const wu::SiteSeries& row : r->value.rows) {
    check_series(row.series);
    const auto again = mov::core::StationId<mov::core::provider::Usgs>::make(
        row.station.value());
    if (not again or not(*again == row.station)) {
      fail();
    }
    if (not mov::core::Location::make(
            {.lat = row.location.lat(), .lon = row.location.lon()})) {
      fail();
    }
  }
  check_paging(r->value.paging, r->value.rows.size(), r->warnings);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size < 1) {
    return 0;
  }
  const std::string storage(data + 1, data + size);
  if ((data[0] % 2) == 0) {
    series(storage);
  } else {
    catalog(storage);
  }
  return 0;
}
