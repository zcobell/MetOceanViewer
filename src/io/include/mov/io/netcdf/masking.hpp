// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
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

/// `v` as a T if T holds exactly that value, else nullopt.
template <Numeric T>
[[nodiscard]] constexpr std::optional<T> exact_from(std::int64_t v) noexcept {
  if constexpr (std::same_as<T, std::int64_t>) {
    return v;
  } else if constexpr (std::integral<T>) {
    return std::in_range<T>(v) ? std::optional{static_cast<T>(v)}
                               : std::nullopt;
  } else {
    const auto t = static_cast<T>(v);  // rounds to nearest
    // 2^63 is the first T beyond int64; casting it back would be undefined.
    constexpr auto beyond = static_cast<T>(9223372036854775808.0);
    if (not(t < beyond)) {
      return std::nullopt;
    }
    return static_cast<std::int64_t>(t) == v ? std::optional{t} : std::nullopt;
  }
}

/// `v` as a T if T holds exactly that value (NaN and the infinities are
/// exact in float), else nullopt.
template <Numeric T>
[[nodiscard]] constexpr std::optional<T> exact_from(double v) noexcept {
  if constexpr (std::same_as<T, double>) {
    return v;
  } else if constexpr (std::same_as<T, float>) {
    // NaN and the infinities are returned as float constants, and the cast
    // only ever sees a value clamped into float's range: MSVC's /O2 folds the
    // cast on paths that are never taken, and a constant out of range there
    // is C4756 (overflow in constant arithmetic). A clamped value that differs
    // from v fails the exactness test below.
    using F = std::numeric_limits<float>;
    constexpr double inf = std::numeric_limits<double>::infinity();
    constexpr auto most = static_cast<double>(F::max());
    if (v != v) {
      return F::quiet_NaN();
    }
    if (v == inf) {
      return F::infinity();
    }
    if (v == -inf) {
      return -F::infinity();
    }
    const auto f = static_cast<float>(std::clamp(v, -most, most));
    return static_cast<double>(f) == v ? std::optional{f} : std::nullopt;
  } else {
    // [-2^(n-1), 2^(n-1)): both ends are exact doubles; NaN fails here.
    constexpr auto lowest = static_cast<double>(std::numeric_limits<T>::min());
    if (not(v >= lowest and v < -lowest)) {
      return std::nullopt;
    }
    const auto t = static_cast<T>(v);  // truncates
    return static_cast<double>(t) == v ? std::optional{t} : std::nullopt;
  }
}

}  // namespace detail

/// The CF missing-data rules of one variable (SN section 8.1), held in the
/// variable's own type T so every comparison is exact: a float
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
