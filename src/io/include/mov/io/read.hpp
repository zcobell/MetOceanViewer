// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <concepts>
#include <expected>
#include <functional>
#include <iterator>
#include <tuple>
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
/// over warnings. Every reader returns `std::expected<Read<T>, E>`, which is
/// `WriterT Warnings (Either E)`: the transformer order is the one where an
/// error discards the warnings so far, because a reader that failed has no
/// value to qualify. (`Either E (Writer W a)` is this type; `Writer W (Either
/// E a)`, which would keep them, is not.) `pure` is the unit, `and_then` and
/// `and_then_read` the bind, and the monad laws hold: `pure(x).and_then(f) ==
/// f(x)`, `m.and_then(pure) == m`, and `and_then` is associative (tested).
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

/// A value with no warnings.
template <class T>
[[nodiscard]] Read<std::remove_cvref_t<T>> pure(T&& value) {
  return {.value = std::forward<T>(value), .warnings = {}};
}

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

namespace detail {

/// What a thunk returns, without reference or const.
template <class F>
using thunk_result_t = std::remove_cvref_t<std::invoke_result_t<const F&>>;

/// The T of a `std::expected<Read<T>, E>`.
template <class X>
using read_value_t = decltype(std::declval<typename X::value_type>().value);

}  // namespace detail

/// The applicative counterpart of `and_then`: runs the thunks in order, each
/// returning a `std::expected<T_i, E>` (one E for all), and yields the tuple
/// of their values, or the first error, after which no further thunk runs.
/// For stages that do not depend on each other's values, so no value is
/// unwrapped before every stage has succeeded:
///
///   auto parts = collect([&] { return a(); }, [&] { return b(); });
///   if (not parts) { return std::unexpected{std::move(parts).error()}; }
///   auto& [x, y] = *parts;
template <class F>
  requires std::invocable<const F&>
[[nodiscard]] auto collect(const F& f)
    -> std::expected<std::tuple<typename detail::thunk_result_t<F>::value_type>,
                     typename detail::thunk_result_t<F>::error_type> {
  using T = typename detail::thunk_result_t<F>::value_type;
  return std::invoke(f).transform(
      [](T&& v) { return std::tuple<T>{std::move(v)}; });
}

template <class F, class G, class... Rest>
  requires std::invocable<const F&> and std::invocable<const G&> and
           (std::invocable<const Rest&> and ...)
[[nodiscard]] auto collect(const F& f, const G& g, const Rest&... rest)
    -> std::expected<
        std::tuple<typename detail::thunk_result_t<F>::value_type,
                   typename detail::thunk_result_t<G>::value_type,
                   typename detail::thunk_result_t<Rest>::value_type...>,
        typename detail::thunk_result_t<F>::error_type> {
  using T = typename detail::thunk_result_t<F>::value_type;
  using Out = std::expected<
      std::tuple<T, typename detail::thunk_result_t<G>::value_type,
                 typename detail::thunk_result_t<Rest>::value_type...>,
      typename detail::thunk_result_t<F>::error_type>;
  return std::invoke(f).and_then([&](T&& head) -> Out {
    return collect(g, rest...).transform([&](auto&& tail) {
      return std::tuple_cat(std::tuple<T>{std::move(head)}, std::move(tail));
    });
  });
}

/// collect over stages that return `std::expected<Read<T_i>, E>`: the
/// values as a tuple, and the warnings of every stage in stage order.
template <class F>
  requires std::invocable<const F&>
[[nodiscard]] auto collect_read(const F& f) -> std::expected<
    Read<std::tuple<detail::read_value_t<detail::thunk_result_t<F>>>>,
    typename detail::thunk_result_t<F>::error_type> {
  using T = detail::read_value_t<detail::thunk_result_t<F>>;
  return std::invoke(f).transform([](Read<T>&& r) {
    return Read<std::tuple<T>>{.value = std::tuple<T>{std::move(r.value)},
                               .warnings = std::move(r.warnings)};
  });
}

template <class F, class G, class... Rest>
  requires std::invocable<const F&> and std::invocable<const G&> and
           (std::invocable<const Rest&> and ...)
[[nodiscard]] auto collect_read(const F& f, const G& g, const Rest&... rest)
    -> std::expected<
        Read<std::tuple<detail::read_value_t<detail::thunk_result_t<F>>,
                        detail::read_value_t<detail::thunk_result_t<G>>,
                        detail::read_value_t<detail::thunk_result_t<Rest>>...>>,
        typename detail::thunk_result_t<F>::error_type> {
  using T = detail::read_value_t<detail::thunk_result_t<F>>;
  using Tuple =
      std::tuple<T, detail::read_value_t<detail::thunk_result_t<G>>,
                 detail::read_value_t<detail::thunk_result_t<Rest>>...>;
  using Out = std::expected<Read<Tuple>,
                            typename detail::thunk_result_t<F>::error_type>;
  return std::invoke(f).and_then([&](Read<T>&& head) -> Out {
    return collect_read(g, rest...).transform([&](auto&& tail) {
      return Read<Tuple>{
          .value = std::tuple_cat(std::tuple<T>{std::move(head.value)},
                                  std::move(tail.value)),
          .warnings = detail::concatenated(std::move(head.warnings),
                                           std::move(tail.warnings))};
    });
  });
}

}  // namespace mov::io
