// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// The vocabulary of the sans-IO fetch machine (docs/providers-design.md
// §5.5): the ids it hands out, the events it takes, the steps it returns,
// and the spec a provider stage implements for it.

#include <chrono>
#include <compare>
#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/time.hpp"
#include "mov/fetch/error.hpp"
#include "mov/fetch/origin.hpp"
#include "mov/fetch/policy.hpp"
#include "mov/io/read.hpp"

namespace mov::fetch {

/// One send of one exchange. Every attempt gets a fresh id, so a late
/// completion of an aborted attempt is never taken for its retry.
struct ExchangeId {
  std::uint32_t value;
  friend constexpr auto operator<=>(ExchangeId, ExchangeId) = default;
};

/// One timer the machine asked for (a retry delay, a cool-down, the
/// deadline). Drawn from the same counter as the exchange ids.
struct TimerId {
  std::uint32_t value;
  friend constexpr auto operator<=>(TimerId, TimerId) = default;
};

/// What came back for an exchange the transport delivered. The body stays
/// with the job; `bytes` is its size, for the statistics.
struct Response {
  int status;
  std::optional<std::chrono::seconds> retry_after{};
  /// `X-RateLimit-Remaining`; 0 on an accepted response cools the origin
  /// down without retrying the exchange.
  std::optional<std::int64_t> rate_remaining{};
  std::uint64_t bytes{0};
  friend constexpr bool operator==(const Response&, const Response&) = default;
};
using Outcome = std::variant<Response, TransportErrc>;

/// Strict: the first failed unit fails the fetch. Lenient (the CLI's
/// --partial, a background catalog refresh): a failed unit is a gap and a
/// UnitFailed warning (§5.6).
enum class Strictness : std::uint8_t { strict, lenient };

/// A unit whose exchanges all came back: their parsed values, in the order
/// of the unit's targets.
template <class Parsed>
struct Received {
  std::span<const Parsed> parsed;
};
/// A unit with an exchange that was absent (NDBC's 404) or parsed to
/// NoData: the gap that may have a fallback.
struct Absent {
  friend constexpr bool operator==(Absent, Absent) = default;
};
template <class Parsed>
using UnitOutcome = std::variant<Received<Parsed>, Absent>;

namespace event {

/// The transport finished an exchange.
struct Completed {
  ExchangeId id;
  Outcome outcome;
};
/// A timer the machine asked for fired.
struct WaitElapsed {
  TimerId id;
};
/// A Parse step's result.
template <class P>
struct Parsed {
  ExchangeId id;
  std::expected<io::Read<P>, Error> parsed;
};
/// The Merge step's result.
template <class V>
struct Merged {
  std::expected<io::Read<V>, Error> value;
};
/// The caller cancelled the fetch.
struct Cancel {};

}  // namespace event

/// Start an exchange (through the origin's gate).
template <class Target>
struct Send {
  ExchangeId id;
  Target target;
  Origin origin;
  Priority priority;
  friend bool operator==(const Send&, const Send&) = default;
};
/// Start a timer; its WaitElapsed comes back with the id.
struct Wait {
  TimerId id;
  std::chrono::milliseconds delay;
  friend constexpr bool operator==(Wait, Wait) = default;
};
/// The origin answered 429: every job's gate holds that origin's exchanges
/// for `for_`.
struct CoolDown {
  Origin origin;
  std::chrono::milliseconds for_;
  friend constexpr bool operator==(CoolDown, CoolDown) = default;
};
/// Parse the body of exchange `id` (sent to `target`) on the pool.
template <class Target>
struct Parse {
  ExchangeId id;
  Target target;
  friend bool operator==(const Parse&, const Parse&) = default;
};
/// Fold the parts, moved out of the machine, and build the value, on the
/// pool.
template <class Part>
struct Merge {
  std::vector<Part> in_plan_order;
  friend bool operator==(const Merge&, const Merge&) = default;
};
/// Abort these exchanges; no completion of theirs is wanted.
struct Abort {
  std::vector<ExchangeId> ids;
  friend bool operator==(const Abort&, const Abort&) = default;
};
/// Units done of units known; the total grows when a unit adds successors.
struct Progress {
  std::uint32_t done;
  std::uint32_t total;
  friend constexpr bool operator==(Progress, Progress) = default;
};
/// The fetch's result. Nothing follows it.
template <class V>
struct Finish {
  std::expected<Fetched<V>, Failure> result;
};

/// One stage of a provider fetch, for the machine (§5.2). Static functions
/// over values:
/// - initial(request, now): the stage's units in plan order, or an error
///   (the fetch fails before any exchange);
/// - targets(request, unit): the unit's exchanges (two for a paired
///   CO-OPS product);
/// - origin(target): where an exchange goes;
/// - label(request, unit): the unit's name in failures and warnings;
/// - next(request, unit, outcome): the units that continue this one (the
///   next page, the next NDBC spelling or source), in plan order; an error
///   fails the unit;
/// - into_part(parsed): the part a parsed page contributes to the merge;
/// - has_data(part): whether it holds any row (else the unit is a gap).
namespace detail {

/// The planning half of FetchSpec: units, their exchanges and successors.
template <class S>
concept PlansUnits =
    requires(const typename S::Request& request, const typename S::Unit& unit,
             core::Time now, const UnitOutcome<typename S::Parsed>& outcome) {
      {
        S::initial(request, now)
      } -> std::same_as<std::expected<std::vector<typename S::Unit>, Error>>;
      {
        S::targets(request, unit)
      } -> std::same_as<std::vector<typename S::Target>>;
      { S::label(request, unit) } -> std::same_as<UnitLabel>;
      {
        S::next(request, unit, outcome)
      } -> std::same_as<std::expected<std::vector<typename S::Unit>, Error>>;
    };

/// The exchange and data half: where a target goes, what a page gives.
template <class S>
concept HandlesData =
    requires(const typename S::Target& target, typename S::Parsed&& parsed,
             const typename S::Part& part) {
      { S::origin(target) } -> std::same_as<Origin>;
      { S::into_part(std::move(parsed)) } -> std::same_as<typename S::Part>;
      { S::has_data(part) } -> std::same_as<bool>;
    };

}  // namespace detail

template <class S>
concept FetchSpec =
    std::copyable<typename S::Request> and std::copyable<typename S::Unit> and
    std::copyable<typename S::Target> and std::movable<typename S::Parsed> and
    std::movable<typename S::Part> and std::movable<typename S::Value> and
    detail::PlansUnits<S> and detail::HandlesData<S>;

template <FetchSpec S>
using Event = std::variant<event::Completed, event::WaitElapsed,
                           event::Parsed<typename S::Parsed>,
                           event::Merged<typename S::Value>, event::Cancel>;

template <FetchSpec S>
using Step = std::variant<Send<typename S::Target>, Wait, CoolDown,
                          Parse<typename S::Target>, Merge<typename S::Part>,
                          Abort, Progress, Finish<typename S::Value>>;

}  // namespace mov::fetch
