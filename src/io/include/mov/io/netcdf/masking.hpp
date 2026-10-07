// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <concepts>
#include <optional>
#include <vector>

#include "mov/core/sample.hpp"
#include "mov/io/netcdf/types.hpp"

namespace mov::io::nc {

namespace detail {

/// v as a double, without a cast GCC calls useless when T is double.
template <Numeric T>
[[nodiscard]] constexpr double widen(T v) noexcept {
  if constexpr (std::same_as<T, double>) {
    return v;
  } else {
    return static_cast<double>(v);
  }
}

}  // namespace detail

/// The CF missing-data rules of one variable (SN section 8.1, C9), held in
/// the variable's own type T so every comparison is exact (B4, B9): a float
/// fill of -99999f is compared as a float, never as a double that happens to
/// be close. File::masking builds it from the attributes.
template <Numeric T>
struct Masking {
  /// _FillValue, else the library's default fill for the type unless the
  /// variable is NC_NOFILL (none for byte, which has no default fill by
  /// convention).
  std::optional<T> fill{};
  /// missing_value: a scalar or a vector.
  std::vector<T> missing_values{};
  /// valid_min and valid_max, or the two ends of valid_range.
  std::optional<T> valid_min{};
  std::optional<T> valid_max{};
  /// scale_factor and add_offset: applied after masking, to the widened
  /// value (CF section 8.1).
  std::optional<double> scale{};
  std::optional<double> offset{};

  /// Whether `raw` is a missing value by the attributes. NaN never equals
  /// anything, so it is caught by apply's finiteness check instead.
  [[nodiscard]] constexpr bool masks(T raw) const noexcept {
    return (fill and raw == *fill) or
           std::ranges::find(missing_values, raw) != missing_values.end() or
           (valid_min and raw < *valid_min) or (valid_max and raw > *valid_max);
  }

  /// Missing when `raw` is masked or not finite, or when unpacking makes it
  /// not finite; else the unpacked value. A factor or offset that is absent
  /// is not applied at all (so -0.0 stays -0.0).
  [[nodiscard]] constexpr core::Sample apply(T raw) const noexcept {
    if (masks(raw)) {
      return core::Missing{};
    }
    const double wide = detail::widen(raw);
    const double scaled = scale ? wide * *scale : wide;
    return core::finite_or_missing(offset ? scaled + *offset : scaled);
  }

  friend constexpr bool operator==(const Masking&, const Masking&) = default;
};

}  // namespace mov::io::nc
