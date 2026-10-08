// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <variant>

#include "mov/core/geo.hpp"
#include "mov/io/warning.hpp"

namespace mov::io {

// Projection runs at the read boundary (C5, C19): a reader turns the points
// of a file's own CRS into WGS84 Locations once, and the core never sees
// another CRS. PROJ is a private dependency of mov_io; no PROJ header is
// public.

enum class ProjectionErrc : std::uint8_t {
  unknown_crs,       // no geographic or projected CRS has this EPSG code
  transform_failed,  // no usable transformation, or it failed for this point
  database_unavailable,  // PROJ cannot open its database (proj.db)
};

/// What the coordinates of a CRS are: angles on the ellipsoid (a PROJ
/// geographic 2D or 3D CRS, whatever the datum: EPSG:4326, 4269, ...) or
/// distances on a map projection. Vector components of a model on a projected
/// grid point along the grid's axes, not east and north (design decision 28).
enum class CrsKind : std::uint8_t { geographic, projected };

struct ProjectionError {
  ProjectionErrc code;
  core::Epsg crs;
  friend constexpr bool operator==(const ProjectionError&,
                                   const ProjectionError&) = default;
};

/// A projection failure, or a result PROJ computed that is not a Location
/// (outside the latitude range, or not finite).
using ToLocationError = std::variant<ProjectionError, core::LocationError>;

/// Where PROJ finds proj.db. An application calls this once at startup, before
/// any projection, with the directory of the database it ships (the build
/// tree's copy is no use on a user's machine). The environment variable
/// `MOV_PROJ_DATA`, when set and not empty, overrides it: that is the
/// escape hatch of a user or a test. With neither, PROJ looks where it was
/// built to look (and in its own `PROJ_DATA`). Set once: the first call
/// wins and later ones are ignored. Safe to call from any thread, but a
/// Projector reads the setting only when it is made.
void set_projection_data_dir(const std::filesystem::path& dir);

/// How good the transformations a Projector has used were. PROJ chooses an
/// operation per point (by the area of use) and tells which only at a cost
/// of about 200 microseconds, a few hundred times the point itself, so the
/// operation is asked for a sample: each of the first 256 points, then one in
/// 64. A file of up to 256 stations is therefore checked entirely.
struct ProjectionAccuracy {
  std::size_t points{};          // points converted
  std::size_t sampled_points{};  // of those, the ones whose operation was asked
  std::size_t ballpark_points{};  // of the sampled, converted by a ballpark one
  /// The largest accuracy in metres that PROJ states for an operation used
  /// by a sampled point; nullopt when none states one.
  std::optional<double> worst_stated_m{};
};

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
  [[nodiscard]] CrsKind kind() const noexcept { return kind_; }

  /// Longitudes in [0, 360] are normalized like Location::make does.
  [[nodiscard]] std::expected<core::Location, ToLocationError> to_location(
      core::Xy p);

  /// What the points converted so far were transformed with. EPSG:4326 is
  /// exact and always reports zeros.
  [[nodiscard]] const ProjectionAccuracy& accuracy() const noexcept {
    return accuracy_;
  }

  /// The `crs_approximate` warning a reader emits after converting its points:
  /// present when a sampled point went through a ballpark transformation (a
  /// datum shift PROJ could not do with a grid) or the stated accuracy is worse
  /// than a metre. Its subject is the CRS, its count the number of points
  /// converted.
  [[nodiscard]] std::optional<Warning> approximation_warning() const;

 private:
  struct Impl;
  Projector(core::Epsg crs, CrsKind kind, std::unique_ptr<Impl> impl) noexcept;

  [[nodiscard]] std::expected<core::Xy, ProjectionError> project(core::Xy p);

  core::Epsg crs_;
  CrsKind kind_;
  std::unique_ptr<Impl> impl_;  // null for EPSG:4326
  ProjectionAccuracy accuracy_;
};

/// The kind of `crs` (EPSG:4326 without building anything; another code builds
/// a Projector). Errors are Projector::make's.
[[nodiscard]] std::expected<CrsKind, ProjectionError> crs_kind(core::Epsg crs);

/// The WGS84 Location of a point in its own CRS. EPSG:4326 takes a fast path
/// that never builds a Projector; any other code builds one for this call (a
/// few milliseconds: it opens the database and plans the transformation). A
/// reader with more than a handful of points holds a Projector instead, and
/// reports its approximation_warning().
[[nodiscard]] std::expected<core::Location, ToLocationError> to_location(
    const core::NativePoint& p);

}  // namespace mov::io
