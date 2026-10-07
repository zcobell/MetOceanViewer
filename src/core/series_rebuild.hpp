// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Private to src/core: the value mapping shared by scale_offset, convert and
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

/// a applied to a value; Missing and Dry unchanged; a result that is not
/// finite becomes Missing. The identity Affine returns every sample as it is
/// (so -0.0 keeps its sign).
[[nodiscard]] inline Sample apply_affine(const Affine& a, Sample s) noexcept {
  if (a == Affine{}) {
    return s;
  }
  if (const auto v = s.value()) {
    return finite_or_missing(a(*v));
  }
  return s;
}

/// The series with a applied to its values and `meta` as its metadata. The
/// caller says how meta relates to the values (a unit or datum rewrite).
[[nodiscard]] inline TimeSeries rebuilt(TimeSeries s, SeriesMeta meta,
                                        const Affine& a) {
  TimeSeriesParts parts = std::move(s).into_parts();
  std::ranges::transform(
      parts.samples, parts.samples.begin(),
      [&a](Sample x) noexcept { return apply_affine(a, x); });
  return TimeSeries{CoreAccess::key(), std::move(parts.times),
                    std::move(parts.samples), std::move(meta)};
}

}  // namespace mov::core::detail
