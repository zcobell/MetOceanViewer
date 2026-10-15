// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// A toy FetchSpec for the machine's tests: no provider, no parser. Each
// parsed page is one number that encodes where it came from, so the merge's
// plan order can be checked by sorting.

#include <chrono>
#include <cstdint>
#include <expected>
#include <utility>
#include <variant>
#include <vector>

#include "mov/core/time.hpp"
#include "mov/fetch/error.hpp"
#include "mov/fetch/origin.hpp"
#include "mov/fetch/steps.hpp"

namespace mov::fetch::test {

struct ToyRequest {
  std::uint32_t units = 1;  ///< initial units
  std::uint32_t slots = 1;  ///< exchanges per unit
  std::uint32_t pages =
      1;  ///< units per chain: a received unit adds the next page
  bool fallback = false;  ///< an absent unit is followed by one fallback unit
  Origin origin = Origin::coops_data;
  bool fail_initial = false;  ///< initial() fails
  bool fail_next = false;     ///< next() fails on a received page 1
  friend bool operator==(const ToyRequest&, const ToyRequest&) = default;
};

struct ToyUnit {
  std::uint32_t unit;
  std::uint32_t page;
  bool fallback;
  friend bool operator==(const ToyUnit&, const ToyUnit&) = default;
};

struct ToyTarget {
  ToyUnit unit;
  std::uint32_t slot;
  Origin origin;
  friend bool operator==(const ToyTarget&, const ToyTarget&) = default;
};

/// The number a target's page parses to: ascending in plan order (unit,
/// then the original chain before the fallback chain, then page, then
/// slot), so a merge in plan order is sorted.
[[nodiscard]] inline std::int64_t encode(const ToyTarget& t) {
  constexpr std::int64_t per_unit = 1'000'000;
  constexpr std::int64_t per_fallback = 100'000;
  constexpr std::int64_t per_page = 100;
  return (static_cast<std::int64_t>(t.unit.unit) * per_unit) +
         (t.unit.fallback ? per_fallback : 0) +
         (static_cast<std::int64_t>(t.unit.page) * per_page) +
         static_cast<std::int64_t>(t.slot);
}

struct ToyPage {
  std::vector<std::int64_t> values;
};

struct ToySpec {
  using Request = ToyRequest;
  using Unit = ToyUnit;
  using Target = ToyTarget;
  using Parsed = ToyPage;
  using Part = std::vector<std::int64_t>;
  using Value = std::vector<std::int64_t>;

  static std::expected<std::vector<Unit>, Error> initial(const Request& r,
                                                         core::Time /*now*/) {
    if (r.fail_initial) {
      return std::unexpected{Error{NoData{}}};
    }
    std::vector<Unit> units;
    for (std::uint32_t i = 0; i < r.units; ++i) {
      units.push_back({.unit = i, .page = 0, .fallback = false});
    }
    return units;
  }

  static std::vector<Target> targets(const Request& r, const Unit& u) {
    std::vector<Target> out;
    for (std::uint32_t s = 0; s < r.slots; ++s) {
      out.push_back({.unit = u, .slot = s, .origin = r.origin});
    }
    return out;
  }

  static Origin origin(const Target& t) { return t.origin; }

  static UnitLabel label(const Request& /*r*/, const Unit& u) {
    constexpr std::uint32_t per_unit = 1000;
    constexpr std::uint32_t per_fallback = 500;
    return CatalogPart{.index = (u.unit * per_unit) +
                                (u.fallback ? per_fallback : 0U) + u.page};
  }

  static std::expected<std::vector<Unit>, Error> next(
      const Request& r, const Unit& u, const UnitOutcome<Parsed>& outcome) {
    if (std::holds_alternative<Absent>(outcome)) {
      if (r.fallback and not u.fallback) {
        return std::vector<Unit>{
            {.unit = u.unit, .page = u.page, .fallback = true}};
      }
      return std::vector<Unit>{};
    }
    if (r.fail_next and u.page == 1) {
      return std::unexpected{Error{MetaMismatch{.column = "toy"}}};
    }
    if (u.page + 1 < r.pages) {
      return std::vector<Unit>{
          {.unit = u.unit, .page = u.page + 1, .fallback = u.fallback}};
    }
    return std::vector<Unit>{};
  }

  static Part into_part(Parsed&& p) { return std::move(p.values); }
  static bool has_data(const Part& p) { return not p.empty(); }
};

static_assert(FetchSpec<ToySpec>);

}  // namespace mov::fetch::test
