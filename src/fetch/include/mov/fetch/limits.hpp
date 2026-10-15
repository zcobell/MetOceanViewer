// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// The limits on one exchange (docs/providers-design.md §5.3): no fixed
// deadline (a large page on a slow link is legitimate), but an inactivity
// timeout, a throughput floor and a body cap. The inactivity timeout is the
// transport's (QNetworkRequest::setTransferTimeout); the floor and the cap
// are decided here, from the progress the transport observes.

#include <chrono>
#include <cstdint>
#include <optional>

#include "mov/fetch/policy.hpp"

namespace mov::fetch {

struct ExchangeLimits {
  /// No byte for this long fails the exchange (inactivity_timeout).
  std::chrono::seconds inactivity{30};
  /// At least this many bytes ...
  std::uint64_t min_bytes_per_window = std::uint64_t{64} << 10;
  /// ... in every window this long after the first byte (too_slow).
  std::chrono::seconds throughput_window{60};
  /// The most a body may hold, decompressed (body_too_large); also the
  /// transport's decompression threshold.
  std::uint64_t max_body_bytes = std::uint64_t{64} << 20;
  friend constexpr bool operator==(const ExchangeLimits&,
                                   const ExchangeLimits&) = default;
};

/// The throughput floor and the body cap of one exchange, fed the progress
/// the transport sees: the time since Transport::start and the bytes so far
/// (decompressed). Windows are consecutive from the first byte: when a
/// progress report falls `throughput_window` or more after the current
/// window began, the window is judged on the bytes that arrived since it
/// began and the next window begins at that report. A drip that keeps the
/// inactivity timer quiet still fails at the end of the first window it
/// cannot fill. Reports must not go back in time or in bytes; a report that
/// does is taken as no progress.
class ThroughputCheck {
 public:
  explicit constexpr ThroughputCheck(const ExchangeLimits& limits) noexcept
      : min_bytes_{limits.min_bytes_per_window},
        window_length_{limits.throughput_window},
        max_bytes_{limits.max_body_bytes} {}

  /// The failure the exchange has met, if any.
  [[nodiscard]] constexpr std::optional<TransportErrc> on_progress(
      std::chrono::milliseconds since_start, std::uint64_t total) noexcept {
    if (total > max_bytes_) {
      return TransportErrc::body_too_large;
    }
    if (not window_) {
      if (total > 0) {
        // The first byte opens the first window, which counts it.
        window_ = Window{.start = since_start, .bytes_at_start = 0};
      }
      return std::nullopt;
    }
    if (since_start - window_->start < window_length_) {
      return std::nullopt;
    }
    const std::uint64_t arrived =
        total > window_->bytes_at_start ? total - window_->bytes_at_start : 0;
    if (arrived < min_bytes_) {
      return TransportErrc::too_slow;
    }
    window_ = Window{.start = since_start, .bytes_at_start = total};
    return std::nullopt;
  }

 private:
  /// The window being filled: when it began and the bytes by then.
  struct Window {
    std::chrono::milliseconds start;
    std::uint64_t bytes_at_start;
  };

  std::uint64_t min_bytes_;
  std::chrono::milliseconds window_length_;
  std::uint64_t max_bytes_;
  std::optional<Window> window_;
};

}  // namespace mov::fetch
