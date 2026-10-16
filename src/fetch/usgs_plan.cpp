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

namespace mov::fetch::usgs {

namespace {

namespace cu = core::usgs;

[[nodiscard]] Failure resolution_failure(Error error) {
  return Failure{.error = std::move(error), .unit = Resolution{.page = 0}};
}

[[nodiscard]] Error lift_resolve_error(cu::ResolveError error,
                                       cu::Collection requested) {
  return std::visit(
      core::Overloaded{[](cu::NoSeries) -> Error { return NoData{}; },
                       [requested](cu::WrongCollection) -> Error {
                         return WrongCollection{.requested = requested};
                       },
                       [](cu::AmbiguousSeries& a) -> Error {
                         return AmbiguousSeries{.candidates =
                                                    std::move(a.candidates)};
                       }},
      error);
}

template <class C>
[[nodiscard]] std::expected<cu::Resolved<C>, Failure> resolve_rows(
    const C& request, const std::vector<cu::SeriesInfo>& rows,
    cu::Collection requested) {
  auto set = cu::SeriesSet::make(rows);
  if (not set) {
    return std::unexpected{
        resolution_failure(DuplicateSeries{.id = set.error().id})};
  }
  auto resolved = cu::resolve(request, *set);
  if (not resolved) {
    return std::unexpected{resolution_failure(
        lift_resolve_error(std::move(resolved).error(), requested))};
  }
  return *std::move(resolved);
}

}  // namespace

std::expected<DataRequest<cu::Continuous>, Failure> bind(
    const cu::Continuous& request, const std::vector<cu::SeriesInfo>& rows) {
  auto series = resolve_rows(request, rows, cu::Collection::continuous);
  if (not series) {
    return std::unexpected{std::move(series).error()};
  }
  return DataRequest<cu::Continuous>{request, *std::move(series)};
}

std::expected<DataRequest<cu::Daily>, Failure> bind(
    const cu::Daily& request, const std::vector<cu::SeriesInfo>& rows) {
  auto series = resolve_rows(request, rows, cu::Collection::daily);
  if (not series) {
    return std::unexpected{std::move(series).error()};
  }
  return DataRequest<cu::Daily>{request, *std::move(series)};
}

}  // namespace mov::fetch::usgs
