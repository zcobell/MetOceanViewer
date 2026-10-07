// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// Well-formed twin of the reject_* files: literals that are valid names, an
// NcNameRef of an NcName that outlives it, and attribute targets.
#include "mov/io/netcdf/name.hpp"
#include "mov/io/netcdf/types.hpp"

namespace {

constexpr mov::io::nc::NcNameRef time_name{"time"};
// 256 bytes: nc_max_name.
constexpr mov::io::nc::NcNameRef longest{
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
    "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"};
static_assert(longest.view().size() == 256);

}  // namespace

int main() {
  const auto owned = mov::io::nc::NcName::make("zeta").value();
  const mov::io::nc::NcNameRef ref{owned};
  const mov::io::nc::AttTarget on{"zeta"};
  const mov::io::nc::AttTarget global{mov::io::nc::global};
  return static_cast<int>(ref.view().size() + time_name.view().size()) +
         (on.variable() ? 0 : 1) + (global.variable() ? 1 : 0) - 8;
}
