// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Putting rows that share one time axis in time order, for the readers whose
// rows hold several columns (station netCDF, provider responses). Private to
// mov::io (public only because tests include it).

#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "mov/core/station_table.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/warning.hpp"

namespace mov::io::detail {

/// Puts the rows of one time axis in time order, as IMEDS does
/// (core::normalizing_order): rows are stably sorted by time and of equal
/// times the first is kept. `times` and every column are reordered together;
/// a dropped row conflicts when any column differs from the kept one. The
/// report counts what was moved and dropped (clean: nothing was changed).
[[nodiscard]] core::NormalizeReport normalize_columns(
    core::TimeAxis& times, std::span<core::Column* const> columns);

/// `sum` plus `more`, field by field: the report of many series as one.
void merge_reports(core::NormalizeReport& sum,
                   const core::NormalizeReport& more);

/// The warnings of a report (`times_reordered`, `duplicate_times_dropped`,
/// `conflicting_duplicate_times`), each counted, about `subject`.
void add_normalize_warnings(std::vector<Warning>& warnings,
                            const core::NormalizeReport& report,
                            std::string_view subject);

/// The warnings of what a report dropped (`duplicate_times_dropped`,
/// `conflicting_duplicate_times`), not of what it reordered: for a source
/// whose order is not a time order to begin with (USGS daily values).
void add_dropped_warnings(std::vector<Warning>& warnings,
                          const core::NormalizeReport& report,
                          std::string_view subject);

}  // namespace mov::io::detail
