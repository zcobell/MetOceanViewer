// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
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
/// meaningful: a reader must reject it before it gets here.
[[nodiscard]] constexpr bool is_dry(double raw) noexcept {
  return raw <= dry_threshold;
}

/// A model point with water on it.
struct Wet {
  Length elevation;
  friend constexpr bool operator==(const Wet&, const Wet&) = default;
};

/// What the model said at a mark: a water level, or no water. Never a
/// sentinel number.
using WetDry = std::variant<Wet, Dry>;

/// Applies the dry rule to a raw value in `unit`. A reader must not pass NaN
/// or +infinity (a non-finite value is a parse error, not a water level); here
/// they come out as Wet and then poison anything that sums them.
[[nodiscard]] constexpr WetDry model_value(double raw,
                                           LengthUnit unit) noexcept {
  if (is_dry(raw)) {
    return Dry{};
  }
  return Wet{.elevation = Length::in(raw, unit)};
}

/// One surveyed mark and the model's value there. `observed` and `ground` are
/// finite (a reader guarantees it). Statistics treat observed as the
/// independent variable and modeled as the dependent one.
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

enum class ClassBreaksError : std::uint8_t {
  not_finite,
  not_strictly_increasing
};

/// The seven error breaks that split wet marks into eight classes (v4's map
/// colours). Finite and strictly increasing, so classify is total. There is no
/// default ErrorClasses: breaks are stated, not assumed.
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
    const auto not_rising = [](const Length& lo, const Length& hi) {
      return not(lo < hi);
    };
    if (std::ranges::adjacent_find(b, not_rising) != b.end()) {
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
  /// temporary ErrorClasses is rejected.
  [[nodiscard]] constexpr std::span<const Length, 7> breaks() const& noexcept {
    return breaks_;
  }
  std::span<const Length, 7> breaks() && = delete;

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

/// The class of a mark. An error exactly on break i goes to the class above
/// it (v4's `e < c[i]` is strict); upper_bound finds the first break greater
/// than the error, so the index counts the breaks at or below it. Exact for
/// errors that are exact in metres; an error in feet that sits on a break in
/// decimal can land either side (see docs/wp-notes/WP4.md). A NaN error (a
/// reader's bug) falls in bin7 rather than being undefined.
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
  const auto above = std::ranges::upper_bound(breaks, *error);
  return bins[static_cast<std::size_t>(above - breaks.begin())];
}

}  // namespace mov::core
