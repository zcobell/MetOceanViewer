// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// An empty name literal is a compile error.
#include "mov/io/netcdf/name.hpp"

constexpr mov::io::nc::NcNameRef empty{""};

int main() { return static_cast<int>(empty.view().size()); }
