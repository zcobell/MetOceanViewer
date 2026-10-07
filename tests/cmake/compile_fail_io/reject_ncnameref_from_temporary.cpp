// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// An NcNameRef of a temporary NcName would dangle.
#include "mov/io/netcdf/name.hpp"

int main() {
  const mov::io::nc::NcNameRef ref{mov::io::nc::NcName::make("zeta").value()};
  return static_cast<int>(ref.view().size());
}
