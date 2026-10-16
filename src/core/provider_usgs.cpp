// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <expected>
#include <iterator>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

#include "core_access.hpp"
#include "mov/core/ascii.hpp"
#include "mov/core/overloaded.hpp"
#include "mov/core/provider/capabilities.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/quantity.hpp"
#include "mov/core/time.hpp"
#include "mov/core/utf8.hpp"
#include "mov/core/windows.hpp"

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

std::expected<Sublocation, SublocationError> Sublocation::make(
    std::string text) {
  if (text.empty()) {
    return std::unexpected{SublocationError::empty};
  }
  if (text.size() > max_bytes) {
    return std::unexpected{SublocationError::too_long};
  }
  const auto control = [](char c) {
    const auto byte = static_cast<unsigned char>(c);
    return byte < 0x20 or byte == 0x7F;
  };
  if (not is_valid_utf8(text) or std::ranges::any_of(text, control) or
      ascii::is_space(text.front()) or ascii::is_space(text.back())) {
    return std::unexpected{SublocationError::invalid_text};
  }
  return Sublocation{std::move(text)};
}

SeriesKey key_of(const SeriesInfo& series) {
  return {.parameter = series.parameter,
          .statistic = series.statistic,
          .sublocation = series.sublocation};
}

namespace {

// The order of a key's epochs: the day the validity begins (unknown
// first), then the id.
auto epoch_order(const SeriesInfo& s) {
  return std::tuple{s.validity.first(), s.id};
}

}  // namespace

std::expected<SeriesSet, DuplicateSeriesId> SeriesSet::make(
    std::vector<SeriesInfo> series) {
  std::ranges::sort(series, {}, &SeriesInfo::id);
  const auto same_id = std::ranges::adjacent_find(
      series, {},
      [](const SeriesInfo& s) -> const TimeSeriesId& { return s.id; });
  if (same_id != series.end()) {
    return std::unexpected{DuplicateSeriesId{same_id->id}};
  }
  std::vector<SeriesInfo> epochs;
  std::ranges::copy_if(series, std::back_inserter(epochs),
                       &SeriesInfo::primary);
  std::ranges::sort(epochs, [](const SeriesInfo& a, const SeriesInfo& b) {
    const SeriesKey ka = key_of(a);
    const SeriesKey kb = key_of(b);
    if (ka != kb) {
      return ka < kb;
    }
    return epoch_order(a) < epoch_order(b);
  });
  std::vector<SeriesKey> keys;
  std::vector<std::size_t> starts;
  for (std::size_t i = 0; i < epochs.size(); ++i) {
    SeriesKey key = key_of(epochs[i]);
    if (keys.empty() or keys.back() != key) {
      keys.push_back(std::move(key));
      starts.push_back(i);
    }
  }
  starts.push_back(epochs.size());
  return SeriesSet{std::move(series), std::move(epochs), std::move(keys),
                   std::move(starts)};
}

const SeriesInfo* SeriesSet::find(const TimeSeriesId& id) const& noexcept {
  const auto it = std::ranges::lower_bound(series_, id, {}, &SeriesInfo::id);
  return it != series_.end() and it->id == id ? &*it : nullptr;
}

std::span<const SeriesInfo> SeriesSet::epochs(
    const SeriesKey& key) const& noexcept {
  const auto it = std::ranges::lower_bound(keys_, key);
  if (it == keys_.end() or *it != key) {
    return {};
  }
  const auto group = static_cast<std::size_t>(it - keys_.begin());
  const std::span<const SeriesInfo> all{epochs_};
  return all.subspan(starts_[group], starts_[group + 1] - starts_[group]);
}

namespace {

// How late a group's data reach: an epoch that has begun and has no end is
// ongoing and reaches furthest; one with neither date reaches nowhere.
struct Reach {
  bool ongoing = false;
  std::optional<std::chrono::sys_days> last;
  std::optional<std::chrono::sys_days> begin;
  friend auto operator<=>(const Reach&, const Reach&) = default;
};

Reach reach_of(std::span<const SeriesInfo> epochs) {
  Reach best;
  for (const SeriesInfo& s : epochs) {
    const Reach r{.ongoing = s.validity.first() and not s.validity.last(),
                  .last = s.validity.last(),
                  .begin = s.validity.first()};
    best = std::max(best, r);
  }
  return best;
}

// The keys of a parameter and statistic, sorted.
std::vector<SeriesKey> keys_of(const SeriesSet& site,
                               const ParameterCode& parameter,
                               const StatisticId& statistic) {
  std::vector<SeriesKey> out;
  for (const SeriesKey& key : site.keys()) {
    if (key.parameter == parameter and key.statistic == statistic) {
      out.push_back(key);
    }
  }
  return out;
}

}  // namespace

std::optional<SeriesKey> map_default(const SeriesSet& site,
                                     const ParameterCode& parameter,
                                     const StatisticId& statistic) {
  std::optional<SeriesKey> best;
  Reach best_reach;
  for (SeriesKey& key : keys_of(site, parameter, statistic)) {
    const Reach r = reach_of(site.epochs(key));
    if (not best or best_reach < r) {
      best_reach = r;
      best = std::move(key);
    }
  }
  return best;
}

namespace {

using std::chrono::days;
using std::chrono::sys_days;

// The key a by-parameter request means (ByParameter's rule).
std::expected<SeriesKey, ResolveError> key_for(
    const SeriesSet& site, const ParameterCode& parameter,
    const StatisticId& statistic,
    const std::optional<Sublocation>& sublocation) {
  std::vector<SeriesKey> candidates = keys_of(site, parameter, statistic);
  if (sublocation) {
    SeriesKey wanted{.parameter = parameter,
                     .statistic = statistic,
                     .sublocation = sublocation};
    if (std::ranges::find(candidates, wanted) == candidates.end()) {
      return std::unexpected{ResolveError{NoSeries{}}};
    }
    return wanted;
  }
  const auto main = std::ranges::find_if(
      candidates, [](const SeriesKey& k) { return not k.sublocation; });
  if (main != candidates.end()) {
    return *main;
  }
  if (candidates.empty()) {
    return std::unexpected{ResolveError{NoSeries{}}};
  }
  if (candidates.size() > 1) {
    return std::unexpected{
        ResolveError{AmbiguousSeries{.candidates = std::move(candidates)}}};
  }
  return candidates.front();
}

// The instants an epoch's validity covers, a day wider at each known end:
// [first - 1 d, last + 2 d).
std::optional<TimeRange> sub_range(const ValidRange& validity,
                                   TimeRange request) {
  const auto at = [](sys_days d) {
    return std::chrono::time_point_cast<std::chrono::milliseconds>(d);
  };
  const Time begin =
      validity.first()
          ? std::max(request.begin(), at(*validity.first() - days{1}))
          : request.begin();
  const Time end = validity.last()
                       ? std::min(request.end(), at(*validity.last() + days{2}))
                       : request.end();
  const auto range = TimeRange::make(begin, end);
  return range ? std::optional{*range} : std::nullopt;
}

// The local days an epoch's validity covers, a day wider at each known end.
std::optional<DayRange> sub_range(const ValidRange& validity,
                                  DayRange request) {
  const auto local = [](sys_days d) {
    return std::chrono::local_days{d.time_since_epoch()};
  };
  const auto first =
      validity.first()
          ? std::max(request.first(), local(*validity.first() - days{1}))
          : request.first();
  const auto end = validity.last() ? std::min(request.end(),
                                              local(*validity.last() + days{2}))
                                   : request.end();
  const auto range = DayRange::make(first, end);
  return range ? std::optional{*range} : std::nullopt;
}

// The epochs of a key that meet the request, in precedence order (the one
// begun last first): the reverse of SeriesSet's epoch order.
template <class C, class Range>
std::expected<std::vector<Epoch<C>>, ResolveError> epochs_for(
    const SeriesSet& site, const SeriesKey& key, Range request) {
  std::vector<Epoch<C>> out;
  for (const SeriesInfo& s : std::views::reverse(site.epochs(key))) {
    if (const auto range = sub_range(s.validity, request)) {
      out.emplace_back(core::detail::CoreAccess::key(), s, *range);
    }
  }
  if (out.empty()) {
    return std::unexpected{ResolveError{NoSeries{}}};
  }
  return out;
}

// A series by id over the whole request.
template <class C, class Range>
std::expected<Resolved<C>, ResolveError> by_id(const SeriesSet& site,
                                               const ByTimeSeries& selector,
                                               Collection wanted,
                                               Range request) {
  const SeriesInfo* found = site.find(selector.id);
  if (found == nullptr) {
    return std::unexpected{ResolveError{NoSeries{}}};
  }
  if (collection_of(found->statistic) != wanted) {
    return std::unexpected{ResolveError{WrongCollection{}}};
  }
  std::vector<Epoch<C>> one;
  one.emplace_back(core::detail::CoreAccess::key(), *found, request);
  return Resolved<C>{core::detail::CoreAccess::key(), key_of(*found),
                     std::move(one)};
}

template <class C, class Range>
std::expected<Resolved<C>, ResolveError> by_parameter(
    const SeriesSet& site, const ParameterCode& parameter,
    const StatisticId& statistic, const std::optional<Sublocation>& sublocation,
    Range request) {
  auto key = key_for(site, parameter, statistic, sublocation);
  if (not key) {
    return std::unexpected{std::move(key).error()};
  }
  auto epochs = epochs_for<C>(site, *key, request);
  if (not epochs) {
    return std::unexpected{std::move(epochs).error()};
  }
  return Resolved<C>{core::detail::CoreAccess::key(), *std::move(key),
                     *std::move(epochs)};
}

}  // namespace

std::expected<Resolved<Continuous>, ResolveError> resolve(
    const Continuous& request, const SeriesSet& site) {
  return std::visit(
      Overloaded{[&](const ByParameter& p) {
                   return by_parameter<Continuous>(
                       site, p.parameter, StatisticId::instantaneous(),
                       p.sublocation, request.range);
                 },
                 [&](const ByTimeSeries& t) {
                   return by_id<Continuous>(site, t, Collection::continuous,
                                            request.range);
                 }},
      request.series);
}

std::expected<Resolved<Daily>, ResolveError> resolve(const Daily& request,
                                                     const SeriesSet& site) {
  return std::visit(
      Overloaded{[&](const DailyByParameter& p)
                     -> std::expected<Resolved<Daily>, ResolveError> {
                   if (collection_of(p.statistic) != Collection::daily) {
                     return std::unexpected{ResolveError{WrongCollection{}}};
                   }
                   return by_parameter<Daily>(site, p.parameter, p.statistic,
                                              p.sublocation, request.days);
                 },
                 [&](const ByTimeSeries& t) {
                   return by_id<Daily>(site, t, Collection::daily,
                                       request.days);
                 }},
      request.series);
}

}  // namespace usgs

namespace {

using usgs::Collection;
using usgs::NotOffered;
using usgs::SeriesInfo;
using usgs::StatisticId;

using Offer = std::expected<void, NotOffered>;

/// parameter, then statistic, then the sublocation (a series), as
/// offered() documents.
Offer by_parameter(const usgs::SeriesSet& site,
                   const usgs::ParameterCode& parameter,
                   const StatisticId& statistic,
                   const std::optional<usgs::Sublocation>& sublocation) {
  const auto of_parameter = [&parameter](const SeriesInfo& s) {
    return s.parameter == parameter;
  };
  if (std::ranges::none_of(site.items(), of_parameter)) {
    return std::unexpected{NotOffered::parameter};
  }
  const auto of_statistic = [&](const SeriesInfo& s) {
    return of_parameter(s) and s.statistic == statistic;
  };
  if (std::ranges::none_of(site.items(), of_statistic)) {
    return std::unexpected{NotOffered::statistic};
  }
  if (sublocation and
      std::ranges::none_of(site.items(), [&](const SeriesInfo& s) {
        return of_statistic(s) and s.sublocation == sublocation;
      })) {
    return std::unexpected{NotOffered::series};
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
                                                 StatisticId::instantaneous(),
                                                 p.sublocation);
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
                             return by_parameter(site, p.parameter, p.statistic,
                                                 p.sublocation);
                           },
                           [&site](const usgs::ByTimeSeries& t) {
                             return by_id(site, t.id, Collection::daily);
                           }},
                d.series);
          }},
      request);
}

}  // namespace mov::core
