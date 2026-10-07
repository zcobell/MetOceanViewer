// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// A char array that is not a constant cannot become an NcNameRef unchecked;
// text known only at run time goes through NcName::make.
#include "mov/io/netcdf/name.hpp"

int main(int argc, char**) {
  char buffer[] = "time";
  buffer[0] = static_cast<char>('a' + argc);
  const mov::io::nc::NcNameRef ref{buffer};
  return static_cast<int>(ref.view().size());
}
