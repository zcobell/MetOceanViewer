// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// detail:: is the fence. Names in mov::core::detail are not API: code outside
// src/core must not use them, even where a public header has to declare them.
//
// CoreKey is the one passkey of the core. Public members that bypass a
// class's own checks (TimeSeries built from parts whose invariant the caller
// has already established, SeriesMeta unit/datum rewrites for convert and
// shift) take a `const CoreKey&`. Only CoreAccess can make one, and
// CoreAccess is defined in src/core/core_access.hpp, which is not on the
// include path of any other layer. So the bypasses are callable from core
// .cpp files only.

namespace mov::core::detail {

struct CoreAccess;

class CoreKey {
 private:
  friend struct CoreAccess;
  constexpr CoreKey() noexcept = default;
};

}  // namespace mov::core::detail
