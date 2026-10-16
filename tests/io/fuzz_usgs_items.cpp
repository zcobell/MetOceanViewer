// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: usgs::read_continuous, read_daily and their parses
// (docs/providers-design.md section 10.4). Byte 0 picks the endpoint (even:
// continuous, odd: daily), the rest is the body. Laws:
//  - no crash, UB or hang; classify_body and classify_error are total;
//  - an accepted page has no more samples than features, in strictly
//    increasing time (or day), and Missing samples only where the text had
//    `null` or `""`; its extent is its first and last time;
//  - its next page is none, a Next or an OffsetEcho, each a Verbatim of the
//    USGS origin whose path is the page's endpoint, without `#`, at most
//    Verbatim::max_bytes; a Next never has `offset` without `cursor`; an
//    OffsetEcho's `offset` and `limit` are the page's features;
//  - a refusal is a ParseError whose line lies inside the text, or a
//    FormatError refused_link or unexpected_link.

#include <chrono>
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
#include "mov/io/detail/text.hpp"
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

const cu::Epoch<cu::Continuous>& continuous() {
  static const cu::Epoch<cu::Continuous> epoch = [] {
    const cu::SeriesInfo row = info("00011");
    const auto range = must(mov::core::TimeRange::make(
        mov::core::Time{}, mov::core::Time{std::chrono::milliseconds{1}}));
    const auto resolved = must(
        cu::resolve(cu::Continuous{.station = station(),
                                   .series = cu::ByTimeSeries{.id = row.id},
                                   .range = range},
                    must(cu::SeriesSet::make({row}))));
    return resolved.epochs()[0];
  }();
  return epoch;
}

const cu::Epoch<cu::Daily>& daily() {
  static const cu::Epoch<cu::Daily> epoch = [] {
    using namespace std::chrono;
    const cu::SeriesInfo row = info("00003");
    const auto days = must(mov::core::DayRange::make(
        local_days{2026y / 1 / 1}, local_days{2026y / 1 / 2}));
    const auto resolved =
        must(cu::resolve(cu::Daily{.station = station(),
                                   .series = cu::ByTimeSeries{.id = row.id},
                                   .days = days},
                         must(cu::SeriesSet::make({row}))));
    return resolved.epochs()[0];
  }();
  return epoch;
}

std::size_t occurrences(std::string_view text, std::string_view what) {
  std::size_t n = 0;
  for (std::size_t at = text.find(what); at != std::string_view::npos;
       at = text.find(what, at + 1)) {
    ++n;
  }
  return n;
}

// The value of the query parameter `name` of `url`, as the reader reads it.
std::optional<std::string_view> parameter(std::string_view url,
                                          std::string_view name) {
  const std::size_t mark = url.find('?');
  if (mark == std::string_view::npos) {
    return std::nullopt;
  }
  std::string_view query = url.substr(mark + 1);
  while (not query.empty()) {
    const std::size_t amp = query.find('&');
    const std::string_view pair = query.substr(0, amp);
    const std::size_t equals = pair.find('=');
    if (pair.substr(0, equals) == name) {
      return equals == std::string_view::npos ? std::string_view{}
                                              : pair.substr(equals + 1);
    }
    if (amp == std::string_view::npos) {
      break;
    }
    query.remove_prefix(amp + 1);
  }
  return std::nullopt;
}

void check_link(const mov::io::wire::Verbatim& link, wu::Endpoint endpoint) {
  const std::string prefix =
      std::string{mov::io::wire::base(mov::io::wire::Origin::usgs)} +
      std::string{wu::items_path(endpoint)};
  const std::string& url = link.url();
  if (link.origin() != mov::io::wire::Origin::usgs or
      not url.starts_with(prefix) or
      (url.size() > prefix.size() and url[prefix.size()] != '?') or
      url.find('#') != std::string::npos or
      url.size() > mov::io::wire::Verbatim::max_bytes) {
    fail();
  }
}

void check_paging(const wu::Paging& paging, wu::Endpoint endpoint,
                  std::size_t samples) {
  if (samples > paging.features) {
    fail();
  }
  if (const auto* next = std::get_if<wu::Next>(&paging.next)) {
    check_link(next->link, endpoint);
    if (parameter(next->link.url(), "offset") and
        not parameter(next->link.url(), "cursor")) {
      fail();
    }
  }
  if (const auto* echo = std::get_if<wu::OffsetEcho>(&paging.next)) {
    check_link(echo->link, endpoint);
    const std::string& url = echo->link.url();
    const auto number =
        [&](std::string_view name) -> std::optional<std::uint64_t> {
      const auto text = parameter(url, name);
      if (not text) {
        return std::nullopt;
      }
      const auto n = mov::io::detail::parse_int<std::uint64_t>(*text);
      return n ? std::optional<std::uint64_t>{*n} : std::nullopt;
    };
    if (parameter(url, "cursor") or number("offset") != paging.features or
        number("limit") != paging.features) {
      fail();
    }
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
  if (f == nullptr or (f->code != mov::io::FormatErrc::refused_link and
                       f->code != mov::io::FormatErrc::unexpected_link)) {
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
  const auto& series = r->value.values;
  for (std::size_t i = 1; i < series.size(); ++i) {
    if (not(series.times()[i - 1] < series.times()[i])) {
      fail();
    }
  }
  const auto extent = r->value.extent();
  if (series.empty() != not extent.has_value()) {
    fail();
  }
  if (extent and (extent->first() != series.times().front() or
                  extent->last() != series.times().back())) {
    fail();
  }
  check_missing(series.samples(), body);
  check_paging(r->value.paging, wu::Endpoint::continuous, series.size());
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
  check_paging(r->value.paging, wu::Endpoint::daily, values.size());
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
  const auto refusal = wu::classify_error(body, "application/json", {});
  if (refusal.text().size() > mov::io::wire::ServiceError::max_text_bytes) {
    fail();
  }
  return 0;
}
