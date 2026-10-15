// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

// The units of a CO-OPS fetch (docs/providers-design.md §5.2): the windows
// of a data request, and the datums stage of a catalog build. CO-OPS units
// have no successors.

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <variant>
#include <vector>

#include "mov/core/provider/coops.hpp"
#include "mov/core/station.hpp"
#include "mov/core/windows.hpp"
#include "mov/fetch/error.hpp"

namespace mov::fetch::coops {

/// One window of a data request: one exchange, two for a paired product.
struct Unit {
  core::TimeRange window;
  friend constexpr bool operator==(Unit, Unit) = default;
};

/// The windows of the request: its range widened to whole minutes, split at
/// the product's longest window (§3.2). Unalignable only at the ends of the
/// time line.
[[nodiscard]] constexpr std::expected<core::Windows, Unalignable> windows(
    const core::coops::Request& request) noexcept {
  const auto range = core::aligned(request.range, core::coops::grain);
  if (not range) {
    return std::unexpected{Unalignable{.range = request.range}};
  }
  return core::split(*range, core::coops::max_window(request.product));
}

/// The data request's units, in time order.
[[nodiscard]] std::expected<std::vector<Unit>, Error> initial(
    const core::coops::Request& request);

/// The exchanges of one window: the observed and the predicted series of a
/// paired product, else one.
[[nodiscard]] constexpr std::size_t exchanges(
    const core::coops::Product& product) noexcept {
  return core::coops::shape(product) ==
                 core::coops::Shape::observed_vs_predicted
             ? std::size_t{2}
             : std::size_t{1};
}

[[nodiscard]] constexpr UnitLabel label(const Unit& unit) noexcept {
  return Window{.range = unit.window};
}

/// The second stage of a CO-OPS catalog build: the stations the lists
/// named, whose datums are fetched one by one.
struct DatumsRequest {
  std::vector<core::StationId<core::provider::Coops>> stations;
  friend bool operator==(const DatumsRequest&, const DatumsRequest&) = default;
};

/// One station's datums.
struct DatumsUnit {
  std::uint32_t index;
  core::StationId<core::provider::Coops> station;
  friend bool operator==(const DatumsUnit&, const DatumsUnit&) = default;
};

/// One unit per station, in the request's order.
[[nodiscard]] std::vector<DatumsUnit> initial(const DatumsRequest& request);

[[nodiscard]] constexpr UnitLabel label(const DatumsUnit& unit) noexcept {
  return CatalogPart{.index = unit.index};
}

}  // namespace mov::fetch::coops
