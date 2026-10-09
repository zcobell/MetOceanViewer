// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <type_traits>

namespace mov::core {

/// A T that an aggregate cannot leave out: no default constructor, so a
/// designated initializer that omits the member does not compile on any
/// compiler (a warning flag catches it only on some). Converts implicitly
/// from and to T, so `.datum = Datum::mllw` and `wire_token(w.datum)` read
/// as if the member were a plain T; comparisons are T's, through the
/// conversion (a comparison of its own would make `required == t`
/// ambiguous).
template <class T>
  requires std::is_trivially_copyable_v<T>
class Required {
 public:
  Required() = delete;
  constexpr Required(T value) noexcept : value_{value} {}

  constexpr operator T() const noexcept { return value_; }
  [[nodiscard]] constexpr T get() const noexcept { return value_; }

 private:
  T value_;
};

}  // namespace mov::core
