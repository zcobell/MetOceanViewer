// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Private to src/core: not under include/, so no other layer can include it.
// See mov/core/detail/core_key.hpp.

#pragma once

#include "mov/core/detail/core_key.hpp"

namespace mov::core::detail {

struct CoreAccess {
  [[nodiscard]] static constexpr CoreKey key() noexcept { return CoreKey{}; }
};

}  // namespace mov::core::detail
