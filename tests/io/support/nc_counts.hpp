// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Counts of netCDF-C opens and closes, kept by the `--wrap` shims of
// nc_wrap.cpp (Linux: the test executable is linked with
// -Wl,--wrap= nc_open, nc_create, nc_close, nc_abort, nc_sync and remove).
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
  std::int64_t opened{0};        // nc_open and nc_create that succeeded
  std::int64_t closed{0};        // nc_close that succeeded, nc_abort of a
                                 // valid id (it always releases the id)
  std::int64_t close_calls{0};   // every nc_close, failed ones included
  std::int64_t abort_calls{0};   // every nc_abort
  std::int64_t remove_calls{0};  // remove() called from static code
                                 // (netCDF-C, HDF5)
  int fail_next_closes{0};       // the next n nc_close calls close the file but
                                 // report NC_EHDFERR
  int fail_next_syncs{0};        // the next n nc_sync calls fail (NC_EHDFERR)
                                 // without syncing
};

/// The process-wide counts; they stay zero unless `available`.
[[nodiscard]] Counts& counts() noexcept;

}  // namespace mov::test::nc_counts
