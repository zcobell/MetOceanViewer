// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <array>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <type_traits>
#include <utility>

namespace mov::core {

/// Every enumerator of E, in order: specialize with a member
/// `static constexpr std::array<E, N> values` (or a reference to one). A
/// DenseEnum's i-th value has the underlying value i, so the array is the
/// whole enum and its size is the enumerator count.
template <class E>
struct Enumerators;

namespace detail {

template <class E>
[[nodiscard]] constexpr bool dense_enumerators() {
  const auto& values = Enumerators<E>::values;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (static_cast<std::size_t>(std::to_underlying(values[i])) != i) {
      return false;
    }
  }
  return not values.empty() and values.size() <= 64;
}

}  // namespace detail

/// An enum whose Enumerators<E>::values lists 0, 1, ..., N - 1 in order,
/// with 1 <= N <= 64: the enumerators EnumSet can hold.
template <class E>
concept DenseEnum = std::is_enum_v<E> and requires {
  { Enumerators<E>::values.size() } -> std::convertible_to<std::size_t>;
} and detail::dense_enumerators<E>();

/// A set of enumerators of a DenseEnum. It holds no duplicates and no order,
/// so two sets with the same members are equal however they were built (a
/// vector would make == depend on the order). A monoid under | (union) with
/// the empty set as identity; & is the intersection. A value outside the
/// enumerators (a bad cast) is never stored: asserted, and ignored in release
/// builds, so it cannot make two equal sets compare unequal.
template <DenseEnum E>
class EnumSet {
 public:
  static constexpr std::size_t capacity = Enumerators<E>::values.size();

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

  friend constexpr EnumSet operator|(EnumSet a, EnumSet b) noexcept {
    return EnumSet(a.bits_ | b.bits_);
  }
  friend constexpr EnumSet operator&(EnumSet a, EnumSet b) noexcept {
    return EnumSet(a.bits_ & b.bits_);
  }
  friend constexpr bool operator==(EnumSet, EnumSet) = default;

 private:
  explicit constexpr EnumSet(std::uint64_t bits) noexcept : bits_{bits} {}

  /// The bit of e; 0 for a value outside the enumerators.
  [[nodiscard]] static constexpr std::uint64_t bit(E e) noexcept {
    const auto index = static_cast<std::size_t>(std::to_underlying(e));
    assert(index < capacity);
    return index < capacity ? std::uint64_t{1} << index : std::uint64_t{0};
  }

  std::uint64_t bits_{};
};

}  // namespace mov::core
