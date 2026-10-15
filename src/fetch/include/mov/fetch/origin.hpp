// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// The origins fetch keys its policies by (docs/providers-design.md §4.1,
// §5.3). io's wire header (`mov/io/wire/origin.hpp`, P2) is where the
// scheme-host-port triples live; until fetch can include it, the enumerators
// are declared here, in the same order, and fetch's name becomes a
// using-declaration of io's when it lands (§5.7).

#include <array>
#include <cstdint>

namespace mov::fetch {

/// A scheme-host-port triple the app talks to; all are https.
enum class Origin : std::uint8_t {
  coops_data,      ///< api.tidesandcurrents.noaa.gov/api/prod
  coops_metadata,  ///< api.tidesandcurrents.noaa.gov/mdapi/prod
  usgs,            ///< api.waterdata.usgs.gov
  ndbc,            ///< www.ndbc.noaa.gov
  github,          ///< api.github.com (the update check)
};

/// Every origin, in enumerator order.
inline constexpr std::array<Origin, 5> every_origin{
    Origin::coops_data, Origin::coops_metadata, Origin::usgs, Origin::ndbc,
    Origin::github};

}  // namespace mov::fetch
