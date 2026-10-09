// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

// A water level without a datum does not compile on any compiler: the datum
// is Required, which has no default (not only a missing-field warning).

#include "mov/core/provider/coops.hpp"

int main() {
  namespace coops = mov::core::coops;
  const coops::Product p = coops::WaterLevel{
      .kind = coops::ObservedLevel{.series = coops::Observed::six_minute}};
  return coops::shape(p) == coops::Shape::series ? 0 : 1;
}
