// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// Two-stage test specs over fetch's real plans, for Then: USGS resolution
// then data (usgs::bind between them), and a CO-OPS catalog's station lists
// then datums. The parsed pages are synthetic (P2–P4 own the parsers).

#include <algorithm>
#include <cstdint>
#include <expected>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/provider/coops.hpp"
#include "mov/core/provider/usgs.hpp"
#include "mov/core/station.hpp"
#include "mov/core/time.hpp"
#include "mov/fetch/error.hpp"
#include "mov/fetch/paging.hpp"
#include "mov/fetch/plan/coops.hpp"
#include "mov/fetch/plan/usgs.hpp"
#include "mov/fetch/steps.hpp"

namespace mov::fetch::test {

using Link = std::string;
namespace cu = core::usgs;

// ---- USGS ------------------------------------------------------------------

struct ResolvePage {
  std::vector<cu::SeriesInfo> rows;
  PageSummary<Link, NoPosition> summary;
};

struct UsgsResolveSpec {
  using Request = cu::Continuous;
  using Unit = usgs::ResolutionUnit<Link>;
  using Target = usgs::ResolutionUnit<Link>;
  using Parsed = ResolvePage;
  using Part = std::vector<cu::SeriesInfo>;
  using Value = std::vector<cu::SeriesInfo>;

  static std::expected<std::vector<Unit>, Error> initial(const Request& /*r*/,
                                                         core::Time /*now*/) {
    return std::vector<Unit>{usgs::first_resolution_page<Link>()};
  }
  static std::vector<Target> targets(const Request& /*r*/, const Unit& u) {
    return {u};
  }
  static Origin origin(const Target& /*t*/) { return Origin::usgs; }
  static UnitLabel label(const Request& /*r*/, const Unit& u) {
    return usgs::label(u);
  }
  static std::expected<std::vector<Unit>, Error> next(
      const Request& /*r*/, const Unit& u, const UnitOutcome<Parsed>& outcome) {
    const auto* received = std::get_if<Received<Parsed>>(&outcome);
    if (received == nullptr) {
      return std::vector<Unit>{};
    }
    return usgs::next(u, std::get<ResolvePage>(received->slots[0]).summary);
  }
  static Part into_part(std::vector<SlotResult<Parsed>> slots) {
    Part rows;
    for (auto& s : slots) {
      if (auto* page = std::get_if<ResolvePage>(&s)) {
        rows.insert(rows.end(), page->rows.begin(), page->rows.end());
      }
    }
    return rows;
  }
  static bool has_data(const Part& p) { return not p.empty(); }
  static std::expected<io::Read<Value>, Error> merge(std::vector<Part> parts) {
    Value all;
    for (auto& p : parts) {
      all.insert(all.end(), p.begin(), p.end());
    }
    return io::Read<Value>{.value = std::move(all), .warnings = {}};
  }
};

struct DataPage {
  std::vector<std::int64_t> values;
  PageSummary<Link, core::Time> summary;
};

struct UsgsDataSpec {
  using Request = usgs::DataRequest<cu::Continuous>;
  using Unit = usgs::ContinuousUnit<Link>;
  using Target = usgs::ContinuousUnit<Link>;
  using Parsed = DataPage;
  using Part = std::vector<std::int64_t>;
  using Value = std::vector<std::int64_t>;

  static std::expected<std::vector<Unit>, Error> initial(const Request& r,
                                                         core::Time /*now*/) {
    return usgs::initial<Link>(r);
  }
  static std::vector<Target> targets(const Request& /*r*/, const Unit& u) {
    return {u};
  }
  static Origin origin(const Target& /*t*/) { return Origin::usgs; }
  static UnitLabel label(const Request& /*r*/, const Unit& u) {
    return usgs::label(u);
  }
  static std::expected<std::vector<Unit>, Error> next(
      const Request& /*r*/, const Unit& u, const UnitOutcome<Parsed>& outcome) {
    const auto* received = std::get_if<Received<Parsed>>(&outcome);
    if (received == nullptr) {
      return std::vector<Unit>{};
    }
    return usgs::next(u, std::get<DataPage>(received->slots[0]).summary);
  }
  static Part into_part(std::vector<SlotResult<Parsed>> slots) {
    Part values;
    for (auto& s : slots) {
      if (auto* page = std::get_if<DataPage>(&s)) {
        values.insert(values.end(), page->values.begin(), page->values.end());
      }
    }
    return values;
  }
  static bool has_data(const Part& p) { return not p.empty(); }
  static std::expected<io::Read<Value>, Error> merge(std::vector<Part> parts) {
    Value all;
    for (auto& p : parts) {
      all.insert(all.end(), p.begin(), p.end());
    }
    return io::Read<Value>{.value = std::move(all), .warnings = {}};
  }
};

struct UsgsBind {
  static std::expected<UsgsDataSpec::Request, Failure> bind(
      const cu::Continuous& request, std::vector<cu::SeriesInfo> rows) {
    return usgs::bind(request, rows);
  }
};

static_assert(FetchSpec<UsgsResolveSpec> and FetchSpec<UsgsDataSpec>);

// ---- CO-OPS catalog --------------------------------------------------------

using CoopsId = core::StationId<core::provider::Coops>;

/// The station lists to read (waterlevels, met, tidepredictions).
struct StationsRequest {
  std::uint32_t lists;
  friend bool operator==(const StationsRequest&,
                         const StationsRequest&) = default;
};
struct ListUnit {
  std::uint32_t index;
  friend bool operator==(const ListUnit&, const ListUnit&) = default;
};
struct StationsPage {
  std::vector<CoopsId> stations;
};

struct CoopsStationsSpec {
  using Request = StationsRequest;
  using Unit = ListUnit;
  using Target = ListUnit;
  using Parsed = StationsPage;
  using Part = std::vector<CoopsId>;
  using Value = std::vector<CoopsId>;

  static std::expected<std::vector<Unit>, Error> initial(const Request& r,
                                                         core::Time /*now*/) {
    std::vector<Unit> units;
    for (std::uint32_t i = 0; i < r.lists; ++i) {
      units.push_back({.index = i});
    }
    return units;
  }
  static std::vector<Target> targets(const Request& /*r*/, const Unit& u) {
    return {u};
  }
  static Origin origin(const Target& /*t*/) { return Origin::coops_metadata; }
  static UnitLabel label(const Request& /*r*/, const Unit& u) {
    return CatalogPart{.index = u.index};
  }
  static std::expected<std::vector<Unit>, Error> next(
      const Request& /*r*/, const Unit& /*u*/,
      const UnitOutcome<Parsed>& /*outcome*/) {
    return std::vector<Unit>{};
  }
  static Part into_part(std::vector<SlotResult<Parsed>> slots) {
    Part ids;
    for (auto& s : slots) {
      if (auto* page = std::get_if<StationsPage>(&s)) {
        ids.insert(ids.end(), page->stations.begin(), page->stations.end());
      }
    }
    return ids;
  }
  static bool has_data(const Part& p) { return not p.empty(); }
  static std::expected<io::Read<Value>, Error> merge(std::vector<Part> parts) {
    Value all;
    for (auto& p : parts) {
      all.insert(all.end(), p.begin(), p.end());
    }
    std::ranges::sort(all);
    const auto [first, last] = std::ranges::unique(all);
    all.erase(first, last);
    return io::Read<Value>{.value = std::move(all), .warnings = {}};
  }
};

struct DatumsPage {
  std::uint32_t index;
};

struct CoopsDatumsSpec {
  using Request = coops::DatumsRequest;
  using Unit = coops::DatumsUnit;
  using Target = coops::DatumsUnit;
  using Parsed = DatumsPage;
  using Part = std::vector<std::uint32_t>;
  using Value = std::vector<std::uint32_t>;

  static std::expected<std::vector<Unit>, Error> initial(const Request& r,
                                                         core::Time /*now*/) {
    return coops::initial(r);
  }
  static std::vector<Target> targets(const Request& /*r*/, const Unit& u) {
    return {u};
  }
  static Origin origin(const Target& /*t*/) { return Origin::coops_metadata; }
  static UnitLabel label(const Request& /*r*/, const Unit& u) {
    return coops::label(u);
  }
  static std::expected<std::vector<Unit>, Error> next(
      const Request& /*r*/, const Unit& /*u*/,
      const UnitOutcome<Parsed>& /*outcome*/) {
    return std::vector<Unit>{};
  }
  static Part into_part(std::vector<SlotResult<Parsed>> slots) {
    Part out;
    for (auto& s : slots) {
      if (auto* page = std::get_if<DatumsPage>(&s)) {
        out.push_back(page->index);
      }
    }
    return out;
  }
  static bool has_data(const Part& p) { return not p.empty(); }
  static std::expected<io::Read<Value>, Error> merge(std::vector<Part> parts) {
    Value all;
    for (auto& p : parts) {
      all.insert(all.end(), p.begin(), p.end());
    }
    return io::Read<Value>{.value = std::move(all), .warnings = {}};
  }
};

struct CoopsBind {
  static std::expected<coops::DatumsRequest, Failure> bind(
      const StationsRequest& /*request*/, std::vector<CoopsId> stations) {
    return coops::DatumsRequest{.stations = std::move(stations)};
  }
};

static_assert(FetchSpec<CoopsStationsSpec> and FetchSpec<CoopsDatumsSpec>);

}  // namespace mov::fetch::test
