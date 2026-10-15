// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

#include "mov/io/wire/origin.hpp"

/// What a response body says before any parser reads its data
/// (docs/providers-design.md sections 4.2 and 4.3): data, no data, or a
/// refusal by the service. The fetch layer reports the last two as its own
/// NoData and ServiceError alternatives (section 5.1), which are these.
namespace mov::io::wire {

/// Why a service refused a request whose HTTP exchange succeeded.
enum class ServiceErrc : std::uint8_t {
  /// CO-OPS "The station is not a valid station or there is system error.":
  /// a bad id or a passing outage, so it is retried once (section 5.3).
  station_or_system,
  /// The station does not serve the datum, or the request named none.
  datum_unavailable,
  /// The window is longer than the service serves: a planner bug.
  range_exceeded,
  /// A parameter the service refused (a date, a product, a list type).
  invalid_parameter,
  /// The service's gateway failed in front of it ("Network error
  /// communicating with endpoint"): transient, so retried with backoff as a
  /// 5xx is (section 5.3).
  gateway,
  api_key_invalid,
  /// A body that is not what the endpoint serves: markup where JSON or text
  /// was expected (a maintenance or captive-portal page).
  unexpected_content,
  unknown,
};

/// A refusal and the service's own words: the message (trimmed of surrounding
/// white space), or the Content-Type of unexpected content, at most
/// `max_text_bytes` and cut on a UTF-8 boundary. The text is raw server
/// bytes, which whatever shows it must escape.
class ServiceError {
 public:
  static constexpr std::size_t max_text_bytes = 120;

  [[nodiscard]] static ServiceError make(Origin origin, ServiceErrc code,
                                         std::string_view text);

  [[nodiscard]] Origin origin() const noexcept { return origin_; }
  [[nodiscard]] ServiceErrc code() const noexcept { return code_; }
  [[nodiscard]] const std::string& text() const& noexcept { return text_; }
  const std::string& text() && = delete;

  friend bool operator==(const ServiceError&, const ServiceError&) = default;

 private:
  ServiceError(Origin origin, ServiceErrc code, std::string text)
      : origin_{origin}, code_{code}, text_{std::move(text)} {}

  Origin origin_;
  ServiceErrc code_;
  std::string text_;
};

/// The service has no data for the request: not a failure (a gap when
/// windows are stitched; section 5.6).
struct NoData {
  friend constexpr bool operator==(NoData, NoData) = default;
};

/// The body is the service's data (or what claims to be): the parser for the
/// request decides whether it is well formed.
struct Payload {
  friend constexpr bool operator==(Payload, Payload) = default;
};

using BodyClass = std::variant<Payload, NoData, ServiceError>;

}  // namespace mov::io::wire
