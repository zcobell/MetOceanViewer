// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// A met product has no datum: naming one is a compile error, not a query
// parameter the API would ignore.

#include "mov/core/provider/coops.hpp"

int main() {
  namespace coops = mov::core::coops;
  const coops::Product p =
      coops::Meteorological{.product = coops::Met::wind,
                            .interval = coops::MetInterval::hourly,
                            .datum = coops::Datum::mllw};
  return coops::shape(p) == coops::Shape::wind ? 0 : 1;
}
