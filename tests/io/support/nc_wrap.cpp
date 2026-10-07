// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The `--wrap` shims (GNU ld, lld) that count every netCDF-C open and close
// in the test executable, mov::io's included (B5), and a Catch2 listener that
// fails the run when a test case ends with a netCDF id still open.

#include <netcdf.h>

#include <catch2/catch_test_case_info.hpp>
#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>
#include <cstdlib>
#include <format>
#include <iostream>

#include "nc_counts.hpp"

// The names are fixed by the linker's --wrap convention.
// NOLINTBEGIN(bugprone-reserved-identifier,readability-identifier-naming)
extern "C" {

int __real_nc_open(const char* path, int mode, int* ncidp);
int __real_nc_create(const char* path, int cmode, int* ncidp);
int __real_nc_close(int ncid);
int __real_nc_abort(int ncid);
int __real_nc_sync(int ncid);
int __real_remove(const char* path);

int __wrap_nc_open(const char* path, int mode, int* ncidp);
int __wrap_nc_create(const char* path, int cmode, int* ncidp);
int __wrap_nc_close(int ncid);
int __wrap_nc_abort(int ncid);
int __wrap_nc_sync(int ncid);
int __wrap_remove(const char* path);

int __wrap_nc_open(const char* path, int mode, int* ncidp) {
  const int status = __real_nc_open(path, mode, ncidp);
  if (status == NC_NOERR) {
    ++mov::test::nc_counts::counts().opened;
  }
  return status;
}

int __wrap_nc_create(const char* path, int cmode, int* ncidp) {
  const int status = __real_nc_create(path, cmode, ncidp);
  if (status == NC_NOERR) {
    ++mov::test::nc_counts::counts().opened;
  }
  return status;
}

int __wrap_nc_close(int ncid) {
  auto& c = mov::test::nc_counts::counts();
  ++c.close_calls;
  const int status = __real_nc_close(ncid);
  if (status == NC_NOERR) {
    ++c.closed;
  }
  if (c.fail_next_closes > 0) {
    // The file is closed, but the caller is told the close failed, as after
    // a flush error: the wrapper must not touch the id again.
    --c.fail_next_closes;
    return NC_EHDFERR;
  }
  return status;
}

int __wrap_nc_abort(int ncid) {
  auto& c = mov::test::nc_counts::counts();
  ++c.abort_calls;
  const int status = __real_nc_abort(ncid);
  if (status != NC_EBADID) {
    ++c.closed;  // nc_abort releases a valid id whatever it returns
  }
  return status;
}

int __wrap_nc_sync(int ncid) {
  auto& c = mov::test::nc_counts::counts();
  if (c.fail_next_syncs > 0) {
    --c.fail_next_syncs;
    return NC_EHDFERR;  // nothing synced, nothing released
  }
  return __real_nc_sync(ncid);
}

// netCDF-C's own remove() calls (NC4_abort deletes a file it created in
// define mode). The tests' files are removed through std::filesystem, whose
// calls live in the shared C++ library and are not wrapped.
int __wrap_remove(const char* path) {
  ++mov::test::nc_counts::counts().remove_calls;
  return __real_remove(path);
}

}  // extern "C"
// NOLINTEND(bugprone-reserved-identifier,readability-identifier-naming)

namespace {

class OpenHandleCheck final : public Catch::EventListenerBase {
 public:
  using Catch::EventListenerBase::EventListenerBase;

  void testCaseEnded(const Catch::TestCaseStats& stats) override {
    const auto& c = mov::test::nc_counts::counts();
    if (c.opened != c.closed) {
      std::cerr
          << std::format(
                 "netCDF id leak: test case \"{}\" ended with {} opened and {} "
                 "closed\n",
                 stats.testInfo->name, c.opened, c.closed)
          << std::flush;
      std::_Exit(EXIT_FAILURE);
    }
  }
};

}  // namespace

CATCH_REGISTER_LISTENER(OpenHandleCheck)
