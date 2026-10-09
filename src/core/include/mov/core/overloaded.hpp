// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

namespace mov::core {

/// Builds one callable out of several, for std::visit with one lambda per
/// alternative: without a catch-all, the visit stops compiling when the
/// variant gains an alternative nobody handles.
template <class... F>
struct Overloaded : F... {
  using F::operator()...;
};

template <class... F>
Overloaded(F...) -> Overloaded<F...>;

}  // namespace mov::core
