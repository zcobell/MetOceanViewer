// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Zach Cobell

#include "mov/io/projection.hpp"

#include <proj.h>

#include <cmath>
#include <cstdlib>
#include <expected>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "mov/core/detail/numeric.hpp"
#include "mov/core/geo.hpp"

namespace mov::io {

namespace {

struct ContextDeleter {
  void operator()(PJ_CONTEXT* ctx) const noexcept { proj_context_destroy(ctx); }
};
struct ObjectDeleter {
  void operator()(PJ* object) const noexcept { proj_destroy(object); }
};

using Context = std::unique_ptr<PJ_CONTEXT, ContextDeleter>;
using Object = std::unique_ptr<PJ, ObjectDeleter>;

// A context that looks for proj.db where the build found PROJ, unless the
// environment says otherwise.
Context make_context() {
  Context ctx{proj_context_create()};
#if defined(MOV_PROJ_DATA_DIR)
  // NOLINTNEXTLINE(concurrency-mt-unsafe): read-only lookup at context creation
  if (ctx and std::getenv("PROJ_DATA") == nullptr) {
    const char* const dir = MOV_PROJ_DATA_DIR;
    proj_context_set_search_paths(ctx.get(), 1, &dir);
  }
#endif
  return ctx;
}

// Only a CRS whose coordinates are positions on the ground can be turned into
// a Location: a vertical or geocentric CRS converts to meaningless angles.
bool is_horizontal_crs(const PJ* crs) {
  switch (proj_get_type(crs)) {
    case PJ_TYPE_GEOGRAPHIC_2D_CRS:
    case PJ_TYPE_GEOGRAPHIC_3D_CRS:
    case PJ_TYPE_PROJECTED_CRS:
      return true;
    default:
      return false;
  }
}

// The Projector of the latest CRS that to_location was asked for, per thread.
thread_local std::optional<Projector> cached_projector;

std::string epsg_name(core::Epsg crs) {
  return std::format("EPSG:{}", crs.code());
}

}  // namespace

// The PROJ objects, in the order they must be destroyed: the transformation
// first, the context last.
struct Projector::Impl {
  Context context;
  Object transformation;
};

Projector::Projector(core::Epsg crs, std::unique_ptr<Impl> impl) noexcept
    : crs_{crs}, impl_{std::move(impl)} {}

Projector::Projector(Projector&&) noexcept = default;
Projector& Projector::operator=(Projector&&) noexcept = default;
Projector::~Projector() = default;

std::expected<Projector, ProjectionError> Projector::make(core::Epsg crs) {
  if (crs == core::Epsg::wgs84()) {
    return Projector{crs, nullptr};
  }
  const auto fail = [crs](ProjectionErrc code) {
    return std::unexpected{ProjectionError{.code = code, .crs = crs}};
  };
  Context context = make_context();
  if (not context) {
    return fail(ProjectionErrc::transform_failed);
  }
  const std::string source = epsg_name(crs);
  const Object source_crs{proj_create(context.get(), source.c_str())};
  if (not source_crs or not is_horizontal_crs(source_crs.get())) {
    return fail(ProjectionErrc::unknown_crs);
  }
  const Object raw{proj_create_crs_to_crs(context.get(), source.c_str(),
                                          "EPSG:4326", nullptr)};
  if (not raw) {
    return fail(ProjectionErrc::transform_failed);
  }
  // Longitude/latitude order in and out, whatever the EPSG axis order says.
  Object normalized{proj_normalize_for_visualization(context.get(), raw.get())};
  if (not normalized) {
    return fail(ProjectionErrc::transform_failed);
  }
  return Projector{crs, std::make_unique<Impl>(
                            Impl{.context = std::move(context),
                                 .transformation = std::move(normalized)})};
}

std::expected<core::Location, ToLocationError> Projector::to_location(
    core::Xy p) {
  core::Xy out = p;
  if (impl_) {
    const PJ_COORD result = proj_trans(impl_->transformation.get(), PJ_FWD,
                                       proj_coord(p.x, p.y, 0.0, HUGE_VAL));
    out = core::Xy{.x = result.xy.x, .y = result.xy.y};
    if (not core::detail::is_finite(out.x) or
        not core::detail::is_finite(out.y)) {
      return std::unexpected{ToLocationError{ProjectionError{
          .code = ProjectionErrc::transform_failed, .crs = crs_}}};
    }
  }
  const auto location = core::Location::make({.lat = out.y, .lon = out.x});
  if (not location) {
    return std::unexpected{ToLocationError{location.error()}};
  }
  return *location;
}

std::expected<core::Location, ToLocationError> to_location(
    const core::NativePoint& p) {
  if (not cached_projector or cached_projector->crs() != p.crs()) {
    auto made = Projector::make(p.crs());
    if (not made) {
      return std::unexpected{ToLocationError{made.error()}};
    }
    cached_projector.emplace(std::move(*made));
  }
  return cached_projector->to_location({.x = p.x(), .y = p.y()});
}

}  // namespace mov::io
