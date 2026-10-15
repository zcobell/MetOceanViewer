// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// The origins fetch keys its policies by (docs/providers-design.md §4.1,
// §5.3): io's (`mov/io/wire/origin.hpp`, P2), where the scheme-host-port
// triples live.

#include <array>

#include "mov/io/wire/origin.hpp"

namespace mov::fetch {

using io::wire::Origin;

/// Every origin, in enumerator order.
inline constexpr std::array<Origin, 5> every_origin{
    Origin::coops_data, Origin::coops_metadata, Origin::usgs, Origin::ndbc,
    Origin::github};

}  // namespace mov::fetch
