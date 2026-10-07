// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <expected>
#include <variant>

#include "mov/core/datum.hpp"
#include "mov/core/timeseries.hpp"

namespace mov::core {

/// The series has no length unit (a wind speed, a temperature, or no unit at
/// all), so a vertical offset cannot be added to it.
struct NotALengthSeries {
  friend constexpr bool operator==(NotALengthSeries,
                                   NotALengthSeries) = default;
};
/// The series has no datum to shift from.
struct UnknownSourceDatum {
  friend constexpr bool operator==(UnknownSourceDatum,
                                   UnknownSourceDatum) = default;
};
using ShiftError =
    std::variant<NotALengthSeries, UnknownSourceDatum, MissingOffset>;

/// Expresses a series in another vertical datum (D18): the source datum is the
/// series' own, and table.offset(from, to) is added to every value (converted
/// to the series' unit). Any datum shifts to any other through the table's MSL
/// pivot.
///
/// Checks, in this order: the unit is a LengthUnit (NotALengthSeries), a datum
/// is engaged (UnknownSourceDatum), then from == to returns the series
/// unchanged, whatever the table holds; then the offset (MissingOffset names
/// `from` before `to`). A missing offset is an error, never zero. The result
/// has datum `to`; Missing and Dry are kept, and a value that overflows
/// becomes Missing.
[[nodiscard]] std::expected<TimeSeries, ShiftError> shift(
    TimeSeries s, VerticalDatum to, const DatumTable& table);

}  // namespace mov::core
