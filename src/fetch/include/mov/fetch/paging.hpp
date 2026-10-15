// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// Bounds on a chain of pages (docs/providers-design.md §5.2, N B1): a server
// that hands out `next` links must not keep the client paging forever, in a
// cycle, or backwards.

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "mov/io/error.hpp"

namespace mov::fetch {

/// What a parsed page says about paging: how many rows it had, its first
/// and last row's position (a time, or a day for daily values; nullopt when
/// the rows have no order, as a site's series do), and the link to the next
/// page, if any.
template <class Link, class Position>
struct PageSummary {
  std::size_t rows;
  std::optional<Position> first;
  std::optional<Position> last;
  std::optional<Link> next;
};

/// The pages a window of `span` may take at one row per `cadence` and at
/// most `page_rows` rows a page: ceil(span / cadence / page_rows) + 2. The
/// two spare pages absorb a partial first page and an empty last one.
/// Saturates at UINT32_MAX.
[[nodiscard]] constexpr std::uint32_t max_pages(
    std::chrono::milliseconds span, std::chrono::milliseconds cadence,
    std::uint32_t page_rows) noexcept {
  const auto ceil_div = [](std::uint64_t n, std::uint64_t d) {
    return (n / d) + (n % d == 0 ? 0 : 1);
  };
  const auto positive = [](std::int64_t v) {
    return static_cast<std::uint64_t>(std::max<std::int64_t>(v, 1));
  };
  const std::uint64_t rows =
      ceil_div(positive(span.count()), positive(cadence.count()));
  const std::uint64_t pages =
      ceil_div(rows, std::max<std::uint64_t>(page_rows, 1)) + 2;
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
  /// and ParseError{corrupt_record} when it may not: a page whose first
  /// position is not after the previous page's last (checked when both are
  /// known), a link on an empty page, a link seen before in this window, or
  /// a link past max_pages. The
  /// error's line is the page number (1-based) and its context names the
  /// rule.
  [[nodiscard]] std::expected<std::optional<Turn>, io::ParseError> after(
      const PageSummary<Link, Position>& summary) const {
    const auto refuse = [&](std::string_view rule) {
      return std::unexpected{io::ParseError::make(io::ParseErrc::corrupt_record,
                                                  {.line = taken_}, rule)};
    };
    if (summary.first and last_ and not(*last_ < *summary.first)) {
      return refuse("page does not advance");
    }
    if (not summary.next) {
      return std::optional<Turn>{};
    }
    if (summary.rows == 0) {
      return refuse("empty page with a next link");
    }
    if (std::ranges::find(seen_, *summary.next) != seen_.end()) {
      return refuse("next link repeated");
    }
    if (taken_ >= max_pages_) {
      return refuse("page limit reached");
    }
    PageGuard next = *this;
    next.taken_ = taken_ + 1;
    next.seen_.push_back(*summary.next);
    next.last_ = summary.last;
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
