// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// Errors, unit labels, warnings and results of a fetch
// (docs/providers-design.md §5.1). Values only: describe() at the edge turns
// them into text.

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/provider/ndbc.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/time.hpp"
#include "mov/core/units.hpp"
#include "mov/core/windows.hpp"
#include "mov/fetch/origin.hpp"
#include "mov/fetch/policy.hpp"
#include "mov/io/error.hpp"
#include "mov/io/part.hpp"
#include "mov/io/warning.hpp"
#include "mov/io/wire/service.hpp"

namespace mov::fetch {

// ---- errors ---------------------------------------------------------------

/// The exchange failed below HTTP, after its retries.
struct TransportError {
  TransportErrc code;
  Origin origin;
  friend constexpr bool operator==(TransportError, TransportError) = default;
};

/// An HTTP status the exchange cannot use, after its retries.
struct HttpError {
  int status;
  Origin origin;
  friend constexpr bool operator==(HttpError, HttpError) = default;
};

/// The origin kept answering 429, or asked for a wait longer than the
/// policy's max_retry_after.
struct RateLimited {
  Origin origin;
  std::optional<std::chrono::seconds> retry_after;
  friend constexpr bool operator==(const RateLimited&,
                                   const RateLimited&) = default;
};

/// The service answered with an error of its own (often with HTTP 200), and
/// "no data": io's (`mov/io/wire/service.hpp`), which classifies the bodies.
/// station_or_system is retried once, gateway with backoff as a 5xx is,
/// api_key_invalid never (§5.3). As a fetch's result, NoData means every unit
/// came back without data and none failed.
using io::wire::NoData;
using io::wire::ServiceErrc;
using io::wire::ServiceError;

/// The request names no sublocation, and the site has several logical
/// series of the parameter and statistic, none without one
/// (core::usgs::AmbiguousSeries): the candidates, sorted.
struct AmbiguousSeries {
  std::vector<core::usgs::SeriesKey> candidates;
  friend bool operator==(const AmbiguousSeries&,
                         const AmbiguousSeries&) = default;
};

/// A USGS request named a series or a statistic of the other collection
/// (core::usgs::WrongCollection).
struct WrongCollection {
  core::usgs::Collection requested;
  friend constexpr bool operator==(WrongCollection, WrongCollection) = default;
};

/// The fetch's deadline passed (the CLI's --timeout).
struct DeadlineExceeded {
  friend constexpr bool operator==(DeadlineExceeded,
                                   DeadlineExceeded) = default;
};

/// The request's range cannot be widened to the provider's grain: it
/// reaches the end of the time line (core::aligned's nullopt).
struct Unalignable {
  core::TimeRange range;
  friend constexpr bool operator==(Unalignable, Unalignable) = default;
};

/// The request's range lies where the provider's calendar cannot go (NDBC:
/// a year outside the civil calendar's ±9999).
struct UnsupportedRange {
  core::TimeRange range;
  friend constexpr bool operator==(UnsupportedRange,
                                   UnsupportedRange) = default;
};

/// Which paging rule a page broke (docs/providers-design.md §5.2, N B1).
enum class PagingRule : std::uint8_t {
  no_progress,  ///< its first row is not after the previous page's last
  empty_page_with_link,  ///< no rows, but a next link
  link_repeated,         ///< a next link already followed in this window
  page_limit,            ///< a next link past the window's max_pages
  outside_window,        ///< rows outside the window the page was asked for
};

/// A page chain broke a paging rule at page `page` (1-based).
struct PagingError {
  PagingRule rule;
  std::uint32_t page;
  friend constexpr bool operator==(PagingError, PagingError) = default;
};

/// Two rows of a site's series have one id.
struct DuplicateSeries {
  core::usgs::TimeSeriesId id;
  friend constexpr bool operator==(const DuplicateSeries&,
                                   const DuplicateSeries&) = default;
};

/// A spec gave a unit no exchanges (FetchSpec's contract is at least one).
struct UnitWithoutExchanges {
  friend constexpr bool operator==(UnitWithoutExchanges,
                                   UnitWithoutExchanges) = default;
};

/// The plan grew past Options::max_units units.
struct UnitLimit {
  std::uint32_t max;
  friend constexpr bool operator==(UnitLimit, UnitLimit) = default;
};

/// A clock value (the start's `now` or the deadline) beyond ±10,000 years:
/// no fetch runs there, and the arithmetic on it could overflow.
struct ClockOutOfRange {
  core::Time time;
  friend constexpr bool operator==(ClockOutOfRange, ClockOutOfRange) = default;
};

/// Merging parts (io::combine) failed.
using io::MetaMismatch;
using io::UnitMismatch;

namespace detail {

template <class V, class... Front>
struct Prepend;
template <class... Back, class... Front>
struct Prepend<std::variant<Back...>, Front...> {
  using type = std::variant<Front..., Back...>;
};

/// How often T is among A...
template <class T, class... A>
inline constexpr std::size_t count_of =
    (std::size_t{0} + ... +
     (std::is_same_v<T, A> ? std::size_t{1} : std::size_t{0}));

template <class V>
inline constexpr bool unique_alternatives = false;
template <class... A>
inline constexpr bool unique_alternatives<std::variant<A...>> =
    ((count_of<A, A...> == 1) and ...);

}  // namespace detail

/// One error for fetch and providers: fetch's own alternatives, then every
/// alternative of io::Error, so lift_io is a total visit and there is one
/// Cancelled.
using Error = detail::Prepend<
    io::Error, TransportError, HttpError, RateLimited, ServiceError, NoData,
    AmbiguousSeries, WrongCollection, DeadlineExceeded, Unalignable,
    UnsupportedRange, PagingError, DuplicateSeries, UnitWithoutExchanges,
    UnitLimit, ClockOutOfRange, UnitMismatch, MetaMismatch>::type;
static_assert(detail::unique_alternatives<Error>,
              "every alternative of fetch::Error is a distinct type");

/// An io error as a fetch error.
[[nodiscard]] Error lift_io(io::Error e);

// ---- unit labels ----------------------------------------------------------

/// Which unit something happened to. Values, formatted only at the edge.
struct Window {
  core::TimeRange range;
  friend constexpr bool operator==(Window, Window) = default;
};
struct DayWindow {
  core::DayRange days;
  friend constexpr bool operator==(DayWindow, DayWindow) = default;
};
/// Page `index` (0 for the first) of a window.
struct Page {
  core::TimeRange window;
  std::uint32_t index;
  friend constexpr bool operator==(Page, Page) = default;
};

/// An NDBC file, by kind: a year's quality-controlled file, a finalized
/// month, a month's latest (not yet finalized) file, or realtime2 (with the
/// fetch's year).
struct HistoricalFile {
  std::chrono::year year;
  friend constexpr bool operator==(HistoricalFile, HistoricalFile) = default;
};
struct MonthlyFile {
  std::chrono::year year;
  std::chrono::month month;
  friend constexpr bool operator==(MonthlyFile, MonthlyFile) = default;
};
struct LatestMonthFile {
  std::chrono::year year;
  std::chrono::month month;
  friend constexpr bool operator==(LatestMonthFile, LatestMonthFile) = default;
};
struct RealtimeFile {
  std::chrono::year year;
  friend constexpr bool operator==(RealtimeFile, RealtimeFile) = default;
};
using NdbcFile =
    std::variant<HistoricalFile, MonthlyFile, LatestMonthFile, RealtimeFile>;

/// A USGS resolution page (stage 1).
struct Resolution {
  std::uint32_t page;
  friend constexpr bool operator==(Resolution, Resolution) = default;
};
/// Unit `index` of a catalog build, in plan order.
struct CatalogPart {
  std::uint32_t index;
  friend constexpr bool operator==(CatalogPart, CatalogPart) = default;
};
using UnitLabel =
    std::variant<Window, DayWindow, Page, NdbcFile, Resolution, CatalogPart>;

/// A failed fetch: the error and, when one unit caused it, that unit.
struct Failure {
  Error error;
  std::optional<UnitLabel> unit;
  friend bool operator==(const Failure&, const Failure&) = default;
};

// ---- warnings -------------------------------------------------------------

/// A lenient fetch left this unit out.
struct UnitFailed {
  UnitLabel unit;
  Error error;
  friend bool operator==(const UnitFailed&, const UnitFailed&) = default;
};
/// A lenient fetch kept this unit's data but could not continue its chain
/// (a page that broke a paging rule): the pages after it are missing.
struct ChainStopped {
  UnitLabel unit;
  Error error;
  friend bool operator==(const ChainStopped&, const ChainStopped&) = default;
};
/// Exchange `slot` of the unit (in the order of the spec's targets; for a
/// paired CO-OPS product 0 is observed, 1 predicted) was absent while
/// another had data, which is kept.
struct PartlyAbsent {
  UnitLabel unit;
  std::uint32_t slot;
  friend constexpr bool operator==(const PartlyAbsent&,
                                   const PartlyAbsent&) = default;
};
/// One of the unit's exchanges succeeded after `attempts` sends.
struct Retried {
  UnitLabel unit;
  std::uint8_t attempts;
  friend constexpr bool operator==(const Retried&, const Retried&) = default;
};
/// The origin answered 429 (or had no requests left) and was paused.
struct CooledDown {
  Origin origin;
  std::chrono::milliseconds for_;
  friend constexpr bool operator==(CooledDown, CooledDown) = default;
};
/// An NDBC file was absent; the next one was tried.
struct FellBack {
  NdbcFile from;
  NdbcFile to;
  friend constexpr bool operator==(const FellBack&, const FellBack&) = default;
};
using FetchWarning = std::variant<UnitFailed, ChainStopped, PartlyAbsent,
                                  Retried, CooledDown, FellBack>;
using Warning = std::variant<io::Warning, FetchWarning>;

// ---- results --------------------------------------------------------------

/// What a fetch cost: sends (retries included), retries and response bytes.
struct Stats {
  std::uint32_t exchanges{0};
  std::uint32_t retries{0};
  std::uint64_t bytes{0};
  friend constexpr Stats operator+(Stats a, Stats b) noexcept {
    return {.exchanges = a.exchanges + b.exchanges,
            .retries = a.retries + b.retries,
            .bytes = a.bytes + b.bytes};
  }
  friend constexpr bool operator==(Stats, Stats) = default;
};

/// What a fetch noticed and what it cost: a monoid under + (warnings
/// concatenated in order, stats summed) with Trail{} as identity, so the
/// trail of a two-stage fetch is the first stage's + the second's.
struct Trail {
  std::vector<Warning> warnings;
  Stats stats;

  friend Trail operator+(Trail a, Trail b) {
    a.warnings.insert(a.warnings.end(),
                      std::make_move_iterator(b.warnings.begin()),
                      std::make_move_iterator(b.warnings.end()));
    a.stats = a.stats + b.stats;
    return a;
  }
  friend bool operator==(const Trail&, const Trail&) = default;
};

/// A fetch's value and its trail.
template <class T>
struct Fetched {
  T value;
  Trail trail;
  friend bool operator==(const Fetched&, const Fetched&) = default;
};

}  // namespace mov::fetch
