// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// The sans-IO fetch machine (docs/providers-design.md §5.5): one stage of a
// fetch as a state machine over events. No bytes, clock or threads inside:
// the job (providers) performs the steps and feeds back what happened, and
// the tests drive it with scripted events in virtual time.

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/overloaded.hpp"
#include "mov/core/time.hpp"
#include "mov/fetch/error.hpp"
#include "mov/fetch/origin.hpp"
#include "mov/fetch/policy.hpp"
#include "mov/fetch/retry.hpp"
#include "mov/fetch/steps.hpp"

namespace mov::fetch {

/// The shortest cool-down: a 429 or an exhausted rate limit always pauses
/// the origin this long at least (never a 0 ms CoolDown).
inline constexpr std::chrono::milliseconds min_cool_down =
    std::chrono::seconds{1};

/// The clock values a machine accepts: within ±10,000 years of the epoch,
/// so `now + delay` and `deadline - now` cannot overflow.
inline constexpr std::chrono::milliseconds clock_limit =
    std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::years{10'000});

namespace detail {

/// d clamped to [0, clock_limit]: a server's Retry-After of any size is a
/// delay the clock arithmetic can carry.
[[nodiscard]] constexpr std::chrono::milliseconds bounded_delay(
    std::chrono::seconds d) noexcept {
  const auto limit_s =
      std::chrono::duration_cast<std::chrono::seconds>(clock_limit);
  return std::chrono::milliseconds{
      std::clamp(d, std::chrono::seconds{0}, limit_s)};
}

[[nodiscard]] constexpr bool within_clock(core::Time t) noexcept {
  return t.time_since_epoch() >= -clock_limit and
         t.time_since_epoch() <= clock_limit;
}

/// t clamped into the accepted clock range.
[[nodiscard]] constexpr core::Time clamp_clock(core::Time t) noexcept {
  return core::Time{
      std::clamp(t.time_since_epoch(), -clock_limit, clock_limit)};
}

}  // namespace detail

template <FetchSpec S>
struct Started;

/// One stage of a fetch. start() plans the units and returns the first
/// steps; on() takes one event and returns the steps it causes.
///
/// The feed must be monotonic: an event's `now` earlier than one already
/// seen is taken as the latest seen.
///
/// Laws (tested over generated scripts and fuzzed, §10.1):
/// - total: an event naming an unknown, aborted or finished id, a Merged
///   before the Merge step, and any event after Finish return no steps
///   (unless the deadline has passed, which finishes the fetch);
/// - each Send's id later appears in exactly one accepted Completed or in
///   an Abort; a slot is sent at most max_attempts times;
/// - no Send to an origin before its cool-down ends; every 429 and every
///   exhausted rate limit emits a CoolDown;
/// - exactly one Finish, after which nothing; after Cancel or past the
///   deadline, only an Abort comes before it;
/// - a successful Finish holds the merge of exactly the parts of the units
///   that neither failed nor were absent, in plan order (initial order; a
///   unit's successors right after it);
/// - Progress.done is monotone and reaches total on success.
template <FetchSpec S>
class Machine {
 public:
  using Steps = std::vector<Step<S>>;

  [[nodiscard]] static Started<S> start(typename S::Request request,
                                        Options options, core::Time now);

  /// The steps one event causes. `now` is the event's time (virtual in the
  /// tests); `jitter` decides any backoff it starts.
  [[nodiscard]] Steps on(Event<S> e, core::Time now, Jitter jitter) {
    Steps out = tick(now);
    if (phase_ == Phase::finished) {
      return out;
    }
    const Context cx{.now = latest_now_, .jitter = jitter, .out = out};
    std::visit([&](auto&& ev) { handle(std::forward<decltype(ev)>(ev), cx); },
               std::move(e));
    return out;
  }

  /// Time passed with no event (what every event does first): past the
  /// deadline, the fetch aborts and finishes DeadlineExceeded.
  [[nodiscard]] Steps tick(core::Time now) {
    Steps out;
    if (phase_ == Phase::finished) {
      return out;
    }
    latest_now_ = std::max(latest_now_, detail::clamp_clock(now));
    if (options_.deadline and latest_now_ >= *options_.deadline) {
      finish_failure(out, DeadlineExceeded{}, std::nullopt);
    }
    return out;
  }

  [[nodiscard]] bool finished() const noexcept {
    return phase_ == Phase::finished;
  }

  /// Where a following stage's ids continue.
  [[nodiscard]] IdCursor next_ids() const noexcept {
    return detail::IdAccess::at(next_id_);
  }

  /// The origins cooling down, until when: what a following stage inherits
  /// (Options::cooling).
  [[nodiscard]] const std::array<std::optional<core::Time>,
                                 every_origin.size()>&
  cooling() const noexcept {
    return cool_until_;
  }

  /// The latest time an event carried (the start's `now` at first).
  [[nodiscard]] core::Time latest_now() const noexcept { return latest_now_; }

 private:
  using Unit = typename S::Unit;
  using Target = typename S::Target;
  using Parsed = typename S::Parsed;
  using Part = typename S::Part;
  using Value = typename S::Value;
  using Result = SlotResult<Parsed>;

  enum class Phase : std::uint8_t { running, merging, finished };

  // One exchange slot of a unit: a target and where its current attempt is.
  struct Idle {};  // not sent yet
  struct InFlight {
    ExchangeId id;
  };
  struct Waiting {
    TimerId id;
    core::Time not_before;  // a timer that fires early waits again
  };
  struct Parsing {
    ExchangeId id;
  };
  struct Done {
    Result result;
  };
  struct Dropped {};  // its unit failed, or its answer is being handled
  using SlotState =
      std::variant<Idle, InFlight, Waiting, Parsing, Done, Dropped>;

  struct Slot {
    Target target;
    Origin origin;
    std::uint8_t attempts = 0;
    bool service_retried = false;
    SlotState state = Idle{};
    std::vector<io::Warning> warnings{};
  };

  // What became of a unit.
  struct Running {};
  struct Settled {
    bool has_data;
  };
  struct Failed {
    Error error;
  };
  using Status = std::variant<Running, Settled, Failed>;

  // One unit. `path` is its place in plan order: the initial unit i is {i},
  // the k-th successor of p is p + {k}, compared lexicographically
  // (pre-order).
  struct Node {
    Unit unit;
    UnitLabel label;
    std::vector<std::uint32_t> path;
    std::vector<Slot> slots;
    Status status = Running{};
    std::vector<Warning> warnings{};
    std::optional<Part> part{};
  };

  struct SlotRef {
    std::size_t node;
    std::size_t slot;
  };

  struct Context {
    core::Time now;
    Jitter jitter;
    Steps& out;
  };

  Machine(typename S::Request request, Options options)
      : request_{std::move(request)},
        options_{options},
        cool_until_{options.cooling},
        next_id_{detail::IdAccess::next(options.ids)} {}

  friend struct Started<S>;

  /// start(): the units, their first sends, the deadline timer.
  void begin(core::Time now, Steps& out) {
    if (not detail::within_clock(now)) {
      finish_failure(out, ClockOutOfRange{.time = now}, std::nullopt);
      return;
    }
    if (options_.deadline and not detail::within_clock(*options_.deadline)) {
      finish_failure(out, ClockOutOfRange{.time = *options_.deadline},
                     std::nullopt);
      return;
    }
    latest_now_ = now;
    if (options_.deadline and now >= *options_.deadline) {
      finish_failure(out, DeadlineExceeded{}, std::nullopt);
      return;
    }
    auto units = S::initial(request_, now);
    if (not units) {
      finish_failure(out, std::move(units).error(), std::nullopt);
      return;
    }
    if (units->size() > options_.max_units) {
      finish_failure(out, UnitLimit{.max = options_.max_units}, std::nullopt);
      return;
    }
    const Context cx{.now = now, .jitter = Jitter::from_bits(0), .out = out};
    if (options_.deadline) {
      arm_deadline(cx);
    }
    for (std::uint32_t i = 0; Unit& u : *units) {
      add_node(std::move(u), {i++});
    }
    report_progress(cx);
    launch_range(0, nodes_.size(), cx);
    maybe_finish(cx);  // no units at all: NoData
  }

  [[nodiscard]] Slot& slot(SlotRef ref) {
    return nodes_[ref.node].slots[ref.slot];
  }

  [[nodiscard]] std::uint32_t fresh() { return next_id_++; }

  [[nodiscard]] std::optional<core::Time>& cool_until(Origin origin) {
    return cool_until_[static_cast<std::size_t>(std::to_underlying(origin))];
  }

  // ---- events ---------------------------------------------------------------

  void handle(event::Completed c, const Context& cx) {
    const auto it = in_flight_.find(c.id);
    if (it == in_flight_.end()) {
      return;
    }
    const SlotRef ref = it->second;
    in_flight_.erase(it);
    slot(ref).state = Dropped{};  // answered: no longer abortable
    std::visit(core::Overloaded{
                   [&](const Response& r) { on_response(ref, c.id, r, cx); },
                   [&](TransportErrc t) { on_transport(ref, t, cx); }},
               c.outcome);
  }

  void handle(event::WaitElapsed w, const Context& cx) {
    if (deadline_timer_ == w.id) {
      // Early: the deadline itself is checked before every event.
      arm_deadline(cx);
      return;
    }
    const auto it = waiting_.find(w.id);
    if (it == waiting_.end()) {
      return;
    }
    const SlotRef ref = it->second;
    waiting_.erase(it);
    const auto* waiting = std::get_if<Waiting>(&slot(ref).state);
    if (waiting != nullptr and cx.now < waiting->not_before) {
      start_timer(ref, waiting->not_before - cx.now, cx);  // fired early
      return;
    }
    send(ref, cx);
  }

  void handle(event::Parsed<Parsed> p, const Context& cx) {
    const auto it = parsing_.find(p.id);
    if (it == parsing_.end()) {
      return;
    }
    const SlotRef ref = it->second;
    parsing_.erase(it);
    slot(ref).state = Dropped{};
    if (p.parsed) {
      slot(ref).warnings = std::move(p.parsed->warnings);
      slot(ref).state = Done{std::move(p.parsed->value)};
      settle(ref.node, cx);
      return;
    }
    on_parse_error(ref, std::move(p.parsed).error(), cx);
  }

  void handle(event::Merged<Value> m, const Context& cx) {
    if (phase_ != Phase::merging) {
      return;
    }
    if (not m.value) {
      finish_failure(cx.out, std::move(m.value).error(), std::nullopt);
      return;
    }
    Trail trail{.warnings = plan_order_warnings(), .stats = stats_};
    for (io::Warning& w : m.value->warnings) {
      trail.warnings.emplace_back(std::move(w));
    }
    phase_ = Phase::finished;
    cx.out.emplace_back(Finish<Value>{Fetched<Value>{
        .value = std::move(m.value->value), .trail = std::move(trail)}});
  }

  void handle(event::Cancel /*cancel*/, const Context& cx) {
    finish_failure(cx.out, core::Cancelled{}, std::nullopt);
  }

  // ---- outcomes -------------------------------------------------------------

  void on_response(SlotRef ref, ExchangeId id, const Response& r,
                   const Context& cx) {
    stats_.bytes += r.bytes;
    const Origin origin = slot(ref).origin;
    switch (classify(origin, r.status)) {
      case Verdict::accept:
        if (r.rate_remaining and *r.rate_remaining <= 0) {
          cool_down(ref,
                    r.retry_after ? detail::bounded_delay(*r.retry_after)
                                  : options_.retry.base(),
                    cx);
        }
        slot(ref).state = Parsing{id};
        parsing_.emplace(id, ref);
        cx.out.emplace_back(
            Parse<Target>{.id = id, .target = slot(ref).target});
        return;
      case Verdict::absent:
        slot(ref).state = Done{Absent{}};
        settle(ref.node, cx);
        return;
      case Verdict::retry:
        retry_or_fail(ref, HttpError{.status = r.status, .origin = origin},
                      r.retry_after, cx);
        return;
      case Verdict::cool_down:
        on_rate_limited(ref, r.retry_after, cx);
        return;
      case Verdict::fail:
        break;
    }
    fail_unit(ref.node, HttpError{.status = r.status, .origin = origin}, cx);
  }

  void on_transport(SlotRef ref, TransportErrc code, const Context& cx) {
    const TransportError error{.code = code, .origin = slot(ref).origin};
    if (classify(code) == Verdict::retry) {
      retry_or_fail(ref, error, std::nullopt, cx);
      return;
    }
    fail_unit(ref.node, error, cx);
  }

  /// A 429: the origin always cools down for the server's wait (or a
  /// backoff), then the exchange is retried when it may be.
  void on_rate_limited(SlotRef ref,
                       std::optional<std::chrono::seconds> server_wait,
                       const Context& cx) {
    const auto delay = server_wait ? detail::bounded_delay(*server_wait)
                                   : backoff(options_.retry, slot(ref).attempts,
                                             std::nullopt, cx.jitter);
    const auto held = cool_down(ref, delay, cx);
    if (not may_retry(ref, server_wait)) {
      fail_unit(
          ref.node,
          RateLimited{.origin = slot(ref).origin, .retry_after = server_wait},
          cx);
      return;
    }
    schedule_retry(ref, held, cx);
  }

  void on_parse_error(SlotRef ref, Error error, const Context& cx) {
    const auto* service = std::get_if<ServiceError>(&error);
    const bool once_more = service != nullptr and
                           service->code() == ServiceErrc::station_or_system and
                           not slot(ref).service_retried;
    if (once_more and may_retry(ref, std::nullopt)) {
      slot(ref).service_retried = true;
      schedule_retry(
          ref,
          backoff(options_.retry, slot(ref).attempts, std::nullopt, cx.jitter),
          cx);
      return;
    }
    fail_unit(ref.node, std::move(error), cx);
  }

  // ---- retries and sends ----------------------------------------------------

  [[nodiscard]] bool may_retry(
      SlotRef ref, std::optional<std::chrono::seconds> server_wait) {
    const bool too_long =
        server_wait and *server_wait > options_.retry.max_retry_after();
    return slot(ref).attempts < options_.retry.max_attempts() and not too_long;
  }

  void retry_or_fail(SlotRef ref, Error error,
                     std::optional<std::chrono::seconds> server_wait,
                     const Context& cx) {
    if (not may_retry(ref, server_wait)) {
      fail_unit(ref.node, std::move(error), cx);
      return;
    }
    schedule_retry(ref,
                   server_wait ? detail::bounded_delay(*server_wait)
                               : backoff(options_.retry, slot(ref).attempts,
                                         std::nullopt, cx.jitter),
                   cx);
  }

  void schedule_retry(SlotRef ref, std::chrono::milliseconds delay,
                      const Context& cx) {
    ++stats_.retries;
    start_timer(ref, delay, cx);
  }

  void start_timer(SlotRef ref, std::chrono::milliseconds delay,
                   const Context& cx) {
    const TimerId timer{fresh()};
    const auto wait = std::max(delay, std::chrono::milliseconds{0});
    slot(ref).state = Waiting{.id = timer, .not_before = cx.now + wait};
    waiting_.emplace(timer, ref);
    cx.out.emplace_back(Wait{.id = timer, .delay = wait});
  }

  /// Pauses the origin for at least min_cool_down; returns the pause.
  std::chrono::milliseconds cool_down(SlotRef ref,
                                      std::chrono::milliseconds delay,
                                      const Context& cx) {
    const Origin origin = slot(ref).origin;
    const auto pause = std::max(delay, min_cool_down);
    std::optional<core::Time>& until = cool_until(origin);
    until = std::max(until.value_or(cx.now), cx.now + pause);
    nodes_[ref.node].warnings.emplace_back(
        FetchWarning{CooledDown{.origin = origin, .for_ = pause}});
    cx.out.emplace_back(CoolDown{.origin = origin, .for_ = pause});
    return pause;
  }

  /// Sends the slot's next attempt, or waits out its origin's cool-down;
  /// a cool-down longer than max_retry_after fails the unit instead.
  void send(SlotRef ref, const Context& cx) {
    Slot& s = slot(ref);
    if (const auto until = cool_until(s.origin); until and *until > cx.now) {
      const auto remaining = *until - cx.now;
      if (remaining > options_.retry.max_retry_after()) {
        fail_unit(
            ref.node,
            RateLimited{.origin = s.origin,
                        .retry_after =
                            std::chrono::ceil<std::chrono::seconds>(remaining)},
            cx);
        return;
      }
      start_timer(ref, remaining, cx);
      return;
    }
    const ExchangeId id{fresh()};
    ++s.attempts;
    ++stats_.exchanges;
    s.state = InFlight{id};
    in_flight_.emplace(id, ref);
    cx.out.emplace_back(Send<Target>{.id = id,
                                     .target = s.target,
                                     .origin = s.origin,
                                     .priority = options_.priority});
  }

  void arm_deadline(const Context& cx) {
    const TimerId timer{fresh()};
    deadline_timer_ = timer;
    cx.out.emplace_back(Wait{.id = timer,
                             .delay = std::max(*options_.deadline - cx.now,
                                               std::chrono::milliseconds{0})});
  }

  // ---- units ----------------------------------------------------------------

  /// Appends a unit's node; launch() starts it.
  void add_node(Unit unit, std::vector<std::uint32_t> path) {
    std::vector<Slot> slots;
    for (Target& t : S::targets(request_, unit)) {
      const Origin origin = S::origin(t);
      slots.push_back(Slot{.target = std::move(t), .origin = origin});
    }
    UnitLabel label = S::label(request_, unit);
    nodes_.push_back(Node{.unit = std::move(unit),
                          .label = std::move(label),
                          .path = std::move(path),
                          .slots = std::move(slots)});
  }

  /// Launches nodes [first, end): a bound taken before any of them runs.
  void launch_range(std::size_t first, std::size_t end, const Context& cx) {
    for (std::size_t i = first; i < end; ++i) {
      launch(i, cx);
    }
  }

  void launch(std::size_t node, const Context& cx) {
    if (phase_ != Phase::running) {
      return;  // an earlier unit ended the fetch
    }
    const std::size_t count = nodes_[node].slots.size();
    if (count == 0) {
      fail_unit(node, UnitWithoutExchanges{}, cx);  // the spec's contract
      return;
    }
    for (std::size_t i = 0; i < count; ++i) {
      send(SlotRef{.node = node, .slot = i}, cx);
      if (not std::holds_alternative<Running>(nodes_[node].status)) {
        return;  // a send failed the unit (a cool-down too long)
      }
    }
  }

  [[nodiscard]] static bool pending(const Node& n) {
    return std::ranges::any_of(n.slots, [](const Slot& s) {
      return not std::holds_alternative<Done>(s.state);
    });
  }

  /// When every exchange of the unit came back: its outcome, its
  /// successors, its part.
  void settle(std::size_t index, const Context& cx) {
    if (pending(nodes_[index])) {
      return;
    }
    std::vector<Result> results = take_results(index);
    const bool absent = std::ranges::all_of(results, [](const Result& r) {
      return std::holds_alternative<Absent>(r);
    });
    const UnitOutcome<Parsed> outcome =
        absent ? UnitOutcome<Parsed>{Absent{}}
               : UnitOutcome<Parsed>{Received<Parsed>{.slots = results}};
    auto successors = S::next(request_, nodes_[index].unit, outcome);
    if (not successors and options_.strictness == Strictness::strict) {
      fail_unit(index, std::move(successors).error(), cx);
      return;
    }
    keep(index, std::move(results), absent);
    if (not successors) {
      // Lenient: the unit's data stays; the rest of its chain is missing.
      nodes_[index].warnings.emplace_back(
          FetchWarning{ChainStopped{.unit = nodes_[index].label,
                                    .error = std::move(successors).error()}});
    } else if (not add_successors(index, std::move(*successors), absent, cx)) {
      return;
    }
    if (phase_ != Phase::running) {
      return;
    }
    report_progress(cx);
    maybe_finish(cx);
  }

  [[nodiscard]] std::vector<Result> take_results(std::size_t index) {
    std::vector<Result> results;
    for (Slot& s : nodes_[index].slots) {
      results.push_back(std::move(std::get<Done>(s.state).result));
    }
    return results;
  }

  /// The slots' io warnings and Retried, in slot order.
  void keep_warnings(Node& n) {
    for (Slot& s : n.slots) {
      for (io::Warning& w : s.warnings) {
        n.warnings.emplace_back(std::move(w));
      }
      s.warnings.clear();
      if (s.attempts > 1 and std::holds_alternative<Done>(s.state)) {
        n.warnings.emplace_back(
            FetchWarning{Retried{.unit = n.label, .attempts = s.attempts}});
      }
    }
  }

  /// The unit's warnings and part.
  void keep(std::size_t index, std::vector<Result> results, bool absent) {
    Node& n = nodes_[index];
    keep_warnings(n);
    if (absent) {
      n.status = Settled{.has_data = false};
      return;
    }
    for (std::uint32_t i = 0; const Result& r : results) {
      if (std::holds_alternative<Absent>(r)) {
        n.warnings.emplace_back(
            FetchWarning{PartlyAbsent{.unit = n.label, .slot = i}});
      }
      ++i;
    }
    Part part = S::into_part(std::move(results));
    n.status = Settled{.has_data = S::has_data(part)};
    n.part = std::move(part);
  }

  /// Adds and launches a unit's successors; false when they would pass
  /// max_units (the fetch then fails).
  [[nodiscard]] bool add_successors(std::size_t index,
                                    std::vector<Unit> successors, bool absent,
                                    const Context& cx) {
    if (nodes_.size() + successors.size() > options_.max_units) {
      finish_failure(cx.out, UnitLimit{.max = options_.max_units},
                     nodes_[index].label);
      return false;
    }
    const std::size_t first = nodes_.size();
    for (std::uint32_t k = 0; Unit& u : successors) {
      std::vector<std::uint32_t> path = nodes_[index].path;
      path.push_back(k++);
      add_node(std::move(u), std::move(path));
      if (absent) {
        note_fallback(index, nodes_.back().label);
      }
    }
    launch_range(first, nodes_.size(), cx);
    return true;
  }

  /// FellBack when an absent NDBC file is followed by another file.
  void note_fallback(std::size_t index, const UnitLabel& to) {
    const auto* from = std::get_if<NdbcFile>(&nodes_[index].label);
    const auto* next = std::get_if<NdbcFile>(&to);
    if (from != nullptr and next != nullptr and *from != *next) {
      nodes_[index].warnings.emplace_back(
          FetchWarning{FellBack{.from = *from, .to = *next}});
    }
  }

  /// A unit failed: strict ends the fetch; lenient leaves a gap, keeping
  /// the unit's warnings.
  void fail_unit(std::size_t index, Error error, const Context& cx) {
    if (options_.strictness == Strictness::strict) {
      finish_failure(cx.out, std::move(error), nodes_[index].label);
      return;
    }
    Node& n = nodes_[index];
    keep_warnings(n);
    drop_slots(index, cx);
    n.warnings.emplace_back(
        FetchWarning{UnitFailed{.unit = n.label, .error = error}});
    n.status = Failed{.error = std::move(error)};
    report_progress(cx);
    maybe_finish(cx);
  }

  /// Forgets a failed unit's other exchanges, aborting those in flight and
  /// stopping its parses.
  void drop_slots(std::size_t index, const Context& cx) {
    Abort abort;
    for (Slot& s : nodes_[index].slots) {
      std::visit(
          core::Overloaded{[&](const InFlight& f) {
                             in_flight_.erase(f.id);
                             abort.exchanges.push_back(f.id);
                           },
                           [&](const Waiting& w) { waiting_.erase(w.id); },
                           [&](const Parsing& p) {
                             parsing_.erase(p.id);
                             abort.parses.push_back(p.id);
                           },
                           [](const auto& /*settled*/) {}},
          s.state);
      s.state = Dropped{};
    }
    if (not abort.exchanges.empty() or not abort.parses.empty()) {
      cx.out.emplace_back(std::move(abort));
    }
  }

  [[nodiscard]] std::uint32_t done() const {
    return static_cast<std::uint32_t>(
        std::ranges::count_if(nodes_, [](const Node& n) {
          return not std::holds_alternative<Running>(n.status);
        }));
  }

  void report_progress(const Context& cx) const {
    cx.out.emplace_back(Progress{
        .done = done(), .total = static_cast<std::uint32_t>(nodes_.size())});
  }

  // ---- the end --------------------------------------------------------------

  /// Node indices in plan order.
  [[nodiscard]] std::vector<std::size_t> plan_order() const {
    std::vector<std::size_t> order(nodes_.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
      order[i] = i;
    }
    std::ranges::sort(order, {}, [&](std::size_t i) -> const auto& {
      return nodes_[i].path;
    });
    return order;
  }

  [[nodiscard]] std::vector<Warning> plan_order_warnings() {
    std::vector<Warning> warnings;
    for (const std::size_t i : plan_order()) {
      for (Warning& w : nodes_[i].warnings) {
        warnings.push_back(std::move(w));
      }
    }
    return warnings;
  }

  [[nodiscard]] static bool has_data(const Node& n) {
    const auto* settled = std::get_if<Settled>(&n.status);
    return settled != nullptr and settled->has_data;
  }

  /// When every unit is done: Merge, or a failure without a merge (§5.6).
  void maybe_finish(const Context& cx) {
    if (phase_ != Phase::running or done() < nodes_.size()) {
      return;
    }
    const std::vector<std::size_t> order = plan_order();
    if (std::ranges::none_of(
            order, [&](std::size_t i) { return has_data(nodes_[i]); })) {
      const auto failed = std::ranges::find_if(order, [&](std::size_t i) {
        return std::holds_alternative<Failed>(nodes_[i].status);
      });
      if (failed == order.end()) {
        finish_failure(cx.out, NoData{}, std::nullopt);
      } else {
        finish_failure(cx.out, std::get<Failed>(nodes_[*failed].status).error,
                       nodes_[*failed].label);
      }
      return;
    }
    Merge<Part> merge;
    for (const std::size_t i : order) {
      if (nodes_[i].part) {
        merge.in_plan_order.push_back(*std::move(nodes_[i].part));
        nodes_[i].part.reset();
      }
    }
    phase_ = Phase::merging;
    cx.out.emplace_back(std::move(merge));
  }

  void finish_failure(Steps& out, Error error, std::optional<UnitLabel> unit) {
    Abort abort;
    for (const auto& entry : in_flight_) {
      abort.exchanges.push_back(entry.first);
    }
    for (const auto& entry : parsing_) {
      abort.parses.push_back(entry.first);
    }
    if (not abort.exchanges.empty() or not abort.parses.empty()) {
      out.emplace_back(std::move(abort));
    }
    in_flight_.clear();
    waiting_.clear();
    parsing_.clear();
    deadline_timer_.reset();
    phase_ = Phase::finished;
    out.emplace_back(Finish<Value>{std::unexpected{
        Failure{.error = std::move(error), .unit = std::move(unit)}}});
  }

  typename S::Request request_;
  Options options_;
  std::vector<Node> nodes_;
  std::map<ExchangeId, SlotRef> in_flight_;
  std::map<ExchangeId, SlotRef> parsing_;
  std::map<TimerId, SlotRef> waiting_;
  std::optional<TimerId> deadline_timer_;
  std::array<std::optional<core::Time>, every_origin.size()> cool_until_{};
  std::uint32_t next_id_;
  core::Time latest_now_{};
  Stats stats_{};
  Phase phase_ = Phase::running;
};

/// A machine and the steps of its start.
template <FetchSpec S>
struct Started {
  Machine<S> machine;
  std::vector<Step<S>> steps;

  /// start's body: it needs the machine's private constructor.
  [[nodiscard]] static Started make(typename S::Request request,
                                    Options options, core::Time now) {
    Started s{.machine = Machine<S>{std::move(request), options}, .steps = {}};
    s.machine.begin(now, s.steps);
    return s;
  }
};

template <FetchSpec S>
Started<S> Machine<S>::start(typename S::Request request, Options options,
                             core::Time now) {
  return Started<S>::make(std::move(request), options, now);
}

}  // namespace mov::fetch
