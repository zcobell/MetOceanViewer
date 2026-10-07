// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <concepts>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

#include "mov/core/detail/numeric.hpp"
#include "mov/core/detail/overloaded.hpp"

namespace mov::core {

/// No measurement: a gap, a fill value, a masked or non-finite number.
struct Missing {
  friend constexpr bool operator==(Missing, Missing) = default;
};

/// A model point that was dry (no water) rather than unmeasured.
struct Dry {
  friend constexpr bool operator==(Dry, Dry) = default;
};

/// One observation: Missing, Dry, or a finite double. Finite by construction:
/// the only ways to a value are Sample::of (refuses NaN and infinities) and
/// finite_or_missing (turns them into Missing), so no sentinel or NaN is ever
/// stored. Dry is allowed on any quantity.
class Sample {
 public:
  constexpr Sample() noexcept = default;  // Missing
  constexpr Sample(Missing) noexcept {}
  constexpr Sample(Dry) noexcept : v_{Dry{}} {}

  /// nullopt iff v is NaN or infinite.
  [[nodiscard]] static constexpr std::optional<Sample> of(double v) noexcept {
    if (not detail::is_finite(v)) {
      return std::nullopt;
    }
    return Sample{v, FiniteTag{}};
  }

  [[nodiscard]] constexpr std::optional<double> value() const noexcept {
    if (const double* v = std::get_if<double>(&v_)) {
      return *v;
    }
    return std::nullopt;
  }
  [[nodiscard]] constexpr bool is_value() const noexcept {
    return std::holds_alternative<double>(v_);
  }
  [[nodiscard]] constexpr bool is_dry() const noexcept {
    return std::holds_alternative<Dry>(v_);
  }
  [[nodiscard]] constexpr bool is_missing() const noexcept {
    return std::holds_alternative<Missing>(v_);
  }

  /// Calls the overload of f... that takes Missing, Dry or double.
  template <class... F>
  [[nodiscard]] constexpr decltype(auto) visit(F&&... f) const {
    return std::visit(detail::Overloaded{std::forward<F>(f)...}, v_);
  }

  friend constexpr bool operator==(const Sample&, const Sample&) = default;

 private:
  struct FiniteTag {};
  constexpr Sample(double finite, FiniteTag) noexcept : v_{finite} {}

  std::variant<Missing, Dry, double> v_;
};

/// The one lossy total factory: a non-finite v becomes Missing.
[[nodiscard]] constexpr Sample finite_or_missing(double v) noexcept {
  return Sample::of(v).value_or(Sample{});
}

/// Missing if either operand is Missing; else Dry if either is Dry; else
/// finite_or_missing(op(a, b)). op is not called unless both are values.
template <std::invocable<double, double> Op>
[[nodiscard]] constexpr Sample combine(Sample a, Sample b, Op op) noexcept(
    std::is_nothrow_invocable_v<Op&, double, double>) {
  const std::optional<double> x = a.value();
  const std::optional<double> y = b.value();
  if (x and y) {
    return finite_or_missing(op(*x, *y));
  }
  return (a.is_missing() or b.is_missing()) ? Sample{Missing{}} : Sample{Dry{}};
}

static_assert(sizeof(Sample) == 16, "C1: a Sample is 16 bytes");

}  // namespace mov::core
