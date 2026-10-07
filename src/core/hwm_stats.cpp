// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/hwm_stats.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <expected>
#include <optional>
#include <span>
#include <utility>

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

/// slope = cxy / m2x; intercept = mean_y - slope mean_x; R^2 = Pearson r^2.
FitOutcome free_fit(const Moments& m) noexcept {
  if (m.n() < 2) {
    return std::unexpected{HwmStatsError{TooFewForFreeFit{.wet = m.n()}}};
  }
  if (m.m2x() <= 0.0) {
    return std::unexpected{HwmStatsError{DegenerateObserved{}}};
  }
  const double slope = m.cxy() / m.m2x();
  const double intercept = m.mean_y() - (slope * m.mean_x());
  const double denominator = m.m2x() * m.m2y();
  // Cauchy-Schwarz bounds r^2 by 1; rounding can overshoot by an ulp.
  const std::optional<double> r_squared =
      denominator > 0.0
          ? std::optional{std::min(1.0, m.cxy() * m.cxy() / denominator)}
          : std::nullopt;
  return FitResult{.fit = Free{.slope = slope, .intercept = metres(intercept)},
                   .r_squared = r_squared};
}

/// slope = sum(xy) / sum(x^2); R^2 = 1 - SSres / sum(y^2), uncentred (D25).
/// SSres = sum(y^2) - slope sum(xy), which is sum((y - slope x)^2) by the
/// normal equation; the clamp keeps rounding from making it negative.
FitOutcome origin_fit(const Moments& m) noexcept {
  const RawSums sums = raw_sums(m);
  if (sums.xx <= 0.0) {
    return std::unexpected{HwmStatsError{DegenerateObserved{}}};
  }
  const double slope = sums.xy / sums.xx;
  const double residual = std::max(0.0, sums.yy - (slope * sums.xy));
  const std::optional<double> r_squared =
      sums.yy > 0.0 ? std::optional{1.0 - (residual / sums.yy)} : std::nullopt;
  return FitResult{.fit = ThroughOrigin{.slope = slope},
                   .r_squared = r_squared};
}

/// sqrt(m2e / (n - 1)): the sample standard deviation of the error (D17).
std::optional<Length> error_stddev(const Moments& m) noexcept {
  if (m.n() < 2) {
    return std::nullopt;
  }
  return metres(std::sqrt(m.m2e() / static_cast<double>(m.n() - 1)));
}

}  // namespace

std::expected<HwmStats, HwmStatsError> hwm_stats(const Moments& wet,
                                                 std::size_t total,
                                                 Intercept mode) noexcept {
  if (wet.n() == 0) {
    return std::unexpected{HwmStatsError{NoWetMarks{.total = total}}};
  }
  const FitOutcome fitted =
      mode == Intercept::free ? free_fit(wet) : origin_fit(wet);
  if (not fitted) {
    return std::unexpected{fitted.error()};
  }
  return HwmStats{.total = total,
                  .wet = wet.n(),
                  .fit = fitted->fit,
                  .r_squared = fitted->r_squared,
                  .mean_error = metres(wet.mean_e()),
                  .error_stddev = error_stddev(wet)};
}

std::expected<HwmStats, HwmStatsError> hwm_stats(
    std::span<const HighWaterMark> marks, Intercept mode) noexcept {
  return hwm_stats(wet_moments(marks), marks.size(), mode);
}

}  // namespace mov::core
