// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/core/datum_shift.hpp"

#include <expected>
#include <optional>
#include <utility>
#include <variant>

#include "core_access.hpp"
#include "mov/core/datum.hpp"
#include "mov/core/meta.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/core/units.hpp"
#include "series_rebuild.hpp"

namespace mov::core {

std::expected<TimeSeries, ShiftError> shift(TimeSeries s, VerticalDatum to,
                                            const DatumTable& table) {
  const std::optional<Unit>& unit = s.meta().unit();
  if (not unit) {
    return std::unexpected{ShiftError{UnknownUnit{}}};
  }
  const LengthUnit* length = std::get_if<LengthUnit>(&*unit);
  if (length == nullptr) {
    return std::unexpected{ShiftError{NotALengthSeries{}}};
  }
  // The source datum and the rewritten metadata come together.
  std::optional<DatumRewrite> rewrite =
      s.meta().rewrite_datum(detail::CoreAccess::key(), to);
  if (not rewrite) {
    return std::unexpected{ShiftError{UnknownSourceDatum{}}};
  }
  if (rewrite->from == to) {
    return s;
  }
  const auto offset = table.offset(rewrite->from, to);
  if (not offset) {
    return std::unexpected{ShiftError{offset.error()}};
  }
  TimeSeriesParts parts = detail::mapped(
      std::move(s), Affine{.scale = 1.0, .offset = offset->as(*length)});
  parts.meta = std::move(rewrite->meta);
  return detail::assembled(std::move(parts));
}

}  // namespace mov::core
