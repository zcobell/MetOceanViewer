// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// The two stages of a USGS fetch (docs/providers-design.md §5.2): stage 1
// reads the site's series and resolves the request to one (a bind), stage 2
// fetches that series, a window at a time, a page at a time. Templates over
// the link type of a `next` page (io's wire::Verbatim), which fetch never
// looks into.

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

#include "mov/core/provider/usgs.hpp"
#include "mov/core/time.hpp"
#include "mov/core/windows.hpp"
#include "mov/fetch/error.hpp"
#include "mov/fetch/paging.hpp"

namespace mov::fetch::usgs {

/// The page size every USGS target asks for (`limit`).
inline constexpr std::uint32_t page_rows = 50000;
/// The finest cadence of instantaneous values, for max_pages.
inline constexpr std::chrono::minutes finest_cadence{1};
/// Pages a site's series list may take (one is the rule).
inline constexpr std::uint32_t max_resolution_pages = 4;

/// One page of a chain: its guard, and the link that fetches it (nullopt
/// for the first page, whose target is composed from the request).
template <class Link, class Position>
struct PageUnit {
  PageGuard<Link, Position> guard;
  std::optional<Link> link;
  friend bool operator==(const PageUnit&, const PageUnit&) = default;
};

/// The page that continues `unit`, after the page `summary` describes: none
/// when the chain ends, one when its link may be followed. A page that
/// breaks a paging rule fails the unit (PageGuard::after).
template <class Link, class Position>
[[nodiscard]] std::expected<std::optional<PageUnit<Link, Position>>, Error>
next_page(const PageUnit<Link, Position>& unit,
          const PageSummary<Link, Position>& summary) {
  auto turn = unit.guard.after(summary);
  if (not turn) {
    return std::unexpected{Error{std::move(turn).error()}};
  }
  if (not *turn) {
    return std::optional<PageUnit<Link, Position>>{};
  }
  return std::optional<PageUnit<Link, Position>>{PageUnit<Link, Position>{
      .guard = std::move((*turn)->guard), .link = std::move((*turn)->link)}};
}

// ---- stage 1: resolution --------------------------------------------------

/// A page of the site's series. Series rows have no order, so the guard
/// checks only the page count, empty pages and repeated links.
template <class Link>
struct ResolutionUnit {
  PageUnit<Link, core::Time> page;
  friend bool operator==(const ResolutionUnit&,
                         const ResolutionUnit&) = default;
};

template <class Link>
[[nodiscard]] ResolutionUnit<Link> first_resolution_page() {
  return {.page = {
              .guard = PageGuard<Link, core::Time>::first(max_resolution_pages),
              .link = std::nullopt}};
}

template <class Link>
[[nodiscard]] UnitLabel label(const ResolutionUnit<Link>& unit) {
  return Resolution{.page = unit.page.guard.taken() - 1};
}

template <class Link>
[[nodiscard]] std::expected<std::vector<ResolutionUnit<Link>>, Error> next(
    const ResolutionUnit<Link>& unit,
    const PageSummary<Link, core::Time>& summary) {
  return next_page(unit.page, summary)
      .transform([](std::optional<PageUnit<Link, core::Time>> page) {
        std::vector<ResolutionUnit<Link>> units;
        if (page) {
          units.push_back({.page = *std::move(page)});
        }
        return units;
      });
}

/// What stage 1 hands stage 2: the request and the series it resolved to.
template <class C>
struct DataRequest {
  C request;
  core::usgs::Resolved<C> series;
  friend bool operator==(const DataRequest&, const DataRequest&) = default;
};

/// The bind between the stages: the site's series rows (stage 1's value) to
/// stage 2's request. Several Primary series of the parameter and statistic
/// are AmbiguousSeries (with their ids), a repeated id a ParseError
/// (corrupt_record), no series NoData, a series of the other collection
/// WrongCollection; each failure is labelled Resolution{0}.
[[nodiscard]] std::expected<DataRequest<core::usgs::Continuous>, Failure> bind(
    const core::usgs::Continuous& request,
    const std::vector<core::usgs::SeriesInfo>& rows);
[[nodiscard]] std::expected<DataRequest<core::usgs::Daily>, Failure> bind(
    const core::usgs::Daily& request,
    const std::vector<core::usgs::SeriesInfo>& rows);

// ---- stage 2: data --------------------------------------------------------

/// A page of one window of instantaneous values.
template <class Link>
struct ContinuousUnit {
  core::TimeRange window;
  PageUnit<Link, core::Time> page;
  friend bool operator==(const ContinuousUnit&,
                         const ContinuousUnit&) = default;
};

/// A page of daily values.
template <class Link>
struct DailyUnit {
  core::DayRange days;
  PageUnit<Link, std::chrono::local_days> page;
  friend bool operator==(const DailyUnit&, const DailyUnit&) = default;
};

/// The windows of a continuous request: its range widened to whole seconds,
/// split at 1000 days. Unalignable only at the ends of the time line.
[[nodiscard]] constexpr std::expected<core::Windows, Unalignable> windows(
    const core::usgs::Continuous& request) noexcept {
  const auto range = core::aligned(request.range, core::usgs::grain);
  if (not range) {
    return std::unexpected{Unalignable{.range = request.range}};
  }
  return core::split(*range, core::usgs::max_window(request));
}

/// The first page of every window, in time order. Each window may take
/// max_pages(window, 1 min, page_rows) pages.
template <class Link>
[[nodiscard]] std::expected<std::vector<ContinuousUnit<Link>>, Error> initial(
    const DataRequest<core::usgs::Continuous>& request) {
  const auto cut = windows(request.request);
  if (not cut) {
    return std::unexpected{Error{cut.error()}};
  }
  std::vector<ContinuousUnit<Link>> units;
  units.reserve(cut->size());
  for (const core::TimeRange window : *cut) {
    const auto pages =
        max_pages(window.end() - window.begin(), finest_cadence, page_rows);
    units.push_back(
        {.window = window,
         .page = {.guard = PageGuard<Link, core::Time>::first(pages),
                  .link = std::nullopt}});
  }
  return units;
}

/// One unit: daily values have no window limit (36 years came back on one
/// page); the chain may take max_pages(days, 1 day, page_rows) pages.
template <class Link>
[[nodiscard]] std::vector<DailyUnit<Link>> initial(
    const DataRequest<core::usgs::Daily>& request) {
  const core::DayRange days = request.request.days;
  const auto pages = max_pages(
      std::chrono::duration_cast<std::chrono::milliseconds>(days.size()),
      std::chrono::days{1}, page_rows);
  return {DailyUnit<Link>{
      .days = days,
      .page = {.guard = PageGuard<Link, std::chrono::local_days>::first(pages),
               .link = std::nullopt}}};
}

template <class Link>
[[nodiscard]] UnitLabel label(const ContinuousUnit<Link>& unit) {
  return Page{.window = unit.window, .index = unit.page.guard.taken() - 1};
}

template <class Link>
[[nodiscard]] UnitLabel label(const DailyUnit<Link>& unit) {
  return DayWindow{.days = unit.days};
}

template <class Link>
[[nodiscard]] std::expected<std::vector<ContinuousUnit<Link>>, Error> next(
    const ContinuousUnit<Link>& unit,
    const PageSummary<Link, core::Time>& summary) {
  return next_page(unit.page, summary)
      .transform([&](std::optional<PageUnit<Link, core::Time>> page) {
        std::vector<ContinuousUnit<Link>> units;
        if (page) {
          units.push_back({.window = unit.window, .page = *std::move(page)});
        }
        return units;
      });
}

template <class Link>
[[nodiscard]] std::expected<std::vector<DailyUnit<Link>>, Error> next(
    const DailyUnit<Link>& unit,
    const PageSummary<Link, std::chrono::local_days>& summary) {
  return next_page(unit.page, summary)
      .transform(
          [&](std::optional<PageUnit<Link, std::chrono::local_days>> page) {
            std::vector<DailyUnit<Link>> units;
            if (page) {
              units.push_back({.days = unit.days, .page = *std::move(page)});
            }
            return units;
          });
}

}  // namespace mov::fetch::usgs
