// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <concepts>
#include <expected>
#include <functional>
#include <iterator>
#include <type_traits>
#include <utility>
#include <vector>

#include "mov/io/warning.hpp"

namespace mov::io {

template <class T>
struct Read;

namespace detail {

template <class R>
inline constexpr bool is_read_v = false;
template <class T>
inline constexpr bool is_read_v<Read<T>> = true;

/// `std::expected<Read<U>, E>`: the shape and_then_read's continuation returns.
template <class X>
struct ExpectedRead {
  static constexpr bool value = false;
};
template <class U, class E>
struct ExpectedRead<std::expected<Read<U>, E>> {
  static constexpr bool value = true;
  using read_type = Read<U>;
  using error_type = E;
};

template <class F, class T>
using continuation_result_t = std::remove_cvref_t<std::invoke_result_t<F, T&&>>;

/// Appends `later` to `earlier` and returns the result: ours first.
[[nodiscard]] inline std::vector<Warning> concatenated(
    std::vector<Warning> earlier, std::vector<Warning> later) {
  earlier.insert(earlier.end(), std::make_move_iterator(later.begin()),
                 std::make_move_iterator(later.end()));
  return earlier;
}

}  // namespace detail

/// A value together with what was noticed while producing it: a writer monad
/// over warnings. Every reader returns `std::expected<Read<T>, E>`; an error
/// carries no warnings, because a reader that failed has no value to qualify.
///
/// Both combinators consume `*this` (`std::move(r).transform(f)`) and keep
/// the warnings in order: the earlier stage's first.
template <class T>
struct Read {
  T value;
  std::vector<Warning> warnings;

  /// Maps the value; the warnings are carried over.
  template <class F>
    requires std::invocable<F, T&&> and
             std::is_object_v<std::invoke_result_t<F, T&&>>
  [[nodiscard]] auto transform(
      F&& f) && -> Read<std::remove_cv_t<std::invoke_result_t<F, T&&>>> {
    return {.value = std::invoke(std::forward<F>(f), std::move(value)),
            .warnings = std::move(warnings)};
  }

  /// Continues with a function that itself returns a `Read<U>`; its warnings
  /// come after ours.
  template <class F>
    requires std::invocable<F, T&&> and
             detail::is_read_v<detail::continuation_result_t<F, T>>
  [[nodiscard]] auto and_then(F&& f) && -> detail::continuation_result_t<F, T> {
    auto next = std::invoke(std::forward<F>(f), std::move(value));
    next.warnings =
        detail::concatenated(std::move(warnings), std::move(next.warnings));
    return next;
  }

  friend bool operator==(const Read&, const Read&) = default;
};

/// Continues a `std::expected<Read<T>, E>` with `f: T&& -> expected<Read<U>,
/// E2>`, where E2 converts to E. Yields `expected<Read<U>, E>`; the warnings
/// of both stages are concatenated, ours first.
template <class T, class E, class F>
  requires std::invocable<F, T&&> and
           detail::ExpectedRead<detail::continuation_result_t<F, T>>::value and
           std::convertible_to<
               typename detail::ExpectedRead<
                   detail::continuation_result_t<F, T>>::error_type,
               E>
[[nodiscard]] auto and_then_read(std::expected<Read<T>, E>&& r, F&& f)
    -> std::expected<typename detail::ExpectedRead<
                         detail::continuation_result_t<F, T>>::read_type,
                     E> {
  if (not r) {
    return std::unexpected<E>{std::move(r).error()};
  }
  Read<T> first = *std::move(r);
  auto next = std::invoke(std::forward<F>(f), std::move(first.value));
  if (not next) {
    return std::unexpected<E>{std::move(next).error()};
  }
  next->warnings = detail::concatenated(std::move(first.warnings),
                                        std::move(next->warnings));
  return *std::move(next);
}

}  // namespace mov::io
