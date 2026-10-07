// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <variant>

#include "mov/core/detail/numeric.hpp"
#include "mov/core/geo.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/units.hpp"

// High-water marks: a surveyed peak water level compared with a model's.
// The statistics over a set of marks are in hwm_stats.hpp.

namespace mov::core {

/// A model value at or below this is "no water here" (ADCIRC writes -99999).
/// It is compared with the raw number in the file's own unit, before any unit
/// is applied. The one rule for the statistics, the classes and the plots;
/// v4 used -999, -9999 and -900 in different places (N2). Only readers of
/// model output use it (design decision C9).
inline constexpr double dry_threshold = -999.0;

/// True for a raw model value that means dry. NaN is neither dry nor
/// meaningful; model_value rejects it.
[[nodiscard]] constexpr bool is_dry(double raw) noexcept {
  return not detail::is_nan(raw) and raw <= dry_threshold;
}

/// The largest elevation a mark may have, in metres, above or below the
/// datum. Far beyond any real water level (Everest is 8849 m) and small enough
/// that no square, product or sum of marks can overflow a double.
inline constexpr double max_elevation_m = 1e4;

enum class ElevationError : std::uint8_t { not_finite, out_of_range };

/// The boundary for observed and ground elevations (and the wet half of
/// model_value): finite and at most max_elevation_m in magnitude, in metres.
/// A reader of an HWM file must build every Length through this, so the
/// statistics never see a value that can overflow.
[[nodiscard]] constexpr std::expected<Length, ElevationError> checked_elevation(
    double raw, LengthUnit unit) noexcept {
  if (not detail::is_finite(raw)) {
    return std::unexpected{ElevationError::not_finite};
  }
  // No unit factor exceeds 1852, so this keeps the conversion below finite.
  if (detail::magnitude(raw) > 1e300) {
    return std::unexpected{ElevationError::out_of_range};
  }
  const Length v = Length::in(raw, unit);
  if (detail::magnitude(v.as(LengthUnit::meter)) > max_elevation_m) {
    return std::unexpected{ElevationError::out_of_range};
  }
  return v;
}

/// A model point with water on it.
struct Wet {
  Length elevation;
  friend constexpr bool operator==(const Wet&, const Wet&) = default;
};

/// What the model said at a mark: a water level, or no water. Never a
/// sentinel number.
using WetDry = std::variant<Wet, Dry>;

/// Applies the dry rule to a raw value in `unit`: a value at or below
/// dry_threshold is Dry (whatever its size, including -infinity and the fill
/// values -99999 and -DBL_MAX); anything else must pass checked_elevation.
/// The dry rule comes first, so a huge negative fill is dry, not an error;
/// NaN and +infinity are not_finite.
[[nodiscard]] constexpr std::expected<WetDry, ElevationError> model_value(
    double raw, LengthUnit unit) noexcept {
  if (is_dry(raw)) {
    return WetDry{Dry{}};
  }
  return checked_elevation(raw, unit).transform(
      [](Length elevation) { return WetDry{Wet{.elevation = elevation}}; });
}

/// One surveyed mark and the model's value there. Statistics treat observed as
/// the independent variable and modeled as the dependent one. The lengths
/// come from checked_elevation / model_value; a mark built any other way with
/// a non-finite or enormous value makes hwm_stats report NonFiniteMoments and
/// is outside classify's contract.
struct HighWaterMark {
  Location location;
  Length ground;
  Length observed;
  WetDry modeled;
  friend constexpr bool operator==(const HighWaterMark&,
                                   const HighWaterMark&) = default;
};

/// modeled - observed, or nullopt when the model is dry there (v4 returned
/// the sentinel difference, a huge negative error).
[[nodiscard]] constexpr std::optional<Length> modeled_error(
    const HighWaterMark& h) noexcept {
  if (const Wet* wet = std::get_if<Wet>(&h.modeled)) {
    return wet->elevation - h.observed;
  }
  return std::nullopt;
}

namespace detail {

/// Class breaks and errors are compared on a 1 nm grid: the metres value times
/// 1e9, rounded half away from zero, after saturating at +-1e6 m (monotone, and
/// it keeps the product within round_half_away's range). A decimal tie such as
/// 2.3 - 1.8 against 0.5 is 0.4999999999999998 in doubles, and a feet error
/// goes through 0.3048 on each side; rounding to the grid removes that noise,
/// which is about 1e-12 m at the largest allowed elevation, far below half a
/// nanometre. The input must be finite (asserted); in a release build a NaN is
/// saturated high rather than reaching the integer conversion.
[[nodiscard]] constexpr std::int64_t grid_nm(Length v) noexcept {
  constexpr double limit_m = 1e6;
  constexpr double nm_per_m = 1e9;
  const double metres = v.as(LengthUnit::meter);
  assert(is_finite(metres));
  const double below_top = metres < limit_m ? metres : limit_m;
  const double clamped = below_top < -limit_m ? -limit_m : below_top;
  return round_half_away(clamped * nm_per_m);
}

}  // namespace detail

enum class ClassBreaksError : std::uint8_t {
  not_finite,
  not_strictly_increasing
};

/// The seven error breaks that split wet marks into eight classes (v4's map
/// colours). Finite and strictly increasing on the 1 nm grid (breaks closer
/// than 1 nm, or both beyond +-1e6 m, are rejected), so classify is total.
/// There is no default ErrorClasses: breaks are stated, not assumed.
class ErrorClasses {
 public:
  using Breaks = std::array<Length, 7>;

  /// not_finite is reported before the order, so a NaN is never blamed on a
  /// neighbour.
  [[nodiscard]] static constexpr std::expected<ErrorClasses, ClassBreaksError>
  make(const Breaks& b) noexcept {
    if (not std::ranges::all_of(b, is_finite_length)) {
      return std::unexpected{ClassBreaksError::not_finite};
    }
    if (std::ranges::adjacent_find(b, std::ranges::greater_equal{},
                                   detail::grid_nm) != b.end()) {
      return std::unexpected{ClassBreaksError::not_strictly_increasing};
    }
    return ErrorClasses{b};
  }

  /// -5, -3.5, -1.5, 0, 1.5, 3.5, 5 ft.
  [[nodiscard]] static constexpr ErrorClasses feet_default() noexcept {
    return ErrorClasses{
        in(LengthUnit::foot, {-5.0, -3.5, -1.5, 0.0, 1.5, 3.5, 5.0})};
  }
  /// -1.5, -1, -0.5, 0, 0.5, 1, 1.5 m.
  [[nodiscard]] static constexpr ErrorClasses meters_default() noexcept {
    return ErrorClasses{
        in(LengthUnit::meter, {-1.5, -1.0, -0.5, 0.0, 0.5, 1.0, 1.5})};
  }

  /// The breaks, lowest first. The view points into this object, so a
  /// temporary ErrorClasses (const or not) is rejected.
  [[nodiscard]] constexpr std::span<const Length, 7> breaks() const& noexcept {
    return breaks_;
  }
  std::span<const Length, 7> breaks() const&& = delete;

  friend constexpr bool operator==(const ErrorClasses&,
                                   const ErrorClasses&) = default;

 private:
  explicit constexpr ErrorClasses(const Breaks& b) noexcept : breaks_{b} {}

  [[nodiscard]] static constexpr bool is_finite_length(Length v) noexcept {
    return detail::is_finite(v.as(LengthUnit::meter));
  }

  [[nodiscard]] static constexpr Breaks in(
      LengthUnit unit, const std::array<double, 7>& values) noexcept {
    Breaks out{};
    std::ranges::transform(values, out.begin(),
                           [unit](double v) { return Length::in(v, unit); });
    return out;
  }

  Breaks breaks_;
};

/// Where a mark falls: dry, or one of eight error classes. bin0 is an error
/// below the first break and bin7 an error at or above the last.
enum class HwmCategory : std::uint8_t {
  dry,
  bin0,
  bin1,
  bin2,
  bin3,
  bin4,
  bin5,
  bin6,
  bin7
};

/// The class of a mark. An error on break i goes to the class above it (v4's
/// `e < c[i]` is strict); upper_bound finds the first break greater than the
/// error, so the index counts the breaks at or below it.
///
/// The comparison is on the 1 nm grid (detail::grid_nm), so the tie rule holds
/// for decimal data: exact for inputs with at most 9 decimal places in metres
/// or 5 in feet or inches, whatever the unit, which is what a survey or a
/// model writes. v4 compared raw doubles and put some ties below the break
/// (observed -9.94 ft, modeled -6.44 ft is 3.499999999999999, under 3.5).
/// Only the comparison is rounded; no statistic is.
///
/// Precondition: the mark's lengths are finite (build them with
/// checked_elevation / model_value); asserted in debug builds.
[[nodiscard]] constexpr HwmCategory classify(const HighWaterMark& h,
                                             const ErrorClasses& c) noexcept {
  constexpr std::array bins{HwmCategory::bin0, HwmCategory::bin1,
                            HwmCategory::bin2, HwmCategory::bin3,
                            HwmCategory::bin4, HwmCategory::bin5,
                            HwmCategory::bin6, HwmCategory::bin7};
  const std::optional<Length> error = modeled_error(h);
  if (not error) {
    return HwmCategory::dry;
  }
  const auto breaks = c.breaks();
  const auto above = std::ranges::upper_bound(
      breaks, detail::grid_nm(*error), std::ranges::less{}, detail::grid_nm);
  return bins[static_cast<std::size_t>(above - breaks.begin())];
}

}  // namespace mov::core
