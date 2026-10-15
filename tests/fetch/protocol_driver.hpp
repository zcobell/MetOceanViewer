// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// Drives a fetch machine (Machine<S>, or Then<S1, Bind, S2>) from a script
// of bytes and checks the laws of docs/providers-design.md §5.5 after every
// step. A World says how to build the machine and how to answer its Parse
// and Merge steps, and may check more at Finish. Shared by the seeded
// property tests and the libFuzzer target fuzz_fetch_machine.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "mov/fetch/machine.hpp"

namespace mov::fetch::test {

/// The script's virtual start, 2026-10-01T00:00:00Z.
[[nodiscard]] constexpr core::Time script_start() {
  return core::Time{std::chrono::sys_days{std::chrono::year{2026} /
                                          std::chrono::October / 1}};
}

/// How a test answers a Parse step.
enum class ParseAnswer : std::uint8_t {
  data,
  absent,
  station_or_system,
  parse_error,
};

/// Reads bytes off the front of a script.
class ScriptBytes {
 public:
  explicit ScriptBytes(std::span<const std::uint8_t> bytes) : bytes_{bytes} {}
  [[nodiscard]] std::uint8_t take() {
    if (bytes_.empty()) {
      return 0;
    }
    const std::uint8_t b = bytes_.front();
    bytes_ = bytes_.subspan(1);
    return b;
  }
  [[nodiscard]] std::span<const std::uint8_t> rest() const { return bytes_; }

 private:
  std::span<const std::uint8_t> bytes_;
};

template <class T>
inline constexpr bool is_send_v = false;
template <class T>
inline constexpr bool is_send_v<Send<T>> = true;
template <class T>
inline constexpr bool is_parse_v = false;
template <class T>
inline constexpr bool is_parse_v<Parse<T>> = true;
template <class T>
inline constexpr bool is_merge_v = false;
template <class T>
inline constexpr bool is_merge_v<Merge<T>> = true;
template <class T>
inline constexpr bool is_finish_v = false;
template <class T>
inline constexpr bool is_finish_v<Finish<T>> = true;

/// Options drawn from two script bytes.
[[nodiscard]] inline Options options_from(ScriptBytes& bytes) {
  const std::uint8_t mode = bytes.take();
  const std::uint8_t more = bytes.take();
  Options o;
  o.strictness = (mode & 1U) != 0 ? Strictness::lenient : Strictness::strict;
  constexpr int deadline_step_s = 30;
  if (((mode >> 1U) & 1U) != 0) {
    o.deadline = script_start() +
                 std::chrono::seconds{deadline_step_s * ((mode >> 2U) & 7U)};
  }
  o.retry = *RetryPolicy::make(
      {.max_attempts = static_cast<std::uint8_t>(1U + (more & 3U))});
  constexpr std::uint32_t id_step = 1000;
  o.ids = detail::IdAccess::at(id_step * ((more >> 2U) & 7U));
  o.priority =
      ((more >> 5U) & 1U) != 0 ? Priority::background : Priority::interactive;
  return o;
}

template <class World>
class Driver {
 public:
  using EventT = typename World::Event;
  using StepT = typename World::Step;
  using Steps = std::vector<StepT>;

  Driver(World world, Options options)
      : world_{std::move(world)},
        options_{options},
        machine_{world_.start(options_, now_)} {
    record(machine_.steps, false);
  }

  /// One scripted action: `op` picks it, `arg` its details.
  void act(std::uint8_t op, std::uint8_t arg) {
    jitter_ = jitter_from(arg);
    constexpr std::uint8_t ops = 13;
    switch (op % ops) {
      case 0:
        respond(arg);
        break;
      case 1:
        transport_failure(arg);
        break;
      case 2:
        fire_timer(arg, false);
        break;
      case 3:
        answer_parse(arg, ParseAnswer::data);
        break;
      case 4:
        answer_parse(arg, static_cast<ParseAnswer>(1 + (arg % 3)));
        break;
      case 5:
        answer_merge(arg);
        break;
      case 6:
        cancel();
        break;
      case 7:
        stale(arg);
        break;
      case 8:
        advance(arg);
        break;
      case 9:
        rate_limited(arg);
        break;
      case 10:
        fire_timer(arg, true);
        break;
      case 11:
        stale_parse(arg);
        break;
      default:
        respond_ok_all();
        break;
    }
  }

  /// Answers everything well until the machine finishes: exchanges, then
  /// parses, then the merge, then the earliest-due timer. A machine that
  /// stops asking while unfinished is stuck.
  void drain() {
    constexpr int bound = 200'000;
    for (int i = 0; i < bound and not finished_ and ok(); ++i) {
      if (not in_flight_.empty()) {
        complete(in_flight_.begin()->first,
                 Response{.status = 200, .bytes = 1});
      } else if (not parsing_.empty()) {
        deliver_parse(parsing_.begin()->first, 0, ParseAnswer::data);
      } else if (merge_) {
        answer_merge(0);
      } else if (not timers_.empty()) {
        fire_earliest();
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
  [[nodiscard]] bool succeeded() const { return succeeded_; }
  [[nodiscard]] World& world() { return world_; }

  void fail(const std::string& what) {
    if (violation_.empty()) {
      violation_ = what;
    }
  }

 private:
  /// The time the next event will make the machine see: the latest it saw
  /// or what the event says, whichever is later.
  [[nodiscard]] core::Time effective() const { return std::max(now_, fed_); }

  [[nodiscard]] bool past_deadline() const {
    return options_.deadline and effective() >= *options_.deadline;
  }

  [[nodiscard]] static Jitter jitter_from(std::uint8_t arg) {
    constexpr std::uint64_t golden = 0x9E3779B97F4A7C15ULL;
    switch (arg % 3) {
      case 0:
        return Jitter::from_bits(0);
      case 1:
        return Jitter::from_bits(~std::uint64_t{0});  // just below 1
      default:
        return Jitter::from_bits(golden * arg);
    }
  }

  /// What a fed response must make the machine cool down.
  struct ExpectedCool {
    Origin origin;
    std::optional<std::chrono::milliseconds> exactly;
  };

  Steps feed(EventT e, std::optional<ExpectedCool> cool = std::nullopt) {
    const bool deadline = past_deadline();
    now_ = effective();
    Steps steps = machine_.machine.on(std::move(e), fed_, jitter_);
    if (finished_ and not steps.empty()) {
      fail("a step after Finish");
    }
    check_cool_downs(steps, cool, deadline);
    record(steps, deadline);
    return steps;
  }

  /// Every CoolDown comes from a response that asked for one, and every
  /// such response gets one (unless the deadline ended the fetch first).
  void check_cool_downs(const Steps& steps, std::optional<ExpectedCool> cool,
                        bool deadline) {
    std::vector<CoolDown> seen;
    for (const auto& s : steps) {
      if (const auto* c = std::get_if<CoolDown>(&s)) {
        seen.push_back(*c);
      }
    }
    if (deadline or finished_) {
      if (not seen.empty()) {
        fail("a CoolDown past the deadline or after Finish");
      }
      return;
    }
    if (not cool) {
      if (not seen.empty()) {
        fail("a CoolDown no response asked for");
      }
      return;
    }
    if (seen.size() != 1 or seen[0].origin != cool->origin or
        seen[0].for_ < min_cool_down) {
      fail("a 429 or exhausted rate limit without its one CoolDown");
      return;
    }
    if (cool->exactly and seen[0].for_ != *cool->exactly) {
      fail("a CoolDown that is not the server's Retry-After");
    }
  }

  /// An event the machine must ignore: no steps, unless the deadline ends it.
  void expect_ignored(EventT e) {
    const bool deadline = past_deadline();
    const auto steps = feed(std::move(e));
    if (not steps.empty() and not deadline) {
      fail("an unknown or stale id caused steps");
    }
  }

  // ---- recording and checking steps ---------------------------------------

  void record(const Steps& steps, bool deadline) {
    for (const auto& step : steps) {
      if (finished_) {
        fail("a step after Finish");
      }
      std::visit([&](const auto& s) { on_step(s, deadline); }, step);
    }
  }

  template <class T>
  void on_step(const T& s, bool deadline) {
    if constexpr (is_send_v<T>) {
      on_send(s.id, s.origin);
      world_.on_send(s, *this);
    } else if constexpr (std::is_same_v<T, Wait>) {
      on_wait(s);
    } else if constexpr (std::is_same_v<T, CoolDown>) {
      auto& until = cool_until_[s.origin];
      until = std::max(until, now_ + s.for_);
    } else if constexpr (is_parse_v<T>) {
      on_parse(s);
    } else if constexpr (is_merge_v<T>) {
      if (merge_) {
        fail("a Merge while one is pending");
      }
      if (++merges_ > World::max_merges) {
        fail("more Merge steps than the machine has stages");
      }
      merge_ = [this, s](std::uint8_t arg) { return world_.merge(s, arg); };
      world_.on_merge(s, *this);
    } else if constexpr (std::is_same_v<T, Abort>) {
      on_abort(s);
    } else if constexpr (std::is_same_v<T, Progress>) {
      on_progress(s);
    } else if constexpr (is_finish_v<T>) {
      on_finish(s, deadline);
    }
  }

  void on_send(ExchangeId id, Origin origin) {
    if (cancelled_ or past_deadline()) {
      fail("Send after Cancel or past the deadline");
    }
    if (not ids_.insert(id.value).second) {
      fail("an id was reused");
    }
    if (const auto until = cool_until_.find(origin);
        until != cool_until_.end() and now_ < until->second) {
      fail("Send during the origin's cool-down");
    }
    ++sends_;
    in_flight_.emplace(id, origin);
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

  template <class T>
  void on_parse(const Parse<T>& p) {
    if (not answered_ok_.contains(p.id)) {
      fail("Parse of an exchange not answered with 2xx");
    }
    parsing_.emplace(p.id, [this, p](std::uint8_t arg, ParseAnswer a) {
      return world_.parse(p, arg, a);
    });
  }

  void on_abort(const Abort& a) {
    for (const ExchangeId id : a.exchanges) {
      if (in_flight_.erase(id) == 0) {
        fail("Abort of an exchange not in flight");
      }
    }
    for (const ExchangeId id : a.parses) {
      if (parsing_.erase(id) == 0) {
        fail("Abort of a parse not running");
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

  template <class V>
  void on_finish(const Finish<V>& f, bool deadline) {
    finished_ = true;
    succeeded_ = f.result.has_value();
    if (not in_flight_.empty() or not parsing_.empty()) {
      fail("Finish with exchanges or parses neither answered nor aborted");
    }
    if (deadline and (f.result or not std::holds_alternative<DeadlineExceeded>(
                                      f.result.error().error))) {
      fail("a run past its deadline did not end DeadlineExceeded");
    }
    if (cancelled_ and not deadline and
        (f.result or
         not std::holds_alternative<core::Cancelled>(f.result.error().error))) {
      fail("Cancel did not finish Cancelled");
    }
    if (f.result) {
      if (last_progress_.done != last_progress_.total) {
        fail("a successful fetch did not reach its total");
      }
      if (f.result->trail.stats.exchanges != sends_) {
        fail("Stats.exchanges is not the number of sends");
      }
    }
    world_.on_finish(f, *this);
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
    const Origin origin = in_flight_.at(id);
    // Past the deadline the machine aborts this exchange with the rest.
    if (not past_deadline()) {
      in_flight_.erase(id);
    }
    std::optional<ExpectedCool> cool;
    if (const auto* r = std::get_if<Response>(&outcome)) {
      const Verdict v = classify(origin, r->status);
      if (v == Verdict::accept) {
        answered_ok_.insert(id);
        if (r->rate_remaining and *r->rate_remaining <= 0) {
          cool = ExpectedCool{
              .origin = origin,
              .exactly = std::max(r->retry_after
                                      ? detail::bounded_delay(*r->retry_after)
                                      : options_.retry.base(),
                                  min_cool_down)};
        }
      } else if (v == Verdict::cool_down) {
        cool = ExpectedCool{.origin = origin, .exactly = std::nullopt};
        if (r->retry_after) {
          cool->exactly =
              std::max(detail::bounded_delay(*r->retry_after), min_cool_down);
        }
      }
    }
    feed(event::Completed{.id = id, .outcome = std::move(outcome)}, cool);
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
    while (not in_flight_.empty() and not finished_ and not past_deadline()) {
      complete(in_flight_.begin()->first, Response{.status = 200});
    }
  }

  void rate_limited(std::uint8_t arg) {
    const auto id = pick(in_flight_, arg);
    if (not id) {
      return;
    }
    const std::optional<std::chrono::seconds> after =
        arg % 2 == 0 ? std::optional{std::chrono::seconds{arg}} : std::nullopt;
    if (arg % 4 == 1) {
      complete(*id, Response{.status = 200,
                             .retry_after = after,
                             .rate_remaining = -(arg % 2)});
      return;
    }
    complete(*id, Response{.status = 429, .retry_after = after});
  }

  void transport_failure(std::uint8_t arg) {
    constexpr std::uint8_t codes = 9;
    if (const auto id = pick(in_flight_, arg)) {
      complete(*id, static_cast<TransportErrc>(arg % codes));
    }
  }

  void fire(TimerId id) {
    timers_.erase(id);
    feed(event::WaitElapsed{.id = id});
  }

  void fire_timer(std::uint8_t arg, bool early) {
    const auto id = pick(timers_, arg);
    if (not id) {
      return;
    }
    const core::Time due = timers_.at(*id);
    if (early and effective() < due and not past_deadline()) {
      // Fired before its time: the machine waits again, nothing else.
      timers_.erase(*id);
      const auto steps = feed(event::WaitElapsed{.id = *id});
      // (A timer of a unit that failed, or of a stage that is over, is
      // stale: nothing at all.)
      const bool waits_again =
          steps.size() == 1 and std::holds_alternative<Wait>(steps[0]);
      if (not steps.empty() and not waits_again) {
        fail("an early timer did something but wait again");
      }
      return;
    }
    set_now(std::max(effective(), due));
    fire(*id);
  }

  void fire_earliest() {
    const auto earliest = std::ranges::min_element(
        timers_, {}, [](const auto& entry) { return entry.second; });
    set_now(std::max(effective(), earliest->second));
    fire(earliest->first);
  }

  void deliver_parse(ExchangeId id, std::uint8_t arg, ParseAnswer answer) {
    auto make = parsing_.at(id);
    // Past the deadline the machine stops this parse with the rest.
    if (not past_deadline()) {
      parsing_.erase(id);
    }
    feed(make(arg, answer));
  }

  void answer_parse(std::uint8_t arg, ParseAnswer answer) {
    if (const auto id = pick(parsing_, arg)) {
      deliver_parse(*id, arg, answer);
    }
  }

  void answer_merge(std::uint8_t arg) {
    if (not merge_) {
      expect_ignored(world_.stale_merge());
      return;
    }
    auto make = std::move(*merge_);
    merge_.reset();
    feed(make(arg));
  }

  void cancel() {
    cancelled_ = not finished_ and not past_deadline();
    feed(event::Cancel{});
  }

  /// Time moves: forward up to 127 s, or back (a feed that is not
  /// monotonic, which the machine takes as the latest it saw).
  void advance(std::uint8_t arg) {
    constexpr std::uint8_t backwards = 0x80;
    const std::chrono::seconds by{arg & 0x7FU};
    if ((arg & backwards) != 0) {
      fed_ -= by;
    } else {
      set_now(effective() + by);
    }
  }

  /// The time the next event says.
  void set_now(core::Time t) { fed_ = t; }

  [[nodiscard]] bool live(std::uint32_t value) const {
    const auto has = [&](const auto& m) {
      return std::ranges::any_of(
          m, [&](const auto& e) { return e.first.value == value; });
    };
    return has(in_flight_) or has(parsing_) or has(timers_);
  }

  /// An id the machine no longer (or never) knows.
  void stale(std::uint8_t arg) {
    constexpr std::uint32_t far = 1U << 30U;
    const std::uint32_t value = arg % 2 == 0 ? far + arg : arg;
    if (live(value)) {
      return;
    }
    if (arg % 3 == 0) {
      expect_ignored(
          event::Completed{.id = {value}, .outcome = Response{.status = 200}});
    } else {
      expect_ignored(event::WaitElapsed{.id = {value}});
    }
  }

  void stale_parse(std::uint8_t arg) {
    constexpr std::uint32_t far = 1U << 29U;
    if (not live(far + arg)) {
      expect_ignored(world_.stale_parse(ExchangeId{.value = far + arg}));
    }
  }

  World world_;
  Options options_;
  core::Time now_ = script_start();  // the machine's clock: the latest fed
  core::Time fed_ = script_start();  // what the next event says
  Jitter jitter_ = Jitter::from_bits(0);
  typename World::Started machine_;
  std::map<ExchangeId, Origin> in_flight_;
  std::map<ExchangeId, std::function<EventT(std::uint8_t, ParseAnswer)>>
      parsing_;
  std::optional<std::function<EventT(std::uint8_t)>> merge_;
  std::map<TimerId, core::Time> timers_;
  std::map<Origin, core::Time> cool_until_;
  std::set<std::uint32_t> ids_;
  std::set<ExchangeId> answered_ok_;
  int merges_ = 0;
  bool cancelled_ = false;
  bool finished_ = false;
  bool succeeded_ = false;
  std::uint32_t sends_ = 0;
  Progress last_progress_{.done = 0, .total = 0};
  std::string violation_;
};

/// Runs a whole script: the world's and the options' setup bytes, then
/// (op, arg) pairs, then drain().
template <class World>
[[nodiscard]] Driver<World> run_script(ScriptBytes bytes) {
  World world = World::from(bytes);
  const Options options = options_from(bytes);
  Driver<World> d{std::move(world), options};
  auto rest = bytes.rest();
  while (rest.size() >= 2 and not d.finished() and d.ok()) {
    d.act(rest[0], rest[1]);
    rest = rest.subspan(2);
  }
  if (d.ok() and not d.finished()) {
    d.drain();
  }
  return d;
}

}  // namespace mov::fetch::test
