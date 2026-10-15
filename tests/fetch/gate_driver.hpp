// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// Drives the admission gate from a script of bytes and checks its laws
// after every call. Shared by the seeded property test and fuzz_fetch_gate.

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "mov/fetch/gate.hpp"

namespace mov::fetch::test {

class GateDriver {
 public:
  explicit GateDriver(KeyPresence key, std::uint8_t background_after)
      : gate_{key, background_after},
        key_{key},
        background_after_{background_after} {}

  void act(std::uint8_t op, std::uint8_t arg) {
    constexpr std::uint8_t ops = 7;
    switch (op % ops) {
      case 0:
      case 1:
        enqueue(arg);
        break;
      case 2:
        release_in_flight(arg);
        break;
      case 3:
        withdraw_queued(arg);
        break;
      case 4:
        abort_job(arg);
        break;
      case 5:
        cool_down(arg);
        break;
      default:
        tick(arg);
        break;
    }
  }

  /// Releases everything admitted and ticks at every wake until the queue
  /// is empty: whatever is queued is admitted in the end.
  void drain() {
    constexpr int bound = 100'000;
    for (int i = 0; i < bound and ok(); ++i) {
      if (not in_flight_.empty()) {
        const auto k = *in_flight_.begin();
        forget_in_flight(k);
        check(gate_.release(k.job, k.exchange, now_));
      } else if (const auto wake = gate_.next_wake()) {
        now_ = std::max(now_, *wake);
        check(gate_.tick(now_));
      } else {
        break;
      }
    }
    if (not queued_.empty()) {
      fail("tickets left queued with nothing to wait for");
    }
  }

  [[nodiscard]] bool ok() const { return violation_.empty(); }
  [[nodiscard]] const std::string& violation() const { return violation_; }
  [[nodiscard]] std::size_t admitted() const { return admitted_; }

 private:
  struct Key {
    JobId job;
    ExchangeId exchange;
    friend constexpr auto operator<=>(const Key&, const Key&) = default;
  };
  struct Queued {
    Origin origin;
    Priority priority;
    std::uint64_t order;  // enqueue order
  };

  void fail(const std::string& what) {
    if (violation_.empty()) {
      violation_ = what;
    }
  }

  void enqueue(std::uint8_t arg) {
    const Ticket t{.job = JobId{.value = arg % 4U},
                   .exchange = ExchangeId{.value = next_id_++},
                   .origin = every_origin[(arg >> 2U) % every_origin.size()],
                   .priority = ((arg >> 5U) & 1U) != 0 ? Priority::background
                                                       : Priority::interactive};
    queued_.emplace(
        Key{.job = t.job, .exchange = t.exchange},
        Queued{.origin = t.origin, .priority = t.priority, .order = order_++});
    check(gate_.enqueue(t, now_));
  }

  void release_in_flight(std::uint8_t arg) {
    if (in_flight_.empty()) {
      return;
    }
    auto it = in_flight_.begin();
    std::advance(it, static_cast<std::ptrdiff_t>(arg % in_flight_.size()));
    const Key k = *it;
    forget_in_flight(k);
    check(gate_.release(k.job, k.exchange, now_));
  }

  void withdraw_queued(std::uint8_t arg) {
    if (queued_.empty()) {
      return;
    }
    auto it = queued_.begin();
    std::advance(it, static_cast<std::ptrdiff_t>(arg % queued_.size()));
    const Key k = it->first;
    const Class c{it->second.origin, it->second.priority};
    queued_.erase(it);
    note_removed(k, c);
    withdrawn_.insert(k);
    check(gate_.release(k.job, k.exchange, now_));
  }

  void abort_job(std::uint8_t arg) {
    const JobId job{.value = arg % 4U};
    std::vector<std::pair<Key, Class>> removed;
    std::erase_if(queued_, [&](const auto& e) {
      if (e.first.job == job) {
        withdrawn_.insert(e.first);
        removed.emplace_back(e.first,
                             Class{e.second.origin, e.second.priority});
        return true;
      }
      return false;
    });
    for (const auto& [k, c] : removed) {
      note_removed(k, c);
    }
    std::erase_if(in_flight_, [&](const Key& k) {
      if (k.job == job) {
        --in_flight_by_origin_[origin_of_.at(k)];
        return true;
      }
      return false;
    });
    check(gate_.abort_job(job, now_));
  }

  void cool_down(std::uint8_t arg) {
    const Origin o = every_origin[arg % every_origin.size()];
    const std::chrono::milliseconds pause{std::int64_t{arg} * 100};
    auto& until = cool_until_[o];
    until = std::max(until, now_ + pause);
    check(gate_.cool_down(o, pause, now_));
  }

  void tick(std::uint8_t arg) {
    constexpr std::uint8_t backwards = 0x80;
    if ((arg & backwards) != 0) {
      // A feed back in time is taken as the latest seen.
      check(gate_.tick(now_ - std::chrono::milliseconds{arg & 0x7FU}));
      return;
    }
    if (arg % 4 == 0) {
      if (const auto wake = gate_.next_wake()) {
        if (*wake <= now_) {
          fail("next_wake is not in the future");
        }
        now_ = *wake;
      }
    } else {
      now_ += std::chrono::milliseconds{std::int64_t{arg} * 10};
    }
    check(gate_.tick(now_));
  }

  void forget_in_flight(const Key& k) {
    in_flight_.erase(k);
    --in_flight_by_origin_[origin_of_.at(k)];
  }

  /// The laws, after a call that admitted `admits` at now_.
  void check(const std::vector<Admit>& admits) {
    for (const Admit& a : admits) {
      admit(Key{.job = a.job, .exchange = a.exchange});
    }
    nothing_left_waiting();
  }

  void admit(const Key& k) {
    if (withdrawn_.contains(k)) {
      fail("a withdrawn ticket was admitted");
      return;
    }
    const auto found = queued_.find(k);
    if (found == queued_.end()) {
      fail("admitted a ticket not queued (or twice)");
      return;
    }
    const Queued q = found->second;
    queued_.erase(found);
    note_removed(k, {q.origin, q.priority});
    const OriginPolicy p = policy(q.origin, key_);
    if (now_ < cool_until_[q.origin]) {
      fail("admitted during the origin's cool-down");
    }
    if (const auto last = last_start_.find(q.origin);
        last != last_start_.end() and now_ - last->second < p.min_spacing) {
      fail("admitted closer than min_spacing");
    }
    last_start_[q.origin] = now_;
    if (++in_flight_by_origin_[q.origin] > p.max_in_flight) {
      fail("more in flight than max_in_flight");
    }
    // FIFO within a job: nothing of this job, origin and class queued before.
    for (const auto& [other, oq] : queued_) {
      if (other.job == k.job and oq.origin == q.origin and
          oq.priority == q.priority and oq.order < q.order) {
        fail("a job's tickets admitted out of order");
      }
    }
    fairness(k, q);
    in_flight_.insert(k);
    origin_of_[k] = q.origin;
    ++admitted_;
  }

  using Class = std::pair<Origin, Priority>;

  [[nodiscard]] bool waits(JobId job, const Class& c) const {
    return std::ranges::any_of(queued_, [&](const auto& e) {
      return e.first.job == job and e.second.origin == c.first and
             e.second.priority == c.second;
    });
  }

  /// A job's queue in a class emptied: it left the turn order.
  void note_removed(const Key& k, const Class& c) {
    if (not waits(k.job, c)) {
      ++epoch_[{c, k.job}];
    }
  }

  /// Interactive first, but background progresses; jobs take turns: every
  /// job waiting in the class when a job went goes before that job goes
  /// again (unless it left the queue meanwhile).
  void fairness(const Key& k, const Queued& q) {
    const bool background_waits =
        std::ranges::any_of(queued_, [&](const auto& e) {
          return e.second.origin == q.origin and
                 e.second.priority == Priority::background;
        });
    auto& streak = streak_[q.origin];
    if (q.priority == Priority::interactive) {
      streak = background_waits ? streak + 1 : 0;
      if (streak > background_after_) {
        fail("background starved behind interactive work");
      }
    } else {
      streak = 0;
    }
    const Class c{q.origin, q.priority};
    Turns& t = turns_[c];
    if (const auto prev = t.waiting.find(k.job); prev != t.waiting.end()) {
      for (const auto& [other, epoch] : prev->second) {
        if (waits(other, c) and epoch_[{c, other}] == epoch and
            not t.since[k.job].contains(other)) {
          fail("a job went twice while another waited its turn");
        }
      }
    }
    std::map<JobId, std::uint64_t> waiting;
    for (const auto& [other, oq] : queued_) {
      if (other.job != k.job and oq.origin == q.origin and
          oq.priority == q.priority) {
        waiting.emplace(other.job, epoch_[{c, other.job}]);
      }
    }
    t.waiting[k.job] = std::move(waiting);
    t.since[k.job].clear();
    for (auto& [job, went] : t.since) {
      if (job != k.job) {
        went.insert(k.job);
      }
    }
  }

  /// After any call, nothing the rules allow is left waiting.
  void nothing_left_waiting() {
    for (const Origin o : every_origin) {
      const OriginPolicy p = policy(o, key_);
      const bool waiting = std::ranges::any_of(
          queued_, [&](const auto& e) { return e.second.origin == o; });
      const bool room = in_flight_by_origin_[o] < p.max_in_flight;
      const bool cooled = now_ >= cool_until_[o];
      const auto last = last_start_.find(o);
      const bool spaced =
          last == last_start_.end() or now_ - last->second >= p.min_spacing;
      if (waiting and room and cooled and spaced) {
        fail("a ticket the rules allow was left waiting");
      }
      if (gate_.in_flight(o) != in_flight_by_origin_[o]) {
        fail("the gate's in-flight count disagrees");
      }
    }
  }

  struct Turns {
    std::map<JobId, std::map<JobId, std::uint64_t>>
        waiting;                             // at its last turn
    std::map<JobId, std::set<JobId>> since;  // jobs that went since its turn
  };

  OriginGate gate_;
  KeyPresence key_;
  std::uint8_t background_after_;
  core::Time now_{std::chrono::sys_days{std::chrono::year{2026} /
                                        std::chrono::October / 1}};
  std::uint32_t next_id_ = 0;
  std::uint64_t order_ = 0;
  std::map<Key, Queued> queued_;
  std::set<Key> in_flight_;
  std::set<Key> withdrawn_;
  std::map<Key, Origin> origin_of_;
  std::map<Origin, std::size_t> in_flight_by_origin_;
  std::map<Origin, core::Time> cool_until_;
  std::map<Origin, core::Time> last_start_;
  std::map<Origin, std::uint32_t> streak_;
  std::map<Class, Turns> turns_;
  std::map<std::pair<Class, JobId>, std::uint64_t> epoch_;
  std::size_t admitted_ = 0;
  std::string violation_;
};

[[nodiscard]] inline GateDriver run_gate_script(
    std::span<const std::uint8_t> bytes) {
  const std::uint8_t setup = bytes.empty() ? 0 : bytes.front();
  if (not bytes.empty()) {
    bytes = bytes.subspan(1);
  }
  GateDriver d{(setup & 1U) != 0 ? KeyPresence::present : KeyPresence::none,
               static_cast<std::uint8_t>(1U + ((setup >> 1U) & 3U))};
  while (bytes.size() >= 2 and d.ok()) {
    d.act(bytes[0], bytes[1]);
    bytes = bytes.subspan(2);
  }
  if (d.ok()) {
    d.drain();
  }
  return d;
}

}  // namespace mov::fetch::test
