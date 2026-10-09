// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// A constituent name literal outside the format's grammar does not compile:
// the catalogue's entries are built this way.

#include "mov/core/tide/constituent.hpp"

int main() {
  constexpr auto name = mov::core::tide::constituent_name("M 2");
  return name.view().size() == 3 ? 0 : 1;
}
