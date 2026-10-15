// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// Bounds on a chain of pages (docs/providers-design.md §5.2, N B1): a server
// that hands out `next` links must not keep the client paging forever, in a
// cycle, or backwards.

#include <algorithm>
#include <chrono>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include "mov/fetch/error.hpp"

namespace mov::fetch {

/// The position type of rows that have no order (a site's series): a page
/// of them has no Extent, so only the count, empty-page and repeated-link
/// rules apply.
struct NoPosition {
  friend constexpr auto operator<=>(NoPosition, NoPosition) = default;
};

/// The first and last row's position on a page (a time, or a day for daily
/// values).
template <class Position>
struct Extent {
  Position first;
  Position last;
  friend constexpr bool operator==(const Extent&, const Extent&) = default;
};

/// What a parsed page says about paging: how many rows it had, their extent
/// when they have positions and there are any, and the link to the next
/// page, if any.
template <class Link, class Position>
struct PageSummary {
  std::size_t rows;
  std::optional<Extent<Position>> extent;
  std::optional<Link> next;
};

/// The arguments of max_pages, by name.
struct PageBudget {
  std::chrono::milliseconds span;     ///< the window
  std::chrono::milliseconds cadence;  ///< the finest row spacing
  std::uint32_t page_rows;            ///< the rows a page holds at most
};

/// The pages a window may take: ceil(span / cadence / page_rows) + 2. The
/// two spare pages absorb a partial first page and an empty last one.
/// Saturates at UINT32_MAX; a span, cadence or page size below 1 counts as
/// 1.
[[nodiscard]] constexpr std::uint32_t max_pages(PageBudget b) noexcept {
  const auto ceil_div = [](std::uint64_t n, std::uint64_t d) {
    return (n / d) + (n % d == 0 ? 0 : 1);
  };
  const auto positive = [](std::int64_t v) {
    return static_cast<std::uint64_t>(std::max<std::int64_t>(v, 1));
  };
  const std::uint64_t rows =
      ceil_div(positive(b.span.count()), positive(b.cadence.count()));
  const std::uint64_t pages =
      ceil_div(rows, std::max<std::uint64_t>(b.page_rows, 1)) + 2;
  return static_cast<std::uint32_t>(std::min<std::uint64_t>(
      pages, std::numeric_limits<std::uint32_t>::max()));
}

/// The state of one window's page chain: how many pages were taken, the
/// links followed, and the last position seen. A value: the unit that owns
/// it is copied, and each accepted page gives a new guard.
template <class Link, class Position>
  requires std::equality_comparable<Link> and std::totally_ordered<Position>
class PageGuard {
 public:
  /// The guard of a window's first page.
  [[nodiscard]] static constexpr PageGuard first(
      std::uint32_t max_pages) noexcept {
    return PageGuard{std::max<std::uint32_t>(max_pages, 1)};
  }

  /// Pages taken so far (the page being judged included).
  [[nodiscard]] constexpr std::uint32_t taken() const noexcept {
    return taken_;
  }

  struct Turn {
    PageGuard guard;  ///< the guard of the next page
    Link link;        ///< the link to follow
  };

  /// After the page `summary` describes: nullopt when the chain ends (no
  /// link), the next page's guard and link when the link may be followed,
  /// and a PagingError (at this page's number) when it may not: a page
  /// whose first position is not after the last position seen (checked when
  /// both are known), a link on an empty page, a link seen before in this
  /// window, or a link past max_pages. The last position seen survives a
  /// page without positions.
  [[nodiscard]] std::expected<std::optional<Turn>, PagingError> after(
      const PageSummary<Link, Position>& summary) const {
    const auto refuse = [&](PagingRule rule) {
      return std::unexpected{PagingError{.rule = rule, .page = taken_}};
    };
    if (summary.extent and last_ and not(*last_ < summary.extent->first)) {
      return refuse(PagingRule::no_progress);
    }
    if (not summary.next) {
      return std::optional<Turn>{};
    }
    if (summary.rows == 0) {
      return refuse(PagingRule::empty_page_with_link);
    }
    if (std::ranges::find(seen_, *summary.next) != seen_.end()) {
      return refuse(PagingRule::link_repeated);
    }
    if (taken_ >= max_pages_) {
      return refuse(PagingRule::page_limit);
    }
    PageGuard next = *this;
    next.taken_ = taken_ + 1;
    next.seen_.push_back(*summary.next);
    if (summary.extent) {
      next.last_ = summary.extent->last;
    }
    return std::optional<Turn>{
        Turn{.guard = std::move(next), .link = *summary.next}};
  }

  friend bool operator==(const PageGuard&, const PageGuard&) = default;

 private:
  explicit constexpr PageGuard(std::uint32_t max_pages) noexcept
      : max_pages_{max_pages} {}

  std::uint32_t max_pages_;
  std::uint32_t taken_{1};
  std::vector<Link> seen_;
  std::optional<Position> last_;
};

}  // namespace mov::fetch
