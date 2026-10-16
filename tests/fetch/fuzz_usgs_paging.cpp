// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// libFuzzer target: a chain of USGS continuous pages, read by io and
// followed by the fetch plan (docs/providers-design.md sections 5.2 and
// 10.4). The bytes are the bodies of successive responses, separated by
// 0xFF (a byte no UTF-8 text has). Each body is read as a page
// (io::detail::usgs::parse_continuous) and its summary handed to
// fetch::usgs::next. Laws:
//  - no crash, UB or hang;
//  - the chain ends within the window's max_pages;
//  - it never follows a link it followed before, a link on an empty page,
//    or past a page whose first time is not after the last time seen, nor
//    an offset echo after the chain's first page (P7's rule, applied here).

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/provider/usgs.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/fetch/paging.hpp"
#include "mov/fetch/plan/usgs.hpp"
#include "mov/io/detail/usgs_json.hpp"
#include "mov/io/wire/origin.hpp"
#include "mov/io/wire/usgs.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size);

namespace {

namespace cu = mov::core::usgs;
namespace fu = mov::fetch::usgs;
using Link = mov::io::wire::Verbatim;
using Unit = fu::ContinuousUnit<Link>;

[[noreturn]] void fail() { std::abort(); }

template <class E>
auto must(E e) {
  if (not e) {
    fail();
  }
  return *std::move(e);
}

// One window of 1000 days: at most max_pages({1000 d, 1 min, 50000}) = 31
// pages.
const fu::DataRequest<cu::Continuous>& request() {
  static const fu::DataRequest<cu::Continuous> bound = [] {
    const cu::SeriesInfo row{
        .id = must(cu::TimeSeriesId::make("66a4e3fb8abe42fcb0942b5c0fe98f68")),
        .parameter = must(cu::ParameterCode::make("00060")),
        .statistic = cu::StatisticId::instantaneous(),
        .parameter_name = "Discharge",
        .primary = true,
        .unit = mov::core::parse_unit("cfs"),
        .validity = {}};
    const mov::core::Time begin{std::chrono::milliseconds{1'759'276'800'000}};
    const cu::Continuous continuous{
        .station = must(mov::core::StationId<mov::core::provider::Usgs>::make(
            "USGS-01646500")),
        .series = cu::ByTimeSeries{.id = row.id},
        .range = must(mov::core::TimeRange::make(
            begin, begin + std::chrono::milliseconds{86'400'000LL * 1000}))};
    return must(fu::bind(continuous, {row}));
  }();
  return bound;
}

// P7's rule: a Next is followed; an OffsetEcho only from a chain's first
// page (from a later one it re-reads the window).
std::optional<Link> link_to_follow(const mov::io::wire::usgs::NextPage& next,
                                   bool first_page) {
  if (const auto* n = std::get_if<mov::io::wire::usgs::Next>(&next)) {
    return n->link;
  }
  if (const auto* echo = std::get_if<mov::io::wire::usgs::OffsetEcho>(&next);
      echo != nullptr and first_page) {
    return echo->link;
  }
  return std::nullopt;
}

std::vector<std::string_view> bodies(std::string_view bytes) {
  std::vector<std::string_view> out;
  while (true) {
    const std::size_t cut = bytes.find('\xFF');
    out.push_back(bytes.substr(0, cut));
    if (cut == std::string_view::npos) {
      return out;
    }
    bytes.remove_prefix(cut + 1);
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data,
                                      std::size_t size) {
  const std::string storage(data, data + size);
  auto units = fu::initial<Link>(request());
  if (not units or units->size() != 1) {
    fail();
  }
  Unit unit = units->front();
  const std::uint32_t budget =
      mov::fetch::max_pages({.span = unit.window.end() - unit.window.begin(),
                             .cadence = fu::finest_cadence,
                             .page_rows = fu::page_rows});
  std::vector<std::string> followed;
  std::optional<mov::core::Time> last;
  std::uint32_t pages = 0;
  for (const std::string_view body : bodies(storage)) {
    ++pages;
    if (pages > budget) {
      fail();
    }
    const auto page = mov::io::detail::usgs::parse_continuous(
        body, fu::epoch_of(request(), unit), {});
    if (not page) {
      return 0;  // the unit fails with the parse error
    }
    const auto extent = page->value.extent();
    const mov::fetch::PageSummary<Link, mov::core::Time> summary{
        .rows = page->value.paging.features,
        .extent = extent
                      ? std::optional{mov::fetch::Extent<mov::core::Time>{
                            .first = extent->first(), .last = extent->last()}}
                      : std::nullopt,
        .next = link_to_follow(page->value.paging.next, pages == 1)};
    const auto next = fu::next(unit, summary);
    if (not next or next->empty()) {
      return 0;  // a paging rule ended the chain, or the page did
    }
    // Followed: the page had rows and advanced, and the link is new.
    const std::optional<Link>& followed_link = next->front().page.link;
    if (not followed_link) {
      fail();  // every page after the first has its link
    }
    const Link& link = *followed_link;
    if (summary.rows == 0 or
        std::ranges::find(followed, link.url()) != followed.end() or
        (extent and last and not(*last < extent->first()))) {
      fail();
    }
    followed.push_back(link.url());
    if (extent) {
      last = extent->last();
    }
    unit = next->front();
  }
  return 0;
}
