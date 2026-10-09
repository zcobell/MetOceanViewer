// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/overloaded.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/quantity.hpp"

namespace mov::core {

namespace usgs {

QuantityId quantity_of(const ParameterCode& parameter) {
  if (const auto mapping = registry_quantity(parameter)) {
    return mapping->quantity;
  }
  // "usgs_" and five digits always form a valid generic token that is not
  // a registry token.
  auto generic = GenericQuantity::parse(
      {.token = "usgs_" + std::string{parameter.view()}, .standard_name = ""});
  assert(generic.has_value());
  return std::move(generic).value_or(GenericQuantity::value());
}

}  // namespace usgs

namespace {

using usgs::SeriesInfo;
using usgs::StatisticId;

using Offer = std::expected<void, NotOffered>;

/// The collection a series belongs to: 00011 is continuous, any other
/// statistic daily.
enum class Collection : std::uint8_t { continuous, daily };

[[nodiscard]] Collection collection_of(const StatisticId& statistic) {
  return statistic == StatisticId::instantaneous() ? Collection::continuous
                                                   : Collection::daily;
}

/// parameter, then statistic, as offered() documents.
[[nodiscard]] Offer by_parameter(const std::vector<SeriesInfo>& series,
                                 const usgs::ParameterCode& parameter,
                                 const StatisticId& statistic) {
  const auto of_parameter = [&parameter](const SeriesInfo& s) {
    return s.parameter == parameter;
  };
  if (std::ranges::none_of(series, of_parameter)) {
    return std::unexpected{NotOffered::parameter};
  }
  if (std::ranges::none_of(series, [&](const SeriesInfo& s) {
        return of_parameter(s) and s.statistic == statistic;
      })) {
    return std::unexpected{NotOffered::statistic};
  }
  return {};
}

/// series, then statistic when the series is of the other collection.
[[nodiscard]] Offer by_id(const std::vector<SeriesInfo>& series,
                          const usgs::TimeSeriesId& id, Collection wanted) {
  const auto found = std::ranges::find(series, id, &SeriesInfo::id);
  if (found == series.end()) {
    return std::unexpected{NotOffered::series};
  }
  if (collection_of(found->statistic) != wanted) {
    return std::unexpected{NotOffered::statistic};
  }
  return {};
}

}  // namespace

std::expected<void, NotOffered> offered(
    const Capabilities<provider::Usgs>& caps, const usgs::Request& request) {
  const std::vector<SeriesInfo>& series = caps.series;
  return std::visit(
      Overloaded{
          [&series](const usgs::Continuous& c) {
            return std::visit(
                Overloaded{[&series](const usgs::ByParameter& p) {
                             return by_parameter(series, p.parameter,
                                                 StatisticId::instantaneous());
                           },
                           [&series](const usgs::ByTimeSeries& t) {
                             return by_id(series, t.id, Collection::continuous);
                           }},
                c.series);
          },
          [&series](const usgs::Daily& d) {
            return std::visit(
                Overloaded{[&series](const usgs::DailyByParameter& p) {
                             return by_parameter(series, p.parameter,
                                                 p.statistic);
                           },
                           [&series](const usgs::ByTimeSeries& t) {
                             return by_id(series, t.id, Collection::daily);
                           }},
                d.series);
          }},
      request);
}

}  // namespace mov::core
