// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <type_traits>
#include <utility>

namespace mov::core {

/// A set of enumerators of E whose underlying values are below N. It holds
/// no duplicates and no order, so two sets with the same members are equal
/// however they were built (a vector would make == depend on the order).
/// Precondition (asserted): every enumerator passed in is below N; each enum
/// used with it declares N next to the enum and static_asserts it.
template <class E, std::size_t N>
  requires std::is_enum_v<E> and (N <= 64)
class EnumSet {
 public:
  constexpr EnumSet() noexcept = default;

  /// The set of these members; a repeated member counts once.
  constexpr EnumSet(std::initializer_list<E> members) noexcept {
    for (const E e : members) {
      insert(e);
    }
  }

  constexpr void insert(E e) noexcept { bits_ |= bit(e); }

  [[nodiscard]] constexpr bool contains(E e) const noexcept {
    return (bits_ & bit(e)) != 0;
  }
  [[nodiscard]] constexpr bool empty() const noexcept { return bits_ == 0; }

  friend constexpr bool operator==(EnumSet, EnumSet) = default;

 private:
  [[nodiscard]] static constexpr std::uint64_t bit(E e) noexcept {
    const auto index = static_cast<std::size_t>(std::to_underlying(e));
    assert(index < N);
    return std::uint64_t{1} << index;
  }

  std::uint64_t bits_{};
};

}  // namespace mov::core
