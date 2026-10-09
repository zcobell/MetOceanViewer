// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace mov::core::tide {

/// The SP 98 node-factor formulas (docs/harmonics-json.md Appendix A.3),
/// named by their SP 98 formula numbers; `unity` is f = 1. The file token is
/// "unity" or "sp98-<n>". The schema's `node_factor` enum is drift-tested
/// against to_token over this enum.
enum class NodeFactor : std::uint8_t {
  unity,
  f73,   // MM
  f74,   // MF
  f75,   // O1, Q1, 2Q1, RHO, SIGMA1
  f76,   // J1, CHI1, MP1, SO1, THETA1
  f77,   // OO1
  f78,   // M2, N2, 2N2, NU2, MU2, LAM2
  f79,   // KJ2
  f144,  // M1C
  f149,  // M3
  f206,  // M1 (NOS)
  f215,  // L2
  f227,  // K1
  f235,  // K2
};

inline constexpr std::size_t node_factor_count = 14;

/// Every enumerator, in enumerator order.
inline constexpr std::array<NodeFactor, node_factor_count> all_node_factors{
    NodeFactor::unity, NodeFactor::f73,  NodeFactor::f74,  NodeFactor::f75,
    NodeFactor::f76,   NodeFactor::f77,  NodeFactor::f78,  NodeFactor::f79,
    NodeFactor::f144,  NodeFactor::f149, NodeFactor::f206, NodeFactor::f215,
    NodeFactor::f227,  NodeFactor::f235};
static_assert(static_cast<std::size_t>(NodeFactor::f235) + 1 ==
              node_factor_count);
static_assert([] {
  for (std::size_t i = 0; i < all_node_factors.size(); ++i) {
    if (all_node_factors[i] != static_cast<NodeFactor>(i)) {
      return false;
    }
  }
  return true;
}());

/// "unity" or "sp98-<n>". Each enumerator sits next to its token; the switch
/// has no default, so a new enumerator without a token does not compile
/// cleanly (-Wswitch).
[[nodiscard]] constexpr std::string_view to_token(NodeFactor f) noexcept {
  switch (f) {
    case NodeFactor::unity:
      return "unity";
    case NodeFactor::f73:
      return "sp98-73";
    case NodeFactor::f74:
      return "sp98-74";
    case NodeFactor::f75:
      return "sp98-75";
    case NodeFactor::f76:
      return "sp98-76";
    case NodeFactor::f77:
      return "sp98-77";
    case NodeFactor::f78:
      return "sp98-78";
    case NodeFactor::f79:
      return "sp98-79";
    case NodeFactor::f144:
      return "sp98-144";
    case NodeFactor::f149:
      return "sp98-149";
    case NodeFactor::f206:
      return "sp98-206";
    case NodeFactor::f215:
      return "sp98-215";
    case NodeFactor::f227:
      return "sp98-227";
    case NodeFactor::f235:
      return "sp98-235";
  }
  return "unity";  // unreachable: every enumerator is handled
}

/// The enumerator whose token is exactly `token` (case-sensitive, as in the
/// file); nullopt for anything else.
[[nodiscard]] constexpr std::optional<NodeFactor> parse_node_factor(
    std::string_view token) noexcept {
  // An iterator, not a pointer: MSVC's std::array iterators are classes.
  const auto found = std::ranges::find_if(
      all_node_factors, [token](NodeFactor f) { return to_token(f) == token; });
  if (found == all_node_factors.end()) {
    return std::nullopt;
  }
  return *found;
}

}  // namespace mov::core::tide
