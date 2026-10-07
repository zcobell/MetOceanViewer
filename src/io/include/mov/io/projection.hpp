// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstdint>
#include <expected>
#include <memory>
#include <variant>

#include "mov/core/geo.hpp"

namespace mov::io {

// Projection runs at the read boundary (C5, C19): a reader turns the points
// of a file's own CRS into WGS84 Locations once, and the core never sees
// another CRS. PROJ is a private dependency of mov_io; no PROJ header is
// public.

enum class ProjectionErrc : std::uint8_t {
  unknown_crs,       // no geographic or projected CRS has this EPSG code
  transform_failed,  // no usable transformation, or it failed for this point
};

struct ProjectionError {
  ProjectionErrc code;
  core::Epsg crs;
  friend constexpr bool operator==(const ProjectionError&,
                                   const ProjectionError&) = default;
};

/// A projection failure, or a result PROJ computed that is not a Location
/// (outside the latitude range, or not finite).
using ToLocationError = std::variant<ProjectionError, core::LocationError>;

/// A coordinate transformation from one EPSG CRS to WGS84, built once and
/// used for many points. Coordinates are in the order of the file's own
/// variables, x then y (easting then northing, or longitude then latitude),
/// whatever axis order the EPSG definition has; EPSG:4326 is the identity.
///
/// Not thread-safe, even for the const-looking queries: PROJ keeps state in
/// the context. Use one Projector per thread.
class Projector {
 public:
  [[nodiscard]] static std::expected<Projector, ProjectionError> make(
      core::Epsg crs);

  Projector(Projector&&) noexcept;
  Projector& operator=(Projector&&) noexcept;
  Projector(const Projector&) = delete;
  Projector& operator=(const Projector&) = delete;
  ~Projector();

  [[nodiscard]] core::Epsg crs() const noexcept { return crs_; }

  /// Longitudes in [0, 360] are normalized like Location::make does.
  [[nodiscard]] std::expected<core::Location, ToLocationError> to_location(
      core::Xy p);

 private:
  struct Impl;
  Projector(core::Epsg crs, std::unique_ptr<Impl> impl) noexcept;

  core::Epsg crs_;
  std::unique_ptr<Impl> impl_;  // null for EPSG:4326
};

/// The WGS84 Location of a point in its own CRS. EPSG:4326 takes a fast path
/// (no PROJ); any other code uses a Projector that is cached per thread for
/// the most recent CRS, so converting the stations of one file is cheap. A
/// reader with many points should still hold its own Projector.
///
/// PROJ finds proj.db in the install the build used, or in $PROJ_DATA when
/// that is set; a packaged application must set PROJ_DATA to its own copy.
[[nodiscard]] std::expected<core::Location, ToLocationError> to_location(
    const core::NativePoint& p);

}  // namespace mov::io
