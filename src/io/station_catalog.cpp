// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The station catalog model: Catalog<P> and the rules for loading and
// refreshing it (docs/providers-design.md section 7).

#include "mov/io/station_catalog.hpp"

#include <algorithm>
#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/provider/capabilities.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"

namespace mov::io {

template <CatalogProvider P>
std::expected<Catalog<P>, DuplicateStation<P>> Catalog<P>::make(
    CatalogHeader header, std::vector<Entry> entries) {
  const auto id_of = [](const Entry& e) -> const core::StationId<P>& {
    return e.station.id;
  };
  std::ranges::stable_sort(entries, std::less<>{}, id_of);
  const auto repeated =
      std::ranges::adjacent_find(entries, std::ranges::equal_to{}, id_of);
  if (repeated != entries.end()) {
    return std::unexpected{DuplicateStation<P>{.id = repeated->station.id}};
  }
  return Catalog{std::move(header), std::move(entries)};
}

template <CatalogProvider P>
auto Catalog<P>::find(const core::StationId<P>& id) const& noexcept
    -> const Entry* {
  const auto found = std::ranges::lower_bound(
      entries_, id, std::less<>{},
      [](const Entry& e) -> const core::StationId<P>& { return e.station.id; });
  if (found == entries_.end() or found->station.id != id) {
    return nullptr;
  }
  return &*found;
}

core::DataSource source_of(const StationCatalog& catalog) {
  return std::visit([]<class P>(const Catalog<P>& /*c*/) { return P::source; },
                    catalog);
}

bool is_stale(const CatalogHeader& header, core::Time now) noexcept {
  return now - header.generated > catalog_max_age;
}

template <CatalogProvider P>
Chosen<P> choose_catalog(
    Catalog<P> snapshot,
    std::type_identity_t<std::optional<Catalog<P>>> cache) {
  if (cache and cache->header().generated > snapshot.header().generated) {
    return {.catalog = *std::move(cache), .origin = CatalogOrigin::cache};
  }
  return {.catalog = std::move(snapshot), .origin = CatalogOrigin::snapshot};
}

template <CatalogProvider P>
std::expected<Catalog<P>, RefreshRefused> accept_refresh(
    const Catalog<P>& current, Catalog<P> refreshed) {
  if (refreshed.header().generated <= current.header().generated) {
    return std::unexpected{NotNewer{.current = current.header().generated,
                                    .refreshed = refreshed.header().generated}};
  }
  // At least half, rounded up, without forming 2 * size.
  const std::size_t half = (current.size() / 2) + (current.size() % 2);
  if (refreshed.size() < half) {
    return std::unexpected{TooFewStations{.current = current.size(),
                                          .refreshed = refreshed.size()}};
  }
  return refreshed;
}

// The three catalog providers.
template class Catalog<core::provider::Coops>;
template class Catalog<core::provider::Usgs>;
template class Catalog<core::provider::Ndbc>;

template Chosen<core::provider::Coops> choose_catalog(
    Catalog<core::provider::Coops>,
    std::optional<Catalog<core::provider::Coops>>);
template Chosen<core::provider::Usgs> choose_catalog(
    Catalog<core::provider::Usgs>,
    std::optional<Catalog<core::provider::Usgs>>);
template Chosen<core::provider::Ndbc> choose_catalog(
    Catalog<core::provider::Ndbc>,
    std::optional<Catalog<core::provider::Ndbc>>);

template std::expected<Catalog<core::provider::Coops>, RefreshRefused>
accept_refresh(const Catalog<core::provider::Coops>&,
               Catalog<core::provider::Coops>);
template std::expected<Catalog<core::provider::Usgs>, RefreshRefused>
accept_refresh(const Catalog<core::provider::Usgs>&,
               Catalog<core::provider::Usgs>);
template std::expected<Catalog<core::provider::Ndbc>, RefreshRefused>
accept_refresh(const Catalog<core::provider::Ndbc>&,
               Catalog<core::provider::Ndbc>);

}  // namespace mov::io
