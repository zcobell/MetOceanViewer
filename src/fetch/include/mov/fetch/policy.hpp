// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// What a fetch does with an exchange's outcome, and how hard it may press an
// origin (docs/providers-design.md §5.3). The retry timing is retry.hpp's,
// the per-exchange limits limits.hpp's.

#include <chrono>
#include <cstdint>

#include "mov/fetch/origin.hpp"

namespace mov::fetch {

/// How an exchange failed below HTTP. The Qt transport maps its network
/// errors onto these; nothing in fetch sees Qt.
enum class TransportErrc : std::uint8_t {
  inactivity_timeout,  ///< no byte for too long (§5.3, exchange limits)
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

/// An HTTP status: 2xx is accepted; 429 cools the origin down and retries;
/// 500, 502, 503 and 504 are retried; NDBC's 404 means the file does not
/// exist (the next spelling or source is tried); everything else, a status
/// outside 100..599 included, fails. Redirects are the transport's, so a 3xx
/// that arrives here fails too.
[[nodiscard]] constexpr Verdict classify(Origin origin, int status) noexcept {
  constexpr int ok_first = 200;
  constexpr int ok_last = 299;
  constexpr int not_found = 404;
  constexpr int too_many_requests = 429;
  constexpr int internal_error = 500;
  constexpr int bad_gateway = 502;
  constexpr int unavailable = 503;
  constexpr int gateway_timeout = 504;
  if (status >= ok_first and status <= ok_last) {
    return Verdict::accept;
  }
  if (status == too_many_requests) {
    return Verdict::cool_down;
  }
  if (status == internal_error or status == bad_gateway or
      status == unavailable or status == gateway_timeout) {
    return Verdict::retry;
  }
  if (status == not_found and origin == Origin::ndbc) {
    return Verdict::absent;
  }
  return Verdict::fail;
}

/// Whether the USGS API key is attached: a keyed client may press harder.
enum class KeyPresence : std::uint8_t { none, present };

/// Interactive work goes first: an origin gate admits a background exchange
/// (a catalog refresh, the update check) only while no interactive one waits.
enum class Priority : std::uint8_t { interactive, background };

/// How hard one origin is pressed, across every job: at most `max_in_flight`
/// exchanges at once, starts at least `min_spacing` apart.
struct OriginPolicy {
  std::uint8_t max_in_flight;
  std::chrono::milliseconds min_spacing;
  friend constexpr bool operator==(OriginPolicy, OriginPolicy) = default;
};

/// The per-origin limits. CO-OPS publishes no number (throttling exists);
/// USGS allows a keyed client more requests; NDBC is a file server, one at
/// a time; the update check makes one request.
[[nodiscard]] constexpr OriginPolicy policy(Origin origin,
                                            KeyPresence key) noexcept {
  using std::chrono::milliseconds;
  constexpr std::uint8_t keyed_usgs = 4;
  switch (origin) {
    case Origin::coops_data:
    case Origin::coops_metadata:
      return {.max_in_flight = 2, .min_spacing = milliseconds{200}};
    case Origin::usgs:
      return {.max_in_flight =
                  key == KeyPresence::present ? keyed_usgs : std::uint8_t{2},
              .min_spacing = milliseconds{0}};
    case Origin::ndbc:
      return {.max_in_flight = 1, .min_spacing = milliseconds{250}};
    case Origin::github:
      break;
  }
  return {.max_in_flight = 1, .min_spacing = milliseconds{0}};
}

}  // namespace mov::fetch
