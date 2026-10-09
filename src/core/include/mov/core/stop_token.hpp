// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <functional>
#include <utility>

namespace mov::core {

/// A cancellation request that long work polls (io's readers between slabs
/// and records, the tide engine between segments) and answers with
/// Cancelled. It wraps a predicate rather than `std::stop_token`, which
/// Apple libc++ (Xcode 16) ships only as experimental, so the provider and
/// app layers can pass `QPromise::isCanceled` directly. A default token
/// never requests a stop. io names it io::StopToken.
class StopToken {
 public:
  StopToken() = default;
  explicit StopToken(std::function<bool()> requested)
      : requested_{std::move(requested)} {}

  [[nodiscard]] bool stop_requested() const {
    return requested_ and requested_();
  }

 private:
  std::function<bool()> requested_;
};

}  // namespace mov::core
