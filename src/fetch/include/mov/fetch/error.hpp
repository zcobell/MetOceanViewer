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
#include "mov/io/warning.hpp"

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

enum class ServiceErrc : std::uint8_t {
  station_or_system,  ///< CO-OPS' "not a valid station or system error":
                      ///< retried once
  datum_unavailable,
  range_exceeded,  ///< the planner asked for too much: a bug
  invalid_parameter,
  api_key_invalid,  ///< fails at once; never retried without the key
  unexpected_content,
  unknown,
};

/// The service answered with an error of its own (often with HTTP 200).
/// `text` is the server's message, raw (escape before showing), at most
/// max_text_bytes and cut on a UTF-8 boundary by make.
class ServiceError {
 public:
  static constexpr std::size_t max_text_bytes = 120;

  [[nodiscard]] static ServiceError make(Origin origin, ServiceErrc code,
                                         std::string_view text);

  [[nodiscard]] Origin origin() const noexcept { return origin_; }
  [[nodiscard]] ServiceErrc code() const noexcept { return code_; }
  [[nodiscard]] const std::string& text() const& noexcept { return text_; }
  const std::string& text() && = delete;

  friend bool operator==(const ServiceError&, const ServiceError&) = default;

 private:
  ServiceError(Origin origin, ServiceErrc code, std::string text)
      : origin_{origin}, code_{code}, text_{std::move(text)} {}

  Origin origin_;
  ServiceErrc code_;
  std::string text_;
};

/// Nothing to show: every unit came back without data and none failed.
struct NoData {
  friend constexpr bool operator==(NoData, NoData) = default;
};

/// Several Primary series of the requested parameter and statistic.
struct AmbiguousSeries {
  std::vector<core::usgs::TimeSeriesId> candidates;
  friend bool operator==(const AmbiguousSeries&,
                         const AmbiguousSeries&) = default;
};

/// A USGS request named a series or a statistic of the other collection
/// (core::usgs::ResolveError::wrong_collection).
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

/// Two parts disagree on a column's unit and neither converts to the other
/// (either may be unset). `column` is the part's column key.
struct UnitMismatch {
  std::string column;
  std::optional<core::Unit> first{};
  std::optional<core::Unit> other{};
  friend bool operator==(const UnitMismatch&, const UnitMismatch&) = default;
};

/// Two parts disagree on a column's quantity, label or datum.
struct MetaMismatch {
  std::string column;
  friend bool operator==(const MetaMismatch&, const MetaMismatch&) = default;
};

namespace detail {

template <class V, class... Front>
struct Prepend;
template <class... Back, class... Front>
struct Prepend<std::variant<Back...>, Front...> {
  using type = std::variant<Front..., Back...>;
};

}  // namespace detail

/// One error for fetch and providers: fetch's own alternatives, then every
/// alternative of io::Error, so lift_io is a total visit and there is one
/// Cancelled.
using Error = detail::Prepend<io::Error, TransportError, HttpError, RateLimited,
                              ServiceError, NoData, AmbiguousSeries,
                              WrongCollection, DeadlineExceeded, Unalignable,
                              UnitMismatch, MetaMismatch>::type;

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
/// An NDBC file. `month` is set for a monthly file; a realtime file has the
/// year of the fetch's `now`.
struct NdbcFile {
  core::ndbc::FileKind kind;
  std::chrono::year year;
  std::optional<std::chrono::month> month;
  friend constexpr bool operator==(const NdbcFile&, const NdbcFile&) = default;
};
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
using FetchWarning = std::variant<UnitFailed, Retried, CooledDown, FellBack>;
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

template <class T>
struct Fetched {
  T value;
  std::vector<Warning> warnings;
  Stats stats;
  friend bool operator==(const Fetched&, const Fetched&) = default;
};

/// What an earlier stage leaves to the result of a later one: its warnings
/// and its cost (§5.2, the bind between stages).
struct Trail {
  std::vector<Warning> warnings;
  Stats stats;
  friend bool operator==(const Trail&, const Trail&) = default;
};

/// The value of a stage's result, and its trail.
template <class T>
[[nodiscard]] std::pair<T, Trail> split(Fetched<T>&& f) {
  return {std::move(f.value),
          Trail{.warnings = std::move(f.warnings), .stats = f.stats}};
}

/// A later stage's result after an earlier stage's trail: the earlier
/// warnings first, the costs summed.
template <class T>
[[nodiscard]] Fetched<T> after(Trail earlier, Fetched<T> later) {
  earlier.warnings.insert(earlier.warnings.end(),
                          std::make_move_iterator(later.warnings.begin()),
                          std::make_move_iterator(later.warnings.end()));
  return {.value = std::move(later.value),
          .warnings = std::move(earlier.warnings),
          .stats = earlier.stats + later.stats};
}

}  // namespace mov::fetch
