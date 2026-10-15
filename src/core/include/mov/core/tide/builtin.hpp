// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <optional>
#include <string_view>

#include "mov/core/tide/constituent.hpp"

// The built-in catalogue as a run-time lookup, for code that needs no
// constant evaluation over it: including catalogue.hpp evaluates the whole
// catalogue's initializer in the including TU (docs/harmonics-engine.md 2.6,
// constexpr budget).

namespace mov::core::tide {

/// The flattened Argument of the catalogue entry named exactly `name`, or of
/// the entry `name` is an alias of (canonical_name); nullopt if neither.
/// A lookup in a table built on first use.
[[nodiscard]] std::optional<Argument> builtin_argument(
    std::string_view name) noexcept;

}  // namespace mov::core::tide
