// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <string_view>

namespace mov::io {

/// Version string of the linked netCDF-C library (nc_inq_libvers); a static
/// string owned by the library.
[[nodiscard]] std::string_view netcdf_library_version() noexcept;

}  // namespace mov::io
