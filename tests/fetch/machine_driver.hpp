// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// Drives the fetch machine (toy spec) from a script of bytes and checks the
// laws of docs/providers-design.md §5.5 after every step. Shared by the
// seeded property test and the libFuzzer target fuzz_fetch_machine.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/overloaded.hpp"
#include "mov/fetch/machine.hpp"
#include "toy_spec.hpp"

namespace mov::fetch::test {

/// The script's virtual start, 2026-10-01T00:00:00Z.
[[nodiscard]] constexpr core::Time t0_driver() {
  return core::Time{std::chrono::sys_days{std::chrono::year{2026} /
                                          std::chrono::October / 1}};
}

/// The run a script byte prefix chooses.
struct DriverSetup {
  ToyRequest request;
  Options options;
};

/// Reads a setup from the first bytes of a script (consumed).
[[nodiscard]] inline DriverSetup setup_from(
    std::span<const std::uint8_t>& bytes) {
  const auto take = [&]() -> std::uint8_t {
    if (bytes.empty()) {
      return 0;
    }
    const std::uint8_t b = bytes.front();
    bytes = bytes.subspan(1);
    return b;
  };
  constexpr std::uint8_t origin_count = 5;
  const std::uint8_t shape = take();
  const std::uint8_t mode = take();
  DriverSetup s{.request = {.units = 1U + (shape & 3U),
                            .slots = 1U + ((shape >> 2U) & 1U),
                            .pages = 1U + ((shape >> 3U) & 3U),
                            .fallback = ((shape >> 5U) & 1U) != 0,
                            .origin = static_cast<Origin>(mode % origin_count),
                            .fail_initial = false,
                            .fail_next = ((shape >> 6U) & 1U) != 0},
                .options = {}};
  s.options.strictness =
      ((mode >> 3U) & 1U) != 0 ? Strictness::lenient : Strictness::strict;
  constexpr int deadline_unit_s = 30;
  if (((mode >> 4U) & 1U) != 0) {
    s.options.deadline = t0_driver() + std::chrono::seconds{deadline_unit_s *
                                                            (1 + (mode >> 5U))};
  }
  const std::uint8_t attempts = static_cast<std::uint8_t>(1U + (take() & 3U));
  s.options.retry = *RetryPolicy::make({.max_attempts = attempts});
  return s;
}

class Driver {
 public:
  using Values = std::vector<std::int64_t>;

  explicit Driver(DriverSetup setup)
      : setup_{setup},
        started_{Machine<ToySpec>::start(setup.request, setup.options, now_)} {
    record(started_.steps);
  }

  /// One scripted action: `op` picks it, `arg` its details.
  void act(std::uint8_t op, std::uint8_t arg) {
    constexpr std::uint8_t ops = 11;
    switch (op % ops) {
      case 0:
        respond(arg);
        break;
      case 1:
        transport_failure(arg);
        break;
      case 2:
        fire_timer(arg);
        break;
      case 3:
        parse_ok(arg);
        break;
      case 4:
        parse_error(arg);
        break;
      case 5:
        merge(arg);
        break;
      case 6:
        cancelled_ = not finished_ and not past_deadline();
        feed(event::Cancel{});
        break;
      case 7:
        stale(arg);
        break;
      case 8:
        now_ += std::chrono::seconds{arg};
        break;
      case 9:
        rate_limited(arg);
        break;
      default:
        respond_ok_all();
        break;
    }
  }

  /// Answers everything well until the machine finishes. Every path ends
  /// within a bound; a machine that stops asking while unfinished is stuck.
  void drain() {
    constexpr int bound = 100'000;
    for (int i = 0; i < bound and not finished_ and ok(); ++i) {
      if (not in_flight_.empty()) {
        complete(in_flight_.begin()->first,
                 Response{.status = 200, .bytes = 1});
      } else if (not parsing_.empty()) {
        deliver_parse(parsing_.begin()->first);
      } else if (merge_pending_) {
        merge(0);
      } else if (not timers_.empty()) {
        fire_timer(0);
      } else {
        fail("stuck: unfinished with nothing to wait for");
      }
    }
    if (not finished_) {
      fail("did not finish");
    }
  }

  [[nodiscard]] bool ok() const { return violation_.empty(); }
  [[nodiscard]] const std::string& violation() const { return violation_; }
  [[nodiscard]] bool finished() const { return finished_; }
  /// Whether the run finished with a value.
  [[nodiscard]] bool succeeded() const {
    return finish_.has_value() and finish_->result.has_value();
  }

 private:
  using Target = ToySpec::Target;
  using TargetKey =
      std::tuple<std::uint32_t, std::uint32_t, bool, std::uint32_t>;

  [[nodiscard]] static TargetKey key(const Target& t) {
    return {t.unit.unit, t.unit.page, t.unit.fallback, t.slot};
  }

  void fail(const std::string& what) {
    if (violation_.empty()) {
      violation_ = what;
    }
  }

  [[nodiscard]] bool past_deadline() const {
    return setup_.options.deadline and now_ >= *setup_.options.deadline;
  }

  std::vector<Step<ToySpec>> feed(Event<ToySpec> e) {
    auto steps = started_.machine.on(std::move(e), now_, jitter_);
    if (finished_ and not steps.empty()) {
      fail("a step after Finish");
    }
    record(steps);
    return steps;
  }

  /// An event the machine must ignore: no steps, unless the deadline ends it.
  void expect_ignored(Event<ToySpec> e) {
    const bool deadline = past_deadline();
    const bool was_finished = finished_;
    const auto steps = feed(std::move(e));
    if (steps.empty()) {
      return;
    }
    const bool deadline_finish =
        deadline and not was_finished and
        std::holds_alternative<Finish<Values>>(steps.back());
    if (not deadline_finish) {
      fail("an unknown or stale id caused steps");
    }
  }

  // ---- recording and checking steps ---------------------------------------

  void record(const std::vector<Step<ToySpec>>& steps) {
    for (const auto& step : steps) {
      if (finished_) {
        fail("a step after Finish");
      }
      std::visit(
          core::Overloaded{[&](const Send<Target>& s) { on_send(s); },
                           [&](const Wait& w) { on_wait(w); },
                           [&](const CoolDown& c) { on_cool_down(c); },
                           [&](const Parse<Target>& p) { on_parse(p); },
                           [&](const Merge<Values>& m) { on_merge(m); },
                           [&](const Abort& a) { on_abort(a); },
                           [&](const Progress& p) { on_progress(p); },
                           [&](const Finish<Values>& f) { on_finish(f); }},
          step);
    }
  }

  void on_send(const Send<Target>& s) {
    if (cancelled_ or past_deadline()) {
      fail("Send after Cancel or past the deadline");
    }
    if (not ids_.insert(s.id.value).second) {
      fail("an id was reused");
    }
    if (const auto until = cool_until_.find(s.origin);
        until != cool_until_.end() and now_ < until->second) {
      fail("Send during the origin's cool-down");
    }
    const auto attempts = ++attempts_[key(s.target)];
    if (attempts > setup_.options.retry.max_attempts()) {
      fail("more sends of one slot than max_attempts");
    }
    ++sends_;
    in_flight_.emplace(s.id, s.target);
  }

  void on_wait(const Wait& w) {
    if (w.delay < std::chrono::milliseconds{0}) {
      fail("a negative wait");
    }
    if (not ids_.insert(w.id.value).second) {
      fail("an id was reused");
    }
    timers_.emplace(w.id, now_ + w.delay);
  }

  void on_cool_down(const CoolDown& c) {
    auto& until = cool_until_[c.origin];
    until = std::max(until, now_ + c.for_);
  }

  void on_parse(const Parse<Target>& p) {
    if (not answered_ok_.contains(p.id)) {
      fail("Parse of an exchange not answered with 2xx");
    }
    parsing_.emplace(p.id, p.target);
  }

  void on_merge(const Merge<Values>& m) {
    if (merge_pending_ or merged_) {
      fail("a second Merge");
    }
    merge_pending_ = true;
    merge_values_.clear();
    for (const auto& part : m.in_plan_order) {
      merge_values_.insert(merge_values_.end(), part.begin(), part.end());
    }
    if (not std::ranges::is_sorted(merge_values_) or
        std::ranges::adjacent_find(merge_values_) != merge_values_.end()) {
      fail("the merge is not in plan order");
    }
    for (const std::int64_t v : merge_values_) {
      if (not delivered_.contains(v)) {
        fail("the merge holds a part that was never parsed");
      }
    }
  }

  void on_abort(const Abort& a) {
    for (const ExchangeId id : a.ids) {
      if (in_flight_.erase(id) == 0) {
        fail("Abort of an exchange not in flight");
      }
    }
  }

  void on_progress(const Progress& p) {
    if (p.done < last_progress_.done or p.total < last_progress_.total or
        p.done > p.total) {
      fail("Progress went back or past its total");
    }
    last_progress_ = p;
  }

  void on_finish(const Finish<Values>& f) {
    finished_ = true;
    finish_ = f;
    if (not in_flight_.empty()) {
      fail("Finish with exchanges neither answered nor aborted");
    }
    if (cancelled_ and (f.result or not std::holds_alternative<core::Cancelled>(
                                        f.result.error().error))) {
      fail("Cancel did not finish Cancelled");
    }
    if (f.result) {
      if (f.result->value != merge_values_) {
        fail("Finish does not hold the merge");
      }
      if (last_progress_.done != last_progress_.total) {
        fail("a successful fetch did not reach its total");
      }
      if (f.result->stats.exchanges != sends_) {
        fail("Stats.exchanges is not the number of sends");
      }
    }
    // Bounded exchanges: every node of the plan at most max_attempts sends a
    // slot.
    const std::uint64_t nodes = std::uint64_t{setup_.request.units} *
                                setup_.request.pages *
                                (setup_.request.fallback ? 2U : 1U);
    if (sends_ >
        nodes * setup_.request.slots * setup_.options.retry.max_attempts()) {
      fail("more exchanges than the plan allows");
    }
  }

  // ---- actions --------------------------------------------------------------

  template <class Map>
  [[nodiscard]] static std::optional<typename Map::key_type> pick(
      const Map& m, std::uint8_t arg) {
    if (m.empty()) {
      return std::nullopt;
    }
    auto it = m.begin();
    std::advance(it, static_cast<std::ptrdiff_t>(arg % m.size()));
    return it->first;
  }

  void complete(ExchangeId id, Outcome outcome) {
    // Past the deadline the machine aborts this exchange with the rest
    // instead of taking its answer.
    if (not past_deadline()) {
      in_flight_.erase(id);
    }
    const bool accepted =
        std::holds_alternative<Response>(outcome) and
        classify(setup_.request.origin, std::get<Response>(outcome).status) ==
            Verdict::accept;
    if (accepted) {
      answered_ok_.insert(id);
    }
    feed(event::Completed{.id = id, .outcome = std::move(outcome)});
  }

  void respond(std::uint8_t arg) {
    constexpr std::array<int, 8> statuses{200, 200, 204, 404,
                                          500, 503, 400, 302};
    if (const auto id = pick(in_flight_, arg)) {
      complete(*id, Response{.status = statuses[arg % statuses.size()],
                             .retry_after = std::nullopt,
                             .rate_remaining = std::nullopt,
                             .bytes = arg});
    }
  }

  void respond_ok_all() {
    while (not in_flight_.empty() and not finished_) {
      complete(in_flight_.begin()->first, Response{.status = 200});
    }
  }

  void rate_limited(std::uint8_t arg) {
    if (const auto id = pick(in_flight_, arg)) {
      constexpr std::uint8_t remaining_zero = 3;
      if (arg % 4 == remaining_zero) {
        complete(*id, Response{.status = 200, .rate_remaining = 0});
        return;
      }
      // Up to 255 s: some beyond max_retry_after (120 s).
      const std::optional<std::chrono::seconds> after =
          arg % 2 == 0
              ? std::optional<std::chrono::seconds>{std::chrono::seconds{arg}}
              : std::nullopt;
      complete(*id, Response{.status = 429, .retry_after = after});
    }
  }

  void transport_failure(std::uint8_t arg) {
    constexpr std::uint8_t codes = 9;
    if (const auto id = pick(in_flight_, arg)) {
      complete(*id, static_cast<TransportErrc>(arg % codes));
    }
  }

  void fire_timer(std::uint8_t arg) {
    if (const auto id = pick(timers_, arg)) {
      now_ = std::max(now_, timers_.at(*id));
      timers_.erase(*id);
      feed(event::WaitElapsed{.id = *id});
    }
  }

  void deliver_parse(ExchangeId id) {
    const Target t = parsing_.at(id);
    parsing_.erase(id);
    const std::int64_t v = encode(t);
    delivered_.insert(v);
    feed(event::Parsed<ToyPage>{
        .id = id,
        .parsed = io::Read<ToyPage>{.value = {.values = {v}}, .warnings = {}}});
  }

  void parse_ok(std::uint8_t arg) {
    if (const auto id = pick(parsing_, arg)) {
      deliver_parse(*id);
    }
  }

  void parse_error(std::uint8_t arg) {
    const auto id = pick(parsing_, arg);
    if (not id) {
      return;
    }
    parsing_.erase(*id);
    constexpr std::uint8_t kinds = 4;
    Error error = NoData{};
    switch (arg % kinds) {
      case 0:
        break;
      case 1:
        error = ServiceError::make(Origin::coops_data,
                                   ServiceErrc::station_or_system, "");
        break;
      case 2:
        error =
            io::ParseError::make(io::ParseErrc::bad_number, {.line = 1}, "");
        break;
      default:
        error = io::ParseError::make(io::ParseErrc::corrupt_record, {.line = 1},
                                     "");
        break;
    }
    feed(event::Parsed<ToyPage>{.id = *id,
                                .parsed = std::unexpected{std::move(error)}});
  }

  void merge(std::uint8_t arg) {
    if (not merge_pending_) {
      expect_ignored(event::Merged<Values>{
          .value = io::Read<Values>{.value = {}, .warnings = {}}});
      return;
    }
    merge_pending_ = false;
    merged_ = true;
    constexpr std::uint8_t fail_one_in = 16;
    if (arg % fail_one_in == 1) {
      feed(event::Merged<Values>{
          .value = std::unexpected{Error{MetaMismatch{.column = "toy"}}}});
      return;
    }
    feed(event::Merged<Values>{
        .value = io::Read<Values>{.value = merge_values_, .warnings = {}}});
  }

  /// An id the machine no longer (or never) knows.
  void stale(std::uint8_t arg) {
    constexpr std::uint32_t far = 1U << 30U;
    const std::uint32_t value = arg % 2 == 0 ? far + arg : arg;
    const bool live =
        std::ranges::any_of(
            in_flight_,
            [&](const auto& e) { return e.first.value == value; }) or
        std::ranges::any_of(
            parsing_, [&](const auto& e) { return e.first.value == value; }) or
        std::ranges::any_of(
            timers_, [&](const auto& e) { return e.first.value == value; });
    if (live) {
      return;
    }
    switch (arg % 3) {
      case 0:
        expect_ignored(event::Completed{.id = {value},
                                        .outcome = Response{.status = 200}});
        break;
      case 1:
        expect_ignored(event::WaitElapsed{.id = {value}});
        break;
      default:
        expect_ignored(event::Parsed<ToyPage>{
            .id = {value},
            .parsed = io::Read<ToyPage>{.value = {}, .warnings = {}}});
        break;
    }
  }

  DriverSetup setup_;
  core::Time now_ = t0_driver();
  Jitter jitter_ = Jitter::from_bits(0x9E3779B97F4A7C15ULL);
  Started<ToySpec> started_;
  std::map<ExchangeId, Target> in_flight_;
  std::map<ExchangeId, Target> parsing_;
  std::map<TimerId, core::Time> timers_;
  std::map<Origin, core::Time> cool_until_;
  std::map<TargetKey, std::uint32_t> attempts_;
  std::set<std::uint32_t> ids_;
  std::set<ExchangeId> answered_ok_;
  std::set<std::int64_t> delivered_;
  Values merge_values_;
  bool merge_pending_ = false;
  bool merged_ = false;
  bool cancelled_ = false;
  bool finished_ = false;
  std::uint32_t sends_ = 0;
  Progress last_progress_{.done = 0, .total = 0};
  std::optional<Finish<Values>> finish_;
  std::string violation_;
};

/// Runs a whole script: setup bytes, then (op, arg) pairs, then drain().
[[nodiscard]] inline Driver run_script(std::span<const std::uint8_t> bytes) {
  Driver d{setup_from(bytes)};
  while (bytes.size() >= 2 and not d.finished() and d.ok()) {
    d.act(bytes[0], bytes[1]);
    bytes = bytes.subspan(2);
  }
  if (d.ok() and not d.finished()) {
    d.drain();
  }
  return d;
}

}  // namespace mov::fetch::test
