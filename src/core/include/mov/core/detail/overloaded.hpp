// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

namespace mov::core::detail {

/// Builds one callable out of several (std::visit with lambdas).
template <class... F>
struct Overloaded : F... {
  using F::operator()...;
};

template <class... F>
Overloaded(F...) -> Overloaded<F...>;

}  // namespace mov::core::detail
