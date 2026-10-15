// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <numeric>
#include <optional>
#include <vector>

// Positions of a sequence ordered by a key, and the first occurrence of each
// key: what the harmonics file model sorts and checks for duplicates with
// (sources, dictionary names, rows, station ids).

namespace mov::core::detail {

/// 0, 1, ..., n - 1 ordered by key(i) under `less`; equivalent keys keep
/// their positions' order. (std::stable_sort is constexpr only from C++26,
/// so ties compare positions.)
template <class Key, class Less = std::ranges::less>
[[nodiscard]] constexpr std::vector<std::size_t> sorted_index(std::size_t n,
                                                              Key key,
                                                              Less less = {}) {
  std::vector<std::size_t> order(n);
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::ranges::sort(order, [&](std::size_t a, std::size_t b) {
    if (less(key(a), key(b))) {
      return true;
    }
    return not less(key(b), key(a)) and a < b;
  });
  return order;
}

/// For each position, the earliest earlier position whose key is equivalent
/// under `less`; nullopt for a key's first occurrence.
template <class Key, class Less = std::ranges::less>
[[nodiscard]] constexpr std::vector<std::optional<std::size_t>>
first_occurrence(std::size_t n, Key key, Less less = {}) {
  const std::vector<std::size_t> order = sorted_index(n, key, less);
  std::vector<std::optional<std::size_t>> first(n);
  std::size_t group = 0;  // where the current run of equivalent keys starts
  for (std::size_t k = 1; k < order.size(); ++k) {
    if (less(key(order[group]), key(order[k]))) {
      group = k;
    } else {
      first[order[k]] = order[group];
    }
  }
  return first;
}

/// The earliest position whose key equals `value`, given `order` from
/// sorted_index(n, key) with the default order.
template <class Key, class T>
[[nodiscard]] constexpr std::optional<std::size_t> find_sorted(
    const std::vector<std::size_t>& order, const T& value, Key key) {
  const auto found = std::ranges::lower_bound(order, value, {}, key);
  if (found == order.end() or key(*found) != value) {
    return std::nullopt;
  }
  return *found;
}

}  // namespace mov::core::detail
