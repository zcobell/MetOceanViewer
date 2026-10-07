// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include <catch2/catch_test_macros.hpp>

#include "mov/io/netcdf_library.hpp"

TEST_CASE("netCDF-C 4.x is linked", "[io][netcdf]") {
  CHECK(mov::io::netcdf_library_version().starts_with("4."));
}
