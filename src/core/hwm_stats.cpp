// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/hwm_stats.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <utility>

#include "mov/core/detail/numeric.hpp"
#include "mov/core/hwm.hpp"
#include "mov/core/units.hpp"

namespace mov::core {

namespace {

struct FitResult {
  LinearFit fit;
  std::optional<double> r_squared;
};

using FitOutcome = std::expected<FitResult, HwmStatsError>;

Length metres(double v) noexcept { return Length::in(v, LengthUnit::meter); }

bool all_finite(std::span<const double> values) noexcept {
  return std::ranges::all_of(values, detail::is_finite);
}

bool moments_finite(const Moments& m) noexcept {
  return all_finite(std::array{m.mean_x(), m.mean_y(), m.mean_e(), m.m2x(),
                               m.m2y(), m.cxy(), m.m2e()});
}

// The clamps below are written as `v > hi ? hi : v` so that a NaN passes
// through to the finite check instead of being replaced by the bound
// (std::min and std::max return the first argument when the comparison is
// false, which turns a NaN into a plausible number).
double at_most(double v, double hi) noexcept { return v > hi ? hi : v; }
double at_least(double v, double lo) noexcept { return v < lo ? lo : v; }

/// Raw (uncentred) sums reconstructed from the central moments:
///   sum(x^2) = m2x + n mean_x^2     sum(xy) = cxy + n mean_x mean_y
///   sum(y^2) = m2y + n mean_y^2
/// sum(x^2) and sum(y^2) add two non-negative terms, so forming them loses
/// nothing; sum(xy) can add terms of opposite sign, with an error of about one
/// ulp of n mean_x mean_y. The through-origin fit is the only user: the free
/// fit works on the central moments directly.
struct RawSums {
  double xx;
  double xy;
  double yy;
};

RawSums raw_sums(const Moments& m) noexcept {
  const auto n = static_cast<double>(m.n());
  return {.xx = m.m2x() + (n * m.mean_x() * m.mean_x()),
          .xy = m.cxy() + (n * m.mean_x() * m.mean_y()),
          .yy = m.m2y() + (n * m.mean_y() * m.mean_y())};
}

/// r^2 = (cxy / m2x) (cxy / m2y): the product of the two regression slopes,
/// with no m2x m2y product to overflow or underflow. Clamped to 1: the exact
/// value is at most 1, and the computed one can exceed it by the rounding of
/// the accumulated moments, a few n ulps (asserted, then clamped).
double pearson_r_squared(const Moments& m) noexcept {
  const double r2 = (m.cxy() / m.m2x()) * (m.cxy() / m.m2y());
  [[maybe_unused]] const auto n = static_cast<double>(m.n());
  assert(not(r2 - 1.0 > 4.0 * n * std::numeric_limits<double>::epsilon()));
  return at_most(r2, 1.0);
}

/// slope = cxy / m2x; intercept = mean_y - slope mean_x; R^2 = Pearson r^2,
/// unless two points leave no degrees of freedom or modeled never varies.
FitOutcome free_fit(const Moments& m) noexcept {
  if (m.n() < 2) {
    return std::unexpected{HwmStatsError{TooFewForFreeFit{.wet = m.n()}}};
  }
  if (m.m2x() <= 0.0) {
    return std::unexpected{HwmStatsError{DegenerateObserved{}}};
  }
  const double slope = m.cxy() / m.m2x();
  const double intercept = m.mean_y() - (slope * m.mean_x());
  const bool has_r_squared = m.n() > 2 and m.m2y() > 0.0;
  return FitResult{.fit = Free{.slope = slope, .intercept = metres(intercept)},
                   .r_squared = has_r_squared
                                    ? std::optional{pearson_r_squared(m)}
                                    : std::nullopt};
}

/// slope = sum(xy) / sum(x^2); R^2 = 1 - SSres / sum(y^2), uncentred, as R
/// and statsmodels report it for a fit without intercept.
/// SSres = sum(y^2) - slope sum(xy), which is sum((y - slope x)^2) by the
/// normal equation; the clamp keeps rounding from making it negative. A single
/// mark fits exactly, so it has no R^2.
FitOutcome origin_fit(const Moments& m) noexcept {
  const RawSums sums = raw_sums(m);
  if (sums.xx <= 0.0) {
    return std::unexpected{HwmStatsError{DegenerateObserved{}}};
  }
  const double slope = sums.xy / sums.xx;
  const double residual = at_least(sums.yy - (slope * sums.xy), 0.0);
  const bool has_r_squared = m.n() > 1 and sums.yy > 0.0;
  return FitResult{.fit = ThroughOrigin{.slope = slope},
                   .r_squared = has_r_squared
                                    ? std::optional{1.0 - (residual / sums.yy)}
                                    : std::nullopt};
}

/// The fit the caller asked for. The switch names every enumerator; a value
/// outside the enum (a bad cast) is treated as the origin fit.
FitOutcome fit_of(const Moments& m, Intercept mode) noexcept {
  switch (mode) {
    case Intercept::free:
      return free_fit(m);
    case Intercept::through_origin:
      break;
  }
  return origin_fit(m);
}

/// sqrt(m2e / (n - 1)): the sample standard deviation of the error (v4
/// divided by n).
std::optional<Length> error_stddev(const Moments& m) noexcept {
  if (m.n() < 2) {
    return std::nullopt;
  }
  return metres(std::sqrt(m.m2e() / static_cast<double>(m.n() - 1)));
}

bool fit_finite(const FitResult& f) noexcept {
  const Free* intercepted = std::get_if<Free>(&f.fit);
  const ThroughOrigin* origin = std::get_if<ThroughOrigin>(&f.fit);
  const bool line_finite =
      intercepted != nullptr
          ? detail::is_finite(intercepted->slope) and
                detail::is_finite(intercepted->intercept.as(LengthUnit::meter))
          : origin != nullptr and detail::is_finite(origin->slope);
  return line_finite and (not f.r_squared or detail::is_finite(*f.r_squared));
}

HwmStats stats_of(const Moments& wet, const FitResult& f) noexcept {
  return HwmStats{.total = wet.seen(),
                  .wet = wet.n(),
                  .fit = f.fit,
                  .r_squared = f.r_squared,
                  .mean_error = metres(wet.mean_e()),
                  .error_stddev = error_stddev(wet)};
}

}  // namespace

std::expected<HwmStats, HwmStatsError> hwm_stats(const Moments& wet,
                                                 Intercept mode) noexcept {
  assert(wet.seen() >= wet.n());
  if (wet.n() == 0) {
    return std::unexpected{HwmStatsError{NoWetMarks{.total = wet.seen()}}};
  }
  if (not moments_finite(wet)) {
    return std::unexpected{HwmStatsError{NonFiniteMoments{}}};
  }
  return fit_of(wet, mode)
      .and_then([](const FitResult& f) -> FitOutcome {
        // A derived number can overflow even when the moments are finite.
        if (not fit_finite(f)) {
          return std::unexpected{HwmStatsError{NonFiniteMoments{}}};
        }
        return f;
      })
      .transform([&wet](const FitResult& f) { return stats_of(wet, f); });
}

std::expected<HwmStats, HwmStatsError> hwm_stats(
    std::span<const HighWaterMark> marks, Intercept mode) noexcept {
  return hwm_stats(wet_moments(marks), mode);
}

}  // namespace mov::core
