// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Private to src/core: the one value-mapping path of scale_offset, convert and
// shift, so the three treat Missing, Dry and overflow alike.

#pragma once

#include <algorithm>
#include <utility>

#include "core_access.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/sample.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"

namespace mov::core::detail {

/// a applied to one sample: Missing and Dry unchanged; a value that maps to a
/// non-finite number becomes Missing.
[[nodiscard]] inline Sample apply_affine(const Affine& a, Sample s) noexcept {
  if (const auto v = s.value()) {
    return finite_or_missing(a(*v));
  }
  return s;
}

/// The parts of `s` with a applied to the samples. The identity Affine is
/// recognized here, once, and leaves the samples as they are (so -0.0 keeps
/// its sign and no pass is made).
[[nodiscard]] inline TimeSeriesParts mapped(TimeSeries s, const Affine& a) {
  TimeSeriesParts parts = std::move(s).into_parts();
  if (not(a == Affine{})) {
    std::ranges::transform(
        parts.samples, parts.samples.begin(),
        [&a](Sample x) noexcept { return apply_affine(a, x); });
  }
  return parts;
}

/// The series made of parts whose invariant is the one TimeSeries had.
[[nodiscard]] inline TimeSeries assembled(TimeSeriesParts parts) {
  return TimeSeries{CoreAccess::key(), std::move(parts.times),
                    std::move(parts.samples), std::move(parts.meta)};
}

}  // namespace mov::core::detail
