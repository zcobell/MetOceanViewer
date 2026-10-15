// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// A thin wrapper that drives a Machine by hand in the scripted tests: it
// keeps the virtual clock and the last steps, and picks steps out by type.

#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/time.hpp"
#include "mov/fetch/machine.hpp"

namespace mov::fetch::test {

/// 2026-10-01T00:00:00Z.
inline constexpr core::Time t0{
    std::chrono::sys_days{std::chrono::year{2026} / std::chrono::October / 1}};

template <FetchSpec S>
class Script {
 public:
  using Steps = std::vector<Step<S>>;

  Script(typename S::Request request, Options options, core::Time now = t0)
      : started_{Machine<S>::start(std::move(request), options, now)},
        now_{now},
        steps_{std::move(started_.steps)} {}

  [[nodiscard]] Machine<S>& machine() { return started_.machine; }
  [[nodiscard]] const Machine<S>& machine() const { return started_.machine; }
  [[nodiscard]] const Steps& steps() const { return steps_; }
  [[nodiscard]] core::Time now() const { return now_; }

  void advance(std::chrono::milliseconds by) { now_ += by; }
  void set_jitter(Jitter j) { jitter_ = j; }

  const Steps& on(Event<S> e) {
    steps_ = started_.machine.on(std::move(e), now_, jitter_);
    return steps_;
  }

  const Steps& complete(ExchangeId id, Outcome outcome) {
    return on(event::Completed{.id = id, .outcome = std::move(outcome)});
  }
  const Steps& respond(ExchangeId id, int status, std::uint64_t bytes = 0) {
    return complete(id, Response{.status = status, .bytes = bytes});
  }
  const Steps& elapse(TimerId id) { return on(event::WaitElapsed{.id = id}); }
  const Steps& parsed(ExchangeId id, typename S::Parsed value,
                      std::vector<io::Warning> warnings = {}) {
    return on(event::Parsed<typename S::Parsed>{
        .id = id,
        .parsed = io::Read<typename S::Parsed>{
            .value = std::move(value), .warnings = std::move(warnings)}});
  }
  const Steps& parse_failed(ExchangeId id, Error error) {
    return on(event::Parsed<typename S::Parsed>{
        .id = id, .parsed = std::unexpected{std::move(error)}});
  }
  const Steps& merged(typename S::Value value,
                      std::vector<io::Warning> warnings = {}) {
    return on(event::Merged<typename S::Value>{
        .value = io::Read<typename S::Value>{.value = std::move(value),
                                             .warnings = std::move(warnings)}});
  }
  const Steps& cancel() { return on(event::Cancel{}); }

  /// The steps of one type in the last batch, in order.
  template <class T>
  [[nodiscard]] std::vector<T> all() const {
    std::vector<T> out;
    for (const auto& s : steps_) {
      if (const T* t = std::get_if<T>(&s)) {
        out.push_back(*t);
      }
    }
    return out;
  }

  template <class T>
  [[nodiscard]] bool has() const {
    return not all<T>().empty();
  }

  [[nodiscard]] std::vector<Send<typename S::Target>> sends() const {
    return all<Send<typename S::Target>>();
  }
  [[nodiscard]] std::vector<Parse<typename S::Target>> parses() const {
    return all<Parse<typename S::Target>>();
  }
  /// The Merge step of the last batch; throws (failing the test) if none.
  [[nodiscard]] Merge<typename S::Part> merge() const {
    return first<Merge<typename S::Part>>();
  }
  /// The Finish step of the last batch; throws (failing the test) if none.
  [[nodiscard]] Finish<typename S::Value> finish() const {
    return first<Finish<typename S::Value>>();
  }
  [[nodiscard]] bool finished_now() const {
    return has<Finish<typename S::Value>>();
  }

  /// The first step of one type in the last batch; throws if none.
  template <class T>
  [[nodiscard]] T first() const {
    auto found = all<T>();
    if (found.empty()) {
      throw std::logic_error{"the expected step is missing"};
    }
    return std::move(found.front());
  }

 private:
  Started<S> started_;
  core::Time now_;
  Jitter jitter_ = Jitter::from_bits(0);
  Steps steps_;
};

}  // namespace mov::fetch::test
