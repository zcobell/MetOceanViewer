// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>

#include "mov/core/sample.hpp"
#include "mov/core/station_table.hpp"
#include "mov/core/time.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"

// Operations on series: summaries (Bucket, extent, quick_stats), the
// observed-minus-predicted residual, and the ones that change a series
// (slice, shift_time, scale_offset, convert). The datum shift is in
// datum_shift.hpp.

namespace mov::core {

// ---- Bucket: the summary and decimation monoid -----------------------------

/// A value and the time of the sample that has it.
struct Extreme {
  double value;
  Time time;
  friend constexpr bool operator==(Extreme, Extreme) = default;
};

namespace detail {

/// The lower of two extremes by value. On a tie `a` (the left operand) wins,
/// so a left fold keeps the first of equal minima. An absent side yields the
/// other.
[[nodiscard]] constexpr std::optional<Extreme> lower_extreme(
    const std::optional<Extreme>& a, const std::optional<Extreme>& b) noexcept {
  if (not a) {
    return b;
  }
  return (b and b->value < a->value) ? b : a;
}

[[nodiscard]] constexpr std::optional<Extreme> upper_extreme(
    const std::optional<Extreme>& a, const std::optional<Extreme>& b) noexcept {
  if (not a) {
    return b;
  }
  return (b and b->value > a->value) ? b : a;
}

}  // namespace detail

/// What a run of consecutive samples contains: how many are values, missing or
/// dry, the smallest and largest value with the time it occurred, and the sum
/// of the values. quick_stats and Phase 5 decimation (one Bucket per pixel
/// column) share it.
///
/// A monoid under `+` with the default Bucket as identity (exactly). `+` is
/// associative but not commutative: a tie between equal minima (or maxima)
/// keeps the left operand's, so folding left to right in time order yields the
/// FIRST occurrence, as std::min_element and std::max_element do. The counts
/// and extremes are exact; the sum is associative only up to rounding, so the
/// grouping is part of the result and summarize folds strictly left to right.
/// The sum of values that overflow a double is infinite.
class Bucket {
 private:
  // Every field in one place, no default member values: a constructor that
  // forgets one fails to compile (-Wmissing-field-initializers).
  struct Fields {
    std::size_t values;
    std::size_t missing;
    std::size_t dry;
    std::optional<Extreme> min;
    std::optional<Extreme> max;
    double sum;
    friend constexpr bool operator==(const Fields&, const Fields&) = default;
  };

 public:
  constexpr Bucket() noexcept : fields_{} {}  // the identity: nothing seen

  /// One sample: a value is its own minimum and maximum; Missing and Dry only
  /// add to their counts.
  [[nodiscard]] static constexpr Bucket of(Time t, Sample s) noexcept {
    if (const std::optional<double> v = s.value()) {
      const Extreme e{.value = *v, .time = t};
      return Bucket{Fields{
          .values = 1, .missing = 0, .dry = 0, .min = e, .max = e, .sum = *v}};
    }
    return Bucket{Fields{.values = 0,
                         .missing = s.is_missing() ? std::size_t{1} : 0,
                         .dry = s.is_dry() ? std::size_t{1} : 0,
                         .min = std::nullopt,
                         .max = std::nullopt,
                         .sum = 0.0}};
  }

  [[nodiscard]] friend constexpr Bucket operator+(const Bucket& a,
                                                  const Bucket& b) noexcept {
    return Bucket{
        Fields{.values = a.fields_.values + b.fields_.values,
               .missing = a.fields_.missing + b.fields_.missing,
               .dry = a.fields_.dry + b.fields_.dry,
               .min = detail::lower_extreme(a.fields_.min, b.fields_.min),
               .max = detail::upper_extreme(a.fields_.max, b.fields_.max),
               .sum = a.fields_.sum + b.fields_.sum}};
  }

  [[nodiscard]] constexpr std::size_t values() const noexcept {
    return fields_.values;
  }
  [[nodiscard]] constexpr std::size_t missing() const noexcept {
    return fields_.missing;
  }
  [[nodiscard]] constexpr std::size_t dry() const noexcept {
    return fields_.dry;
  }
  /// The first smallest value; nullopt when there is no value.
  [[nodiscard]] constexpr std::optional<Extreme> min() const noexcept {
    return fields_.min;
  }
  /// The first largest value; nullopt when there is no value.
  [[nodiscard]] constexpr std::optional<Extreme> max() const noexcept {
    return fields_.max;
  }
  [[nodiscard]] constexpr double sum() const noexcept { return fields_.sum; }
  /// Some sample is Missing or Dry: a decimated line draws a break here.
  [[nodiscard]] constexpr bool has_gap() const noexcept {
    return fields_.missing + fields_.dry > 0;
  }

  friend constexpr bool operator==(const Bucket&, const Bucket&) = default;

 private:
  explicit constexpr Bucket(Fields f) noexcept : fields_{f} {}

  Fields fields_;
};

/// The Bucket of the whole series: an ordered left fold over its samples.
[[nodiscard]] Bucket summarize(const TimeSeries& s);

// ---- Extent ----

/// The smallest and largest value of a series.
struct ValueRange {
  double min;
  double max;
  friend constexpr bool operator==(ValueRange, ValueRange) = default;
};

/// The span of a series in time and, if it has a value, in value (B13, B14).
struct Extent {
  Time first;
  Time last;
  std::optional<ValueRange> values;
  friend constexpr bool operator==(const Extent&, const Extent&) = default;
};

/// Joins two extents: nullopt is the identity, and the result is the smallest
/// box holding both. Associative, commutative and idempotent, and exact (a
/// semilattice), so any grouping gives the same answer.
[[nodiscard]] constexpr std::optional<Extent> combine(
    const std::optional<Extent>& a, const std::optional<Extent>& b) noexcept {
  if (not a) {
    return b;
  }
  if (not b) {
    return a;
  }
  std::optional<ValueRange> values = a->values;
  if (a->values and b->values) {
    values =
        ValueRange{.min = b->values->min < a->values->min ? b->values->min
                                                          : a->values->min,
                   .max = b->values->max > a->values->max ? b->values->max
                                                          : a->values->max};
  } else if (b->values) {
    values = b->values;
  }
  return Extent{.first = b->first < a->first ? b->first : a->first,
                .last = b->last > a->last ? b->last : a->last,
                .values = values};
}

/// nullopt iff the series is empty. A series with only Missing and Dry
/// samples has times and no `values`.
[[nodiscard]] std::optional<Extent> extent(const TimeSeries& s);

/// The join of the extents of all series; nullopt iff every series is empty
/// (or there are none).
[[nodiscard]] std::optional<Extent> extent(std::span<const TimeSeries> all);

// ---- Quick statistics ----

struct ValueStats {
  Extreme min;  // the first minimum
  Extreme max;  // the first maximum: the "peak"
  double mean;
  friend constexpr bool operator==(const ValueStats&,
                                   const ValueStats&) = default;
};

/// Counts and, when the series has a value, its extremes and mean.
struct QuickStats {
  std::size_t values;
  std::size_t missing;
  std::size_t dry;
  std::optional<ValueStats> stats;
  friend constexpr bool operator==(const QuickStats&,
                                   const QuickStats&) = default;
};

/// Total: an empty series, or one without a value, has no `stats`. The mean
/// is finite even when the plain sum would overflow.
[[nodiscard]] QuickStats quick_stats(const TimeSeries& s);

// ---- Residual ----

/// Why observed - predicted is not defined, in the order the checks run.
enum class ResidualErrc : std::uint8_t {
  unit_unknown,            // a series has no unit
  units_differ,            // the units are not equal
  temperature_difference,  // equal temperature units (a difference of two
                           // temperatures is not a temperature)
  datum_unknown,           // a datum-carrying series has no datum
  datums_differ,           // the datums are not equal
};

/// Observed minus predicted at exactly the times both have (a merge-join,
/// O(n + m); a time in only one series is dropped, and no common time gives an
/// empty series). The units must be known and equal and not temperatures. If
/// either quantity can carry a datum (datum_applicable), both datums must be
/// engaged and equal. The result is the generic `value` quantity in the common
/// unit with no datum, labelled "<observed> - <predicted>" with a U+2212 minus
/// sign; the samples follow the combine rule (Missing, then Dry, then the
/// difference).
[[nodiscard]] std::expected<TimeSeries, ResidualErrc> residual(
    const ObsVsPred& pair);

// ---- Changing a series ----

/// The samples with time in [r.begin(), r.end()): two binary searches. The
/// rvalue overload erases in place.
[[nodiscard]] TimeSeries slice(const TimeSeries& s, TimeRange r);
[[nodiscard]] TimeSeries slice(TimeSeries&& s, TimeRange r);

/// The first sample whose shifted time leaves +-max_abs_time_ms (or overflows
/// 64 bits).
struct TimeOverflow {
  std::size_t index;
  friend constexpr bool operator==(TimeOverflow, TimeOverflow) = default;
};

/// Every time moved by dt. All shifted times must stay within
/// +-max_abs_time_ms; otherwise nothing changes and the error names the first
/// sample that leaves the range.
[[nodiscard]] std::expected<TimeSeries, TimeOverflow> shift_time(
    TimeSeries s, std::chrono::milliseconds dt);

/// y = a.scale * x + a.offset on every value (v4's multiplier and y shift).
/// Missing and Dry stay; a result that is not finite becomes Missing. The unit
/// and datum in the metadata are unchanged: this is a calibration, not a unit
/// conversion (see convert).
[[nodiscard]] TimeSeries scale_offset(TimeSeries s, Affine a);

/// The series in unit `to`: the values converted, the unit rewritten, Missing
/// and Dry kept, a result that is not finite becomes Missing. UnknownUnit if
/// the series has no unit; IncompatibleUnits across families, or between
/// unequal OtherUnits. An equal unit returns the series unchanged.
[[nodiscard]] std::expected<TimeSeries, UnitError> convert(TimeSeries s,
                                                           const Unit& to);

/// The same for one column of a table, at every station. Precondition:
/// column.value() < t.schema().size().
[[nodiscard]] std::expected<StationTable, UnitError> convert(StationTable t,
                                                             ColumnIndex column,
                                                             const Unit& to);

}  // namespace mov::core
