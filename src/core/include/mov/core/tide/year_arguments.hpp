// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cassert>
#include <chrono>
#include <cstddef>
#include <expected>
#include <utility>
#include <vector>

#include "mov/core/tide/astronomy.hpp"
#include "mov/core/tide/constituent.hpp"
#include "mov/core/tide/constituent_set.hpp"
#include "mov/core/tide/phase.hpp"
#include "mov/core/time.hpp"

// The NOS yearly nodal scheme (docs/harmonics-json.md section 5.2): V0 at
// 1 January 00:00 UTC, u and f at mid-year, speeds from the 1900 rates. One
// YearAstronomy per year segment holds what the year needs (the elements at
// its start, the nodal terms and the 14 node-factor values at its middle);
// YearArguments is the table of every constituent's (V0 + u, f) for one
// ConstituentSet and year.

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
/// (for V0), the nodal terms at its middle (for u), and every node-factor
/// formula evaluated there once (for f).
class YearAstronomy {
 public:
  /// YearOutOfRange outside [first_year, last_year].
  [[nodiscard]] static std::expected<YearAstronomy, YearOutOfRange> make(
      std::chrono::year y) noexcept;

  [[nodiscard]] std::chrono::year year() const noexcept { return year_; }
  [[nodiscard]] Time start() const noexcept { return year_start(year_); }
  [[nodiscard]] Elements at_start() const noexcept { return at_start_; }
  [[nodiscard]] NodalTerms at_mid_year() const noexcept { return at_mid_; }
  /// NodeFactorValues::of(at_mid_year()), evaluated once by make().
  [[nodiscard]] const NodeFactorValues& node_factors() const& noexcept {
    return factors_;
  }
  const NodeFactorValues& node_factors() const&& = delete;  // would dangle

  friend bool operator==(const YearAstronomy&, const YearAstronomy&) = default;

 private:
  YearAstronomy(std::chrono::year y, Elements start_elements,
                NodalTerms mid_terms) noexcept
      : year_{y},
        at_start_{start_elements},
        at_mid_{mid_terms},
        factors_{NodeFactorValues::of(mid_terms)} {}

  std::chrono::year year_;
  Elements at_start_;
  NodalTerms at_mid_;
  NodeFactorValues factors_;
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
/// at its middle (f from the year's cached node factors). Prediction within
/// the year uses argument + speed * (hours since the start).
[[nodiscard]] Equilibrium equilibrium(const Argument& a,
                                      const YearAstronomy& y) noexcept;

/// Every constituent's Equilibrium for one ConstituentSet and one year: what
/// a year segment of a prediction evaluates once. Indexed by the set's
/// ConstituentIndex; built only by year_arguments().
class YearArguments {
 public:
  [[nodiscard]] std::chrono::year year() const noexcept { return year_; }
  /// year()-01-01T00:00:00Z, the origin of the hours in the argument.
  [[nodiscard]] Time start() const noexcept { return year_start(year_); }
  [[nodiscard]] std::size_t size() const noexcept {
    return by_constituent_.size();
  }

  /// The entry of `i`, an index of the set this table was built for (an
  /// index of a larger set is out of range: asserted).
  [[nodiscard]] const Equilibrium& operator[](
      ConstituentIndex i) const& noexcept {
    assert(i.value() < by_constituent_.size());
    return by_constituent_[i.value()];
  }
  const Equilibrium& operator[](ConstituentIndex) const&& = delete;

  friend bool operator==(const YearArguments&, const YearArguments&) = default;

 private:
  friend YearArguments year_arguments(const ConstituentSet& set,
                                      const YearAstronomy& y);
  YearArguments(std::chrono::year y, std::vector<Equilibrium> by_constituent)
      : year_{y}, by_constituent_{std::move(by_constituent)} {}

  std::chrono::year year_;
  std::vector<Equilibrium> by_constituent_;
};

/// The table of `set` for the year of `y`.
[[nodiscard]] YearArguments year_arguments(const ConstituentSet& set,
                                           const YearAstronomy& y);

/// The table of `set` for year `y`; YearOutOfRange outside
/// [first_year, last_year].
[[nodiscard]] std::expected<YearArguments, YearOutOfRange> year_arguments(
    const ConstituentSet& set, std::chrono::year y);

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
