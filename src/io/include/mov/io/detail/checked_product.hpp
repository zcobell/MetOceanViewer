// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <limits>
#include <optional>
#include <span>

namespace mov::io::detail {

/// The product of `factors` (element counts, dimension lengths), or nullopt
/// when it does not fit in std::size_t. The product of no factors is 1,
/// the element count of a scalar. A zero factor makes the product 0 whatever
/// the other factors are, so an empty hyperslab of huge dimensions is not an
/// overflow.
[[nodiscard]] constexpr std::optional<std::size_t> checked_product(
    std::span<const std::size_t> factors) noexcept {
  constexpr std::size_t max = std::numeric_limits<std::size_t>::max();
  std::size_t product = 1;
  bool overflow = false;
  for (const std::size_t factor : factors) {
    if (factor == 0) {
      return 0;
    }
    overflow = overflow or product > max / factor;
    product = overflow ? product : product * factor;
  }
  return overflow ? std::nullopt : std::optional{product};
}

}  // namespace mov::io::detail
