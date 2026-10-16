// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: usgs::read_continuous, read_daily and their parses
// (docs/providers-design.md section 10.4). Byte 0 picks the collection
// (even: continuous, odd: daily), the rest is the body. Laws:
//  - no crash, UB or hang; classify_body is total;
//  - an accepted page has no more samples than features, in strictly
//    increasing time (or day), and Missing samples only where the text had
//    `null` or `""`;
//  - its next link is absent, or a Verbatim of the USGS origin whose path is
//    the page's collection;
//  - a refusal is a ParseError whose line lies inside the text, or a
//    FormatError foreign_link.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "mov/core/provider/usgs.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/windows.hpp"
#include "mov/io/detail/usgs_json.hpp"
#include "mov/io/error.hpp"
#include "mov/io/read_limits.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/service.hpp"
#include "mov/io/wire/usgs.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace cu = mov::core::usgs;
namespace wu = mov::io::wire::usgs;

[[noreturn]] void fail() { std::abort(); }

template <class E>
auto must(E e) {
  if (not e) {
    fail();
  }
  return *std::move(e);
}

cu::SeriesInfo info(std::string_view statistic) {
  return {
      .id = must(cu::TimeSeriesId::make("66a4e3fb8abe42fcb0942b5c0fe98f68")),
      .parameter = must(cu::ParameterCode::make("00065")),
      .statistic = must(cu::StatisticId::make(statistic)),
      .parameter_name = "Gage height",
      .primary = true,
      .unit = mov::core::parse_unit("ft"),
      .validity = {}};
}

mov::core::StationId<mov::core::provider::Usgs> station() {
  return must(
      mov::core::StationId<mov::core::provider::Usgs>::make("USGS-01646500"));
}

const cu::Resolved<cu::Continuous>& continuous() {
  static const cu::Resolved<cu::Continuous> resolved = [] {
    const cu::SeriesInfo row = info("00011");
    const auto range = must(mov::core::TimeRange::make(
        mov::core::Time{}, mov::core::Time{std::chrono::milliseconds{1}}));
    return must(
        cu::resolve(cu::Continuous{.station = station(),
                                   .series = cu::ByTimeSeries{.id = row.id},
                                   .range = range},
                    must(cu::SeriesSet::make({row}))));
  }();
  return resolved;
}

const cu::Resolved<cu::Daily>& daily() {
  static const cu::Resolved<cu::Daily> resolved = [] {
    using namespace std::chrono;
    const cu::SeriesInfo row = info("00003");
    const auto days = must(mov::core::DayRange::make(
        local_days{2026y / 1 / 1}, local_days{2026y / 1 / 2}));
    return must(cu::resolve(cu::Daily{.station = station(),
                                      .series = cu::ByTimeSeries{.id = row.id},
                                      .days = days},
                            must(cu::SeriesSet::make({row}))));
  }();
  return resolved;
}

std::size_t occurrences(std::string_view text, std::string_view what) {
  std::size_t n = 0;
  for (std::size_t at = text.find(what); at != std::string_view::npos;
       at = text.find(what, at + 1)) {
    ++n;
  }
  return n;
}

void check_paging(const wu::Paging& paging, wu::Collection collection,
                  std::size_t samples) {
  if (samples > paging.features) {
    fail();
  }
  if (not paging.next) {
    return;
  }
  const std::string prefix =
      std::string{mov::io::wire::base(mov::io::wire::Origin::usgs)} +
      std::string{wu::items_path(collection)};
  const std::string& url = paging.next->url();
  if (paging.next->origin() != mov::io::wire::Origin::usgs or
      not url.starts_with(prefix) or
      (url.size() > prefix.size() and url[prefix.size()] != '?')) {
    fail();
  }
}

template <class Samples>
void check_missing(const Samples& samples, std::string_view body) {
  std::size_t missing = 0;
  for (const mov::core::Sample& s : samples) {
    missing += s.is_missing() ? 1 : 0;
    if (s.is_dry()) {
      fail();
    }
  }
  if (missing > occurrences(body, "null") + occurrences(body, "\"\"")) {
    fail();
  }
}

template <class Result>
void check_error(const Result& r, std::string_view body) {
  if (const auto* p = std::get_if<mov::io::ParseError>(&r.error())) {
    if (p->line() < 1 or p->line() > occurrences(body, "\n") + 1) {
      fail();
    }
    return;
  }
  const auto* f = std::get_if<mov::io::FormatError>(&r.error());
  if (f == nullptr or f->code != mov::io::FormatErrc::foreign_link) {
    fail();
  }
}

void continuous_page(std::string_view body) {
  const auto r =
      mov::io::detail::usgs::parse_continuous(body, continuous(), {});
  if (not r) {
    check_error(r, body);
    return;
  }
  const auto& part = r->value.part;
  if (part.columns().size() != 1) {
    fail();
  }
  const auto& column = part.columns().begin()->second;
  for (std::size_t i = 1; i < column.size(); ++i) {
    if (not(column.times()[i - 1] < column.times()[i])) {
      fail();
    }
  }
  if (column.empty() != not r->value.extent.has_value()) {
    fail();
  }
  check_missing(column.samples(), body);
  check_paging(r->value.paging, wu::Collection::continuous, column.size());
}

void daily_page(std::string_view body) {
  const auto r = mov::io::detail::usgs::parse_daily(body, daily(), {});
  if (not r) {
    check_error(r, body);
    return;
  }
  const auto& values = r->value.values;
  for (std::size_t i = 1; i < values.size(); ++i) {
    if (not(values.days()[i - 1] < values.days()[i])) {
      fail();
    }
  }
  check_missing(values.samples(), body);
  check_paging(r->value.paging, wu::Collection::daily, values.size());
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  if (size < 1) {
    return 0;
  }
  const bool is_daily = (data[0] % 2) == 1;
  const std::string storage(data + 1, data + size);
  const std::string_view body{storage};
  // The entry points: classification, then the parse of a Payload.
  if (is_daily) {
    if (const auto entry =
            wu::read_daily(body, "application/json", daily(), {});
        not entry) {
      check_error(entry, body);
    }
    daily_page(body);
  } else {
    if (const auto entry =
            wu::read_continuous(body, "application/json", continuous(), {});
        not entry) {
      check_error(entry, body);
    }
    continuous_page(body);
  }
  // classify_error is total: every body is some refusal.
  const auto refusal = wu::classify_error(body, "application/json");
  if (refusal.text().size() > mov::io::wire::ServiceError::max_text_bytes) {
    fail();
  }
  return 0;
}
