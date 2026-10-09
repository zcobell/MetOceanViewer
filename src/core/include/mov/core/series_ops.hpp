// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <type_traits>

#include "mov/core/detail/numeric.hpp"
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

namespace detail {

/// Exact powers of two for the scaled sum of a Bucket.
inline constexpr double two_pow_64 = 0x1p64;
inline constexpr double two_pow_minus_64 = 0x1p-64;

/// The join of two optionals that each hold at most one fact: the one that is
/// there if the other is not, else f of both (the left argument first).
/// nullopt is the identity of the join.
template <class T, class F>
  requires std::same_as<std::invoke_result_t<F&, const T&, const T&>, T>
[[nodiscard]] constexpr std::optional<T> join_optional(
    const std::optional<T>& a, const std::optional<T>& b, F f) {
  if (not a) {
    return b;
  }
  if (not b) {
    return a;
  }
  return f(*a, *b);
}

}  // namespace detail

// ---- Bucket: the summary and decimation monoid -----------------------------

/// A value and the time of the sample that has it.
struct Extreme {
  double value;
  Time time;
  friend constexpr bool operator==(Extreme, Extreme) = default;
};

class Bucket;

/// What a Bucket knows once it has seen at least one value: how many, the
/// first smallest and first largest value (with their times), the first and
/// last value in time order, and the sum.
///
/// The sum is carried scaled by 2^-64 (exact: a power of two), so joining
/// buckets cannot overflow or make a NaN even for values near 1e308 with
/// opposite signs. mean() is therefore finite for any finite values and
/// bit-identical to sum() / count() wherever that is finite (values smaller
/// than about 1e-288 in magnitude lose bits in the scaling). sum() itself is
/// the scaled sum times 2^64 and is infinite if the true sum is out of range.
///
/// Built only by Bucket.
class ValueSummary {
 public:
  [[nodiscard]] constexpr std::size_t count() const noexcept {
    return f_.count;
  }
  /// The first of the smallest values.
  [[nodiscard]] constexpr Extreme min() const noexcept { return f_.min; }
  /// The first of the largest values.
  [[nodiscard]] constexpr Extreme max() const noexcept { return f_.max; }
  /// The earliest and the latest value.
  [[nodiscard]] constexpr Extreme first() const noexcept { return f_.first; }
  [[nodiscard]] constexpr Extreme last() const noexcept { return f_.last; }
  /// May be infinite.
  [[nodiscard]] constexpr double sum() const noexcept {
    return f_.scaled_sum * detail::two_pow_64;
  }
  [[nodiscard]] constexpr double mean() const noexcept {
    return (f_.scaled_sum / static_cast<double>(f_.count)) * detail::two_pow_64;
  }

  friend constexpr bool operator==(const ValueSummary&,
                                   const ValueSummary&) = default;

 private:
  friend class Bucket;

  struct Fields {
    std::size_t count;
    Extreme min;
    Extreme max;
    Extreme first;
    Extreme last;
    double scaled_sum;
    friend constexpr bool operator==(const Fields&, const Fields&) = default;
  };

  explicit constexpr ValueSummary(const Fields& f) noexcept : f_{f} {}

  [[nodiscard]] static constexpr ValueSummary of(Time t, double v) noexcept {
    const Extreme e{.value = v, .time = t};
    return ValueSummary{Fields{.count = 1,
                               .min = e,
                               .max = e,
                               .first = e,
                               .last = e,
                               .scaled_sum = v * detail::two_pow_minus_64}};
  }

  /// a then b in time. Ties keep a's extreme (ranges::min and max return
  /// their first argument when equivalent); first is a's, last is b's.
  [[nodiscard]] static constexpr ValueSummary joined(
      const ValueSummary& a, const ValueSummary& b) noexcept {
    return ValueSummary{
        Fields{.count = a.f_.count + b.f_.count,
               .min = std::ranges::min(a.f_.min, b.f_.min, {}, &Extreme::value),
               .max = std::ranges::max(a.f_.max, b.f_.max, {}, &Extreme::value),
               .first = a.f_.first,
               .last = b.f_.last,
               .scaled_sum = a.f_.scaled_sum + b.f_.scaled_sum}};
  }

  Fields f_;
};

/// What a run of consecutive samples contains: how many are Missing or Dry,
/// and, if there is a value, its ValueSummary. quick_stats and the chart
/// decimation (one Bucket per pixel column) share it.
///
/// A monoid under `+` with the default Bucket as identity (exactly). `+` is
/// associative but not commutative: it is a join of time-ordered runs. A tie
/// between equal minima (or maxima) keeps the left operand's, so folding left
/// to right in time order yields the FIRST occurrence, as std::min_element and
/// std::max_element do; first() is the left operand's and last() the right's.
/// Counts and extremes are exact; the sum is associative only up to rounding,
/// so the grouping is part of the result and summarize folds strictly left to
/// right.
class Bucket {
 private:
  // Every field in one place, no default member values: a constructor that
  // forgets one fails to compile (-Wmissing-field-initializers).
  struct Fields {
    std::size_t missing;
    std::size_t dry;
    std::optional<ValueSummary> values;
    friend constexpr bool operator==(const Fields&, const Fields&) = default;
  };

 public:
  constexpr Bucket() noexcept : fields_{} {}  // the identity: nothing seen

  /// One sample: a value is its own minimum, maximum, first and last;
  /// Missing and Dry only add to their counts. (Not noexcept: Sample::visit
  /// is std::visit.)
  [[nodiscard]] static constexpr Bucket of(Time t, Sample s) {
    return s.visit(
        [t](double v) {
          return Bucket{
              Fields{.missing = 0, .dry = 0, .values = ValueSummary::of(t, v)}};
        },
        [](Missing) {
          return Bucket{Fields{.missing = 1, .dry = 0, .values = std::nullopt}};
        },
        [](Dry) {
          return Bucket{Fields{.missing = 0, .dry = 1, .values = std::nullopt}};
        });
  }

  [[nodiscard]] friend constexpr Bucket operator+(const Bucket& a,
                                                  const Bucket& b) noexcept {
    return Bucket{
        Fields{.missing = a.fields_.missing + b.fields_.missing,
               .dry = a.fields_.dry + b.fields_.dry,
               .values = join_values(a.fields_.values, b.fields_.values)}};
  }

  /// The number of values.
  [[nodiscard]] constexpr std::size_t values() const noexcept {
    return fields_.values ? fields_.values->count() : 0;
  }
  [[nodiscard]] constexpr std::size_t missing() const noexcept {
    return fields_.missing;
  }
  [[nodiscard]] constexpr std::size_t dry() const noexcept {
    return fields_.dry;
  }
  /// nullopt iff there is no value.
  [[nodiscard]] constexpr std::optional<ValueSummary> summary() const noexcept {
    return fields_.values;
  }
  /// Some sample is Missing or Dry: a decimated line draws a break here.
  [[nodiscard]] constexpr bool has_gap() const noexcept {
    return fields_.missing + fields_.dry > 0;
  }

  friend constexpr bool operator==(const Bucket&, const Bucket&) = default;

 private:
  explicit constexpr Bucket(const Fields& f) noexcept : fields_{f} {}

  // A member, not inline in operator+: MSVC does not extend Bucket's
  // friendship with ValueSummary to Bucket's hidden friends.
  [[nodiscard]] static constexpr std::optional<ValueSummary> join_values(
      const std::optional<ValueSummary>& a,
      const std::optional<ValueSummary>& b) noexcept {
    return detail::join_optional(a, b, ValueSummary::joined);
  }

  Fields fields_;
};

/// The Bucket of parallel spans of times and samples (equal lengths, a
/// precondition): an ordered left fold. The chart decimation summarizes the
/// sub-spans between pixel-column edges with this.
[[nodiscard]] Bucket summarize(std::span<const Time> times,
                               std::span<const Sample> samples);

/// The Bucket of the whole series.
[[nodiscard]] Bucket summarize(const TimeSeries& s);

// ---- Extent
// ------------------------------------------------------------------

/// The smallest and largest value of a series.
struct ValueRange {
  double min;
  double max;
  friend constexpr bool operator==(ValueRange, ValueRange) = default;
};

/// The span of a series in time and, if it has a value, in value (v4 began
/// with min and max inverted and dereferenced end() of an empty series).
struct Extent {
  Time first;
  Time last;
  std::optional<ValueRange> values;
  friend constexpr bool operator==(const Extent&, const Extent&) = default;
};

namespace detail {

[[nodiscard]] constexpr ValueRange join_ranges(const ValueRange& a,
                                               const ValueRange& b) noexcept {
  return {.min = std::ranges::min(a.min, b.min),
          .max = std::ranges::max(a.max, b.max)};
}

[[nodiscard]] constexpr Extent join_extents(const Extent& a,
                                            const Extent& b) noexcept {
  return {.first = std::ranges::min(a.first, b.first),
          .last = std::ranges::max(a.last, b.last),
          .values = join_optional(a.values, b.values, join_ranges)};
}

}  // namespace detail

/// Joins two extents: nullopt is the identity, and the result is the smallest
/// box holding both. Associative, commutative and idempotent, and exact (a
/// semilattice), so any grouping gives the same answer.
[[nodiscard]] constexpr std::optional<Extent> join(
    const std::optional<Extent>& a, const std::optional<Extent>& b) noexcept {
  return detail::join_optional(a, b, detail::join_extents);
}

/// nullopt iff the series is empty. A series with only Missing and Dry
/// samples has times and no `values`.
[[nodiscard]] std::optional<Extent> extent(const TimeSeries& s);

/// The join of the extents of all series; nullopt iff every series is empty
/// (or there are none).
[[nodiscard]] std::optional<Extent> extent(std::span<const TimeSeries> all);

// ---- Quick statistics
// ------------------------------------------------------------

struct ValueStats {
  std::size_t count;  // at least 1
  Extreme min;        // the first minimum
  Extreme max;        // the first maximum: the "peak"
  double mean;        // finite
  friend constexpr bool operator==(const ValueStats&,
                                   const ValueStats&) = default;
};

/// Counts and, when the series has a value, its extremes and mean. The number
/// of values lives in `stats`, so "no stats but some values" cannot be
/// written.
struct QuickStats {
  std::size_t missing;
  std::size_t dry;
  std::optional<ValueStats> stats;

  [[nodiscard]] constexpr std::size_t values() const noexcept {
    return stats ? stats->count : 0;
  }
  friend constexpr bool operator==(const QuickStats&,
                                   const QuickStats&) = default;
};

/// Total: an empty series, or one without a value, has no `stats`. The mean
/// is finite for any finite values.
[[nodiscard]] QuickStats quick_stats(const TimeSeries& s);

// ---- Residual
// -----------------------------------------------------------------------

/// Why observed - predicted is not defined, in the order the checks run.
enum class ResidualErrc : std::uint8_t {
  quantities_differ,       // not the same quantity, nor an observed and its
                           // prediction
  unit_unknown,            // a series has no unit
  units_differ,            // the units are not equal
  temperature_difference,  // equal temperature units (a difference of two
                           // temperatures is not a temperature)
  datum_unknown,           // a datum-carrying series has no datum
  datums_differ,           // the datums are not equal
};

/// Observed minus predicted at exactly the times both have (a merge-join,
/// O(n + m); a time in only one series is dropped, and no common time gives an
/// empty series).
///
/// The quantities must be equal (a registry quantity, or a generic one with
/// the same token) or the pair water_level (observed) and
/// water_level_prediction. The units must be known and equal and not
/// temperatures. If the quantity can carry a datum (datum_applicable), both
/// datums must be engaged and equal.
///
/// The result is the `difference` quantity in the common unit with no datum,
/// labelled "<observed> - <predicted>" with a U+2212 minus sign; the samples
/// follow the combine rule (Missing, then Dry, then the difference). A
/// `difference` is not datum_applicable, so it cannot be shifted; two
/// differences are the same quantity, so residual accepts them.
[[nodiscard]] std::expected<TimeSeries, ResidualErrc> residual(
    const ObsVsPred& pair);

// ---- Changing a series
// -----------------------------------------------------------------

/// A half-open window [lo, hi) of indices.
struct Window {
  std::size_t lo;
  std::size_t hi;
  friend constexpr bool operator==(Window, Window) = default;
};

/// The indices of the times in [r.begin(), r.end()): two binary searches over
/// strictly increasing times. The chart decimation uses this and summarize to
/// summarize pixel columns without copying.
[[nodiscard]] Window index_window(std::span<const Time> times, TimeRange r);

/// The samples with time in [r.begin(), r.end()): index_window and a copy. The
/// rvalue overload erases in place.
[[nodiscard]] TimeSeries slice(const TimeSeries& s, TimeRange r);
[[nodiscard]] TimeSeries slice(TimeSeries&& s, TimeRange r);

/// The first sample whose shifted time overflows 64 bits.
struct TimeOverflow {
  std::size_t index;
  friend constexpr bool operator==(TimeOverflow, TimeOverflow) = default;
};

/// Every time moved by dt. The only limit is the 64-bit range of the
/// milliseconds: otherwise nothing changes and the error names the first
/// sample that overflows. (The +-2^53 ms file bound is StationTable's.)
/// shift_time(s, 0ms) is s for every TimeSeries.
[[nodiscard]] std::expected<TimeSeries, TimeOverflow> shift_time(
    TimeSeries s, std::chrono::milliseconds dt);

/// y = scale * x + offset with both finite: v4's multiplier and y shift. A
/// calibration of the values; unlike convert and shift it records nothing in
/// the metadata.
class Calibration {
 public:
  struct Coefficients {
    double scale;
    double offset;
  };

  /// The identity: scale 1, offset 0.
  constexpr Calibration() noexcept = default;

  /// nullopt if either coefficient is NaN or infinite. (Zero is allowed.)
  [[nodiscard]] static constexpr std::optional<Calibration> make(
      Coefficients c) noexcept {
    if (not detail::is_finite(c.scale) or not detail::is_finite(c.offset)) {
      return std::nullopt;
    }
    return Calibration{c};
  }

  [[nodiscard]] constexpr double scale() const noexcept { return scale_; }
  [[nodiscard]] constexpr double offset() const noexcept { return offset_; }
  [[nodiscard]] constexpr Affine affine() const noexcept {
    return {.scale = scale_, .offset = offset_};
  }

  friend constexpr bool operator==(const Calibration&,
                                   const Calibration&) = default;

 private:
  explicit constexpr Calibration(Coefficients c) noexcept
      : scale_{c.scale}, offset_{c.offset} {}

  double scale_{1.0};
  double offset_{0.0};
};

/// The calibration applied to every value. Missing and Dry stay; a result that
/// is not finite (the product overflowed) becomes Missing. The metadata is
/// unchanged. The identity calibration returns the series as it is.
[[nodiscard]] TimeSeries scale_offset(TimeSeries s, const Calibration& c);

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
