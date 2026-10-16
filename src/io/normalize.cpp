// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/detail/normalize.hpp"

#include <algorithm>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "mov/core/station_table.hpp"
#include "mov/core/timeseries.hpp"
#include "mov/io/detail/reporting.hpp"
#include "mov/io/warning.hpp"

namespace mov::io::detail {

core::NormalizeReport normalize_columns(
    core::TimeAxis& times, std::span<core::Column* const> columns) {
  const auto differs = [&columns](std::size_t kept, std::size_t dropped) {
    return std::ranges::any_of(columns, [kept, dropped](const core::Column* c) {
      return (*c)[kept] != (*c)[dropped];
    });
  };
  const core::NormalizingOrder order = core::normalizing_order(times, differs);
  if (order.report.clean()) {
    return order.report;
  }
  const auto pick = [&order](auto& values) {
    std::remove_reference_t<decltype(values)> out;
    out.reserve(order.kept.size());
    for (const std::size_t row : order.kept) {
      out.push_back(values[row]);
    }
    values = std::move(out);
  };
  pick(times);
  for (core::Column* column : columns) {
    pick(*column);
  }
  return order.report;
}

void merge_reports(core::NormalizeReport& sum,
                   const core::NormalizeReport& more) {
  sum.descents += more.descents;
  sum.duplicates_dropped += more.duplicates_dropped;
  sum.conflicting_duplicates += more.conflicting_duplicates;
}

void add_normalize_warnings(std::vector<Warning>& warnings,
                            const core::NormalizeReport& report,
                            std::string_view subject) {
  const std::string text = subject_of(subject);
  append_if_counted(warnings, {.code = WarningCode::times_reordered,
                               .subject = text,
                               .count = report.descents});
  add_dropped_warnings(warnings, report, subject);
}

void add_dropped_warnings(std::vector<Warning>& warnings,
                          const core::NormalizeReport& report,
                          std::string_view subject) {
  const std::string text = subject_of(subject);
  append_if_counted(warnings, {.code = WarningCode::duplicate_times_dropped,
                               .subject = text,
                               .count = report.duplicates_dropped});
  append_if_counted(warnings, {.code = WarningCode::conflicting_duplicate_times,
                               .subject = text,
                               .count = report.conflicting_duplicates});
}

}  // namespace mov::io::detail
