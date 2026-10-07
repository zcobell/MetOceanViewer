// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <netcdf.h>

#include "mov/io/netcdf_library.hpp"

namespace mov::io {

std::string_view netcdf_library_version() noexcept { return nc_inq_libvers(); }

}  // namespace mov::io
