// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// A name literal with an embedded NUL is a compile error: netCDF-C would read
// only the part before it.
#include "mov/io/netcdf/name.hpp"

constexpr mov::io::nc::NcNameRef cut{"ti\0me"};

int main() { return static_cast<int>(cut.view().size()); }
