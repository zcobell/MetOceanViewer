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
/// breaks a paging rule fails the unit with the PagingError
/// (PageGuard::after).
template <class Link, class Position>
[[nodiscard]] std::expected<std::optional<PageUnit<Link, Position>>, Error>
next_page(const PageUnit<Link, Position>& unit,
          const PageSummary<Link, Position>& summary) {
  auto turn = unit.guard.after(summary);
  if (not turn) {
    return std::unexpected{Error{turn.error()}};
  }
  if (not turn->has_value()) {
    return std::optional<PageUnit<Link, Position>>{};
  }
  auto& t = turn->value();
  return std::optional<PageUnit<Link, Position>>{PageUnit<Link, Position>{
      .guard = std::move(t.guard), .link = std::move(t.link)}};
}

/// The units that continue a chain: the next page as the unit `make` builds
/// from it, or none.
template <class Unit, class Link, class Position, class Make>
[[nodiscard]] std::expected<std::vector<Unit>, Error> continue_chain(
    const PageUnit<Link, Position>& page,
    const PageSummary<Link, Position>& summary, Make make) {
  return next_page(page, summary)
      .transform([&](std::optional<PageUnit<Link, Position>> next) {
        std::vector<Unit> units;
        if (next) {
          units.push_back(make(*std::move(next)));
        }
        return units;
      });
}

// ---- stage 1: resolution --------------------------------------------------

/// A page of the site's series. Series rows have no order (NoPosition), so
/// the guard checks only the page count, empty pages and repeated links.
template <class Link>
struct ResolutionUnit {
  PageUnit<Link, NoPosition> page;
  friend bool operator==(const ResolutionUnit&,
                         const ResolutionUnit&) = default;
};

template <class Link>
[[nodiscard]] ResolutionUnit<Link> first_resolution_page() {
  return {.page = {
              .guard = PageGuard<Link, NoPosition>::first(max_resolution_pages),
              .link = std::nullopt}};
}

template <class Link>
[[nodiscard]] UnitLabel label(const ResolutionUnit<Link>& unit) {
  return Resolution{.page = unit.page.guard.taken() - 1};
}

template <class Link>
[[nodiscard]] std::expected<std::vector<ResolutionUnit<Link>>, Error> next(
    const ResolutionUnit<Link>& unit,
    const PageSummary<Link, NoPosition>& summary) {
  return continue_chain<ResolutionUnit<Link>>(
      unit.page, summary, [](PageUnit<Link, NoPosition> page) {
        return ResolutionUnit<Link>{.page = std::move(page)};
      });
}

template <class C>
class DataRequest;

/// The bind between the stages: the site's series rows (stage 1's value) to
/// stage 2's request (core::usgs::resolve: the logical series and its
/// epochs). A repeated id is DuplicateSeries; a request without a
/// sublocation at a site with several sublocation groups and none without
/// one AmbiguousSeries (with their keys); no series, or no epoch meeting the
/// range, NoData; a series of the other collection WrongCollection. Each
/// failure is labelled Resolution{0}.
[[nodiscard]] std::expected<DataRequest<core::usgs::Continuous>, Failure> bind(
    const core::usgs::Continuous& request,
    const std::vector<core::usgs::SeriesInfo>& rows);
[[nodiscard]] std::expected<DataRequest<core::usgs::Daily>, Failure> bind(
    const core::usgs::Daily& request,
    const std::vector<core::usgs::SeriesInfo>& rows);

/// What stage 1 hands stage 2: the request and the series it resolved to.
/// Made only by bind.
template <class C>
class DataRequest {
 public:
  [[nodiscard]] const C& request() const& noexcept { return request_; }
  const C& request() && = delete;
  [[nodiscard]] const core::usgs::Resolved<C>& series() const& noexcept {
    return series_;
  }
  const core::usgs::Resolved<C>& series() && = delete;

  friend bool operator==(const DataRequest&, const DataRequest&) = default;

 private:
  friend std::expected<DataRequest<core::usgs::Continuous>, Failure> bind(
      const core::usgs::Continuous& request,
      const std::vector<core::usgs::SeriesInfo>& rows);
  friend std::expected<DataRequest<core::usgs::Daily>, Failure> bind(
      const core::usgs::Daily& request,
      const std::vector<core::usgs::SeriesInfo>& rows);

  DataRequest(C request, core::usgs::Resolved<C> series)
      : request_{std::move(request)}, series_{std::move(series)} {}

  C request_;
  core::usgs::Resolved<C> series_;
};

// ---- stage 2: data --------------------------------------------------------

/// A page of one window of one epoch's instantaneous values.
template <class Link>
struct ContinuousUnit {
  /// The epoch fetched: an index into the request's series().epochs().
  std::uint32_t epoch;
  core::TimeRange window;
  PageUnit<Link, core::Time> page;
  friend bool operator==(const ContinuousUnit&,
                         const ContinuousUnit&) = default;
};

/// A page of one epoch's daily values.
template <class Link>
struct DailyUnit {
  /// The epoch fetched: an index into the request's series().epochs().
  std::uint32_t epoch;
  core::DayRange days;
  PageUnit<Link, std::chrono::local_days> page;
  friend bool operator==(const DailyUnit&, const DailyUnit&) = default;
};

/// The windows of an epoch's range: widened to whole seconds, split at 1000
/// days. Unalignable only at the ends of the time line.
[[nodiscard]] constexpr std::expected<core::Windows, Unalignable> windows(
    const core::usgs::Epoch<core::usgs::Continuous>& epoch) noexcept {
  const auto range = core::aligned(epoch.range(), core::usgs::grain);
  if (not range) {
    return std::unexpected{Unalignable{.range = epoch.range()}};
  }
  return core::split(*range, core::usgs::max_continuous_window);
}

/// The first page of every (epoch, window), in plan order: the epochs in
/// the resolved precedence order (the epoch begun last first), each epoch's
/// windows in time order. The merge folds the parts in this order with a
/// left-biased union, so where epochs overlap the later-begun epoch's rows
/// win (decision 37). Each window may take max_pages({window, 1 min,
/// page_rows}) pages.
template <class Link>
[[nodiscard]] std::expected<std::vector<ContinuousUnit<Link>>, Error> initial(
    const DataRequest<core::usgs::Continuous>& request) {
  std::vector<ContinuousUnit<Link>> units;
  const auto epochs = request.series().epochs();
  for (std::uint32_t e = 0; e < epochs.size(); ++e) {
    const auto cut = windows(epochs[e]);
    if (not cut) {
      return std::unexpected{Error{cut.error()}};
    }
    for (const core::TimeRange window : *cut) {
      const auto pages = max_pages({.span = window.end() - window.begin(),
                                    .cadence = finest_cadence,
                                    .page_rows = page_rows});
      units.push_back(
          {.epoch = e,
           .window = window,
           .page = {.guard = PageGuard<Link, core::Time>::first(pages),
                    .link = std::nullopt}});
    }
  }
  return units;
}

/// One unit per epoch, in the resolved precedence order (as above): daily
/// values have no window limit (36 years came back on one page); a chain may
/// take max_pages({days, 1 day, page_rows}) pages.
template <class Link>
[[nodiscard]] std::vector<DailyUnit<Link>> initial(
    const DataRequest<core::usgs::Daily>& request) {
  std::vector<DailyUnit<Link>> units;
  const auto epochs = request.series().epochs();
  for (std::uint32_t e = 0; e < epochs.size(); ++e) {
    const core::DayRange days = epochs[e].range();
    const auto pages = max_pages(
        {.span =
             std::chrono::duration_cast<std::chrono::milliseconds>(days.size()),
         .cadence = std::chrono::days{1},
         .page_rows = page_rows});
    units.push_back(DailyUnit<Link>{
        .epoch = e,
        .days = days,
        .page = {
            .guard = PageGuard<Link, std::chrono::local_days>::first(pages),
            .link = std::nullopt}});
  }
  return units;
}

/// The epoch a unit fetches (its series id and range go into the target).
template <class C, class Unit>
[[nodiscard]] const core::usgs::Epoch<C>& epoch_of(
    const DataRequest<C>& request, const Unit& unit) {
  return request.series().epochs()[unit.epoch];
}

template <class Link>
[[nodiscard]] UnitLabel label(const ContinuousUnit<Link>& unit) {
  return Page{.window = unit.window, .index = unit.page.guard.taken() - 1};
}

template <class Link>
[[nodiscard]] UnitLabel label(const DailyUnit<Link>& unit) {
  return DayWindow{.days = unit.days};
}

/// The next page of a window, after a page that `summary` describes. Its
/// rows must lie in the window the page was asked for: a server that
/// ignored `datetime` (on the first page or a followed one) fails the unit
/// with PagingError outside_window rather than leak rows of another window
/// into the merge.
template <class Link>
[[nodiscard]] std::expected<std::vector<ContinuousUnit<Link>>, Error> next(
    const ContinuousUnit<Link>& unit,
    const PageSummary<Link, core::Time>& summary) {
  if (summary.extent and (summary.extent->first < unit.window.begin() or
                          not(summary.extent->last < unit.window.end()))) {
    return std::unexpected{Error{PagingError{.rule = PagingRule::outside_window,
                                             .page = unit.page.guard.taken()}}};
  }
  return continue_chain<ContinuousUnit<Link>>(
      unit.page, summary, [&](PageUnit<Link, core::Time> page) {
        return ContinuousUnit<Link>{.epoch = unit.epoch,
                                    .window = unit.window,
                                    .page = std::move(page)};
      });
}

template <class Link>
[[nodiscard]] std::expected<std::vector<DailyUnit<Link>>, Error> next(
    const DailyUnit<Link>& unit,
    const PageSummary<Link, std::chrono::local_days>& summary) {
  return continue_chain<DailyUnit<Link>>(
      unit.page, summary, [&](PageUnit<Link, std::chrono::local_days> page) {
        return DailyUnit<Link>{
            .epoch = unit.epoch, .days = unit.days, .page = std::move(page)};
      });
}

}  // namespace mov::fetch::usgs
