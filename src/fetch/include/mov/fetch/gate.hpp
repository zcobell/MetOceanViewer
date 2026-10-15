// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// The admission gate (docs/providers-design.md §5.3, §6.3): which queued
// exchange of which job may start now, per origin. A second pure state
// machine beside the fetch machine; P7's Qt gate drives it (feeds the
// calls, starts what it admits, sets a timer for next_wake()).

#include <algorithm>
#include <array>
#include <chrono>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "mov/core/time.hpp"
#include "mov/fetch/origin.hpp"
#include "mov/fetch/policy.hpp"
#include "mov/fetch/steps.hpp"

namespace mov::fetch {

/// A job the gate serves (one fetch, or one stage chain).
struct JobId {
  std::uint32_t value;
  friend constexpr auto operator<=>(JobId, JobId) = default;
};

/// An exchange waiting for admission.
struct Ticket {
  JobId job;
  ExchangeId exchange;
  Origin origin;
  Priority priority;
  friend constexpr bool operator==(Ticket, Ticket) = default;
};

/// Start this exchange now.
struct Admit {
  JobId job;
  ExchangeId exchange;
  friend constexpr bool operator==(Admit, Admit) = default;
};

/// Per origin (policy(Origin, KeyPresence)): at most max_in_flight
/// exchanges at once, starts at least min_spacing apart, none during a
/// cool-down. Interactive tickets go ahead of background ones, but after
/// `background_after` interactive admissions in a row with background work
/// waiting, one background ticket goes, so background still progresses.
/// Within a class the jobs take turns (round-robin), each job's tickets in
/// the order they were queued.
///
/// Every call takes `now` (a feed that goes back in time is taken as the
/// latest seen) and returns what it admits. After any call, nothing the
/// rules allow is left waiting; next_wake() says when spacing or a
/// cool-down next lets a waiting ticket go, so the driver ticks then.
class OriginGate {
 public:
  explicit OriginGate(KeyPresence key, std::uint8_t background_after = 4)
      : key_{key}, background_after_{background_after} {}

  /// Queues a ticket (a repeated one is ignored).
  [[nodiscard]] std::vector<Admit> enqueue(Ticket t, core::Time now);

  /// The exchange is over (completed, failed, or aborted in flight), or a
  /// queued one is withdrawn (aborted before it started): its place is free.
  [[nodiscard]] std::vector<Admit> release(JobId job, ExchangeId exchange,
                                           core::Time now);

  /// Every ticket of the job, queued or in flight, is withdrawn.
  [[nodiscard]] std::vector<Admit> abort_job(JobId job, core::Time now);

  /// The origin holds its tickets for `pause` from now (the longest
  /// cool-down wins).
  [[nodiscard]] std::vector<Admit> cool_down(Origin origin,
                                             std::chrono::milliseconds pause,
                                             core::Time now);

  /// Time passed: admits what spacing or a cool-down held back.
  [[nodiscard]] std::vector<Admit> tick(core::Time now);

  /// When a waiting ticket next becomes admissible by the clock alone (not
  /// by a release); nullopt when none is waiting on the clock.
  [[nodiscard]] std::optional<core::Time> next_wake() const;

  /// Exchanges of an origin in flight, and waiting.
  [[nodiscard]] std::size_t in_flight(Origin origin) const;
  [[nodiscard]] std::size_t queued(Origin origin) const;

 private:
  using Key = std::pair<JobId, ExchangeId>;

  /// One priority class of one origin: per job, its tickets in order, and
  /// the jobs' turn order.
  struct ClassQueue {
    std::map<JobId, std::deque<ExchangeId>> tickets;
    std::deque<JobId> turn;

    [[nodiscard]] bool empty() const { return turn.empty(); }
    void push(JobId job, ExchangeId exchange);
    /// The next job's first ticket; the job goes to the back of the turn.
    [[nodiscard]] Key pop();
    /// Removes one ticket; true if it was queued here.
    bool remove(JobId job, ExchangeId exchange);
    /// Removes a job's tickets.
    void remove_job(JobId job);
    [[nodiscard]] std::size_t size() const;
  };

  struct OriginState {
    ClassQueue interactive;
    ClassQueue background;
    std::set<Key> in_flight;
    std::optional<core::Time> last_start;
    core::Time cool_until{core::Time::min()};
    std::uint32_t interactive_streak = 0;
  };

  [[nodiscard]] OriginState& state(Origin origin) {
    return origins_[static_cast<std::size_t>(std::to_underlying(origin))];
  }
  [[nodiscard]] const OriginState& state(Origin origin) const {
    return origins_[static_cast<std::size_t>(std::to_underlying(origin))];
  }

  /// The earliest time spacing and the cool-down allow a start at `o`.
  [[nodiscard]] core::Time ready_at(Origin o) const;
  /// Admits at every origin what the rules allow at now.
  [[nodiscard]] std::vector<Admit> pump(core::Time now);
  void pump_origin(Origin o, core::Time now, std::vector<Admit>& out);
  [[nodiscard]] core::Time advance(core::Time now) {
    latest_ = std::max(latest_, now);
    return latest_;
  }

  KeyPresence key_;
  std::uint8_t background_after_;
  std::array<OriginState, every_origin.size()> origins_{};
  /// Where each ticket is: its origin, while queued or in flight.
  std::map<Key, Origin> where_;
  core::Time latest_{core::Time::min()};
};

}  // namespace mov::fetch
