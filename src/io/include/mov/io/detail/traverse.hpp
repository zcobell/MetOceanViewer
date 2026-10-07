// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <expected>
#include <functional>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

namespace mov::io::detail {

template <class R>
struct ExpectedParts;
template <class T, class E>
struct ExpectedParts<std::expected<T, E>> {
  using value = T;
  using error = E;
};

/// `f` applied to every element of `range`, in order, collected; the first
/// error ends the walk and is the result (Haskell's traverse over
/// Either). `f` returns std::expected<T, E>.
template <std::ranges::input_range R, class F>
[[nodiscard]] auto traverse(R&& range, F&& f) -> std::expected<
    std::vector<typename ExpectedParts<std::remove_cvref_t<
        std::invoke_result_t<F&, std::ranges::range_reference_t<R>>>>::value>,
    typename ExpectedParts<std::remove_cvref_t<
        std::invoke_result_t<F&, std::ranges::range_reference_t<R>>>>::error> {
  using Parts = ExpectedParts<std::remove_cvref_t<
      std::invoke_result_t<F&, std::ranges::range_reference_t<R>>>>;
  std::vector<typename Parts::value> out;
  if constexpr (std::ranges::sized_range<R>) {
    out.reserve(std::ranges::size(range));
  }
  for (auto&& element : range) {
    auto result = std::invoke(f, std::forward<decltype(element)>(element));
    if (not result) {
      return std::unexpected{std::move(result).error()};
    }
    out.push_back(*std::move(result));
  }
  return out;
}

}  // namespace mov::io::detail
