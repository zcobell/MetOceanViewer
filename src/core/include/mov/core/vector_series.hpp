// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>

#include "mov/core/station_table.hpp"
#include "mov/core/timeseries.hpp"

namespace mov::core {

/// Why two or more series cannot be combined sample by sample.
enum class AlignmentErrc : std::uint8_t {
  times_differ,
  units_differ,
  unit_unknown,
  datums_differ,
  datum_unknown,
  not_a_vector_pair,
  temperature_difference,
};

/// The eastward (u) and northward (v) components of one vector quantity.
/// The pairs are (current_u, current_v), (wind_u, wind_v) and (generic,
/// generic); any other combination, or the components swapped, is
/// not_a_vector_pair. Both components have the same times and the same known
/// unit, and the same datum (both unset, or both set and equal).
class VectorSeries {
 public:
  /// Checks, in order: the pair, the times, the units (unit_unknown, then
  /// units_differ), the datums (datum_unknown if only one is set, then
  /// datums_differ).
  [[nodiscard]] static std::expected<VectorSeries, AlignmentErrc> make(
      TimeSeries u, TimeSeries v);

  [[nodiscard]] const TimeSeries& u() const& noexcept { return u_; }
  const TimeSeries& u() const&& = delete;
  [[nodiscard]] const TimeSeries& v() const& noexcept { return v_; }
  const TimeSeries& v() const&& = delete;

  /// hypot(u, v) under the combine rule (Missing, then Dry, wins; N7).
  /// Wind: quantity wind_speed, label "wind speed". Otherwise the generic
  /// `value` quantity, label "<stem> speed". Unit: the components'; no datum.
  [[nodiscard]] TimeSeries magnitude() const;

  /// atan2(v, u) in degrees, in (-180, 180] (-180 becomes 180): the
  /// mathematical direction the vector points to, counter-clockwise from
  /// east. A zero vector has none (Missing). Generic `value` quantity (never
  /// wind_direction, which is the meteorological "from" direction), unit
  /// degree, label "<stem> direction (cartesian: degrees counter-clockwise
  /// from east, toward)"; no datum.
  [[nodiscard]] TimeSeries cartesian_direction() const;

  friend bool operator==(const VectorSeries&, const VectorSeries&) = default;

 private:
  VectorSeries(TimeSeries u, TimeSeries v) noexcept
      : u_{std::move(u)}, v_{std::move(v)} {}

  TimeSeries u_;
  TimeSeries v_;
};

/// Columns ku and kv of one station as a vector. Precondition: station <
/// t.size() and ku, kv < t.schema().size().
[[nodiscard]] std::expected<VectorSeries, AlignmentErrc> vector_series(
    const StationTable& t, std::size_t station, std::size_t ku, std::size_t kv);

/// sqrt(x^2 + y^2 + z^2) under the combine rule. (x, y) must be a current or
/// generic vector pair and z generic (a vertical velocity has no registry
/// quantity); then the VectorSeries checks for (x, y) and (x, z). Generic
/// `value` quantity, label "3D current speed", the components' unit, no
/// datum.
[[nodiscard]] std::expected<TimeSeries, AlignmentErrc> magnitude3(
    const TimeSeries& x, const TimeSeries& y, const TimeSeries& z);

}  // namespace mov::core
