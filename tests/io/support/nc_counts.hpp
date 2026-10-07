// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Counts of netCDF-C opens and closes, kept by the `--wrap` shims of
// nc_wrap.cpp (Linux: the test executable is linked with
// -Wl,--wrap=nc_open,--wrap=nc_create,--wrap=nc_close,--wrap=nc_abort).
// Elsewhere (no MOV_TEST_NC_WRAP) nothing counts and the tests that need the
// counts skip.

#pragma once

#include <cstdint>

namespace mov::test::nc_counts {

#if defined(MOV_TEST_NC_WRAP)
inline constexpr bool available = true;
#else
inline constexpr bool available = false;
#endif

struct Counts {
  std::int64_t opened{0};       // nc_open and nc_create that succeeded
  std::int64_t closed{0};       // nc_close that succeeded, nc_abort of a
                                // valid id (it always releases the id)
  std::int64_t close_calls{0};  // every nc_close, failed ones included
  std::int64_t abort_calls{0};  // every nc_abort
  int fail_next_closes{0};      // make the next n nc_close calls fail
                                // (NC_EHDFERR) without closing
};

/// The process-wide counts; they stay zero unless `available`.
[[nodiscard]] Counts& counts() noexcept;

}  // namespace mov::test::nc_counts
