// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// The vocabulary of the sans-IO fetch machine (docs/providers-design.md
// §5.5): the ids it hands out, the events it takes, the steps it returns,
// and the spec a provider stage implements for it.

#include <array>
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
#include "mov/fetch/retry.hpp"
#include "mov/io/read.hpp"

namespace mov::fetch {

/// One send of one exchange. Every attempt gets a fresh id, so a late
/// completion of an aborted attempt is never taken for its retry.
struct ExchangeId {
  std::uint32_t value;
  friend constexpr auto operator<=>(ExchangeId, ExchangeId) = default;
};

/// One timer the machine asked for (a retry delay, a cool-down hold, the
/// deadline). Drawn from the same counter as the exchange ids.
struct TimerId {
  std::uint32_t value;
  friend constexpr auto operator<=>(TimerId, TimerId) = default;
};

namespace detail {
struct IdAccess;
}  // namespace detail

/// Where a job's ids continue: job_start() for its first stage, the earlier
/// machine's next_ids() for a later one, so no id is reused within a job.
/// Opaque: only a machine advances it.
class IdCursor {
 public:
  [[nodiscard]] static constexpr IdCursor job_start() noexcept {
    return IdCursor{0};
  }
  friend constexpr bool operator==(IdCursor, IdCursor) = default;

 private:
  friend struct detail::IdAccess;

  explicit constexpr IdCursor(std::uint32_t next) noexcept : next_{next} {}

  std::uint32_t next_;
};

namespace detail {

/// The machine's access to an IdCursor's value.
struct IdAccess {
  [[nodiscard]] static constexpr std::uint32_t next(IdCursor c) noexcept {
    return c.next_;
  }
  [[nodiscard]] static constexpr IdCursor at(std::uint32_t next) noexcept {
    return IdCursor{next};
  }
};

}  // namespace detail

/// What came back for an exchange the transport delivered. The body stays
/// with the job; `bytes` is its size, for the statistics.
struct Response {
  int status;
  std::optional<std::chrono::seconds> retry_after{};
  /// `X-RateLimit-Remaining`; 0 or less on an accepted response cools the
  /// origin down (for Retry-After, else the policy's base) without retrying
  /// the exchange.
  std::optional<std::int64_t> rate_remaining{};
  std::uint64_t bytes{0};
  friend constexpr bool operator==(const Response&, const Response&) = default;
};
using Outcome = std::variant<Response, TransportErrc>;

/// Strict: the first failed unit fails the fetch. Lenient (the CLI's
/// --partial, a background catalog refresh): a failed unit is a gap and a
/// UnitFailed warning (§5.6).
enum class Strictness : std::uint8_t { strict, lenient };

/// The body held nothing for the request: NDBC's 404, CO-OPS' "No data was
/// found". A success value of a parse, not an error: a gap that may have a
/// fallback. NoData is only ever a fetch's final result.
struct Absent {
  friend constexpr bool operator==(Absent, Absent) = default;
};

/// What one exchange of a unit gave: its parsed body, or Absent.
template <class Parsed>
using SlotResult = std::variant<Parsed, Absent>;

/// Every exchange of a unit came back and at least one has data: the result
/// of each, in the order of the unit's targets (an absent half of a paired
/// product included).
template <class Parsed>
struct Received {
  std::span<const SlotResult<Parsed>> slots;
};
/// What a unit came to: data in some slot, or every slot absent.
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
  std::expected<io::Read<SlotResult<P>>, Error> parsed;
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
/// The origin answered 429 (or had no requests left): every job's gate
/// holds that origin's exchanges for `for_` (at least min_cool_down).
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
/// Fold the parts, moved out of the machine, and build the value
/// (S::merge), on the pool.
template <class Part>
struct Merge {
  std::vector<Part> in_plan_order;
  friend bool operator==(const Merge&, const Merge&) = default;
};
/// Abort these exchanges, and stop these parses (their tasks' stop
/// tokens); no result of theirs is wanted.
struct Abort {
  std::vector<ExchangeId> exchanges;
  std::vector<ExchangeId> parses;
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

/// The data half: where a target goes, what a unit's pages give, and the
/// merge of the parts.
template <class S>
concept HandlesData = requires(
    const typename S::Target& target,
    std::vector<SlotResult<typename S::Parsed>>&& slots,
    const typename S::Part& part, std::vector<typename S::Part>&& parts) {
  { S::origin(target) } -> std::same_as<Origin>;
  { S::into_part(std::move(slots)) } -> std::same_as<typename S::Part>;
  { S::has_data(part) } -> std::same_as<bool>;
  {
    S::merge(std::move(parts))
  } -> std::same_as<std::expected<io::Read<typename S::Value>, Error>>;
};

}  // namespace detail

/// One stage of a provider fetch, for the machine (§5.2). Static functions
/// over values:
/// - initial(request, now): the stage's units in plan order, or an error
///   (the fetch fails before any exchange);
/// - targets(request, unit): the unit's exchanges, at least one (two for a
///   paired CO-OPS product); a unit with none fails with
///   UnitWithoutExchanges;
/// - origin(target): where an exchange goes;
/// - label(request, unit): the unit's name in failures and warnings;
/// - next(request, unit, outcome): the units that continue this one (the
///   next page, the next NDBC spelling or source), in plan order; an error
///   ends the chain (strict: the fetch fails; lenient: the unit's data is
///   kept with a ChainStopped warning);
/// - into_part(slots): the part a received unit contributes to the merge,
///   from each slot's result (an absent half of a pair included);
/// - has_data(part): whether it holds any row (else the unit is a gap);
/// - merge(parts): the parts folded in plan order and finished into the
///   stage's value, run by the job on the pool for the Merge step.
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

/// How a machine runs.
struct Options {
  Strictness strictness = Strictness::strict;
  RetryPolicy retry = RetryPolicy::defaults();
  /// Past it the machine aborts everything and finishes DeadlineExceeded.
  std::optional<core::Time> deadline = std::nullopt;
  Priority priority = Priority::interactive;
  /// Where the ids continue (IdCursor).
  IdCursor ids = IdCursor::job_start();
  /// The most units a plan may grow to (initial units and successors); more
  /// fails the fetch with UnitLimit.
  std::uint32_t max_units = 10'000;
  /// Origins already cooling down at start, until when (by origin): a later
  /// stage inherits the earlier stage's cool-downs.
  std::array<std::optional<core::Time>, every_origin.size()> cooling{};
};

}  // namespace mov::fetch
