// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// Two stages composed (docs/providers-design.md §5.2, §6.3): stage 1's
// value, bound into stage 2's request, as one machine with the same
// event/step protocol. USGS resolves the series, then fetches it; a CO-OPS
// catalog reads the station lists, then each station's datums.

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <expected>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/overloaded.hpp"
#include "mov/core/time.hpp"
#include "mov/fetch/error.hpp"
#include "mov/fetch/machine.hpp"
#include "mov/fetch/steps.hpp"

namespace mov::fetch {

/// How stage 1's value becomes stage 2's request; a failure (a resolution
/// that found no series) ends the fetch.
template <class B, class S1, class S2>
concept StageBind =
    requires(const typename S1::Request& request, typename S1::Value&& value) {
      {
        B::bind(request, std::move(value))
      } -> std::same_as<std::expected<typename S2::Request, Failure>>;
    };

template <FetchSpec S1, class Bind, FetchSpec S2>
  requires StageBind<Bind, S1, S2>
class Then;

/// A composed machine and the steps of its start.
template <FetchSpec S1, class Bind, FetchSpec S2>
  requires StageBind<Bind, S1, S2>
struct ThenStarted;

/// Stage 1, then stage 2, as one machine. It owns what crosses the
/// boundary:
/// - stage 1's Finish never reaches the job: a failure becomes the fetch's,
///   a value is bound and stage 2 starts in the same call;
/// - a Cancel latches: once seen, stage 2 never starts;
/// - stage 2 continues stage 1's ids (next_ids()) and keeps its absolute
///   deadline, strictness, retry policy, priority and unit cap;
/// - progress is offset: stage 2 reports done and total after stage 1's
///   total;
/// - the result's trail is stage 1's + stage 2's.
/// Events of the stage that is over are ignored (their ids are unknown).
template <FetchSpec S1, class Bind, FetchSpec S2>
  requires StageBind<Bind, S1, S2>
class Then {
 public:
  using Request = typename S1::Request;
  using Value = typename S2::Value;
  using Event = std::variant<
      event::Completed, event::WaitElapsed, event::Parsed<typename S1::Parsed>,
      event::Parsed<typename S2::Parsed>, event::Merged<typename S1::Value>,
      event::Merged<typename S2::Value>, event::Cancel>;
  using Step =
      std::variant<Send<typename S1::Target>, Send<typename S2::Target>, Wait,
                   CoolDown, Parse<typename S1::Target>,
                   Parse<typename S2::Target>, Merge<typename S1::Part>,
                   Merge<typename S2::Part>, Abort, Progress, Finish<Value>>;
  using Steps = std::vector<Step>;
  static_assert(detail::unique_alternatives<Event> and
                    detail::unique_alternatives<Step>,
                "the stages' targets, parsed pages, parts and values must be "
                "distinct types, so an event or step names its stage");

  [[nodiscard]] static ThenStarted<S1, Bind, S2> start(Request request,
                                                       Options options,
                                                       core::Time now);

  [[nodiscard]] Steps on(Event e, core::Time now, Jitter jitter) {
    Steps out;
    if (finished_) {
      return out;
    }
    std::visit(core::Overloaded{[&](event::Cancel c) {
                                  cancelled_ = true;
                                  route_common(c, now, jitter, out);
                                },
                                [&](event::Parsed<typename S1::Parsed> p) {
                                  to_first(std::move(p), now, jitter, out);
                                },
                                [&](event::Merged<typename S1::Value> m) {
                                  to_first(std::move(m), now, jitter, out);
                                },
                                [&](event::Parsed<typename S2::Parsed> p) {
                                  to_second(std::move(p), now, jitter, out);
                                },
                                [&](event::Merged<typename S2::Value> m) {
                                  to_second(std::move(m), now, jitter, out);
                                },
                                [&](auto common) {
                                  route_common(std::move(common), now, jitter,
                                               out);
                                }},
               std::move(e));
    return out;
  }

  [[nodiscard]] bool finished() const noexcept { return finished_; }

 private:
  friend struct ThenStarted<S1, Bind, S2>;

  Then(Request request, Options options, Machine<S1> first)
      : request_{std::move(request)},
        options_{options},
        first_{std::move(first)} {}

  /// Completed, WaitElapsed and Cancel go to the running stage.
  template <class E>
  void route_common(E e, core::Time now, Jitter jitter, Steps& out) {
    if (second_) {
      forward_second(second_->on(std::move(e), now, jitter), out);
    } else {
      forward_first(first_.on(std::move(e), now, jitter), now, out);
    }
  }

  /// An event of the stage that is over or not yet started is ignored,
  /// but its time still counts (the deadline).
  void tick_running(core::Time now, Steps& out) {
    if (second_) {
      forward_second(second_->tick(now), out);
    } else {
      forward_first(first_.tick(now), now, out);
    }
  }

  template <class E>
  void to_first(E e, core::Time now, Jitter jitter, Steps& out) {
    if (second_) {
      tick_running(now, out);
      return;
    }
    forward_first(first_.on(std::move(e), now, jitter), now, out);
  }

  template <class E>
  void to_second(E e, core::Time now, Jitter jitter, Steps& out) {
    if (not second_) {
      tick_running(now, out);
      return;
    }
    forward_second(second_->on(std::move(e), now, jitter), out);
  }

  /// Stage 1's steps: all but Finish pass through.
  void forward_first(std::vector<fetch::Step<S1>> steps, core::Time now,
                     Steps& out) {
    for (auto& step : steps) {
      std::visit(core::Overloaded{
                     [&](Finish<typename S1::Value>& f) {
                       end_first(std::move(f), now, out);
                     },
                     [&](Progress& p) {
                       first_total_ = p.total;
                       out.emplace_back(p);
                     },
                     [&](auto& other) { out.emplace_back(std::move(other)); }},
                 step);
    }
  }

  /// Stage 2's steps, with progress after stage 1's and the trail joined.
  void forward_second(std::vector<fetch::Step<S2>> steps, Steps& out) {
    for (auto& step : steps) {
      std::visit(
          core::Overloaded{
              [&](Finish<Value>& f) {
                if (f.result) {
                  f.result->trail = first_trail_ + std::move(f.result->trail);
                }
                finished_ = true;
                out.emplace_back(std::move(f));
              },
              [&](Progress& p) {
                out.emplace_back(Progress{.done = first_total_ + p.done,
                                          .total = first_total_ + p.total});
              },
              [&](auto& other) { out.emplace_back(std::move(other)); }},
          step);
    }
  }

  /// Stage 1 finished: its failure is the fetch's; its value is bound and
  /// stage 2 starts, unless a Cancel came first.
  void end_first(Finish<typename S1::Value> f, core::Time now, Steps& out) {
    if (not f.result) {
      fail(std::move(f.result).error(), out);
      return;
    }
    if (cancelled_) {
      fail(Failure{.error = core::Cancelled{}, .unit = std::nullopt}, out);
      return;
    }
    first_trail_ = std::move(f.result->trail);
    auto request = Bind::bind(request_, std::move(f.result->value));
    if (not request) {
      fail(std::move(request).error(), out);
      return;
    }
    Options next = options_;
    next.ids = first_.next_ids();
    next.cooling = first_.cooling();
    // Stage 1's clock: a feed that went back in time does not rewind it.
    auto started = Machine<S2>::start(*std::move(request), next,
                                      std::max(now, first_.latest_now()));
    second_.emplace(std::move(started.machine));
    forward_second(std::move(started.steps), out);
  }

  void fail(Failure failure, Steps& out) {
    finished_ = true;
    out.emplace_back(Finish<Value>{std::unexpected{std::move(failure)}});
  }

  Request request_;
  Options options_;
  Machine<S1> first_;
  std::optional<Machine<S2>> second_;
  Trail first_trail_;
  std::uint32_t first_total_ = 0;
  bool cancelled_ = false;
  bool finished_ = false;
};

template <FetchSpec S1, class Bind, FetchSpec S2>
  requires StageBind<Bind, S1, S2>
struct ThenStarted {
  Then<S1, Bind, S2> machine;
  typename Then<S1, Bind, S2>::Steps steps;

  [[nodiscard]] static ThenStarted make(typename S1::Request request,
                                        Options options, core::Time now) {
    auto first = Machine<S1>::start(request, options, now);
    ThenStarted s{.machine = Then<S1, Bind, S2>{std::move(request), options,
                                                std::move(first.machine)},
                  .steps = {}};
    s.machine.forward_first(std::move(first.steps), now, s.steps);
    return s;
  }
};

template <FetchSpec S1, class Bind, FetchSpec S2>
  requires StageBind<Bind, S1, S2>
ThenStarted<S1, Bind, S2> Then<S1, Bind, S2>::start(Request request,
                                                    Options options,
                                                    core::Time now) {
  return ThenStarted<S1, Bind, S2>::make(std::move(request), options, now);
}

}  // namespace mov::fetch
