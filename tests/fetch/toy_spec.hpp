// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// A toy FetchSpec for the machine's tests: no provider, no parser. A unit
// is named by its plan path (initial index, then successor indices), each
// parsed page is that path plus the slot, so a merge in plan order is sorted
// and the units it came from can be read back.

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

/// A unit's place: its plan path (initial index first).
using ToyPath = std::vector<std::uint32_t>;
/// A parsed page: its unit's path and slot (key_of).
using ToyKey = std::vector<std::uint32_t>;

struct ToyRequest {
  std::uint32_t units = 1;   ///< initial units
  std::uint32_t slots = 1;   ///< exchanges per unit (0 breaks the contract)
  std::uint32_t depth = 0;   ///< successor levels after a received unit
  std::uint32_t branch = 1;  ///< successors per received unit
  bool fallback = false;  ///< an absent unit gets one fallback, twice at most
  Origin origin = Origin::coops_data;
  bool fail_initial = false;  ///< initial() fails
  bool fail_next = false;     ///< next() fails on a received unit at depth 1
  friend bool operator==(const ToyRequest&, const ToyRequest&) = default;
};

struct ToyUnit {
  ToyPath path;
  std::uint32_t depth;      ///< successor levels below the initial unit
  std::uint32_t fallbacks;  ///< fallbacks taken on the way here
  friend bool operator==(const ToyUnit&, const ToyUnit&) = default;
};

struct ToyTarget {
  ToyPath path;
  std::uint32_t slot;
  Origin origin;
  friend bool operator==(const ToyTarget&, const ToyTarget&) = default;
};

/// A page's key: the path's steps + 1, a 0, then the slot, so keys sort in
/// plan order (a unit's slots before its successors).
[[nodiscard]] inline ToyKey key_of(const ToyTarget& t) {
  ToyKey k;
  for (const std::uint32_t step : t.path) {
    k.push_back(step + 1);
  }
  k.push_back(0);
  k.push_back(t.slot);
  return k;
}

/// The path a key names.
[[nodiscard]] inline ToyPath path_of(const ToyKey& k) {
  ToyPath path;
  for (const std::uint32_t step : k) {
    if (step == 0) {
      break;
    }
    path.push_back(step - 1);
  }
  return path;
}

/// A label index that names a path (unit < 8, at most 6 levels, index < 3).
[[nodiscard]] inline std::uint32_t label_index(const ToyPath& path) {
  constexpr std::uint32_t base = 4;
  std::uint32_t index = 0;
  for (const std::uint32_t step : path) {
    index = (index * base) + step + 1;
  }
  return index;
}

struct ToyPage {
  std::vector<ToyKey> keys;
};

struct ToySpec {
  using Request = ToyRequest;
  using Unit = ToyUnit;
  using Target = ToyTarget;
  using Parsed = ToyPage;
  using Part = std::vector<ToyKey>;
  using Value = std::vector<ToyKey>;

  static std::expected<std::vector<Unit>, Error> initial(const Request& r,
                                                         core::Time /*now*/) {
    if (r.fail_initial) {
      return std::unexpected{Error{NoData{}}};
    }
    std::vector<Unit> units;
    for (std::uint32_t i = 0; i < r.units; ++i) {
      units.push_back({.path = {i}, .depth = 0, .fallbacks = 0});
    }
    return units;
  }

  static std::vector<Target> targets(const Request& r, const Unit& u) {
    std::vector<Target> out;
    for (std::uint32_t s = 0; s < r.slots; ++s) {
      out.push_back({.path = u.path, .slot = s, .origin = r.origin});
    }
    return out;
  }

  static Origin origin(const Target& t) { return t.origin; }

  static UnitLabel label(const Request& /*r*/, const Unit& u) {
    return CatalogPart{.index = label_index(u.path)};
  }

  static std::expected<std::vector<Unit>, Error> next(
      const Request& r, const Unit& u, const UnitOutcome<Parsed>& outcome) {
    constexpr std::uint32_t max_fallbacks = 2;
    std::vector<Unit> out;
    const auto child = [&](std::uint32_t k, std::uint32_t depth,
                           std::uint32_t fallbacks) {
      ToyPath path = u.path;
      path.push_back(k);
      out.push_back(
          {.path = std::move(path), .depth = depth, .fallbacks = fallbacks});
    };
    if (std::holds_alternative<Absent>(outcome)) {
      if (r.fallback and u.fallbacks < max_fallbacks) {
        child(0, u.depth, u.fallbacks + 1);
      }
      return out;
    }
    if (r.fail_next and u.depth == 1) {
      return std::unexpected{
          Error{PagingError{.rule = PagingRule::link_repeated, .page = 1}}};
    }
    if (u.depth < r.depth) {
      for (std::uint32_t k = 0; k < r.branch; ++k) {
        child(k, u.depth + 1, u.fallbacks);
      }
    }
    return out;
  }

  static Part into_part(std::vector<SlotResult<Parsed>> slots) {
    Part part;
    for (auto& s : slots) {
      if (auto* page = std::get_if<ToyPage>(&s)) {
        for (auto& k : page->keys) {
          part.push_back(std::move(k));
        }
      }
    }
    return part;
  }

  static bool has_data(const Part& p) { return not p.empty(); }

  static std::expected<io::Read<Value>, Error> merge(std::vector<Part> parts) {
    Value all;
    for (auto& p : parts) {
      for (auto& k : p) {
        all.push_back(std::move(k));
      }
    }
    return io::Read<Value>{.value = std::move(all), .warnings = {}};
  }
};

static_assert(FetchSpec<ToySpec>);

/// The page a target parses to: its key.
[[nodiscard]] inline ToyPage page_for(const ToyTarget& t) {
  return ToyPage{.keys = {key_of(t)}};
}

}  // namespace mov::fetch::test
