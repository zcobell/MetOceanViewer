// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <netcdf.h>

#include <string_view>

#include "mov/io/netcdf_library.hpp"
#include "nc_call.hpp"

namespace mov::io {

std::string_view netcdf_library_version() noexcept {
  // Through the choke point like every other call, though it only reads a
  // static string.
  const char* version = nullptr;
  static_cast<void>(nc::detail::nc_status([&version] {
    version = nc_inq_libvers();
    return nc::detail::nc_noerr;
  }));
  return version;
}

}  // namespace mov::io
