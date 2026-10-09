// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// The well-formed twin of reject_tide_name_literal.cpp.

#include "mov/core/tide/constituent.hpp"

int main() {
  constexpr auto name = mov::core::tide::constituent_name("2(MS)8");
  return name.view().size() == 6 ? 0 : 1;
}
