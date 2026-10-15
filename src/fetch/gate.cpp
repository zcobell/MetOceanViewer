// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/fetch/gate.hpp"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace mov::fetch {

void OriginGate::ClassQueue::push(JobId job, ExchangeId exchange) {
  auto& mine = tickets[job];
  if (mine.empty()) {
    turn.push_back(job);
  }
  mine.push_back(exchange);
}

OriginGate::Key OriginGate::ClassQueue::pop() {
  const JobId job = turn.front();
  turn.pop_front();
  auto& mine = tickets.at(job);
  const ExchangeId exchange = mine.front();
  mine.pop_front();
  if (mine.empty()) {
    tickets.erase(job);
  } else {
    turn.push_back(job);
  }
  return {job, exchange};
}

bool OriginGate::ClassQueue::remove(JobId job, ExchangeId exchange) {
  const auto found = tickets.find(job);
  if (found == tickets.end()) {
    return false;
  }
  auto& mine = found->second;
  const auto it = std::ranges::find(mine, exchange);
  if (it == mine.end()) {
    return false;
  }
  mine.erase(it);
  if (mine.empty()) {
    remove_job(job);
  }
  return true;
}

void OriginGate::ClassQueue::remove_job(JobId job) {
  tickets.erase(job);
  std::erase(turn, job);
}

std::size_t OriginGate::ClassQueue::size() const {
  std::size_t n = 0;
  for (const auto& entry : tickets) {
    n += entry.second.size();
  }
  return n;
}

std::vector<Admit> OriginGate::enqueue(Ticket t, core::Time now) {
  const core::Time at = advance(now);
  const Key key{t.job, t.exchange};
  if (where_.contains(key)) {
    return pump(at);
  }
  where_.emplace(key, t.origin);
  OriginState& s = state(t.origin);
  (t.priority == Priority::interactive ? s.interactive : s.background)
      .push(t.job, t.exchange);
  return pump(at);
}

std::vector<Admit> OriginGate::release(JobId job, ExchangeId exchange,
                                       core::Time now) {
  const core::Time at = advance(now);
  const Key key{job, exchange};
  const auto found = where_.find(key);
  if (found != where_.end()) {
    OriginState& s = state(found->second);
    if (s.in_flight.erase(key) == 0 and
        not s.interactive.remove(job, exchange)) {
      s.background.remove(job, exchange);
    }
    where_.erase(found);
  }
  return pump(at);
}

std::vector<Admit> OriginGate::abort_job(JobId job, core::Time now) {
  const core::Time at = advance(now);
  for (OriginState& s : origins_) {
    s.interactive.remove_job(job);
    s.background.remove_job(job);
    std::erase_if(s.in_flight, [&](const Key& k) { return k.first == job; });
  }
  std::erase_if(where_,
                [&](const auto& entry) { return entry.first.first == job; });
  return pump(at);
}

std::vector<Admit> OriginGate::cool_down(Origin origin,
                                         std::chrono::milliseconds pause,
                                         core::Time now) {
  const core::Time at = advance(now);
  OriginState& s = state(origin);
  s.cool_until = std::max(s.cool_until,
                          at + std::max(pause, std::chrono::milliseconds{0}));
  return pump(at);
}

std::vector<Admit> OriginGate::tick(core::Time now) {
  return pump(advance(now));
}

core::Time OriginGate::ready_at(Origin o) const {
  const OriginState& s = state(o);
  const core::Time spaced = s.last_start
                                ? *s.last_start + policy(o, key_).min_spacing
                                : core::Time::min();
  return std::max(spaced, s.cool_until);
}

std::optional<core::Time> OriginGate::next_wake() const {
  std::optional<core::Time> wake;
  for (const Origin o : every_origin) {
    const OriginState& s = state(o);
    const bool waiting = not s.interactive.empty() or not s.background.empty();
    const bool room = s.in_flight.size() < policy(o, key_).max_in_flight;
    const core::Time ready = ready_at(o);
    if (waiting and room and latest_ < ready) {
      wake = wake ? std::min(*wake, ready) : ready;
    }
  }
  return wake;
}

std::size_t OriginGate::in_flight(Origin origin) const {
  return state(origin).in_flight.size();
}

std::size_t OriginGate::queued(Origin origin) const {
  const OriginState& s = state(origin);
  return s.interactive.size() + s.background.size();
}

std::vector<Admit> OriginGate::pump(core::Time now) {
  std::vector<Admit> out;
  for (const Origin o : every_origin) {
    pump_origin(o, now, out);
  }
  return out;
}

void OriginGate::pump_origin(Origin o, core::Time now,
                             std::vector<Admit>& out) {
  OriginState& s = state(o);
  const OriginPolicy p = policy(o, key_);
  while (s.in_flight.size() < p.max_in_flight and now >= ready_at(o)) {
    const bool interactive_waits = not s.interactive.empty();
    const bool background_waits = not s.background.empty();
    if (not interactive_waits and not background_waits) {
      return;
    }
    const bool background_turn =
        background_waits and
        (not interactive_waits or s.interactive_streak >= background_after_);
    const Key key = background_turn ? s.background.pop() : s.interactive.pop();
    s.interactive_streak = (background_turn or not background_waits)
                               ? 0
                               : s.interactive_streak + 1;
    s.in_flight.insert(key);
    s.last_start = now;
    out.push_back(Admit{.job = key.first, .exchange = key.second});
  }
}

}  // namespace mov::fetch
