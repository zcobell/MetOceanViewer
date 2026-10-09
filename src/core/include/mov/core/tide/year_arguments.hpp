// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <chrono>
#include <expected>

#include "mov/core/tide/astronomy.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/phase.hpp"
#include "mov/core/time.hpp"

// The NOS yearly nodal scheme (docs/harmonics-json.md section 5.2): V0 at
// 1 January 00:00 UTC, u and f at mid-year, speeds from the 1900 rates. One
// YearAstronomy per year segment is all that prediction needs; the per-set
// table of every constituent's (V0 + u, f) is YearArguments (WP H2).

namespace mov::core::tide {

/// The years conforming readers support (docs/harmonics-json.md 5.3): the
/// SP 98 polynomials are fitted about 1900.
inline constexpr std::chrono::year first_year{1700};
inline constexpr std::chrono::year last_year{2200};

struct YearOutOfRange {
  std::chrono::year year;
  friend constexpr bool operator==(YearOutOfRange, YearOutOfRange) = default;
};

/// y-01-01T00:00:00Z.
[[nodiscard]] constexpr Time year_start(std::chrono::year y) noexcept {
  return std::chrono::sys_days{y / std::chrono::January / 1};
}

/// The exact middle of year y: 2 July 12:00 in a common year, 2 July 00:00
/// in a leap year.
[[nodiscard]] constexpr Time mid_year(std::chrono::year y) noexcept {
  const Time start = year_start(y);
  return start + ((year_start(y + std::chrono::years{1}) - start) / 2);
}

/// The UTC calendar year of `t`.
[[nodiscard]] constexpr std::chrono::year year_of(Time t) noexcept {
  return std::chrono::year_month_day{std::chrono::floor<std::chrono::days>(t)}
      .year();
}

/// The astronomy of one year in the yearly scheme: the elements at its start
/// (for V0) and the nodal terms at its middle (for u and f).
class YearAstronomy {
 public:
  /// YearOutOfRange outside [first_year, last_year].
  [[nodiscard]] static std::expected<YearAstronomy, YearOutOfRange> make(
      std::chrono::year y) noexcept;

  [[nodiscard]] std::chrono::year year() const noexcept { return year_; }
  [[nodiscard]] Time start() const noexcept { return year_start(year_); }
  [[nodiscard]] Elements at_start() const noexcept { return at_start_; }
  [[nodiscard]] NodalTerms at_mid_year() const noexcept { return at_mid_; }

  friend bool operator==(const YearAstronomy&, const YearAstronomy&) = default;

 private:
  YearAstronomy(std::chrono::year y, Elements start_elements,
                NodalTerms mid_terms) noexcept
      : year_{y}, at_start_{start_elements}, at_mid_{mid_terms} {}

  std::chrono::year year_;
  Elements at_start_;
  NodalTerms at_mid_;
};

/// A constituent's equilibrium argument (V + u) and node factor f, both from
/// one nodal scheme.
struct Equilibrium {
  Phase argument;
  double node_factor;
  friend constexpr bool operator==(const Equilibrium&,
                                   const Equilibrium&) = default;
};

/// (V0 + u) and f of `a` for the year of `y`: V0 at the year's start, u and f
/// at its middle. Prediction within the year uses
/// argument + speed * (hours since the start).
[[nodiscard]] Equilibrium equilibrium(const Argument& a,
                                      const YearAstronomy& y) noexcept;

namespace diagnostic {

/// The astronomy of one instant, for the instantaneous scheme: V, u and f
/// all at `time()`. For tests and investigations only; never conforming
/// output (docs/harmonics-json.md 5.2). Mirrors YearAstronomy, with the same
/// year range.
class InstantAstronomy {
 public:
  /// YearOutOfRange when the UTC year of `t` is outside
  /// [first_year, last_year].
  [[nodiscard]] static std::expected<InstantAstronomy, YearOutOfRange> make(
      Time t) noexcept;

  [[nodiscard]] Time time() const noexcept { return time_; }
  [[nodiscard]] Elements at_instant() const noexcept { return at_; }
  [[nodiscard]] NodalTerms nodal_at_instant() const noexcept { return nodal_; }

  friend bool operator==(const InstantAstronomy&,
                         const InstantAstronomy&) = default;

 private:
  InstantAstronomy(Time t, Elements e, NodalTerms n) noexcept
      : time_{t}, at_{e}, nodal_{n} {}

  Time time_;
  Elements at_;
  NodalTerms nodal_;
};

/// (V + u) and f of `a` with V, u and f all at the instant. At mid-year it
/// agrees with the yearly scheme in u and f but not in V
/// (docs/harmonics-engine.md section 1.3).
[[nodiscard]] Equilibrium equilibrium(const Argument& a,
                                      const InstantAstronomy& now) noexcept;

}  // namespace diagnostic

}  // namespace mov::core::tide
