// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "nc_counts.hpp"

namespace mov::test::nc_counts {

Counts& counts() noexcept {
  static Counts instance;
  return instance;
}

}  // namespace mov::test::nc_counts
