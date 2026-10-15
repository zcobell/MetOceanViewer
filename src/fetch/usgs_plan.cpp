// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <expected>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/overloaded.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/fetch/error.hpp"
#include "mov/fetch/plan/usgs.hpp"
#include "mov/io/error.hpp"

namespace mov::fetch::usgs {

namespace {

namespace cu = core::usgs;

[[nodiscard]] Failure resolution_failure(Error error) {
  return Failure{.error = std::move(error), .unit = Resolution{.page = 0}};
}

/// The ids of the Primary series of one parameter and statistic.
[[nodiscard]] AmbiguousSeries ambiguous(
    const cu::SeveralPrimary& several,
    const std::vector<cu::SeriesInfo>& rows) {
  AmbiguousSeries out;
  for (const cu::SeriesInfo& row : rows) {
    if (row.primary and row.parameter == several.parameter and
        row.statistic == several.statistic) {
      out.candidates.push_back(row.id);
    }
  }
  std::ranges::sort(out.candidates);
  return out;
}

[[nodiscard]] Error lift_set_error(const cu::SeriesSetError& error,
                                   const std::vector<cu::SeriesInfo>& rows) {
  return std::visit(
      core::Overloaded{[](const cu::DuplicateSeriesId& d) -> Error {
                         return io::ParseError::make(
                             io::ParseErrc::corrupt_record, {.line = 0},
                             d.id.view());
                       },
                       [&](const cu::SeveralPrimary& s) -> Error {
                         return ambiguous(s, rows);
                       }},
      error);
}

[[nodiscard]] Error lift_resolve_error(cu::ResolveError error,
                                       cu::Collection requested) {
  switch (error) {
    case cu::ResolveError::no_series:
      break;
    case cu::ResolveError::wrong_collection:
      return WrongCollection{.requested = requested};
  }
  return NoData{};
}

template <class C>
[[nodiscard]] std::expected<DataRequest<C>, Failure> bind_stage(
    const C& request, const std::vector<cu::SeriesInfo>& rows,
    cu::Collection requested) {
  auto set = cu::SeriesSet::make(rows);
  if (not set) {
    return std::unexpected{
        resolution_failure(lift_set_error(set.error(), rows))};
  }
  auto resolved = cu::resolve(request, *set);
  if (not resolved) {
    return std::unexpected{
        resolution_failure(lift_resolve_error(resolved.error(), requested))};
  }
  return DataRequest<C>{.request = request, .series = *std::move(resolved)};
}

}  // namespace

std::expected<DataRequest<cu::Continuous>, Failure> bind(
    const cu::Continuous& request, const std::vector<cu::SeriesInfo>& rows) {
  return bind_stage(request, rows, cu::Collection::continuous);
}

std::expected<DataRequest<cu::Daily>, Failure> bind(
    const cu::Daily& request, const std::vector<cu::SeriesInfo>& rows) {
  return bind_stage(request, rows, cu::Collection::daily);
}

}  // namespace mov::fetch::usgs
