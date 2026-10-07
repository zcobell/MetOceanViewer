// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The single choke point through which mov::io calls netCDF-C (C11): every
// nc_* call, nc_free_string, nc_abort and the destructor's nc_close included,
// is a lambda handed to nc_status or nc_call. Private to src/io/netcdf/.
//
// netCDF-C is not thread-safe and io has no lock: the caller serializes
// (file.hpp). In debug builds every entry sets a process-wide flag and
// asserts that it was clear, which catches a second thread inside netCDF-C
// and a call made from inside another (re-entry). Release builds check
// nothing.

#pragma once

#include <atomic>
#include <cassert>
#include <concepts>
#include <expected>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "mov/io/error.hpp"

namespace mov::io::nc::detail {

/// NC_NOERR, mirrored so this header needs no <netcdf.h> (static_asserted in
/// file.cpp).
inline constexpr int nc_noerr = 0;

#if !defined(NDEBUG)

/// Set while a netCDF-C call is in progress.
inline std::atomic_flag nc_busy{};

/// Sets nc_busy for its lifetime; asserts it was clear. A detector, not a
/// lock: it orders nothing (relaxed), it only notices a second entry and
/// aborts. Serializing the calls is the caller's job.
class EntryCheck {
 public:
  EntryCheck() noexcept {
    const bool busy = nc_busy.test_and_set(std::memory_order_relaxed);
    assert(not busy and
           "netCDF-C entered concurrently or re-entrantly: callers must "
           "serialize all mov::io netCDF calls (C11)");
  }
  EntryCheck(const EntryCheck&) = delete;
  EntryCheck& operator=(const EntryCheck&) = delete;
  EntryCheck(EntryCheck&&) = delete;
  EntryCheck& operator=(EntryCheck&&) = delete;
  ~EntryCheck() { nc_busy.clear(std::memory_order_relaxed); }
};

#endif

/// Runs `call`, a lambda around exactly one netCDF-C function, and returns
/// its status.
template <class F>
  requires std::same_as<std::invoke_result_t<F>, int>
[[nodiscard]] int nc_status(F&& call) {
#if !defined(NDEBUG)
  const EntryCheck check;
#endif
  return std::invoke(std::forward<F>(call));
}

/// `status` as an expected: NC_NOERR is success, anything else an NcError
/// that names the operation, the object and the file. Not a template, so
/// the error path is one piece of code for every call (file.cpp).
[[nodiscard]] std::expected<void, NcError> status_to_expected(
    int status, NcOp op, std::string_view object,
    const std::filesystem::path& file);

/// nc_status, with the status turned into an expected.
template <class F>
  requires std::same_as<std::invoke_result_t<F>, int>
[[nodiscard]] std::expected<void, NcError> nc_call(
    NcOp op, std::string_view object, const std::filesystem::path& file,
    F&& call) {
  return status_to_expected(nc_status(std::forward<F>(call)), op, object, file);
}

}  // namespace mov::io::nc::detail
