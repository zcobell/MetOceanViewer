// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// When a failed exchange is tried again (docs/providers-design.md §5.3): the
// retry policy, full-jitter backoff and the server's Retry-After.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

namespace mov::fetch {

/// A uniform random number in [0, 1), the jitter of one backoff. The machine
/// takes one with every event, so it holds no generator and a test or a
/// fuzzer decides every delay.
class Jitter {
 public:
  /// nullopt unless 0 <= u < 1 (NaN included).
  [[nodiscard]] static constexpr std::optional<Jitter> make(double u) noexcept {
    if (not(u >= 0.0 and u < 1.0)) {
      return std::nullopt;
    }
    return Jitter{u};
  }

  /// The top 53 bits of `bits` as a fraction of 2^53: every 64-bit value is
  /// a jitter, so a random engine's output needs no checking.
  [[nodiscard]] static constexpr Jitter from_bits(std::uint64_t bits) noexcept {
    constexpr int dropped = 11;                         // 64 - 53
    constexpr double scale = 1.0 / 9007199254740992.0;  // 2^-53
    return Jitter{static_cast<double>(bits >> dropped) * scale};
  }

  [[nodiscard]] constexpr double value() const noexcept { return u_; }

  friend constexpr bool operator==(Jitter, Jitter) = default;

 private:
  explicit constexpr Jitter(double u) noexcept : u_{u} {}

  double u_;
};

enum class RetryPolicyError : std::uint8_t {
  attempts_out_of_range,     ///< max_attempts outside [1, max_attempts_limit]
  base_out_of_range,         ///< base below 1 ms or above cap
  cap_out_of_range,          ///< cap above cap_limit
  retry_after_out_of_range,  ///< max_retry_after outside [1 s, 1 day]
};

/// How often and how patiently an exchange is retried. Built by make, which
/// checks the bounds, or defaults(): 4 attempts, backoff from 1 s up to
/// 30 s, a server's Retry-After honored up to 120 s (a longer one fails the
/// exchange with RateLimited rather than wait).
class RetryPolicy {
 public:
  static constexpr std::uint8_t max_attempts_limit = 16;
  static constexpr std::chrono::milliseconds cap_limit = std::chrono::hours{1};

  /// The input of make, for designated initializers; the defaults are
  /// defaults()'s.
  struct Fields {
    std::uint8_t max_attempts = 4;
    std::chrono::milliseconds base = std::chrono::seconds{1};
    std::chrono::milliseconds cap = std::chrono::seconds{30};
    std::chrono::seconds max_retry_after = std::chrono::seconds{120};
  };

  [[nodiscard]] static constexpr std::expected<RetryPolicy, RetryPolicyError>
  make(Fields f) noexcept {
    if (f.max_attempts < 1 or f.max_attempts > max_attempts_limit) {
      return std::unexpected{RetryPolicyError::attempts_out_of_range};
    }
    if (f.cap > cap_limit) {
      return std::unexpected{RetryPolicyError::cap_out_of_range};
    }
    if (f.base < std::chrono::milliseconds{1} or f.base > f.cap) {
      return std::unexpected{RetryPolicyError::base_out_of_range};
    }
    if (f.max_retry_after < std::chrono::seconds{1} or
        f.max_retry_after > std::chrono::days{1}) {
      return std::unexpected{RetryPolicyError::retry_after_out_of_range};
    }
    return RetryPolicy{f};
  }

  [[nodiscard]] static constexpr RetryPolicy defaults() noexcept {
    return RetryPolicy{Fields{}};
  }

  /// Sends of one exchange slot, the first included: at least 1.
  [[nodiscard]] constexpr std::uint8_t max_attempts() const noexcept {
    return f_.max_attempts;
  }
  [[nodiscard]] constexpr std::chrono::milliseconds base() const noexcept {
    return f_.base;
  }
  [[nodiscard]] constexpr std::chrono::milliseconds cap() const noexcept {
    return f_.cap;
  }
  [[nodiscard]] constexpr std::chrono::seconds max_retry_after()
      const noexcept {
    return f_.max_retry_after;
  }

  friend constexpr bool operator==(const RetryPolicy& a,
                                   const RetryPolicy& b) noexcept {
    return a.f_.max_attempts == b.f_.max_attempts and a.f_.base == b.f_.base and
           a.f_.cap == b.f_.cap and
           a.f_.max_retry_after == b.f_.max_retry_after;
  }

 private:
  explicit constexpr RetryPolicy(Fields f) noexcept : f_{f} {}

  Fields f_;
};

/// The wait before the next attempt, after `attempt` attempts (1 after the
/// first). A server's Retry-After is honored as it is. Otherwise full
/// jitter: uniform in [0, min(cap, base * 2^(attempt - 1))), with the
/// exponent clamped so the product cannot overflow (base <= cap <= 1 h, so
/// 2^40 base fits int64 milliseconds); an attempt below 1 counts as 1.
[[nodiscard]] constexpr std::chrono::milliseconds backoff(
    const RetryPolicy& rules, int attempt,
    std::optional<std::chrono::seconds> server_wait, Jitter jitter) noexcept {
  if (server_wait) {
    return *server_wait;
  }
  constexpr int max_exponent = 40;
  const int exponent = std::clamp(attempt, 1, max_exponent + 1) - 1;
  const std::int64_t grown = rules.base().count() << exponent;
  const std::int64_t ceiling =
      std::min<std::int64_t>(rules.cap().count(), grown);
  // ceiling <= 1 h in ms < 2^53: the product is exact enough and the cast
  // cannot overflow.
  return std::chrono::milliseconds{
      static_cast<std::int64_t>(static_cast<double>(ceiling) * jitter.value())};
}

/// A Retry-After header: delta-seconds (1*DIGIT; past 2^31 it saturates
/// there, as RFC 9111 §1.2.2 says) or an HTTP-date (IMF-fixdate, or the
/// obsolete RFC 850 and asctime forms that RFC 9110 §5.6.7 requires a
/// recipient to accept), taken relative to the response's own `Date`
/// (never the local clock: a server with a skewed clock is still obeyed);
/// an HTTP-date without a usable `Date` is nullopt. Optional whitespace
/// around either value is ignored. The result is at least 1 s; the upper
/// bound is the machine's (RetryPolicy::max_retry_after), which fails rather
/// than clamps, so a long wait is reported, not hidden. nullopt for anything
/// else.
[[nodiscard]] std::optional<std::chrono::seconds> retry_after(
    std::string_view value, std::optional<std::string_view> date) noexcept;

/// An HTTP-date (the three forms above) as an instant; nullopt for anything
/// else. RFC 850's two-digit year is the year with those digits at most 50
/// years after `reference` (RFC 9110 §5.6.7); without a reference, 70 to 99
/// are 19yy and the rest 20yy. retry_after passes the Date header's year.
[[nodiscard]] std::optional<std::chrono::sys_seconds> parse_http_date(
    std::string_view text,
    std::optional<std::chrono::year> reference = std::nullopt) noexcept;

/// An instant as an IMF-fixdate ("Sun, 06 Nov 1994 08:49:37 GMT"); nullopt
/// outside the four-digit years.
[[nodiscard]] std::optional<std::string> format_imf_fixdate(
    std::chrono::sys_seconds t);

}  // namespace mov::fetch
