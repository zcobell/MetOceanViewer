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
#include <map>
#include <optional>
#include <ranges>
#include <span>
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

/// How a machine runs. A later stage passes the earlier machine's next_id()
/// as `first_id`, so no id is reused within a job.
struct Options {
  Strictness strictness = Strictness::strict;
  RetryPolicy retry = RetryPolicy::defaults();
  /// Past it the machine aborts everything and finishes DeadlineExceeded.
  std::optional<core::Time> deadline = std::nullopt;
  Priority priority = Priority::interactive;
  std::uint32_t first_id = 0;
};

template <FetchSpec S>
struct Started;

/// One stage of a fetch. start() plans the units and returns the first
/// steps; on() takes one event and returns the steps it causes.
///
/// Laws (tested over generated scripts and fuzzed, §10.1):
/// - total: an event naming an unknown, aborted or finished id, a Merged
///   before the Merge step, and any event after Finish return no steps
///   (unless the deadline has passed, which finishes the fetch);
/// - each Send's id later appears in exactly one accepted Completed or in
///   an Abort; a slot is sent at most max_attempts times;
/// - no Send to an origin before its cool-down ends;
/// - exactly one Finish, after which nothing; after Cancel or past the
///   deadline, only an Abort of the exchanges in flight comes before it;
/// - a successful Finish holds the merge of exactly the accepted parts in
///   plan order (initial order; a unit's successors right after it);
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
    Steps out;
    if (phase_ == Phase::finished) {
      return out;
    }
    if (options_.deadline and now >= *options_.deadline) {
      finish_failure(out, DeadlineExceeded{}, std::nullopt);
      return out;
    }
    const Context cx{.now = now, .jitter = jitter, .out = out};
    std::visit([&](auto&& ev) { handle(std::forward<decltype(ev)>(ev), cx); },
               std::move(e));
    return out;
  }

  [[nodiscard]] bool finished() const noexcept {
    return phase_ == Phase::finished;
  }

  /// The first id a following stage may use.
  [[nodiscard]] std::uint32_t next_id() const noexcept { return next_id_; }

 private:
  using Unit = typename S::Unit;
  using Target = typename S::Target;
  using Parsed = typename S::Parsed;
  using Part = typename S::Part;
  using Value = typename S::Value;

  enum class Phase : std::uint8_t { running, merging, finished };

  // One exchange slot of a unit: a target and where its current attempt is.
  struct InFlight {
    ExchangeId id;
  };
  struct Waiting {
    TimerId id;
  };
  struct Parsing {
    ExchangeId id;
  };
  struct Done {
    Parsed parsed;
  };
  struct Gone {};     // absent, or parsed to NoData
  struct Dropped {};  // its unit failed
  using SlotState =
      std::variant<InFlight, Waiting, Parsing, Done, Gone, Dropped>;

  struct Slot {
    Target target;
    Origin origin;
    std::uint8_t attempts = 0;
    bool service_retried = false;
    SlotState state = Dropped{};
    std::vector<io::Warning> warnings{};
  };

  // One unit and what became of it. `path` is its place in plan order: the
  // initial unit i is {i}, the k-th successor of p is p + {k}, compared
  // lexicographically (pre-order).
  struct Node {
    Unit unit;
    UnitLabel label;
    std::vector<std::uint32_t> path;
    std::vector<Slot> slots;
    std::vector<Warning> warnings{};
    std::vector<Part> parts{};
    bool has_data = false;
    std::optional<Error> failure{};
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
        next_id_{options.first_id} {}

  friend struct Started<S>;

  /// start(): the units, their first sends, the deadline timer.
  void begin(core::Time now, Steps& out) {
    if (options_.deadline and now >= *options_.deadline) {
      finish_failure(out, DeadlineExceeded{}, std::nullopt);
      return;
    }
    auto units = S::initial(request_, now);
    if (not units) {
      finish_failure(out, std::move(units).error(), std::nullopt);
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
    const std::size_t initial = nodes_.size();
    for (std::size_t i = 0; i < initial; ++i) {
      launch(i, cx);
    }
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
    std::vector<Warning> warnings = plan_order_warnings();
    for (io::Warning& w : m.value->warnings) {
      warnings.emplace_back(std::move(w));
    }
    phase_ = Phase::finished;
    cx.out.emplace_back(
        Finish<Value>{Fetched<Value>{.value = std::move(m.value->value),
                                     .warnings = std::move(warnings),
                                     .stats = stats_}});
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
        if (r.rate_remaining == 0) {
          cool_down(ref,
                    backoff(options_.retry, slot(ref).attempts, std::nullopt,
                            cx.jitter),
                    cx);
        }
        slot(ref).state = Parsing{id};
        parsing_.emplace(id, ref);
        cx.out.emplace_back(
            Parse<Target>{.id = id, .target = slot(ref).target});
        return;
      case Verdict::absent:
        slot(ref).state = Gone{};
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

  void on_rate_limited(SlotRef ref,
                       std::optional<std::chrono::seconds> server_wait,
                       const Context& cx) {
    const RateLimited error{.origin = slot(ref).origin,
                            .retry_after = server_wait};
    if (not may_retry(ref, server_wait)) {
      fail_unit(ref.node, error, cx);
      return;
    }
    const auto delay =
        backoff(options_.retry, slot(ref).attempts, server_wait, cx.jitter);
    cool_down(ref, delay, cx);
    schedule_retry(ref, delay, cx);
  }

  void on_parse_error(SlotRef ref, Error error, const Context& cx) {
    if (std::holds_alternative<NoData>(error)) {
      slot(ref).state = Gone{};
      settle(ref.node, cx);
      return;
    }
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
    schedule_retry(
        ref,
        backoff(options_.retry, slot(ref).attempts, server_wait, cx.jitter),
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
    slot(ref).state = Waiting{timer};
    waiting_.emplace(timer, ref);
    cx.out.emplace_back(Wait{.id = timer, .delay = delay});
  }

  void cool_down(SlotRef ref, std::chrono::milliseconds delay,
                 const Context& cx) {
    const Origin origin = slot(ref).origin;
    std::optional<core::Time>& until = cool_until(origin);
    until = std::max(until.value_or(cx.now), cx.now + delay);
    nodes_[ref.node].warnings.emplace_back(
        FetchWarning{CooledDown{.origin = origin, .for_ = delay}});
    cx.out.emplace_back(CoolDown{.origin = origin, .for_ = delay});
  }

  /// Sends the slot's next attempt, or waits out its origin's cool-down.
  void send(SlotRef ref, const Context& cx) {
    Slot& s = slot(ref);
    if (const auto until = cool_until(s.origin); until and *until > cx.now) {
      start_timer(ref, *until - cx.now, cx);
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
    cx.out.emplace_back(
        Wait{.id = timer, .delay = *options_.deadline - cx.now});
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

  void launch(std::size_t node, const Context& cx) {
    if (phase_ != Phase::running) {
      return;  // an earlier unit ended the fetch
    }
    const std::size_t count = nodes_[node].slots.size();
    for (std::size_t i = 0; i < count; ++i) {
      send(SlotRef{.node = node, .slot = i}, cx);
    }
    if (count == 0) {
      settle(node, cx);
    }
  }

  [[nodiscard]] static bool pending(const Node& n) {
    return std::ranges::any_of(n.slots, [](const Slot& s) {
      return std::holds_alternative<InFlight>(s.state) or
             std::holds_alternative<Waiting>(s.state) or
             std::holds_alternative<Parsing>(s.state);
    });
  }

  /// When every exchange of the unit came back: its outcome, its
  /// successors, its parts.
  void settle(std::size_t index, const Context& cx) {
    if (pending(nodes_[index])) {
      return;
    }
    const bool absent = std::ranges::any_of(
        nodes_[index].slots,
        [](const Slot& s) { return std::holds_alternative<Gone>(s.state); });
    std::vector<Parsed> parsed =
        absent ? std::vector<Parsed>{} : take_parsed(index);
    const UnitOutcome<Parsed> outcome =
        absent ? UnitOutcome<Parsed>{Absent{}}
               : UnitOutcome<Parsed>{Received<Parsed>{.parsed = parsed}};
    auto successors = S::next(request_, nodes_[index].unit, outcome);
    if (not successors) {
      fail_unit(index, std::move(successors).error(), cx);
      return;
    }
    keep(index, std::move(parsed));
    add_successors(index, std::move(*successors), absent, cx);
    if (phase_ != Phase::running) {
      return;  // a successor without exchanges already ended the stage
    }
    report_progress(cx);
    maybe_finish(cx);
  }

  [[nodiscard]] std::vector<Parsed> take_parsed(std::size_t index) {
    std::vector<Parsed> parsed;
    for (Slot& s : nodes_[index].slots) {
      if (auto* done = std::get_if<Done>(&s.state)) {
        parsed.push_back(std::move(done->parsed));
      }
    }
    return parsed;
  }

  /// The unit's warnings and parts, in slot order.
  void keep(std::size_t index, std::vector<Parsed> parsed) {
    Node& n = nodes_[index];
    for (Slot& s : n.slots) {
      for (io::Warning& w : s.warnings) {
        n.warnings.emplace_back(std::move(w));
      }
      if (s.attempts > 1) {
        n.warnings.emplace_back(
            FetchWarning{Retried{.unit = n.label, .attempts = s.attempts}});
      }
    }
    for (Parsed& p : parsed) {
      Part part = S::into_part(std::move(p));
      n.has_data = n.has_data or S::has_data(part);
      n.parts.push_back(std::move(part));
    }
    ++done_;
  }

  void add_successors(std::size_t index, std::vector<Unit> successors,
                      bool absent, const Context& cx) {
    const std::size_t first = nodes_.size();
    for (std::uint32_t k = 0; Unit& u : successors) {
      std::vector<std::uint32_t> path = nodes_[index].path;
      path.push_back(k++);
      add_node(std::move(u), std::move(path));
      if (absent) {
        note_fallback(index, nodes_.back().label);
      }
    }
    for (std::size_t i = first; i < nodes_.size(); ++i) {
      launch(i, cx);
    }
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

  /// A unit failed: strict ends the fetch; lenient leaves a gap.
  void fail_unit(std::size_t index, Error error, const Context& cx) {
    if (options_.strictness == Strictness::strict) {
      finish_failure(cx.out, std::move(error), nodes_[index].label);
      return;
    }
    drop_slots(index, cx);
    Node& n = nodes_[index];
    n.warnings.emplace_back(
        FetchWarning{UnitFailed{.unit = n.label, .error = error}});
    n.failure = std::move(error);
    ++done_;
    report_progress(cx);
    maybe_finish(cx);
  }

  /// Forgets a failed unit's other exchanges, aborting those in flight.
  void drop_slots(std::size_t index, const Context& cx) {
    Abort abort;
    for (Slot& s : nodes_[index].slots) {
      std::visit(
          core::Overloaded{[&](const InFlight& f) {
                             in_flight_.erase(f.id);
                             abort.ids.push_back(f.id);
                           },
                           [&](const Waiting& w) { waiting_.erase(w.id); },
                           [&](const Parsing& p) { parsing_.erase(p.id); },
                           [](const auto& /*settled*/) {}},
          s.state);
      s.state = Dropped{};
    }
    if (not abort.ids.empty()) {
      cx.out.emplace_back(std::move(abort));
    }
  }

  void report_progress(const Context& cx) const {
    cx.out.emplace_back(Progress{
        .done = done_, .total = static_cast<std::uint32_t>(nodes_.size())});
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

  /// When every unit is done: Merge, or a failure without a merge (§5.6).
  void maybe_finish(const Context& cx) {
    if (phase_ != Phase::running or done_ < nodes_.size()) {
      return;
    }
    const std::vector<std::size_t> order = plan_order();
    const bool any_data = std::ranges::any_of(
        order, [&](std::size_t i) { return nodes_[i].has_data; });
    if (not any_data) {
      const auto failed = std::ranges::find_if(
          order, [&](std::size_t i) { return nodes_[i].failure.has_value(); });
      if (failed == order.end()) {
        finish_failure(cx.out, NoData{}, std::nullopt);
      } else {
        finish_failure(cx.out, *nodes_[*failed].failure, nodes_[*failed].label);
      }
      return;
    }
    Merge<Part> merge;
    for (const std::size_t i : order) {
      for (Part& p : nodes_[i].parts) {
        merge.in_plan_order.push_back(std::move(p));
      }
    }
    phase_ = Phase::merging;
    cx.out.emplace_back(std::move(merge));
  }

  void finish_failure(Steps& out, Error error, std::optional<UnitLabel> unit) {
    Abort abort;
    for (const auto& entry : in_flight_) {
      abort.ids.push_back(entry.first);
    }
    if (not abort.ids.empty()) {
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
  std::uint32_t done_ = 0;
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
