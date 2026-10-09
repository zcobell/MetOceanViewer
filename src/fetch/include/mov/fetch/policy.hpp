// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// The retry rules of a fetch (docs/providers-design.md §5.3). H0 holds the
// transport part; P6 adds the HTTP statuses, Retry-After, backoff and the
// origin policies.

#include <cstdint>

namespace mov::fetch {

/// How an exchange failed below HTTP. The Qt transport maps its network
/// errors onto these; nothing in fetch sees Qt.
enum class TransportErrc : std::uint8_t {
  inactivity_timeout,  ///< no byte for ExchangeLimits::inactivity
  too_slow,            ///< under the throughput floor
  connection_refused,
  connection_closed,  ///< closed before the response was complete
  host_not_found,
  tls,
  redirect_refused,  ///< to another origin, to http, or past the limit
  body_too_large,
  other,
};

/// What the fetch machine does with one exchange's outcome.
enum class Verdict : std::uint8_t {
  accept,
  retry,
  absent,     ///< NDBC's 404: the file does not exist, try the next one
  cool_down,  ///< 429: the origin waits, then the exchange is retried
  fail,
};

/// A failure the next attempt may not meet again (a timeout, a refused or
/// dropped connection) is retried; one it would meet again (DNS, TLS, a
/// refused redirect, the body cap) fails at once.
[[nodiscard]] constexpr Verdict classify(TransportErrc code) noexcept {
  switch (code) {
    case TransportErrc::inactivity_timeout:
    case TransportErrc::too_slow:
    case TransportErrc::connection_refused:
    case TransportErrc::connection_closed:
      return Verdict::retry;
    case TransportErrc::host_not_found:
    case TransportErrc::tls:
    case TransportErrc::redirect_refused:
    case TransportErrc::body_too_large:
    case TransportErrc::other:
      break;
  }
  return Verdict::fail;
}

}  // namespace mov::fetch
