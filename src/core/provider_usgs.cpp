// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <expected>
#include <iterator>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core_access.hpp"
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
  // A valid generic token that is no registry token (static_assert in
  // usgs.hpp), so GenericQuantity::parse's checks would always pass.
  return GenericQuantity{core::detail::CoreAccess::key(),
                         "usgs_" + std::string{parameter.view()}};
}

std::expected<SeriesSet, SeriesSetError> SeriesSet::make(
    std::vector<SeriesInfo> series) {
  std::ranges::sort(series, {}, &SeriesInfo::id);
  const auto same_id = std::ranges::adjacent_find(
      series, {},
      [](const SeriesInfo& s) -> const TimeSeriesId& { return s.id; });
  if (same_id != series.end()) {
    return std::unexpected{SeriesSetError{DuplicateSeriesId{same_id->id}}};
  }
  for (auto it = series.begin(); it != series.end(); ++it) {
    if (not it->primary) {
      continue;
    }
    const auto twin =
        std::find_if(std::next(it), series.end(), [&it](const SeriesInfo& s) {
          return s.primary and s.parameter == it->parameter and
                 s.statistic == it->statistic;
        });
    if (twin != series.end()) {
      return std::unexpected{SeriesSetError{SeveralPrimary{
          .parameter = it->parameter, .statistic = it->statistic}}};
    }
  }
  return SeriesSet{std::move(series)};
}

const SeriesInfo* SeriesSet::find(const TimeSeriesId& id) const& noexcept {
  const auto it = std::ranges::lower_bound(series_, id, {}, &SeriesInfo::id);
  return it != series_.end() and it->id == id ? &*it : nullptr;
}

const SeriesInfo* SeriesSet::primary(
    const ParameterCode& parameter,
    const StatisticId& statistic) const& noexcept {
  const auto it = std::ranges::find_if(series_, [&](const SeriesInfo& s) {
    return s.primary and s.parameter == parameter and s.statistic == statistic;
  });
  return it != series_.end() ? &*it : nullptr;
}

namespace {

/// The series a selector names, if it belongs to `wanted`.
std::expected<SeriesInfo, ResolveError> pick(const SeriesSet& site,
                                             const ByTimeSeries& selector,
                                             Collection wanted) {
  const SeriesInfo* found = site.find(selector.id);
  if (found == nullptr) {
    return std::unexpected{ResolveError::no_series};
  }
  if (collection_of(found->statistic) != wanted) {
    return std::unexpected{ResolveError::wrong_collection};
  }
  return *found;
}

std::expected<SeriesInfo, ResolveError> pick(const SeriesSet& site,
                                             const ParameterCode& parameter,
                                             const StatisticId& statistic) {
  const SeriesInfo* found = site.primary(parameter, statistic);
  if (found == nullptr) {
    return std::unexpected{ResolveError::no_series};
  }
  return *found;
}

}  // namespace

// The constructor calls stay in the friend functions themselves: a lambda
// inside one is a local class without the function's friendship.
std::expected<Resolved<Continuous>, ResolveError> resolve(
    const Continuous& request, const SeriesSet& site) {
  auto picked = std::visit(
      Overloaded{[&site](const ByParameter& p) {
                   return pick(site, p.parameter, StatisticId::instantaneous());
                 },
                 [&site](const ByTimeSeries& t) {
                   return pick(site, t, Collection::continuous);
                 }},
      request.series);
  if (not picked) {
    return std::unexpected{picked.error()};
  }
  return Resolved<Continuous>{*std::move(picked)};
}

std::expected<Resolved<Daily>, ResolveError> resolve(const Daily& request,
                                                     const SeriesSet& site) {
  auto picked = std::visit(
      Overloaded{[&site](const DailyByParameter& p)
                     -> std::expected<SeriesInfo, ResolveError> {
                   if (collection_of(p.statistic) != Collection::daily) {
                     return std::unexpected{ResolveError::wrong_collection};
                   }
                   return pick(site, p.parameter, p.statistic);
                 },
                 [&site](const ByTimeSeries& t) {
                   return pick(site, t, Collection::daily);
                 }},
      request.series);
  if (not picked) {
    return std::unexpected{picked.error()};
  }
  return Resolved<Daily>{*std::move(picked)};
}

}  // namespace usgs

namespace {

using usgs::Collection;
using usgs::NotOffered;
using usgs::SeriesInfo;
using usgs::StatisticId;

using Offer = std::expected<void, NotOffered>;

/// parameter, then statistic, as offered() documents.
Offer by_parameter(const usgs::SeriesSet& site,
                   const usgs::ParameterCode& parameter,
                   const StatisticId& statistic) {
  const auto of_parameter = [&parameter](const SeriesInfo& s) {
    return s.parameter == parameter;
  };
  if (std::ranges::none_of(site.items(), of_parameter)) {
    return std::unexpected{NotOffered::parameter};
  }
  if (std::ranges::none_of(site.items(), [&](const SeriesInfo& s) {
        return of_parameter(s) and s.statistic == statistic;
      })) {
    return std::unexpected{NotOffered::statistic};
  }
  return {};
}

/// series, then statistic when the series is of the other collection.
Offer by_id(const usgs::SeriesSet& site, const usgs::TimeSeriesId& id,
            Collection wanted) {
  const SeriesInfo* found = site.find(id);
  if (found == nullptr) {
    return std::unexpected{NotOffered::series};
  }
  if (usgs::collection_of(found->statistic) != wanted) {
    return std::unexpected{NotOffered::statistic};
  }
  return {};
}

}  // namespace

std::expected<void, usgs::NotOffered> offered(
    const Capabilities<provider::Usgs>& caps, const usgs::Request& request) {
  const usgs::SeriesSet& site = caps.series;
  return std::visit(
      Overloaded{
          [&site](const usgs::Continuous& c) {
            return std::visit(
                Overloaded{[&site](const usgs::ByParameter& p) {
                             return by_parameter(site, p.parameter,
                                                 StatisticId::instantaneous());
                           },
                           [&site](const usgs::ByTimeSeries& t) {
                             return by_id(site, t.id, Collection::continuous);
                           }},
                c.series);
          },
          [&site](const usgs::Daily& d) {
            return std::visit(
                Overloaded{[&site](const usgs::DailyByParameter& p) -> Offer {
                             if (usgs::collection_of(p.statistic) !=
                                 Collection::daily) {
                               return std::unexpected{NotOffered::statistic};
                             }
                             return by_parameter(site, p.parameter,
                                                 p.statistic);
                           },
                           [&site](const usgs::ByTimeSeries& t) {
                             return by_id(site, t.id, Collection::daily);
                           }},
                d.series);
          }},
      request);
}

}  // namespace mov::core
