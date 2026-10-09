// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <numeric>
#include <optional>
#include <ranges>
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
/// e = y - x (metres, metres squared), plus how many marks were looked at.
///
/// Holds `seen` (every mark folded in, wet or dry), n (the wet ones), the
/// three means, and the "M2" sums about the means:
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
/// Monoid. The identity is the default Moments (nothing seen). A dry mark is
/// {seen = 1, n = 0}: it joins like any other, changes only `seen`, and leaves
/// the wet fields bit for bit as they were (so `total >= wet` cannot be
/// violated). Joining is associative and commutative only up to rounding
/// (about 1e-16 relative per join). The order is therefore part of the result:
/// wet_moments folds the marks left to right, which is deterministic; a tree
/// reduction would give a last-digits-different answer. operator== is true when
/// every field is bit-identical (so -0.0 differs from 0.0, and a NaN equals
/// itself).
class Moments {
 private:
  // Every field in one place, no default member values: a constructor that
  // forgets one fails to compile (-Wmissing-field-initializers).
  struct Fields {
    std::size_t seen;
    std::size_t n;
    double mean_x;
    double mean_y;
    double mean_e;
    double m2x;
    double m2y;
    double cxy;
    double m2e;
  };

 public:
  constexpr Moments() noexcept : fields_{} {}  // the identity: nothing seen

  /// One observation; for a dry mark, the wet-free {seen = 1} (a null object,
  /// so a fold needs no `if`).
  [[nodiscard]] static constexpr Moments of(const HighWaterMark& h) noexcept {
    const Wet* wet = std::get_if<Wet>(&h.modeled);
    if (wet == nullptr) {
      return Moments{Fields{.seen = 1,
                            .n = 0,
                            .mean_x = 0.0,
                            .mean_y = 0.0,
                            .mean_e = 0.0,
                            .m2x = 0.0,
                            .m2y = 0.0,
                            .cxy = 0.0,
                            .m2e = 0.0}};
    }
    const double x = h.observed.as(LengthUnit::meter);
    const double y = wet->elevation.as(LengthUnit::meter);
    return Moments{Fields{.seen = 1,
                          .n = 1,
                          .mean_x = x,
                          .mean_y = y,
                          .mean_e = y - x,  // as modeled_error() subtracts
                          .m2x = 0.0,  // the M2 sums of one observation are 0
                          .m2y = 0.0,
                          .cxy = 0.0,
                          .m2e = 0.0}};
  }

  /// The join. An operand with no wet mark leaves the other's wet fields
  /// untouched (exactly) and only adds to `seen`. Otherwise the pairwise
  /// update, with w = nb / (na + nb) and c = na nb / (na + nb):
  ///   mean = mean_a + w (mean_b - mean_a)
  ///   M2   = M2_a + M2_b + c (mean_b - mean_a)^2
  /// and c dx dy for the cross sum cxy. A one-observation b is exactly Welford.
  [[nodiscard]] friend constexpr Moments operator+(const Moments& a,
                                                   const Moments& b) noexcept {
    const std::size_t seen = a.fields_.seen + b.fields_.seen;
    if (a.fields_.n == 0) {
      return Moments{recounted(b.fields_, seen)};
    }
    if (b.fields_.n == 0) {
      return Moments{recounted(a.fields_, seen)};
    }
    return Moments{pooled(a.fields_, b.fields_, seen)};
  }

  /// Marks folded in, wet or dry.
  [[nodiscard]] constexpr std::size_t seen() const noexcept {
    return fields_.seen;
  }
  /// Wet marks.
  [[nodiscard]] constexpr std::size_t n() const noexcept { return fields_.n; }
  [[nodiscard]] constexpr double mean_x() const noexcept {
    return fields_.mean_x;
  }
  [[nodiscard]] constexpr double mean_y() const noexcept {
    return fields_.mean_y;
  }
  [[nodiscard]] constexpr double mean_e() const noexcept {
    return fields_.mean_e;
  }
  [[nodiscard]] constexpr double m2x() const noexcept { return fields_.m2x; }
  [[nodiscard]] constexpr double m2y() const noexcept { return fields_.m2y; }
  [[nodiscard]] constexpr double cxy() const noexcept { return fields_.cxy; }
  [[nodiscard]] constexpr double m2e() const noexcept { return fields_.m2e; }

  /// Bit-for-bit: the monoid-identity law is checked with this.
  [[nodiscard]] friend constexpr bool operator==(const Moments& a,
                                                 const Moments& b) noexcept {
    return std::bit_cast<Bytes>(a.fields_) == std::bit_cast<Bytes>(b.fields_);
  }

 private:
  static_assert(sizeof(Fields) == 9 * 8, "Fields has no padding");
  using Bytes = std::array<std::byte, sizeof(Fields)>;

  explicit constexpr Moments(const Fields& f) noexcept : fields_{f} {}

  [[nodiscard]] static constexpr Fields recounted(Fields f,
                                                  std::size_t seen) noexcept {
    f.seen = seen;
    return f;
  }

  // Both operands have wet marks.
  [[nodiscard]] static constexpr Fields pooled(const Fields& a, const Fields& b,
                                               std::size_t seen) noexcept {
    const auto na = static_cast<double>(a.n);
    const auto nb = static_cast<double>(b.n);
    const double total = na + nb;
    const double w = nb / total;
    const double c = na * nb / total;
    const double dx = b.mean_x - a.mean_x;
    const double dy = b.mean_y - a.mean_y;
    const double de = b.mean_e - a.mean_e;
    return Fields{.seen = seen,
                  .n = a.n + b.n,
                  .mean_x = a.mean_x + (w * dx),
                  .mean_y = a.mean_y + (w * dy),
                  .mean_e = a.mean_e + (w * de),
                  .m2x = a.m2x + b.m2x + (c * dx * dx),
                  .m2y = a.m2y + b.m2y + (c * dy * dy),
                  .cxy = a.cxy + b.cxy + (c * dx * dy),
                  .m2e = a.m2e + b.m2e + (c * de * de)};
  }

  Fields fields_;
};

/// The moments of the marks, folded left to right in the order given.
///
/// std::accumulate is an ordered fold: result = result + of(mark), starting
/// from the identity. std::reduce and std::transform_reduce may regroup and
/// reorder the operands; the join is only approximately associative, so they
/// would make the last digits depend on the implementation and the thread
/// count. (A parallel reduction would need a deliberate, fixed tree.)
[[nodiscard]] constexpr Moments wet_moments(
    std::span<const HighWaterMark> marks) noexcept {
  const auto each = marks | std::views::transform(Moments::of);
  return std::accumulate(each.begin(), each.end(), Moments{}, std::plus{});
}

/// Which line to fit. A named choice, never a bool: v4 passed a checkbox
/// pointer that always converted to true, so every fit went through the
/// origin whatever the user chose.
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
  std::size_t total;  ///< all marks, wet and dry (never less than `wet`)
  std::size_t wet;    ///< marks that went into the statistics
  LinearFit fit;      ///< the alternative matches the requested Intercept
  /// Free fit: the Pearson r^2. Through the origin: the uncentred
  /// 1 - SSres / sum(y^2), as R and statsmodels report it (v4 centred the
  /// denominator, which can make a good origin fit look negative). In [0, 1].
  /// nullopt when the fit has no degrees of freedom left (free fit with at
  /// most 2 wet marks, origin fit with 1: the line passes through the data
  /// exactly, and 1.0 would be a claim about nothing), or when the denominator
  /// is zero (all modeled values equal for the free fit, all zero for the
  /// origin fit).
  std::optional<double> r_squared;
  Length mean_error;  ///< mean of modeled - observed
  /// sqrt(sum (e - mean)^2 / (n - 1)), the sample standard deviation (v4
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
/// The moments, or a number derived from them, are NaN or infinite: a mark
/// held a non-finite or enormous length (marks from checked_elevation /
/// model_value cannot), or the data overflowed a sum.
struct NonFiniteMoments {
  friend constexpr bool operator==(NonFiniteMoments,
                                   NonFiniteMoments) = default;
};
/// Checked in this order: no wet marks, non-finite moments, too few for a free
/// fit, degenerate observed.
using HwmStatsError = std::variant<NoWetMarks, NonFiniteMoments,
                                   TooFewForFreeFit, DegenerateObserved>;

/// Statistics from moments already folded; `total` is `wet.seen()`.
[[nodiscard]] std::expected<HwmStats, HwmStatsError> hwm_stats(
    const Moments& wet, Intercept mode) noexcept;

/// Statistics of the wet marks, with the requested fit. The marks are folded
/// in order, so the result is reproducible bit for bit.
[[nodiscard]] std::expected<HwmStats, HwmStatsError> hwm_stats(
    std::span<const HighWaterMark> marks, Intercept mode) noexcept;

}  // namespace mov::core
