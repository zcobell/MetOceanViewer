// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <chrono>
#include <expected>
#include <optional>
#include <variant>
#include <vector>

#include "mov/core/overloaded.hpp"
#include "mov/core/provider/ndbc.hpp"
#include "mov/core/time.hpp"
#include "mov/fetch/error.hpp"
#include "mov/fetch/plan/ndbc.hpp"

namespace mov::fetch::ndbc {

namespace {

using std::chrono::month;
using std::chrono::year;

/// The instants std::chrono::year can name: [year::min() Jan 1,
/// year::max() Dec 31 + 1 day).
constexpr core::Time first_nameable{
    std::chrono::sys_days{year::min() / std::chrono::January / 1}};
constexpr core::Time after_last_nameable{
    std::chrono::sys_days{year::max() / std::chrono::December / 31} +
    std::chrono::days{1}};

struct YearMonth {
  year y;
  month m;
};

[[nodiscard]] YearMonth civil(core::Time t) {
  const std::chrono::year_month_day d{std::chrono::floor<std::chrono::days>(t)};
  return {.y = d.year(), .m = d.month()};
}

/// The first and last month of the range, as calendar months.
struct Span {
  YearMonth first;
  YearMonth last;
};

[[nodiscard]] Span span_of(const core::ndbc::StdMet& request) {
  return {.first = civil(request.range.begin()),
          .last = civil(request.range.end() - std::chrono::milliseconds{1})};
}

/// [first day of `from`, first day of `to`) as instants.
[[nodiscard]] core::TimeRange between(std::chrono::year_month from,
                                      std::chrono::year_month to) {
  return *core::TimeRange::make(core::Time{std::chrono::sys_days{from / 1}},
                                core::Time{std::chrono::sys_days{to / 1}});
}

/// The months of year `y` in the range, January to December, as monthly
/// files; none when the range misses the year. `before` caps them
/// (exclusive), for the current year's past months.
[[nodiscard]] std::vector<Source> months_of(const Span& s, year y,
                                            unsigned before) {
  std::vector<Source> out;
  if (y < s.first.y or s.last.y < y) {
    return out;
  }
  constexpr unsigned january = 1;
  constexpr unsigned december = 12;
  const unsigned lo =
      s.first.y == y ? static_cast<unsigned>(s.first.m) : january;
  const unsigned hi =
      s.last.y == y ? static_cast<unsigned>(s.last.m) : december;
  for (unsigned m = lo; m <= hi and m < before; ++m) {
    out.emplace_back(Monthly{.year = y, .month = month{m}});
  }
  return out;
}

/// The late month a Monthly file names, if it is one.
[[nodiscard]] std::optional<LateMonth> late(month m) {
  constexpr unsigned october = 10;
  constexpr unsigned december = 12;
  const auto n = static_cast<unsigned>(m);
  if (n < october or n > december) {
    return std::nullopt;
  }
  return static_cast<LateMonth>(n - october);
}

}  // namespace

std::expected<std::vector<Source>, Error> initial(
    const core::ndbc::StdMet& request, core::Time now) {
  if (request.range.begin() < first_nameable or
      after_last_nameable < request.range.end()) {
    return std::unexpected{Error{UnsupportedRange{.range = request.range}}};
  }
  std::vector<Source> out;
  if (not(request.range.begin() < now)) {
    return out;
  }
  const Span s = span_of(request);
  const YearMonth today = civil(now);
  const year last_year = today.y - std::chrono::years{1};
  for (year y = std::max(s.first.y, earliest_year);
       y <= s.last.y and y < today.y; ++y) {
    if (y == last_year) {
      out.emplace_back(HistoricalLastYear{.year = y});
    } else {
      out.emplace_back(Historical{.year = y});
    }
  }
  if (today.y >= earliest_year) {
    const std::vector<Source> past_months =
        months_of(s, today.y, static_cast<unsigned>(today.m));
    out.insert(out.end(), past_months.begin(), past_months.end());
  }
  if (request.range.end() > now - realtime_reach) {
    out.emplace_back(Realtime{.year = today.y});
  }
  return out;
}

std::vector<Source> fallback(const core::ndbc::StdMet& request,
                             const Source& source) {
  constexpr unsigned after_december = 13;
  return std::visit(
      core::Overloaded{
          [](const Historical& /*h*/) { return std::vector<Source>{}; },
          [&](const HistoricalLastYear& h) {
            return months_of(span_of(request), h.year, after_december);
          },
          [](const Monthly& m) {
            if (const auto l = late(m.month)) {
              return std::vector<Source>{
                  MonthlyDecimal{.year = m.year, .month = *l}};
            }
            return std::vector<Source>{
                MonthlyLatest{.year = m.year, .month = m.month}};
          },
          [](const MonthlyDecimal& m) {
            return std::vector<Source>{
                MonthlyLatest{.year = m.year, .month = month_of(m.month)}};
          },
          [](const MonthlyLatest& /*latest*/) { return std::vector<Source>{}; },
          [](const Realtime& /*realtime*/) { return std::vector<Source>{}; }},
      source);
}

core::TimeRange extent(const core::ndbc::StdMet& request,
                       const Source& source) {
  const auto file_span = std::visit(
      core::Overloaded{
          [](const Historical& h) -> std::optional<core::TimeRange> {
            return between(
                h.year / std::chrono::January,
                (h.year + std::chrono::years{1}) / std::chrono::January);
          },
          [](const HistoricalLastYear& h) -> std::optional<core::TimeRange> {
            return between(
                h.year / std::chrono::January,
                (h.year + std::chrono::years{1}) / std::chrono::January);
          },
          [](const Monthly& m) -> std::optional<core::TimeRange> {
            const auto ym = m.year / m.month;
            return between(ym, ym + std::chrono::months{1});
          },
          [](const MonthlyDecimal& m) -> std::optional<core::TimeRange> {
            const auto ym = m.year / month_of(m.month);
            return between(ym, ym + std::chrono::months{1});
          },
          [](const MonthlyLatest& m) -> std::optional<core::TimeRange> {
            const auto ym = m.year / m.month;
            return between(ym, ym + std::chrono::months{1});
          },
          [](const Realtime& /*r*/) -> std::optional<core::TimeRange> {
            return std::nullopt;
          }},
      source);
  if (not file_span) {
    return request.range;
  }
  const auto clipped =
      core::TimeRange::make(std::max(file_span->begin(), request.range.begin()),
                            std::min(file_span->end(), request.range.end()));
  return clipped.value_or(request.range);
}

NdbcFile label(const Source& source) {
  return std::visit(
      core::Overloaded{
          [](const Historical& h) -> NdbcFile {
            return HistoricalFile{.year = h.year};
          },
          [](const HistoricalLastYear& h) -> NdbcFile {
            return HistoricalFile{.year = h.year};
          },
          [](const Monthly& m) -> NdbcFile {
            return MonthlyFile{.year = m.year, .month = m.month};
          },
          [](const MonthlyDecimal& m) -> NdbcFile {
            return MonthlyFile{.year = m.year, .month = month_of(m.month)};
          },
          [](const MonthlyLatest& m) -> NdbcFile {
            return LatestMonthFile{.year = m.year, .month = m.month};
          },
          [](const Realtime& r) -> NdbcFile {
            return RealtimeFile{.year = r.year};
          }},
      source);
}

}  // namespace mov::fetch::ndbc
