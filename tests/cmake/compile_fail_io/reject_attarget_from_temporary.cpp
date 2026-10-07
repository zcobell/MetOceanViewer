// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// An attribute target naming a temporary NcName would dangle.
#include "mov/io/netcdf/name.hpp"
#include "mov/io/netcdf/types.hpp"

int main() {
  const mov::io::nc::AttTarget on{mov::io::nc::NcName::make("zeta").value()};
  return on.variable() ? 0 : 1;
}
