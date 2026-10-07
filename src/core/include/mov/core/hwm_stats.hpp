// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <numeric>
#include <optional>
#include <span>
#include <variant>

#include "mov/core/hwm.hpp"
#include "mov/core/units.hpp"

// Statistics of a model against surveyed high-water marks: a linear fit of
// modeled (y) on observed (x), its R^2, and the mean and spread of the error
// e = y - x. Dry marks are left out; a set with no wet mark has no statistics.
// Everything is computed in metres, so a slope or R^2 does not depend on the
// unit of the file.

namespace mov::core {

/// Running central moments of the wet marks, x = observed, y = modeled and
/// e = y - x (metres, metres squared).
///
/// Holds n, the three means, and the "M2" sums about the means:
///   m2x = sum (x - mean_x)^2     m2y = sum (y - mean_y)^2
///   cxy = sum (x - mean_x)(y - mean_y)     m2e = sum (e - mean_e)^2
/// m2e could be derived (m2x - 2 cxy + m2y), but that subtracts large
/// numbers to get a small one, so it is carried and updated on its own.
///
/// Numerics. One mark is added with Welford's update; two sets are joined with
/// the pairwise formula of Chan, Golub and LeVeque (1983). Welford is the
/// pairwise formula with a one-element right operand, so a single operator+
/// serves both. Neither forms sum(x^2) - n mean^2, the textbook formula that
/// loses every digit when the data sit far from zero. Each M2 update adds a
/// non-negative term to a non-negative sum, so no cancellation.
///
/// Monoid. The identity is the default Moments (n == 0): operator+ returns the
/// other operand untouched when either side is empty, so it is exact. Joining
/// is associative and commutative only up to rounding (about 1e-16 relative per
/// join). The order is therefore part of the result: wet_moments folds the
/// marks left to right, which is deterministic; a tree reduction would give a
/// last-digits-different answer. The default operator== compares bits.
class Moments {
 public:
  constexpr Moments() noexcept = default;  // the identity

  /// One observation, or the identity for a dry mark (a null object, so a
  /// fold needs no `if`).
  [[nodiscard]] static constexpr Moments of(const HighWaterMark& h) noexcept {
    const Wet* wet = std::get_if<Wet>(&h.modeled);
    if (wet == nullptr) {
      return Moments{};
    }
    const double x = h.observed.as(LengthUnit::meter);
    const double y = wet->elevation.as(LengthUnit::meter);
    Moments one;
    one.n_ = 1;
    one.mean_x_ = x;
    one.mean_y_ = y;
    one.mean_e_ = y - x;  // the same subtraction modeled_error() does
    return one;           // M2 sums of one observation are zero
  }

  /// The pairwise update. With w = nb / (na + nb) and c = na nb / (na + nb):
  ///   mean = mean_a + w (mean_b - mean_a)
  ///   M2   = M2_a + M2_b + c (mean_b - mean_a)^2        (and the cross
  ///   term c dx dy for cxy). A one-observation b is exactly Welford.
  [[nodiscard]] friend constexpr Moments operator+(const Moments& a,
                                                   const Moments& b) noexcept {
    if (a.n_ == 0) {
      return b;
    }
    if (b.n_ == 0) {
      return a;
    }
    const auto na = static_cast<double>(a.n_);
    const auto nb = static_cast<double>(b.n_);
    const double n = na + nb;
    const double w = nb / n;
    const double c = na * nb / n;
    const double dx = b.mean_x_ - a.mean_x_;
    const double dy = b.mean_y_ - a.mean_y_;
    const double de = b.mean_e_ - a.mean_e_;

    Moments sum;
    sum.n_ = a.n_ + b.n_;
    sum.mean_x_ = a.mean_x_ + (w * dx);
    sum.mean_y_ = a.mean_y_ + (w * dy);
    sum.mean_e_ = a.mean_e_ + (w * de);
    sum.m2x_ = a.m2x_ + b.m2x_ + (c * dx * dx);
    sum.m2y_ = a.m2y_ + b.m2y_ + (c * dy * dy);
    sum.cxy_ = a.cxy_ + b.cxy_ + (c * dx * dy);
    sum.m2e_ = a.m2e_ + b.m2e_ + (c * de * de);
    return sum;
  }

  /// Number of wet marks.
  [[nodiscard]] constexpr std::size_t n() const noexcept { return n_; }
  [[nodiscard]] constexpr double mean_x() const noexcept { return mean_x_; }
  [[nodiscard]] constexpr double mean_y() const noexcept { return mean_y_; }
  [[nodiscard]] constexpr double mean_e() const noexcept { return mean_e_; }
  [[nodiscard]] constexpr double m2x() const noexcept { return m2x_; }
  [[nodiscard]] constexpr double m2y() const noexcept { return m2y_; }
  [[nodiscard]] constexpr double cxy() const noexcept { return cxy_; }
  [[nodiscard]] constexpr double m2e() const noexcept { return m2e_; }

  friend constexpr bool operator==(const Moments&,
                                   const Moments&) noexcept = default;

 private:
  std::size_t n_{};
  double mean_x_{};
  double mean_y_{};
  double mean_e_{};
  double m2x_{};
  double m2y_{};
  double cxy_{};
  double m2e_{};
};

/// The moments of the wet marks, folded left to right in the order given
/// (std::accumulate is an ordered fold: result = result + of(mark)).
[[nodiscard]] constexpr Moments wet_moments(
    std::span<const HighWaterMark> marks) noexcept {
  return std::accumulate(marks.begin(), marks.end(), Moments{},
                         [](const Moments& so_far, const HighWaterMark& h) {
                           return so_far + Moments::of(h);
                         });
}

/// Which line to fit. A named choice, never a bool: v4 passed a checkbox
/// pointer that always converted to true, so every fit went through the
/// origin whatever the user chose (N1).
enum class Intercept : std::uint8_t { free, through_origin };

/// y = slope * x.
struct ThroughOrigin {
  double slope;
  friend constexpr bool operator==(const ThroughOrigin&,
                                   const ThroughOrigin&) = default;
};
/// y = slope * x + intercept.
struct Free {
  double slope;
  Length intercept;
  friend constexpr bool operator==(const Free&, const Free&) = default;
};
/// The fit that was computed. Its alternative is the Intercept that was asked
/// for, so a through-origin result has no intercept to misread.
using LinearFit = std::variant<ThroughOrigin, Free>;

struct HwmStats {
  std::size_t total;  ///< all marks, wet and dry
  std::size_t wet;    ///< marks that went into the statistics
  LinearFit fit;      ///< the alternative matches the requested Intercept
  /// Free fit: the Pearson r^2. Through the origin: the uncentred
  /// 1 - SSres / sum(y^2) (decision D25; v4 centred the denominator, which
  /// can make a good origin fit look negative). In [0, 1]. nullopt when the
  /// denominator is zero (all modeled values equal for the free fit, all zero
  /// for the origin fit).
  std::optional<double> r_squared;
  Length mean_error;  ///< mean of modeled - observed
  /// sqrt(sum (e - mean)^2 / (n - 1)), the sample standard deviation (D17; v4
  /// divided by n). nullopt for a single wet mark.
  std::optional<Length> error_stddev;
  friend bool operator==(const HwmStats&, const HwmStats&) = default;
};

/// No mark is wet (also: no marks). `total` is how many there were, so the
/// message can say "all 12 marks are dry".
struct NoWetMarks {
  std::size_t total;
  friend constexpr bool operator==(const NoWetMarks&,
                                   const NoWetMarks&) = default;
};
/// A free fit needs two wet marks.
struct TooFewForFreeFit {
  std::size_t wet;
  friend constexpr bool operator==(const TooFewForFreeFit&,
                                   const TooFewForFreeFit&) = default;
};
/// Every observed value is the same (free fit) or zero (origin fit): the
/// slope has a zero denominator.
struct DegenerateObserved {
  friend constexpr bool operator==(DegenerateObserved,
                                   DegenerateObserved) = default;
};
/// Checked in this order: no wet marks, too few for a free fit, degenerate.
using HwmStatsError =
    std::variant<NoWetMarks, TooFewForFreeFit, DegenerateObserved>;

/// Statistics from moments already folded. `total` is the number of marks the
/// moments were folded from, wet or not.
[[nodiscard]] std::expected<HwmStats, HwmStatsError> hwm_stats(
    const Moments& wet, std::size_t total, Intercept mode) noexcept;

/// Statistics of the wet marks, with the requested fit. The marks are folded
/// in order, so the result is reproducible bit for bit. A mark with a
/// non-finite observed or wet elevation breaks the precondition (readers
/// reject them) and gives NaN statistics.
[[nodiscard]] std::expected<HwmStats, HwmStatsError> hwm_stats(
    std::span<const HighWaterMark> marks, Intercept mode) noexcept;

}  // namespace mov::core
