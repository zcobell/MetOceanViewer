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
  const LengthUnit* length = unit ? std::get_if<LengthUnit>(&*unit) : nullptr;
  if (length == nullptr) {
    return std::unexpected{ShiftError{NotALengthSeries{}}};
  }
  // `from` and the rewritten metadata are one fact: both are engaged iff the
  // series has a datum.
  const std::optional<VerticalDatum> from = s.meta().datum();
  std::optional<SeriesMeta> shifted =
      s.meta().rewrite_datum(detail::CoreAccess::key(), to);
  if (not from or not shifted) {
    return std::unexpected{ShiftError{UnknownSourceDatum{}}};
  }
  if (*from == to) {
    return s;
  }
  const auto offset = table.offset(*from, to);
  if (not offset) {
    return std::unexpected{ShiftError{offset.error()}};
  }
  return detail::rebuilt(std::move(s), std::move(*shifted),
                         Affine{.scale = 1.0, .offset = offset->as(*length)});
}

}  // namespace mov::core
