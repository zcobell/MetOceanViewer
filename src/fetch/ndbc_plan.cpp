// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <chrono>
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

/// The months of year `y` in the range, January to December, as primary
/// monthly files; none when the range misses the year. `before` caps them
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
    out.emplace_back(Monthly{
        .year = y, .month = month{m}, .spelling = MonthSpelling::primary});
  }
  return out;
}

}  // namespace

std::vector<Source> initial(const core::ndbc::StdMet& request, core::Time now) {
  std::vector<Source> out;
  if (not(request.range.begin() < now)) {
    return out;
  }
  const Span s = span_of(request);
  const YearMonth today = civil(now);
  const year last_year = today.y - std::chrono::years{1};
  for (year y = s.first.y; y <= s.last.y and y < today.y; ++y) {
    out.emplace_back(
        Historical{.year = y, .months_when_absent = y == last_year});
  }
  const std::vector<Source> past_months =
      months_of(s, today.y, static_cast<unsigned>(today.m));
  out.insert(out.end(), past_months.begin(), past_months.end());
  if (request.range.end() > now - realtime_reach) {
    out.emplace_back(Realtime{.year = today.y});
  }
  return out;
}

std::vector<Source> fallback(const core::ndbc::StdMet& request,
                             const Source& source) {
  constexpr unsigned october = 10;
  constexpr unsigned after_december = 13;
  return std::visit(
      core::Overloaded{
          [&](const Historical& h) {
            return h.months_when_absent
                       ? months_of(span_of(request), h.year, after_december)
                       : std::vector<Source>{};
          },
          [](const Monthly& m) {
            if (m.spelling == MonthSpelling::primary and
                static_cast<unsigned>(m.month) >= october) {
              return std::vector<Source>{
                  Monthly{.year = m.year,
                          .month = m.month,
                          .spelling = MonthSpelling::decimal}};
            }
            return std::vector<Source>{
                MonthlyLatest{.year = m.year, .month = m.month}};
          },
          [](const MonthlyLatest& /*latest*/) { return std::vector<Source>{}; },
          [](const Realtime& /*realtime*/) { return std::vector<Source>{}; }},
      source);
}

NdbcFile label(const Source& source) {
  const core::ndbc::FileKind k = kind(source);
  return std::visit(
      core::Overloaded{
          [&](const Historical& h) {
            return NdbcFile{.kind = k, .year = h.year, .month = std::nullopt};
          },
          [&](const Monthly& m) {
            return NdbcFile{.kind = k, .year = m.year, .month = m.month};
          },
          [&](const MonthlyLatest& m) {
            return NdbcFile{.kind = k, .year = m.year, .month = m.month};
          },
          [&](const Realtime& r) {
            return NdbcFile{.kind = k, .year = r.year, .month = std::nullopt};
          }},
      source);
}

}  // namespace mov::fetch::ndbc
