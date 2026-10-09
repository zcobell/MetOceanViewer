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

}  // namespace mov::core::tide

namespace mov::core::detail {

// Indexed by the enumerator.
inline constexpr std::array<std::string_view, tide::node_factor_count>
    node_factor_tokens{"unity",    "sp98-73",  "sp98-74",  "sp98-75",
                       "sp98-76",  "sp98-77",  "sp98-78",  "sp98-79",
                       "sp98-144", "sp98-149", "sp98-206", "sp98-215",
                       "sp98-227", "sp98-235"};

}  // namespace mov::core::detail

namespace mov::core::tide {

/// "unity" or "sp98-<n>".
[[nodiscard]] constexpr std::string_view to_token(NodeFactor f) noexcept {
  return detail::node_factor_tokens[static_cast<std::size_t>(f)];
}

/// The enumerator whose token is exactly `token` (case-sensitive, as in the
/// file); nullopt for anything else.
[[nodiscard]] constexpr std::optional<NodeFactor> parse_node_factor(
    std::string_view token) noexcept {
  // An iterator, not a pointer: MSVC's std::array iterators are classes.
  const auto found = std::ranges::find(detail::node_factor_tokens, token);
  if (found == detail::node_factor_tokens.end()) {
    return std::nullopt;
  }
  return all_node_factors[static_cast<std::size_t>(
      found - detail::node_factor_tokens.begin())];
}

}  // namespace mov::core::tide
