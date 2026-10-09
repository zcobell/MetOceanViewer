// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstdint>
#include <expected>
#include <utility>

#include "mov/core/station_table.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"

namespace mov::core {

/// Which pair a VectorSeries is: one of the registered component pairs, or
/// two generic series the caller declared to be components.
enum class VectorKind : std::uint8_t { current, wind, generic };

enum class VectorErrc : std::uint8_t {
  not_a_vector_pair,
  times_differ,
  unit_unknown,
  units_differ,
};

/// The eastward (u) and northward (v) components of one vector quantity, on
/// the same times and in the same known unit. Component datums are not
/// compared: no derived series carries a datum.
class VectorSeries {
 public:
  /// The registered pairs only: (current_u, current_v) or (wind_u, wind_v),
  /// in that order. Checks the pair, then the times, then the units
  /// (unit_unknown, then units_differ).
  [[nodiscard]] static std::expected<VectorSeries, VectorErrc> make(
      TimeSeries u, TimeSeries v);

  /// Two generic (non-registry) series that the caller asserts are the u and
  /// v components of one vector, e.g. two IMEDS files. A registry quantity
  /// on either side is not_a_vector_pair (use make). Same alignment checks.
  [[nodiscard]] static std::expected<VectorSeries, VectorErrc>
  assume_components(TimeSeries u, TimeSeries v);

  [[nodiscard]] const TimeSeries& u() const& noexcept { return u_; }
  const TimeSeries& u() const&& = delete;
  [[nodiscard]] const TimeSeries& v() const& noexcept { return v_; }
  const TimeSeries& v() const&& = delete;
  [[nodiscard]] VectorKind kind() const noexcept { return kind_; }
  /// The components' unit.
  [[nodiscard]] const Unit& unit() const& noexcept { return unit_; }
  const Unit& unit() const&& = delete;

  /// hypot(u, v) under the combine rule (Missing, then Dry, wins).
  /// Wind: quantity wind_speed, label "wind speed". Otherwise the generic
  /// `value` quantity, label "<stem> speed". Unit: unit(); no datum.
  [[nodiscard]] TimeSeries magnitude() const;

  /// atan2(v, u) in degrees, in (-180, 180] (-180 becomes 180): the
  /// mathematical direction the vector points to, counter-clockwise from
  /// east. A zero vector has none (Missing). Generic `value` quantity (never
  /// wind_direction, which is the meteorological "from" direction), unit
  /// degree(), label "<stem> direction (cartesian: degrees counter-clockwise
  /// from east, toward)"; no datum.
  [[nodiscard]] TimeSeries cartesian_direction() const;

  friend bool operator==(const VectorSeries&, const VectorSeries&) = default;

 private:
  VectorSeries(TimeSeries u, TimeSeries v, VectorKind kind, Unit unit) noexcept
      : u_{std::move(u)},
        v_{std::move(v)},
        kind_{kind},
        unit_{std::move(unit)} {}

  TimeSeries u_;
  TimeSeries v_;
  VectorKind kind_;
  Unit unit_;
};

/// Columns ku and kv of one station as a registered pair (VectorSeries::make).
/// Precondition: station < t.size() and ku, kv < t.schema().size().
[[nodiscard]] std::expected<VectorSeries, VectorErrc> vector_series(
    const StationTable& t, StationIndex station, ColumnIndex ku,
    ColumnIndex kv);

enum class VerticalErrc : std::uint8_t {
  not_generic,  // the registry has no vertical component quantity
  times_differ,
  unit_unknown,
  units_differ,
};

/// The 3D speed hypot(u, v, w) (std::hypot of three) under the combine rule,
/// with w the vertical component: a generic quantity on the horizontal
/// times, in the horizontal unit. Generic `value` quantity, label
/// "3D <stem> speed" ("3D current speed"), unit horizontal.unit(), no datum.
[[nodiscard]] std::expected<TimeSeries, VerticalErrc> magnitude3(
    const VectorSeries& horizontal, const TimeSeries& w);

}  // namespace mov::core
